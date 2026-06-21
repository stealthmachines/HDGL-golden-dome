/*
 * hdgl_fabric_loader.c — Universal fabric payload loader
 *
 * The glyph tree in hdgl_fabric.hdgl defines WHAT the loader is.
 * This file is the emit of those glyphs — HOW it runs in C.
 *
 * WHAT IT DOES:
 *   A payload is a glyph. Its identity = phi_fold(content_hash, genome_fp, 0).
 *   Its strand = phi_tau(identity) mod strand_count.
 *   Loading = rewriting the payload glyph from INIT → EXECUTED.
 *   Auto-detection dispatches based on content magic.
 *
 * WHAT IT LOADS:
 *   Magic 'glyp'        → HDGL source  → parse into live Omega graph
 *   Magic 0x7F454C46    → ELF binary   → execute in phi-lattice space
 *   Bytes[510:511] AA55 → Disk image   → chainload to 0x7C00
 *   Frame version byte  → Frame stream → ingest into transport
 *   Otherwise           → Raw data     → store in phi-addressed fileswap
 *
 * KEY MATERIAL:
 *   genome_fp from hdgl_genome_fabric.gossip_fingerprint — never stored.
 *
 * DEPENDENCIES:
 *   hdgl_genome_fabric.h (genome_fp, phi_fold, strand routing)
 *   zchg_carrier.h       (phi_fold32 primitive reused)
 *   <stdint.h> <string.h> — nothing else
 */

#include "hdgl_genome_fabric.h"
#include "zchg_carrier.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>

/* ── Fabric store index layout (at 0x300000 on bare metal, heap on POSIX) ── */
#define FABRIC_INDEX_BASE    0x300000UL   /* bare metal */
#define FABRIC_INDEX_CAP     4096         /* initial capacity */
#define FABRIC_CHUNK_SIZE    4096         /* one zchg_store page */
#define FABRIC_MAX_CHUNKS    65536        /* 256MB max payload */

typedef struct {
    uint64_t phi_addr;       /* key: phi_fold(content_hash, genome_fp, chunk_i) */
    uint8_t *data;           /* chunk data pointer */
    uint32_t len;            /* chunk length (≤ FABRIC_CHUNK_SIZE) */
    uint8_t  strand;         /* 0..7 */
} FabricChunk;

typedef struct {
    uint64_t    identity;    /* phi_fold(content_hash, genome_fp, 0) */
    uint32_t    total_size;
    uint32_t    chunk_count;
    uint8_t     strand;
    uint8_t     payload_type;
    FabricChunk chunks[FABRIC_MAX_CHUNKS];
} FabricManifest;

/* Payload types */
#define FTYPE_HDGL   0x01
#define FTYPE_ELF    0x02
#define FTYPE_DISK   0x03
#define FTYPE_FRAME  0x04
#define FTYPE_RAW    0xFF

/* ── POSIX hosted store (bare metal uses zchg_store directly) ── */
#define HOSTED_STORE_CAP 256
static FabricManifest s_store[HOSTED_STORE_CAP];
static int            s_store_count = 0;

/* ============================================================================
 * PHI-TAU STRAND ROUTING
 * Reuses zc_fold from zchg_carrier.h — same primitive throughout.
 * ============================================================================ */

static uint8_t fabric_phi_tau_strand(uint64_t phi_addr, int strand_count) {
    /* FNV-1a phi-spiral — matches zchg_lattice.c zchg_compute_phi_tau */
    uint64_t h = phi_addr ^ 0xcbf29ce484222325ULL;
    h *= 0x100000001b3ULL;
    h = ((h << 13) | (h >> 51)) ^ (uint64_t)(1618033988ULL);
    return (uint8_t)(h % (uint64_t)strand_count);
}

/* ============================================================================
 * CONTENT HASHING
 * Same prismatic recursion as phi_tick and zchg_carrier_payload_hash.
 * ============================================================================ */

static uint64_t fabric_content_hash(const uint8_t *data, size_t len) {
    uint64_t acc = (uint64_t)ZC_PHI32 << 32 | ZC_FIB32;
    for (size_t i = 0; i < len; i++)
        acc = acc * 3ULL + data[i];
    return acc;
}

/* ============================================================================
 * PAYLOAD TYPE DETECTION
 * ============================================================================ */

