import { DurableObject } from 'cloudflare:workers';
import { Env } from '.';
import { Config } from './config';

export type Role = 'desktop' | 'phone';

function otherRole(role: Role): Role {
  return role === 'desktop' ? 'phone' : 'desktop';
}

function isRole(value: string | null): value is Role {
  return value === 'desktop' || value === 'phone';
}

/**
 * One pairing code's worth of state. Desktop and phone each open a
 * WebSocket here (tagged by role) and the object relays whatever opaque
 * JSON messages they send each other (STUN-discovered candidate
 * addresses, in practice). It never sees any key material - the
 * ECDH/AEAD handshake that actually secures the session happens directly
 * between the two devices once they have each other's address.
 */
export class PairingSession extends DurableObject<Env> {
  constructor(ctx: DurableObjectState, env: Env) {
    super(ctx, env);
  }

  async fetch(request: Request): Promise<Response> {
    const url = new URL(request.url);
    if (url.pathname === '/claim') {
      return this.handleClaim();
    }
    return this.handleWebSocketUpgrade(request, url);
  }

  /** Called once by the Worker's /session handler when a code is first issued. */
  private async handleClaim(): Promise<Response> {
    const alreadyClaimed = await this.ctx.storage.get<boolean>('claimed');
    if (alreadyClaimed) {
      return new Response('already claimed', { status: 409 });
    }
    await this.ctx.storage.put('claimed', true);
    await this.ctx.storage.setAlarm(Date.now() + Config.sessionTtlSeconds * 1000);
    return new Response('ok');
  }

  private async handleWebSocketUpgrade(request: Request, url: URL): Promise<Response> {
    if (request.headers.get('Upgrade') !== 'websocket') {
      return new Response('Expected a WebSocket upgrade', { status: 400 });
    }

    const claimed = await this.ctx.storage.get<boolean>('claimed');
    if (!claimed) {
      // Nobody ever requested this code from POST /session - refuse to let
      // someone connect to an arbitrary, never-issued pairing code.
      return new Response('Unknown pairing code', { status: 404 });
    }

    const role = url.searchParams.get('role');
    if (!isRole(role)) {
      return new Response('role must be "desktop" or "phone"', { status: 400 });
    }
    if (this.ctx.getWebSockets(role).length > 0) {
      return new Response(`a ${role} is already connected to this code`, { status: 409 });
    }

    const pair = new WebSocketPair();
    const client = pair[0];
    const server = pair[1];
    this.ctx.acceptWebSocket(server, [role]);

    // If the peer already sent something before we connected, deliver it
    // immediately so a late-joining side doesn't miss the first candidate.
    const lastFromPeer = await this.ctx.storage.get<string>(`last:${otherRole(role)}`);
    if (lastFromPeer !== undefined) {
      server.send(lastFromPeer);
    }

    return new Response(null, { status: 101, webSocket: client });
  }

  async webSocketMessage(ws: WebSocket, message: string | ArrayBuffer): Promise<void> {
    if (typeof message !== 'string') return;

    const [role] = this.ctx.getTags(ws) as [Role];
    await this.ctx.storage.put(`last:${role}`, message);

    for (const peer of this.ctx.getWebSockets(otherRole(role))) {
      peer.send(message);
    }
  }

  async webSocketClose(ws: WebSocket, code: number, reason: string, wasClean: boolean): Promise<void> {
    try {
      ws.close(code, reason);
    } catch {
      // Already closed/closing - nothing to do.
    }
  }

  /** TTL expiry: close any open sockets and drop all state, invalidating the code. */
  async alarm(): Promise<void> {
    for (const ws of this.ctx.getWebSockets()) {
      ws.close(1000, 'Pairing code expired');
    }
    await this.ctx.storage.deleteAll();
  }
}
