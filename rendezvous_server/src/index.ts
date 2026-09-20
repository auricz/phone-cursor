import { Config } from './config';
import { PairingSession } from './PairingSession';

export { PairingSession };

export interface Env {
  PAIRING_SESSION: DurableObjectNamespace;
}

const sessionWebSocketPath = new RegExp(`^/session/(\\d{${Config.pairingCodeDigits}})/ws$`);

function generatePairingCode(): string {
  const max = 10 ** Config.pairingCodeDigits;
  const value = Math.floor(Math.random() * max);
  return value.toString().padStart(Config.pairingCodeDigits, '0');
}

/** POST /session: allocate a fresh pairing code and its backing Durable Object. */
async function handleCreateSession(env: Env): Promise<Response> {
  for (let attempt = 0; attempt < Config.maxCodeGenerationAttempts; attempt++) {
    const code = generatePairingCode();
    const stub = env.PAIRING_SESSION.get(env.PAIRING_SESSION.idFromName(code));
    const claimResponse = await stub.fetch('https://internal/claim', { method: 'POST' });
    if (claimResponse.ok) {
      return new Response(JSON.stringify({ code }), {
        headers: { 'content-type': 'application/json' },
      });
    }
  }
  return new Response('Could not allocate a pairing code, please try again', { status: 503 });
}

/** GET /session/:code/ws?role=desktop|phone: forward the WebSocket upgrade into that code's Durable Object. */
function handleSessionWebSocket(request: Request, env: Env, code: string): Promise<Response> {
  const stub = env.PAIRING_SESSION.get(env.PAIRING_SESSION.idFromName(code));
  return stub.fetch(request);
}

export default {
  async fetch(request: Request, env: Env): Promise<Response> {
    const url = new URL(request.url);

    console.log({ method: request.method, url: url.href });

    if (request.method === 'POST' && url.pathname === '/session') {
      return handleCreateSession(env);
    }

    const wsMatch = url.pathname.match(sessionWebSocketPath);
    const code = wsMatch?.[1];
    if (request.method === 'GET' && code !== undefined) {
      return handleSessionWebSocket(request, env, code);
    }

    return new Response('Not found', { status: 404 });
  },
};
