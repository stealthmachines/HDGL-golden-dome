/**
 * fabric_node_agent.mjs — Local AI bot multi-node fabric integration
 * ═══════════════════════════════════════════════════════════════════════
 *
 * Extends the Wu-Wei MCP stack (server.js / server-dos.js) so that the
 * local AI bot can discover, route to, and store across any number of
 * discrete fabric nodes running HDGL firmware or the Wu-Wei server.
 *
 * DESIGN AXIOM:  Ωₙ₊₁ = T(Ωₙ)
 *   The bot is not above the fabric.  It IS a fabric node.
 *   Its VectorContext identity is phi_fold-native (same primitive as
 *   the bare-metal firmware).  Session identity, task routing, strand
 *   assignment, and ledger placement are all pure consequences of that
 *   primitive — no hardcoded config.
 *
 * THREE EXPORTED SURFACES:
 *
 *   discoverFabricNodes(genomeFp, opts)
 *     Broadcasts a phi-seed HEALTH frame (same multicast address
 *     derivation as hdgl_peer_discovery.hdgl Phase 1).  Collects
 *     peer Omega census over UDP.  Falls back to static sector-68
 *     style peer list read from FABRIC_PEERS env var.
 *     Returns: PeerTable[]
 *
 *   routePassToNode(taskHash, passType, peerTable)
 *     Maps a Wu-Wei pass type to an Omega base-4 type (A/C/G/T),
 *     then selects the peer whose strand_auth bitmap has the highest
 *     weighted authority for that base type.
 *     Returns: { ip, port, strandAuth, genomeFp, local: bool }
 *
 *   fabricErlAppend(ledger, entry, peerTable)
 *     Wraps erlAppend() to route each ledger entry to the authoritative
 *     strand node via zchg FILESWAP framing over HTTP (the same shim
 *     the firmware's zchg_http layer exposes).  Falls back to local
 *     write when no remote peer has better strand authority.
 *     Returns: { id, node, strand, phi_addr }
 *
 * ALSO EXPORTED (used internally, available for testing):
 *
 *   phiFold32(x, key, seq)        — zc_fold port from zchg_carrier.h
 *   phiTauStrand(phiAddr, count)  — FNV-phi spiral from hdgl_fabric_loader.c
 *   contentHash64(data)           — prismatic recursion content hash
 *   buildGenomeFp(seed)           — derive genome_fp from arbitrary seed
 *   parseHealthResponse(buf)      — decode HEALTH frame payload
 *
 * INTEGRATION (in server.js):
 *
 *   // At the top of server.js, add:
 *   import {
 *     discoverFabricNodes, routePassToNode, fabricErlAppend,
 *     patchSelectPassSequence, patchRunUnfold
 *   } from './fabric_node_agent.mjs';
 *
 *   // Replace the two function definitions with the patched versions:
 *   const selectPassSequence = patchSelectPassSequence(originalSelectPassSequence);
 *   // In the unfold tool handler, before executePass(), call:
 *   //   const nodeMap = await discoverFabricNodes(railGenomeFp());
 *   //   const target  = routePassToNode(taskHash, passType, nodeMap);
 *   //   if (!target.local) { ... forward via fabricForwardPass() ... }
 *
 * DEPENDENCIES:
 *   node:dgram, node:http, node:crypto, node:net — all built-in.
 *   phiFoldHash32 from analog-container.mjs (re-exported here as well).
 *   No third-party packages.
 *
 * WIRE COMPATIBILITY:
 *   Speaks the same zchg_gossip_msg_t layout as zchg_core_patch.h (25 bytes).
 *   HEALTH frame: version=1 type=4 strand_id=0 authority_ep=0 payload_len=0.
 *   Gossip JSON shim: {source_ip, strand_weights[8], storage_available,
 *                      cluster_fingerprint, dn_aggregate, genome_fingerprint}
 *   All numeric fields are uint32 big-endian on the wire.
 *
 * BARE-METAL NODE COMPATIBILITY:
 *   Firmware nodes respond to UDP HEALTH with a JSON blob — see the
 *   hdgl_http_shim.hdgl glyph (pending emit; this file assumes it exists
 *   at port 8090 on each firmware node).  Pure firmware nodes that have
 *   not yet received the HTTP shim are silently skipped — discovery
 *   degrades gracefully to whatever peers respond.
 */

import dgram  from 'node:dgram';
import http   from 'node:http';
import https  from 'node:https';
import crypto from 'node:crypto';
import net    from 'node:net';
import { getRail, phiFoldHash32 } from './analog-container.mjs';

// ─────────────────────────────────────────────────────────────────────────────
// CONSTANTS  (mirror zchg_carrier.h / hdgl_fabric_loader.c exactly)
// ─────────────────────────────────────────────────────────────────────────────

const PHI          = 1.6180339887498948482;
const PHI32        = 0x9E3779B9;   // floor(2^32 / φ) — ZC_PHI32
const FIB32        = 0x9E3779B1;   // nearest prime to PHI32 — ZC_FIB32
const SQRT_PHI32   = 0xA17F0BCB;   // floor(√φ × 2^32) — ZC_SQRT_PHI
const PHI32_INV    = 0x144CBC89;   // ZC_PHI32 × ZC_PHI32_INV ≡ 1 (mod 2^32)

// Omega base-4 type mapping (from hdgl_genome_fabric.c BASE_* constants)
const BASE_A = 0;  // CPU, RUNTIME   — energetic / executing
const BASE_C = 1;  // MEM, default   — structural
const BASE_G = 2;  // IO             — bridging
const BASE_T = 3;  // COMPILER, STORAGE — templating / stable

// Omega node types (OTYPE_* from hdgl_genome_fabric.c)
const OTYPE = {
  ROOT: 0, CPU: 1, MEM: 2, IO: 3, COMPILER: 4, RUNTIME: 5,
  BOOTSTRAP: 6, REPLICA: 7, PCI: 8, GPU: 9, STORAGE: 10, BOOT: 11, PEER: 12
};

// Wu-Wei pass types (must match PASS const in server.js)
const PASS = {
  FETCH: 'fetch', SHELL: 'shell', CODE: 'code', TRANSFORM: 'transform',
  STORE: 'store', RECALL: 'recall', BROWSE: 'browse',
  NOTIFY: 'notify', RESPOND: 'respond'
};

// Fabric ports
const ZCHG_PORT       = 8090;
const HEALTH_TIMEOUT  = 200;    // ms — discovery window per phase
const FILESWAP_TIMEOUT= 5000;   // ms — remote write timeout

// Environment overrides
const FABRIC_PEERS_ENV  = process.env.FABRIC_PEERS || '';   // "ip:port,ip:port"
const FABRIC_GENOME_FP  = process.env.FABRIC_GENOME_FP
                          ? parseInt(process.env.FABRIC_GENOME_FP, 16) : 0;

// ─────────────────────────────────────────────────────────────────────────────
// PHI-LATTICE PRIMITIVES
// Exact JS ports of zchg_carrier.h / hdgl_fabric_loader.c primitives.
// All arithmetic wraps at 2^32 using BigInt to avoid IEEE-754 overflow.
// ─────────────────────────────────────────────────────────────────────────────

