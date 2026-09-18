// Central place for every tunable value used by the rendezvous server,
// per project convention (see repo root CLAUDE.md).
export const Config = {
  /** Digits in a pairing code, e.g. 6 -> codes like "042817". */
  pairingCodeDigits: 6,

  /** How long an unused or in-progress pairing code stays valid. */
  sessionTtlSeconds: 300,

  /** How many times /session will retry generating a code that collides
   * with one already claimed (extremely unlikely at 10^pairingCodeDigits
   * possibilities, but the DO's claim step makes collisions detectable). */
  maxCodeGenerationAttempts: 5,
} as const;