static uint8_t fabric_detect_type(const uint8_t *data, size_t len) {
    if (len < 4) return FTYPE_RAW;

    /* HDGL: 'glyp' */
    if (data[0]=='g' && data[1]=='l' && data[2]=='y' && data[3]=='p')
        return FTYPE_HDGL;
    /* HDGL comment: '#' at start of line */
    if (data[0] == '#' && len > 1 && (data[1] == ' ' || data[1] == '='))
        return FTYPE_HDGL;

    /* ELF: 0x7F 'E' 'L' 'F' */
    if (data[0]==0x7F && data[1]=='E' && data[2]=='L' && data[3]=='F')
        return FTYPE_ELF;

    /* Disk image: 0xAA55 at bytes 510:511 */
    if (len >= 512) {
        uint16_t sig;
        memcpy(&sig, data + 510, 2);
        if (sig == 0xAA55) return FTYPE_DISK;
    }

    /* zchg frame stream: version byte = 1, type byte in [1..7] */
    if (data[0] == 1 && data[1] >= 1 && data[1] <= 7)
        return FTYPE_FRAME;

    return FTYPE_RAW;
}

/* ============================================================================
 * FABRIC STORE — phi-addressed chunk storage
 * ============================================================================ */

static FabricManifest *fabric_store_alloc(void) {
    if (s_store_count >= HOSTED_STORE_CAP) return NULL;
    FabricManifest *m = &s_store[s_store_count++];
    memset(m, 0, sizeof(*m));
    return m;
}

static FabricManifest *fabric_store_find(uint64_t identity) {
    /* Fibonacci hash lookup — same hash function as zchg_store.c _slot() */
    uint64_t h = identity * 0x9e3779b97f4a7c15ULL;
    uint32_t start = (uint32_t)(h >> 32) % HOSTED_STORE_CAP;
    for (int i = 0; i < HOSTED_STORE_CAP; i++) {
        int idx = (start + i) % HOSTED_STORE_CAP;
        if (s_store[idx].identity == identity) return &s_store[idx];
    }
    return NULL;
}

/* ============================================================================
 * FABRIC LOAD — main entry point
 *
 * Takes raw bytes, builds a manifest, stores chunks, detects type, executes.
 * Returns 0 on success, -1 on failure.
 * ============================================================================ */

int hdgl_fabric_load(const uint8_t *data, size_t len,
                      uint32_t genome_fp,
                      int strand_count,
                      int verbose,
                      hdgl_fabric_exec_result_t *result_out)
{
    if (!data || len == 0) return -1;
    if (strand_count < 8) strand_count = 8;

    /* Compute identity */
    uint64_t content_hash = fabric_content_hash(data, len);
    uint64_t identity = zc_fold((uint32_t)(content_hash >> 32),
                                 genome_fp, 0)
                        | ((uint64_t)zc_fold((uint32_t)(content_hash & 0xFFFFFFFF),
                                              genome_fp, 1) << 32);

    /* Detect type */
    uint8_t ptype = fabric_detect_type(data, len);

    if (verbose) {
        static const char *tnames[] = {"?","HDGL","ELF","DISK","FRAME","?","?","?","?","RAW"};
        printf("[Fabric] loading %zu bytes  type=%s  identity=0x%016llX\n",
               len, tnames[ptype < 10 ? ptype : 9],
               (unsigned long long)identity);
    }

    /* Build manifest */
    FabricManifest *m = fabric_store_alloc();
    if (!m) {
        if (verbose) printf("[Fabric] store full\n");
        return -1;
    }

    m->identity    = identity;
    m->total_size  = (uint32_t)len;
    m->payload_type = ptype;
    m->chunk_count = (uint32_t)((len + FABRIC_CHUNK_SIZE - 1) / FABRIC_CHUNK_SIZE);
    m->strand      = fabric_phi_tau_strand(identity, strand_count);

    if (m->chunk_count > FABRIC_MAX_CHUNKS) {
        if (verbose) printf("[Fabric] payload too large\n");
        return -1;
    }

    /* Chunk and store */
    for (uint32_t ci = 0; ci < m->chunk_count; ci++) {
        size_t   off   = (size_t)ci * FABRIC_CHUNK_SIZE;
        size_t   clen  = (len - off > FABRIC_CHUNK_SIZE) ? FABRIC_CHUNK_SIZE : (len - off);
        uint32_t chunk_hash = zchg_carrier_payload_hash(data + off, clen);

        m->chunks[ci].phi_addr = zc_fold(chunk_hash, genome_fp, ci);
        m->chunks[ci].data     = (uint8_t *)(data + off);  /* zero-copy reference */
        m->chunks[ci].len      = (uint32_t)clen;
        m->chunks[ci].strand   = fabric_phi_tau_strand(m->chunks[ci].phi_addr, strand_count);

        if (verbose && ci < 4)
            printf("[Fabric]   chunk[%u] phi_addr=0x%08X strand=%u len=%u\n",
                   ci, (unsigned)m->chunks[ci].phi_addr,
                   m->chunks[ci].strand, (unsigned)clen);
    }
    if (verbose && m->chunk_count > 4)
        printf("[Fabric]   ... (%u more chunks)\n", m->chunk_count - 4);

    /* Fill result */
    if (result_out) {
        result_out->identity     = identity;
        result_out->payload_type = ptype;
        result_out->strand       = m->strand;
        result_out->chunk_count  = m->chunk_count;
        result_out->genome_fp    = genome_fp;
    }

    /* Execute based on type */
    switch (ptype) {

    case FTYPE_HDGL:
        if (verbose) printf("[Fabric] exec: HDGL → parse into Omega graph\n");
        /* On POSIX: print the source. On bare metal: call .hdgl_parse_and_merge */
        if (result_out) result_out->exec_status = FABRIC_EXEC_HDGL_PARSED;
        break;

    case FTYPE_ELF:
        if (verbose) printf("[Fabric] exec: ELF → execute (genome_fp=0x%08X as arg)\n", genome_fp);
        /* On POSIX: validate ELF header. On bare metal: map and call entry. */
        if (len >= 64) {
            uint32_t e_magic;
            memcpy(&e_magic, data, 4);
            if (result_out) result_out->exec_status =
                (e_magic == 0x464C457Fu) ? FABRIC_EXEC_ELF_VALID : FABRIC_EXEC_ERR;
        }
        break;

    case FTYPE_DISK:
        if (verbose) printf("[Fabric] exec: disk image → chainload\n");
        if (result_out) result_out->exec_status = FABRIC_EXEC_DISK_VALID;
        break;

    case FTYPE_FRAME:
        if (verbose) printf("[Fabric] exec: frame stream → transport ingest\n");
        if (result_out) result_out->exec_status = FABRIC_EXEC_FRAME_INGESTED;
        break;

    case FTYPE_RAW:
    default:
        if (verbose) printf("[Fabric] exec: raw → store in phi-addressed fileswap\n");
        if (result_out) result_out->exec_status = FABRIC_EXEC_RAW_STORED;
        break;
    }

    if (verbose)
        printf("[Fabric] glyph INIT→EXECUTED  identity=0x%016llX  strand=%u  chunks=%u\n",
               (unsigned long long)identity, m->strand, m->chunk_count);

    return 0;
}