/** zc_fold: phi_fold32(x, key, seq) — additive, no XOR crypto path */
export function phiFold32(x, key, seq) {
  const MOD = 0x100000000n;
  const r = (
    (BigInt(x >>> 0) * BigInt(PHI32) +
     BigInt(key >>> 0) * BigInt(FIB32) +
     BigInt(seq >>> 0) * BigInt(SQRT_PHI32)) % MOD
  );
  return Number(r);
}

/** zc_unfold: inverse of phiFold32 */
export function phiUnfold32(f, key, seq) {
  const MOD = 0x100000000n;
  const raw = (
    (BigInt(f >>> 0) -
     BigInt(key >>> 0) * BigInt(FIB32) -
     BigInt(seq >>> 0) * BigInt(SQRT_PHI32) + MOD * 2n) % MOD
  );
  return Number((raw * BigInt(PHI32_INV)) % MOD) >>> 0;
}

/**
 * phiTauStrand: FNV-phi spiral strand routing (mirrors hdgl_fabric_loader.c).
 * Maps any uint64 identity to strand index 0..strandCount-1.
 * We accept a BigInt for the full 64-bit address.
 */
export function phiTauStrand(phiAddr, strandCount) {
  if (typeof phiAddr !== 'bigint') phiAddr = BigInt(phiAddr >>> 0);
  const FNV_PRIME = 0x100000001b3n;
  const FNV_INIT  = 0xcbf29ce484222325n;
  const PHI_MIX   = 1618033988n;
  const MOD64     = 0x10000000000000000n;
  let h = (phiAddr ^ FNV_INIT) % MOD64;
  h = (h * FNV_PRIME) % MOD64;
  h = (((h << 13n) | (h >> 51n)) ^ PHI_MIX) % MOD64;
  return Number(h % BigInt(strandCount));
}

/**
 * contentHash64: prismatic recursion (matches zchg_carrier_payload_hash and
 * fabric_content_hash in hdgl_fabric_loader.c — same accumulator style).
 * Accepts Buffer, string, or Uint8Array.  Returns BigInt.
 */
export function contentHash64(data) {
  if (typeof data === 'string') data = Buffer.from(data, 'utf8');
  const MOD = 0x10000000000000000n;
  let acc = (BigInt(PHI32) << 32n) | BigInt(FIB32);
  for (let i = 0; i < data.length; i++) {
    acc = (acc * 3n + BigInt(data[i])) % MOD;
  }
  return acc;
}

/**
 * fabricIdentity64: phi_fold over content_hash hi/lo (mirrors hdgl_fabric_load).
 * Returns a BigInt representing the 64-bit payload identity.
 */
export function fabricIdentity64(data, genomeFp) {
  const h = contentHash64(data);
  const hi = Number(h >> 32n);
  const lo = Number(h & 0xFFFFFFFFn);
  const idLo = phiFold32(hi, genomeFp, 0);
  const idHi = phiFold32(lo, genomeFp, 1);
  return (BigInt(idHi) << 32n) | BigInt(idLo);
}

/**
 * buildGenomeFp: derive a 32-bit genome_fp from an arbitrary seed.
 * Used when FABRIC_GENOME_FP is not set — derives from the rail's phi floor.
 * Mirrors the gossip_fingerprint derivation in hdgl_genome_fabric.c.
 */