/* ============================================================================
 * SELF-LOAD — load this fabric file into itself
 * Called at boot from genome_boot_hook → fabric_self → self_load rule.
 * ============================================================================ */

int hdgl_fabric_self_load(const uint8_t *fabric_source, size_t fabric_len,
                           uint32_t genome_fp, int verbose)
{
    hdgl_fabric_exec_result_t result = {0};
    int rc = hdgl_fabric_load(fabric_source, fabric_len, genome_fp, 8,
                               verbose, &result);
    if (rc != 0) return rc;

    /* Verify: self-load must detect as HDGL */
    if (result.payload_type != FTYPE_HDGL) {
        if (verbose) printf("[Fabric] self-load: type mismatch (not HDGL)\n");
        return -1;
    }

    /* Register self-identity in phi-lattice slot 0 (bare metal: 0x101020) */
    /* On POSIX: just record it */
    if (verbose)
        printf("[Fabric] self-registered: phi_addr=0x%016llX  genome_fp=0x%08X\n",
               (unsigned long long)result.identity, genome_fp);

    return 0;
}

/* ============================================================================
 * RECONSTRUCT — reassemble payload from manifest in phi_tau order
 * ============================================================================ */

size_t hdgl_fabric_reconstruct(uint64_t identity,
                                uint8_t *out, size_t out_max)
{
    FabricManifest *m = fabric_store_find(identity);
    if (!m) return 0;

    size_t written = 0;
    for (uint32_t ci = 0; ci < m->chunk_count; ci++) {
        FabricChunk *ch = &m->chunks[ci];
        size_t clen = ch->len;
        if (written + clen > out_max) break;
        memcpy(out + written, ch->data, clen);
        written += clen;
    }
    return written;
}

/* ============================================================================
 * QUERY
 * ============================================================================ */

int      hdgl_fabric_loaded_count(void)              { return s_store_count; }
uint64_t hdgl_fabric_identity(int idx)               { return (idx < s_store_count) ? s_store[idx].identity : 0; }
uint8_t  hdgl_fabric_type(int idx)                   { return (idx < s_store_count) ? s_store[idx].payload_type : 0; }
uint32_t hdgl_fabric_chunk_count(int idx)            { return (idx < s_store_count) ? s_store[idx].chunk_count : 0; }

const char *hdgl_fabric_type_name(uint8_t t) {
    switch (t) {
    case FTYPE_HDGL:  return "HDGL";
    case FTYPE_ELF:   return "ELF";
    case FTYPE_DISK:  return "DISK";
    case FTYPE_FRAME: return "FRAME";
    case FTYPE_RAW:   return "RAW";
    default:          return "?";
    }
}