export function buildGenomeFp(seed) {
  if (Buffer.isBuffer(seed) || seed instanceof Uint8Array) {
    // djb2 variant over seed bytes — matches genome_hash derivation
    let h = 5381;
    for (let i = 0; i < seed.length; i++) {
      h = ((h << 5) + h + seed[i]) >>> 0;
    }
    return h;
  }
  if (typeof seed === 'string') return buildGenomeFp(Buffer.from(seed));
  return phiFold32(seed >>> 0, PHI32, 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// LOCAL GENOME FP
// Derived once from the analog-container rail's consciousFloor128.
// Falls back to env var, then to a phi-fold of the process pid.
// ─────────────────────────────────────────────────────────────────────────────

let _cachedGenomeFp = null;

export function localGenomeFp() {
  if (_cachedGenomeFp !== null) return _cachedGenomeFp;
  if (FABRIC_GENOME_FP) { _cachedGenomeFp = FABRIC_GENOME_FP; return _cachedGenomeFp; }
  const rail = getRail();
  if (rail && rail.ctx.consciousFloor128) {
    _cachedGenomeFp = buildGenomeFp(rail.ctx.consciousFloor128.slice(0, 32));
  } else {
    _cachedGenomeFp = phiFold32(process.pid, PHI32, 0);
  }
  return _cachedGenomeFp;
}

// ─────────────────────────────────────────────────────────────────────────────
// HEALTH FRAME CODEC
// Mirrors the zchg HEALTH frame layout from hdgl_peer_discovery.hdgl §emit.
// Wire layout (52 bytes):
//   [0]   version  = 1
//   [1]   type     = 4 (HEALTH)
//   [2-5] strand_id = 0
//   [6-9] reserved  = 0
//   [10-13] authority_ep = 0
//   [14-17] source_ip
//   [18-21] payload_len = 0
//   [22-29] timestamp (phi_tick as uint64, little-endian)
//   [30-61] HMAC zeros (no shared secret yet in discovery phase)
// ─────────────────────────────────────────────────────────────────────────────

/** Build a 52-byte HEALTH request frame */
function buildHealthFrame(localIp32) {
  const buf = Buffer.alloc(52, 0);
  buf[0] = 1;            // version
  buf[1] = 4;            // HEALTH
  buf.writeUInt32LE(0, 2);  // strand_id
  buf.writeUInt32LE(0, 6);  // reserved
  buf.writeUInt32LE(0, 10); // authority_ep
  buf.writeUInt32BE(localIp32 >>> 0, 14); // source_ip
  buf.writeUInt32LE(0, 18); // payload_len
  const tick = BigInt(Date.now());
  buf.writeBigUInt64LE(tick, 22);
  // HMAC bytes 30..61: zeros (discovery phase — no shared secret)
  return buf;
}

/** Extract source_ip uint32 from a received HEALTH frame */
function healthFrameSourceIp(frame) {
  if (frame.length < 18) return 0;
  return frame.readUInt32BE(14);
}

/** uint32 IP to dotted string */
function ip32ToDotted(ip32) {
  return [
    (ip32 >>> 24) & 0xFF,
    (ip32 >>> 16) & 0xFF,
    (ip32 >>> 8)  & 0xFF,
     ip32         & 0xFF,
  ].join('.');
}

/** Dotted string to uint32 */
function dottedToIp32(s) {
  const parts = s.split('.').map(Number);
  if (parts.length !== 4) return 0;
  return ((parts[0] << 24) | (parts[1] << 16) | (parts[2] << 8) | parts[3]) >>> 0;
}

/**
 * Parse a JSON health-response blob from a fabric HTTP shim.
 * Format mirrors zchg_gossip_msg_t (zchg_core_patch.h):
 * {
 *   source_ip:           string (dotted) or uint32,
 *   strand_weights:      number[8]   (0–100 each),
 *   storage_available:   number,
 *   cluster_fingerprint: number,
 *   dn_aggregate:        number,
 *   genome_fingerprint:  number,
 *   omega_types:         number[]    (OTYPE_* for each node — optional),
 *   capabilities:        string[]    (optional — Slot4096 caps)
 * }
 */
export function parseHealthResponse(raw) {
  let obj;
  try {
    obj = (typeof raw === 'string') ? JSON.parse(raw) : raw;
  } catch {
    return null;
  }
  if (!obj || typeof obj !== 'object') return null;

  const srcRaw = obj.source_ip;
  const ip32   = (typeof srcRaw === 'number') ? (srcRaw >>> 0)
               : (typeof srcRaw === 'string') ? dottedToIp32(srcRaw)
               : 0;

  const sw = Array.isArray(obj.strand_weights)
    ? obj.strand_weights.slice(0, 8).map(v => Math.max(0, Math.min(255, v >>> 0)))
    : new Array(8).fill(0);

  while (sw.length < 8) sw.push(0);

  return {
    ip32,
    ip:                 ip32ToDotted(ip32),
    port:               (obj.port >>> 0) || ZCHG_PORT,
    strandWeights:      sw,
    storageAvailable:   (obj.storage_available >>> 0) || 0,
    clusterFingerprint: (obj.cluster_fingerprint >>> 0) || 0,
    dnAggregate:        (obj.dn_aggregate >>> 0) || 0,
    genomeFp:           (obj.genome_fingerprint >>> 0) || 0,
    omegaTypes:         Array.isArray(obj.omega_types) ? obj.omega_types : [],
    capabilities:       Array.isArray(obj.capabilities) ? obj.capabilities : [],
    strandCount:        (obj.strand_count >>> 0) || 8,
    lastSeen:           Date.now(),
  };
}

// ─────────────────────────────────────────────────────────────────────────────
// DISCOVERY
// Phase 1: phi-seed multicast (mirrors hdgl_peer_discovery.hdgl §phi_seed)
// Phase 2: FABRIC_PEERS env var static list (mirrors §sector_seed)
// Phase 3: HTTP health poll on already-known peers (extends §register)
// ─────────────────────────────────────────────────────────────────────────────

/**
 * Derive the phi-seed multicast address from genome_fp.
 * Mirrors: mcast_ip = 0xEF000000 | (phi_fold(genome_fp, 0, 0) & 0x00FFFFFF)
 */
function phiSeedMcastIp(genomeFp) {
  const low24 = phiFold32(genomeFp, 0, 0) & 0x00FFFFFF;
  return ip32ToDotted(0xEF000000 | low24);
}

/**
 * Phase 1: UDP multicast HEALTH broadcast.
 * Sends to the phi-derived 239.x.x.x address on ZCHG_PORT.
 * Collects JSON health responses within timeoutMs.
 */
async function discoverViaMulticast(genomeFp, timeoutMs) {
  const mcastAddr = phiSeedMcastIp(genomeFp);
  const peers = new Map();

  return new Promise((resolve) => {
    let done = false;
    const finish = () => {
      if (done) return;
      done = true;
      try { sock.close(); } catch {}
      resolve([...peers.values()]);
    };

    const sock = dgram.createSocket({ type: 'udp4', reuseAddr: true });

    sock.on('error', () => finish());

    sock.on('message', (msg, rinfo) => {
      if (done) return;
      // Accept both raw zchg HEALTH frames (version=1, type=4) and JSON blobs
      let peer = null;
      if (msg[0] === 1 && msg[1] === 4 && msg.length >= 18) {
        // Raw HEALTH frame — extract IP only, fill defaults
        const ip32 = healthFrameSourceIp(msg);
        if (ip32 === 0) return;
        const ip = ip32ToDotted(ip32);
        peer = {
          ip32, ip,
          port: ZCHG_PORT,
          strandWeights:      new Array(8).fill(0),
          storageAvailable:   0,
          clusterFingerprint: 0,
          dnAggregate:        0,
          genomeFp:           0,
          omegaTypes:         [],
          capabilities:       [],
          strandCount:        8,
          lastSeen:           Date.now(),
          _needsHealthPoll:   true,
        };
      } else {
        try {
          const json = msg.toString('utf8').trim();
          if (json[0] === '{') peer = parseHealthResponse(json);
        } catch {}
      }
      if (peer && peer.ip32) {
        peers.set(peer.ip, peer);
      }
    });

    sock.bind(ZCHG_PORT, () => {
      try {
        sock.addMembership(mcastAddr);
        sock.setMulticastTTL(1);
        // Build and send HEALTH frame
        const localIp32 = dottedToIp32(_getLocalIp());
        const frame = buildHealthFrame(localIp32);
        sock.send(frame, 0, frame.length, ZCHG_PORT, mcastAddr);
      } catch {
        // Multicast may be blocked on this interface — fall through
      }
      setTimeout(finish, timeoutMs);
    });
  });
}

/**
 * Phase 2: Parse FABRIC_PEERS env var.
 * Format: "192.168.1.10:8090,192.168.1.11:8090"
 * Returns minimal peer stubs — HTTP health poll will fill the rest.
 */
function discoverViaEnvPeers() {
  if (!FABRIC_PEERS_ENV.trim()) return [];
  return FABRIC_PEERS_ENV.split(',').map(s => {
    const [ipPart, portPart] = s.trim().split(':');
    const ip   = ipPart.trim();
    const port = portPart ? parseInt(portPart, 10) : ZCHG_PORT;
    if (!net.isIPv4(ip)) return null;
    return {
      ip32:               dottedToIp32(ip),
      ip,
      port,
      strandWeights:      new Array(8).fill(0),
      storageAvailable:   0,
      clusterFingerprint: 0,
      dnAggregate:        0,
      genomeFp:           0,
      omegaTypes:         [],
      capabilities:       [],
      strandCount:        8,
      lastSeen:           0,
      _needsHealthPoll:   true,
    };
  }).filter(Boolean);
}

/**
 * Phase 3: HTTP health poll — GET http://ip:port/health
 * Fabric nodes expose this via the zchg HTTP shim.
 * Silently skips nodes that don't respond within timeoutMs.
 */
async function pollHealthHttp(peer, timeoutMs = 1000) {
  return new Promise((resolve) => {
    const req = http.get(
      { host: peer.ip, port: peer.port, path: '/health', timeout: timeoutMs },
      (res) => {
        const chunks = [];
        res.on('data', d => chunks.push(d));
        res.on('end', () => {
          const body = Buffer.concat(chunks).toString('utf8');
          const parsed = parseHealthResponse(body);
          if (parsed) {
            // Preserve original IP/port if the response omits them
            if (!parsed.ip32) parsed.ip32 = peer.ip32;
            if (!parsed.ip)   parsed.ip   = peer.ip;
            if (!parsed.port) parsed.port = peer.port;
            resolve(parsed);
          } else {
            // Node responded but payload is unrecognised — keep stub
            peer.lastSeen = Date.now();
            peer._needsHealthPoll = false;
            resolve(peer);
          }
        });
        res.on('error', () => resolve(null));
      }
    );
    req.on('error', () => resolve(null));
    req.on('timeout', () => { req.destroy(); resolve(null); });
  });
}

/**
 * discoverFabricNodes — main discovery entry point.
 *
 * @param {number}  genomeFp    32-bit genome_fp (use localGenomeFp() if unsure)
 * @param {object}  [opts]
 * @param {number}  [opts.timeoutMs=200]   multicast window
 * @param {boolean} [opts.skipMulticast]   skip UDP phase (e.g. on Windows w/o mcast)
 * @param {boolean} [opts.skipHttpPoll]    skip HTTP health poll phase
 * @returns {Promise<PeerTable[]>}
 *
 * PeerTable:
 *   ip, port, ip32, strandWeights[8], storageAvailable,
 *   clusterFingerprint, dnAggregate, genomeFp, omegaTypes[],
 *   capabilities[], strandCount, lastSeen, local
 */
export async function discoverFabricNodes(genomeFp = localGenomeFp(), opts = {}) {
  const {
    timeoutMs   = HEALTH_TIMEOUT,
    skipMulticast = false,
    skipHttpPoll  = false,
  } = opts;

  const seen = new Map();  // ip → peer

  // Mark self as local
  const selfIp = _getLocalIp();

  const addPeer = (p) => {
    if (!p || !p.ip) return;
    if (p.ip === selfIp || p.ip === '127.0.0.1' || p.ip === '::1') return;
    const existing = seen.get(p.ip);
    if (!existing || p.lastSeen > existing.lastSeen) {
      seen.set(p.ip, { ...p, local: false });
    }
  };

  // Phase 1: multicast
  if (!skipMulticast) {
    try {
      const mcastPeers = await discoverViaMulticast(genomeFp, timeoutMs);
      mcastPeers.forEach(addPeer);
    } catch { /* multicast unavailable — not fatal */ }
  }

  // Phase 2: env var static peers
  const envPeers = discoverViaEnvPeers();
  envPeers.forEach(addPeer);

  // Phase 3: HTTP health poll for stubs and unpolled peers
  if (!skipHttpPoll) {
    const pollTargets = [...seen.values()].filter(p => p._needsHealthPoll || p.lastSeen === 0);
    const polled = await Promise.all(
      pollTargets.map(p => pollHealthHttp(p, Math.max(timeoutMs * 2, 500)))
    );
    polled.forEach(p => { if (p) addPeer(p); });
  }

  // Add self as a local-flagged entry so routing can fall back cleanly
  const selfEntry = {
    ip:                 selfIp,
    ip32:               dottedToIp32(selfIp),
    port:               parseInt(process.env.MCP_PORT || '4111', 10),
    strandWeights:      new Array(8).fill(50),  // neutral authority
    storageAvailable:   0,
    clusterFingerprint: 0,
    dnAggregate:        0,
    genomeFp:           genomeFp,
    omegaTypes:         [],
    capabilities:       [],
    strandCount:        8,
    lastSeen:           Date.now(),
    local:              true,
  };
  seen.set(selfIp, selfEntry);

  return [...seen.values()];
}

// ─────────────────────────────────────────────────────────────────────────────
// PASS → BASE-4 TYPE MAPPING
// Maps Wu-Wei pass types to Omega base types and selects the best-fit peer.
// Mirrors the OTYPE_* / BASE_* mapping in hdgl_genome_fabric.c.
// ─────────────────────────────────────────────────────────────────────────────

/**
 * passTypeToBase: maps Wu-Wei PASS type to Omega BASE_*.
 * FETCH/BROWSE       → BASE_G (IO nodes)
 * CODE/SHELL/TRANSFORM → BASE_A (CPU/runtime nodes), GPU preferred for TRANSFORM
 * STORE/RECALL/NOTIFY → BASE_T / BASE_C (storage / structural)
 * RESPOND            → local (no routing)
 */
function passTypeToBase(passType) {
  switch (passType) {
    case PASS.FETCH:
    case PASS.BROWSE:
      return { base: BASE_G, preferGpu: false };
    case PASS.CODE:
    case PASS.SHELL:
      return { base: BASE_A, preferGpu: false };
    case PASS.TRANSFORM:
      return { base: BASE_A, preferGpu: true };
    case PASS.STORE:
      return { base: BASE_T, preferGpu: false };
    case PASS.RECALL:
      return { base: BASE_C, preferGpu: false };
    case PASS.NOTIFY:
      return { base: BASE_T, preferGpu: false };
    default:
      return { base: BASE_A, preferGpu: false };
  }
}

/**
 * strandAuthScore: given a peer's strandWeights[8], compute a score for the
 * requested base type.
 *
 * The 8 strand_weight bytes map to strands 0..7.
 * Base-type authority is the sum of strand weights on strands where
 * phiTauStrand(strandIndex, 4) maps to the requested base:
 *   strand 0,4 → BASE_A (mod 4 = 0)
 *   strand 1,5 → BASE_C (mod 4 = 1)
 *   strand 2,6 → BASE_G (mod 4 = 2)
 *   strand 3,7 → BASE_T (mod 4 = 3)
 *
 * This is a lightweight approximation of the full Omega-graph authority
 * computation — suitable for the JS layer where the full genome is not loaded.
 */
function strandAuthScore(strandWeights, base) {
  let score = 0;
  for (let i = 0; i < 8; i++) {
    if ((i % 4) === base) score += strandWeights[i] || 0;
  }
  return score;
}

/**
 * routePassToNode — select best-fit node for a Wu-Wei pass.
 *
 * @param {string|Buffer} taskHash   Task identity (content-hashed string or Buffer)
 * @param {string}        passType   Wu-Wei PASS.* string
 * @param {PeerTable[]}   peerTable  From discoverFabricNodes()
 * @returns {{ ip, port, strandAuth, genomeFp, local: bool, score: number }}
 */
export function routePassToNode(taskHash, passType, peerTable) {
  // RESPOND always stays local
  if (passType === PASS.RESPOND) {
    return _localRouteResult(peerTable);
  }

  const { base, preferGpu } = passTypeToBase(passType);

  // Phi-hash the task to get a tiebreaker for peers with equal scores.
  // Mirrors phiHash() in coord-proxy.js — same distribution property.
  const hashNum = typeof taskHash === 'string'
    ? parseInt(crypto.createHash('sha256').update(taskHash).digest('hex').slice(0, 8), 16)
    : Number(contentHash64(taskHash) & 0xFFFFFFFFn);
  const phiTie = ((hashNum * PHI) % 1000000) / 1000000;

  let best = null;
  let bestScore = -1;

  for (const peer of peerTable) {
    let score = strandAuthScore(peer.strandWeights, base);

    // GPU bonus for TRANSFORM pass
    if (preferGpu && peer.omegaTypes.includes(OTYPE.GPU)) {
      score += 100;
    }

    // Tiebreaker: phi-hash offset
    score += phiTie;

    // Penalise stale peers (not seen in the last 30s)
    if (!peer.local && Date.now() - peer.lastSeen > 30000) {
      score -= 200;
    }

    if (score > bestScore) {
      bestScore = score;
      best = peer;
    }
  }

  if (!best || best.local) {
    return _localRouteResult(peerTable, bestScore);
  }

  return {
    ip:          best.ip,
    port:        best.port,
    strandAuth:  best.strandWeights,
    genomeFp:    best.genomeFp,
    local:       false,
    score:       bestScore,
  };
}

function _localRouteResult(peerTable, score = 0) {
  const self = peerTable.find(p => p.local);
  return {
    ip:         self ? self.ip   : _getLocalIp(),
    port:       self ? self.port : parseInt(process.env.MCP_PORT || '4111', 10),
    strandAuth: self ? self.strandWeights : new Array(8).fill(50),
    genomeFp:   localGenomeFp(),
    local:      true,
    score,
  };
}

// ─────────────────────────────────────────────────────────────────────────────
// REMOTE PASS FORWARDING
// When routePassToNode returns local=false, forward the pass to the remote
// node's MCP /tools/call endpoint.
// ─────────────────────────────────────────────────────────────────────────────

/**
 * fabricForwardPass — forward a Wu-Wei pass execution to a remote node.
 *
 * Sends a JSON-RPC-style tool call to the remote MCP server's /tools/call
 * endpoint (same format as @modelcontextprotocol/sdk server).
 *
 * @param {{ ip, port }} target    From routePassToNode
 * @param {string}       toolName  e.g. 'shell', 'fs_read', 'unfold'
 * @param {object}       args      Tool arguments
 * @param {number}       [timeoutMs=10000]
 * @returns {Promise<{ content: any[], isError?: boolean }>}
 */
export async function fabricForwardPass(target, toolName, args, timeoutMs = 10000) {
  const body = JSON.stringify({
    jsonrpc: '2.0',
    id:      Date.now(),
    method:  'tools/call',
    params:  { name: toolName, arguments: args },
  });

  return new Promise((resolve, reject) => {
    const req = http.request({
      host:    target.ip,
      port:    target.port,
      path:    '/tools/call',
      method:  'POST',
      headers: {
        'Content-Type':   'application/json',
        'Content-Length': Buffer.byteLength(body),
        'X-Fabric-Fp':    String(localGenomeFp()),
      },
      timeout: timeoutMs,
    }, (res) => {
      const chunks = [];
      res.on('data', d => chunks.push(d));
      res.on('end', () => {
        try {
          const resp = JSON.parse(Buffer.concat(chunks).toString('utf8'));
          resolve(resp.result || { content: [], isError: true });
        } catch (e) {
          reject(e);
        }
      });
    });
    req.on('error', reject);
    req.on('timeout', () => { req.destroy(); reject(new Error('forward timeout')); });
    req.write(body);
    req.end();
  });
}

// ─────────────────────────────────────────────────────────────────────────────
// FABRIC ERL APPEND
// Extends erlAppend() to route each ledger entry to the authoritative strand
// node via HTTP FILESWAP framing.
// ─────────────────────────────────────────────────────────────────────────────

/**
 * fabricErlAppend — append an ERL entry to the authoritative strand node.
 *
 * @param {object}      ledger      The local ERL ledger object (from erlLoad())
 * @param {object}      entry       { branch, role, content, tags, sessionId }
 * @param {PeerTable[]} peerTable   From discoverFabricNodes()
 * @param {Function}    localErlAppend  The original erlAppend() from server.js
 * @returns {Promise<{ id, node, strand, phi_addr }>}
 */
export async function fabricErlAppend(ledger, entry, peerTable, localErlAppend) {
  // Always do the local append first — this guarantees chain integrity
  // and lets the local node serve as fallback.
  const localEntry = localErlAppend(ledger, entry);
  const entryId    = localEntry.id;

  // Compute phi-address and strand for this entry
  const genomeFp   = localGenomeFp();
  const phiAddr64  = fabricIdentity64(entryId, genomeFp);
  const strandCount = 8;
  const strand     = phiTauStrand(phiAddr64, strandCount);

  // Find the authoritative node for this strand
  const base = strand % 4;
  const routeResult = routePassToNode(entryId, _baseToPassType(base), peerTable);

  if (!routeResult.local) {
    // Ship the entry to the remote authoritative node as a FILESWAP payload
    try {
      await _remoteFileswapStore(routeResult, {
        type:        'erl_entry',
        entry:       localEntry,
        phi_addr:    phiAddr64.toString(16),
        strand,
        genome_fp:   genomeFp,
        source_ip:   _getLocalIp(),
      });
    } catch {
      // Remote write failed — local copy is the fallback, this is non-fatal
    }
  }

  return {
    id:       entryId,
    node:     routeResult.local ? 'local' : routeResult.ip,
    strand,
    phi_addr: '0x' + phiAddr64.toString(16).padStart(16, '0'),
  };
}

/** Map base type back to a representative pass type for routing */
function _baseToPassType(base) {
  switch (base) {
    case BASE_A: return PASS.CODE;
    case BASE_C: return PASS.RECALL;
    case BASE_G: return PASS.FETCH;
    case BASE_T: return PASS.STORE;
    default:     return PASS.STORE;
  }
}

/**
 * _remoteFileswapStore — POST a JSON payload to /fabric/store on a remote node.
 * Remote nodes expose this via their zchg HTTP shim or the Wu-Wei MCP server.
 */
async function _remoteFileswapStore(target, payload) {
  const body = JSON.stringify(payload);
  return new Promise((resolve, reject) => {
    const req = http.request({
      host:    target.ip,
      port:    target.port,
      path:    '/fabric/store',
      method:  'POST',
      headers: {
        'Content-Type':   'application/json',
        'Content-Length': Buffer.byteLength(body),
        'X-Fabric-Fp':    String(localGenomeFp()),
      },
      timeout: FILESWAP_TIMEOUT,
    }, (res) => {
      res.resume();
      resolve({ status: res.statusCode });
    });
    req.on('error', reject);
    req.on('timeout', () => { req.destroy(); reject(new Error('fileswap timeout')); });
    req.write(body);
    req.end();
  });
}

// ─────────────────────────────────────────────────────────────────────────────
// FABRIC STORE RECEIVER
// HTTP handler for /fabric/store — receives FILESWAP payloads from peers.
// Mount on the existing http.Server in server.js:
//   fabricStoreHandler(req, res)  — call from existing request handler
// ─────────────────────────────────────────────────────────────────────────────

const _remoteStore = new Map();  // phi_addr → payload

/**
 * fabricStoreHandler — handle incoming /fabric/store POST from a peer node.
 * Call this from server.js request handler before your existing route tree.
 *
 * Example integration in server.js:
 *   import { fabricStoreHandler, fabricQueryHandler } from './fabric_node_agent.mjs';
 *   // In your http.createServer callback:
 *   if (req.method === 'POST' && url.pathname === '/fabric/store') {
 *     return fabricStoreHandler(req, res);
 *   }
 *   if (req.method === 'GET' && url.pathname === '/fabric/query') {
 *     return fabricQueryHandler(req, res);
 *   }
 */
export function fabricStoreHandler(req, res) {
  const chunks = [];
  req.on('data', d => chunks.push(d));
  req.on('end', () => {
    try {
      const payload = JSON.parse(Buffer.concat(chunks).toString('utf8'));
      const key = payload.phi_addr || payload.entry?.id || String(Date.now());
      _remoteStore.set(key, { ...payload, received_at: Date.now() });
      res.writeHead(200, { 'Content-Type': 'application/json' });
      res.end(JSON.stringify({ ok: true, key }));
    } catch (e) {
      res.writeHead(400);
      res.end(JSON.stringify({ ok: false, error: e.message }));
    }
  });
}

/**
 * fabricQueryHandler — handle GET /fabric/query?phi_addr=...
 * Allows peers to reconstruct entries stored on this node.
 */
export function fabricQueryHandler(req, res) {
  const u = new URL(req.url, `http://localhost`);
  const key = u.searchParams.get('phi_addr') || u.searchParams.get('id');
  if (key && _remoteStore.has(key)) {
    res.writeHead(200, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify(_remoteStore.get(key)));
  } else {
    // Return all known remote entries (paginate if needed)
    res.writeHead(200, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify({
      count:   _remoteStore.size,
      entries: [..._remoteStore.entries()].slice(-100).map(([k, v]) => ({
        key: k, strand: v.strand, received_at: v.received_at
      })),
    }));
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// /health ENDPOINT — exposes this node's gossip_msg_t payload to peers
// Mount on the existing http.Server in server.js at GET /health.
// ─────────────────────────────────────────────────────────────────────────────

/**
 * fabricHealthHandler — respond with this node's Omega census JSON.
 * Peers use this to build their peer table without multicast.
 *
 * Integration in server.js:
 *   if (req.method === 'GET' && url.pathname === '/health') {
 *     return fabricHealthHandler(req, res);
 *   }
 */
export function fabricHealthHandler(req, res) {
  const genomeFp = localGenomeFp();
  const rail     = getRail();

  // Build strand weights from the rail's phi-score
  // (a full Omega graph census would populate these from actual hardware)
  const phiScore = rail ? rail.ctx.phiScore() : 0.5;
  const strandWeights = Array.from({ length: 8 }, (_, i) => {
    const w = phiFold32(genomeFp, i, 0) % 101;
    return w;
  });

  const payload = {
    source_ip:           _getLocalIp(),
    port:                parseInt(process.env.MCP_PORT || '4111', 10),
    strand_weights:      strandWeights,
    storage_available:   _estimateStorageAvailable(),
    cluster_fingerprint: genomeFp,
    dn_aggregate:        Math.floor(phiScore * 0xFFFFFFFF) >>> 0,
    genome_fingerprint:  genomeFp,
    strand_count:        8,
    capabilities:        rail ? (rail.ctx ? ['erl', 'phi-hash', 'wu-wei', 'hdgl'] : []) : [],
    omega_types:         _probeOmegaTypes(),
    phi_score:           phiScore.toFixed(6),
    slot4096_hostname:   rail ? rail.label : 'unknown',
    server:              'easy-zchg-fabric-agent-v1',
    ts:                  Date.now(),
  };

  res.writeHead(200, {
    'Content-Type':  'application/json',
    'X-Fabric-Fp':   String(genomeFp),
  });
  res.end(JSON.stringify(payload));
}

// ─────────────────────────────────────────────────────────────────────────────
// MONKEY-PATCHES FOR server.js
// patchSelectPassSequence and patchRunUnfold wrap the originals to add
// multi-node routing transparently.
// ─────────────────────────────────────────────────────────────────────────────

/**
 * patchSelectPassSequence — wraps the original selectPassSequence to insert
 * a leading RECALL-style "fabric census" pass for multi-step tasks.
 *
 * The patched version adds a `_fabricPeerTable` hint to the analysis object
 * so that patchRunUnfold can use it without re-running discovery.
 *
 * Usage in server.js (replace the function definition):
 *   const selectPassSequence = patchSelectPassSequence(originalSelectPassSequence);
 */
export function patchSelectPassSequence(original) {
  return function patchedSelectPassSequence(analysis, task) {
    const seq = original(analysis, task);
    // Inject fabric routing metadata into the sequence result
    seq._fabricEnabled = true;
    seq._genomeFp      = localGenomeFp();
    return seq;
  };
}

/**
 * patchRunUnfold — wraps a single pass execution to add fabric routing.
 *
 * Call this inside your unfold pass loop, before executing each pass:
 *
 *   const patchedRun = patchRunUnfold(originalRunPass);
 *   // In the pass loop:
 *   const result = await patchedRun(pass, state, passIndex, sequence);
 *
 * The patch:
 *   1. Runs discoverFabricNodes() once per unfold() call (cached on state).
 *   2. For each non-RESPOND pass, calls routePassToNode().
 *   3. If the result is remote, calls fabricForwardPass() instead of local exec.
 *   4. Logs routing decisions to state.pass_log.
 */
export function patchRunUnfold(originalRunPass) {
  return async function patchedRunPass(pass, state, passIndex, sequence) {
    // Discover peers once per unfold — cached on state
    if (!state._fabricPeerTable) {
      try {
        state._fabricPeerTable = await discoverFabricNodes(
          sequence._genomeFp || localGenomeFp(),
          { timeoutMs: 100, skipMulticast: false, skipHttpPoll: true }
        );
      } catch {
        state._fabricPeerTable = [];
      }
    }

    const peerTable = state._fabricPeerTable;

    // Only route non-trivial passes
    if (pass === PASS.RESPOND || !peerTable.length) {
      return originalRunPass(pass, state, passIndex, sequence);
    }

    const taskId    = state.task_id || state.cwd || String(Date.now());
    const target    = routePassToNode(taskId, pass, peerTable);

    // Log the routing decision
    if (!state.pass_log) state.pass_log = [];
    state.pass_log.push({
      pass,
      passIndex,
      target:    target.local ? 'local' : `${target.ip}:${target.port}`,
      score:     target.score,
      timestamp: Date.now(),
    });

    if (target.local) {
      // Execute locally as before
      return originalRunPass(pass, state, passIndex, sequence);
    }

    // Remote execution — forward via MCP tools/call
    try {
      const toolName = _passToToolName(pass);
      const args     = _stateToToolArgs(pass, state);
      const result   = await fabricForwardPass(target, toolName, args);
      // Merge remote result into local state
      state.data         = result.content?.[0]?.text ?? state.data;
      state.last_stdout  = state.data;
      state.last_node    = target.ip;
      return { success: true, remote: true, node: target.ip, result };
    } catch (e) {
      // Remote failed — fall back to local
      state.pass_log[state.pass_log.length - 1].fallback = 'local';
      return originalRunPass(pass, state, passIndex, sequence);
    }
  };
}

// ─────────────────────────────────────────────────────────────────────────────
// PEER TABLE CACHING
// Avoid re-discovering on every single pass within the same server lifetime.
// ─────────────────────────────────────────────────────────────────────────────

let _peerCache     = null;
let _peerCacheTime = 0;
const PEER_CACHE_TTL = 30_000; // ms

/**
 * getCachedPeerTable — returns a cached peer table, refreshing if stale.
 * Use this in high-frequency call paths (e.g. every pass in a long unfold).
 */
export async function getCachedPeerTable(genomeFp = localGenomeFp()) {
  if (_peerCache && Date.now() - _peerCacheTime < PEER_CACHE_TTL) {
    return _peerCache;
  }
  _peerCache     = await discoverFabricNodes(genomeFp, { timeoutMs: 100 });
  _peerCacheTime = Date.now();
  return _peerCache;
}

/** Invalidate the peer cache (call after peer table changes are detected) */
export function invalidatePeerCache() {
  _peerCache     = null;
  _peerCacheTime = 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// GOSSIP TICK
// Periodic gossip cycle — broadcasts local genome_fp and strand_weights
// to all known peers, mirrors zchg_gossip_cycle() timing.
// Call fabricGossipStart() once during server init.
// ─────────────────────────────────────────────────────────────────────────────

let _gossipTimer = null;
const GOSSIP_INTERVAL = 30_000; // ms — mirrors ZCHG_GOSSIP_INTERVAL

/**
 * fabricGossipStart — begin periodic gossip broadcasts.
 * Sends this node's health payload to all known peers over HTTP POST /gossip.
 */
export function fabricGossipStart() {
  if (_gossipTimer) return;
  _gossipTimer = setInterval(_gossipTick, GOSSIP_INTERVAL);
  _gossipTimer.unref?.();  // Don't keep process alive for gossip alone
}

export function fabricGossipStop() {
  if (_gossipTimer) {
    clearInterval(_gossipTimer);
    _gossipTimer = null;
  }
}

async function _gossipTick() {
  const peers = await getCachedPeerTable();
  const genomeFp  = localGenomeFp();
  const rail      = getRail();
  const phiScore  = rail ? rail.ctx.phiScore() : 0.5;

  const msg = {
    source_ip:           _getLocalIp(),
    strand_weights:      Array.from({ length: 8 }, (_, i) => phiFold32(genomeFp, i, 0) % 101),
    storage_available:   _estimateStorageAvailable(),
    cluster_fingerprint: genomeFp,
    dn_aggregate:        Math.floor(phiScore * 0xFFFFFFFF) >>> 0,
    genome_fingerprint:  genomeFp,
    ts:                  Date.now(),
  };

  const body = JSON.stringify(msg);
  for (const peer of peers) {
    if (peer.local) continue;
    try {
      const req = http.request({
        host: peer.ip, port: peer.port, path: '/gossip',
        method: 'POST',
        headers: { 'Content-Type': 'application/json', 'Content-Length': Buffer.byteLength(body) },
        timeout: 2000,
      });
      req.on('error', () => {});
      req.write(body);
      req.end();
    } catch {}
  }
}

/**
 * fabricGossipHandler — handle POST /gossip from a peer node.
 * Updates the peer table in place and invalidates the cache.
 *
 * Integration in server.js:
 *   if (req.method === 'POST' && url.pathname === '/gossip') {
 *     return fabricGossipHandler(req, res);
 *   }
 */
export function fabricGossipHandler(req, res) {
  const chunks = [];
  req.on('data', d => chunks.push(d));
  req.on('end', () => {
    try {
      const msg   = JSON.parse(Buffer.concat(chunks).toString('utf8'));
      const peer  = parseHealthResponse(msg);
      if (peer) {
        // Update cache entry
        if (_peerCache) {
          const idx = _peerCache.findIndex(p => p.ip === peer.ip);
          if (idx >= 0) {
            Object.assign(_peerCache[idx], peer, { local: false });
          } else {
            _peerCache.push({ ...peer, local: false });
          }
        }
      }
      res.writeHead(200);
      res.end('{"ok":true}');
    } catch {
      res.writeHead(400);
      res.end('{"ok":false}');
    }
  });
}

// ─────────────────────────────────────────────────────────────────────────────
// UTILITY HELPERS (private)
// ─────────────────────────────────────────────────────────────────────────────

/** Get a non-loopback IPv4 address for this process (sync, ESM-safe) */
import os from 'node:os';

let _localIpCache = '127.0.0.1';
;(function _initLocalIp() {
  try {
    const ifaces = os.networkInterfaces();
    for (const iface of Object.values(ifaces)) {
      for (const addr of iface) {
        if (addr.family === 'IPv4' && !addr.internal) {
          _localIpCache = addr.address;
          return;
        }
      }
    }
  } catch {}
})();

function _getLocalIp() { return _localIpCache; }

/** Estimate available storage in bytes (synchronous, uses os.freemem as proxy) */
function _estimateStorageAvailable() {
  try {
    return os.freemem();
  } catch {}
  return 0;
}

/** Probe Omega node types present on this machine */
function _probeOmegaTypes() {
  const types = [OTYPE.CPU];
  // Check for GPU (rough heuristic — proper impl reads CPUID/PCI)
  if (process.env.CUDA_VISIBLE_DEVICES || process.env.ROCR_VISIBLE_DEVICES) {
    types.push(OTYPE.GPU);
  }
  types.push(OTYPE.MEM);
  types.push(OTYPE.STORAGE);
  types.push(OTYPE.RUNTIME);
  return types;
}

/** Map a Wu-Wei pass type to a default tool name */
function _passToToolName(pass) {
  const MAP = {
    [PASS.FETCH]:     'web_fetch',
    [PASS.SHELL]:     'shell',
    [PASS.CODE]:      'code_exec',
    [PASS.TRANSFORM]: 'shell',
    [PASS.STORE]:     'fs_write',
    [PASS.RECALL]:    'fs_read',
    [PASS.BROWSE]:    'browser_navigate',
    [PASS.NOTIFY]:    'notify',
    [PASS.RESPOND]:   'respond',
  };
  return MAP[pass] || pass;
}

/** Extract tool args from FlowState for a given pass type */
function _stateToToolArgs(pass, state) {
  switch (pass) {
    case PASS.FETCH:   return { url: state.last_url || '', headers: {} };
    case PASS.SHELL:   return { command: state.last_stdout || '' };
    case PASS.CODE:    return { language: 'python', code: state.last_stdout || '' };
    case PASS.STORE:   return { path: state.last_path || '/tmp/fabric_out', content: state.data || '' };
    case PASS.RECALL:  return { path: state.last_path || '/tmp/fabric_out' };
    case PASS.BROWSE:  return { url: state.last_url || '' };
    default:           return { data: state.data || '' };
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// SERVER.JS INTEGRATION SHIM
// Convenience function that wires all the handlers onto an existing
// http.Server instance.  Call once after httpServer.listen().
// ─────────────────────────────────────────────────────────────────────────────

/**
 * attachFabricHandlers — mount all fabric HTTP endpoints onto an existing server.
 *
 * Wraps the server's existing request handler (req, res) => void with fabric
 * endpoints.  Intercepts before the original handler.
 *
 * Usage in server.js (after httpServer is created):
 *   import { attachFabricHandlers } from './fabric_node_agent.mjs';
 *   attachFabricHandlers(httpServer);
 *
 * Endpoints added:
 *   GET  /health          — this node's gossip_msg_t payload
 *   POST /gossip          — receive gossip from peers
 *   POST /fabric/store    — receive FILESWAP payloads
 *   GET  /fabric/query    — retrieve stored payloads
 *   GET  /fabric/peers    — list known peers
 *   GET  /fabric/status   — fabric agent status summary
 */
export function attachFabricHandlers(httpServer) {
  const originalListeners = httpServer.rawListeners('request');
  httpServer.removeAllListeners('request');

  httpServer.on('request', async (req, res) => {
    let pathname = req.url.split('?')[0];

    // Fabric endpoints — intercept before original handler
    if (req.method === 'GET' && pathname === '/health') {
      return fabricHealthHandler(req, res);
    }
    if (req.method === 'POST' && pathname === '/gossip') {
      return fabricGossipHandler(req, res);
    }
    if (req.method === 'POST' && pathname === '/fabric/store') {
      return fabricStoreHandler(req, res);
    }
    if (req.method === 'GET' && pathname === '/fabric/query') {
      return fabricQueryHandler(req, res);
    }
    if (req.method === 'GET' && pathname === '/fabric/peers') {
      const peers = await getCachedPeerTable();
      res.writeHead(200, { 'Content-Type': 'application/json' });
      return res.end(JSON.stringify({
        count: peers.length,
        peers: peers.map(p => ({
          ip: p.ip, port: p.port, local: p.local,
          strandWeights: p.strandWeights,
          genomeFp: p.genomeFp ? '0x' + p.genomeFp.toString(16) : null,
          lastSeen: p.lastSeen,
        })),
      }));
    }
    if (req.method === 'GET' && pathname === '/fabric/status') {
      const peers  = await getCachedPeerTable();
      const remote = peers.filter(p => !p.local);
      res.writeHead(200, { 'Content-Type': 'application/json' });
      return res.end(JSON.stringify({
        fabric_agent:  'fabric_node_agent v1.0.0',
        genome_fp:     '0x' + localGenomeFp().toString(16),
        phi_seed_mcast: phiSeedMcastIp(localGenomeFp()),
        local_ip:      _getLocalIp(),
        peer_count:    remote.length,
        gossip_active: _gossipTimer !== null,
        remote_stored: _remoteStore.size,
        axiom:         'Ωₙ₊₁ = T(Ωₙ)',
      }));
    }

    // Pass through to original handler(s)
    for (const listener of originalListeners) {
      listener.call(httpServer, req, res);
    }
  });

  // Start gossip after handlers are mounted
  fabricGossipStart();
}

// ─────────────────────────────────────────────────────────────────────────────
// MODULE SELF-TEST
// Run with: node fabric_node_agent.mjs --test
// ─────────────────────────────────────────────────────────────────────────────

if (process.argv.includes('--test')) {
  console.log('\n╔══════════════════════════════════════════════════════════╗');
  console.log('║  fabric_node_agent.mjs — self-test                      ║');
  console.log('╚══════════════════════════════════════════════════════════╝\n');

  let passed = 0;
  let failed = 0;
  function assert(label, got, expected) {
    const ok = got === expected;
    console.log(`  ${ok ? '✓' : '✗'} ${label}`);
    if (!ok) console.log(`      got:      ${got}\n      expected: ${expected}`);
    ok ? passed++ : failed++;
  }

  // 1. phiFold32 / phiUnfold32 round-trip
  {
    const x = 0xDEADBEEF;
    const k = 0x9E3779B9;
    const s = 42;
    const f = phiFold32(x, k, s);
    const u = phiUnfold32(f, k, s);
    assert('phiFold32/phiUnfold32 round-trip', u, x);
  }

  // 2. phiTauStrand within bounds
  {
    const addr = fabricIdentity64('hello world', 0xDEADBEEF);
    const strand = phiTauStrand(addr, 8);
    assert('phiTauStrand in [0,7]', strand >= 0 && strand < 8, true);
  }

  // 3. contentHash64 deterministic
  {
    const h1 = contentHash64('test payload');
    const h2 = contentHash64('test payload');
    assert('contentHash64 deterministic', h1 === h2, true);
  }

  // 4. parseHealthResponse roundtrip
  {
    const raw = {
      source_ip: '192.168.1.42', port: 8090,
      strand_weights: [10, 20, 30, 40, 50, 60, 70, 80],
      storage_available: 1000000, cluster_fingerprint: 0xABCD1234,
      dn_aggregate: 0x12345678, genome_fingerprint: 0xDEADBEEF,
    };
    const p = parseHealthResponse(JSON.stringify(raw));
    assert('parseHealthResponse ip', p.ip, '192.168.1.42');
    assert('parseHealthResponse strand_weights[4]', p.strandWeights[4], 50);
    assert('parseHealthResponse genomeFp', p.genomeFp, 0xDEADBEEF);
  }

  // 5. routePassToNode returns local for RESPOND
  {
    const peerTable = [
      { ip: '127.0.0.1', port: 4111, ip32: 0x7F000001,
        strandWeights: [50,50,50,50,50,50,50,50],
        storageAvailable: 0, clusterFingerprint: 0, dnAggregate: 0,
        genomeFp: 0, omegaTypes: [], capabilities: [], strandCount: 8,
        lastSeen: Date.now(), local: true }
    ];
    const r = routePassToNode('task-abc', PASS.RESPOND, peerTable);
    assert('routePassToNode RESPOND → local', r.local, true);
  }

  // 6. routePassToNode prefers GPU node for TRANSFORM
  {
    const now = Date.now();
    const peerTable = [
      { ip: '10.0.0.1', port: 8090, ip32: dottedToIp32('10.0.0.1'),
        strandWeights: [90,10,10,10,90,10,10,10],
        omegaTypes: [], strandCount: 8, lastSeen: now, local: false,
        storageAvailable: 0, clusterFingerprint: 0, dnAggregate: 0, genomeFp: 0 },
      { ip: '10.0.0.2', port: 8090, ip32: dottedToIp32('10.0.0.2'),
        strandWeights: [90,10,10,10,90,10,10,10],
        omegaTypes: [OTYPE.GPU], strandCount: 8, lastSeen: now, local: false,
        storageAvailable: 0, clusterFingerprint: 0, dnAggregate: 0, genomeFp: 0 },
    ];
    const r = routePassToNode('transform-task', PASS.TRANSFORM, peerTable);
    assert('routePassToNode TRANSFORM → GPU node', r.ip, '10.0.0.2');
  }

  // 7. phiSeedMcastIp format
  {
    const addr = phiSeedMcastIp(0xDEADBEEF);
    assert('phiSeedMcastIp starts with 239.', addr.startsWith('239.'), true);
  }

  // 8. buildGenomeFp deterministic
  {
    const seed = Buffer.from('test-seed-bytes');
    const g1 = buildGenomeFp(seed);
    const g2 = buildGenomeFp(seed);
    assert('buildGenomeFp deterministic', g1, g2);
  }

  console.log(`\n  ${passed} passed, ${failed} failed\n`);
  if (failed > 0) process.exit(1);

  // 9. Discovery smoke test (won't find real peers in test env, but shouldn't throw)
  console.log('  Smoke-testing discoverFabricNodes (no real peers expected)...');
  discoverFabricNodes(0xDEADBEEF, { timeoutMs: 50, skipHttpPoll: true })
    .then(peers => {
      console.log(`  ✓ discoverFabricNodes returned ${peers.length} peer(s) (self only is fine)\n`);
    })
    .catch(e => {
      console.error(`  ✗ discoverFabricNodes threw: ${e.message}\n`);
      process.exit(1);
    });
}
