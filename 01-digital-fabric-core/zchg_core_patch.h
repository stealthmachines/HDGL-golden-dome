/*
 * zchg_core_patch.h — Gap 3 patch 1/2
 *
 * Add dn_aggregate to zchg_gossip_msg_t in zchg_core.h.
 * Apply by replacing the gossip_msg_t definition.
 *
 * BEFORE (in NGINX-HDGL-0.6-c/include/zchg_core.h):
 *
 *   typedef struct {
 *       uint32_t    source_ip;
 *       uint8_t     strand_weights[8];
 *       uint32_t    storage_available;
 *       uint32_t    cluster_fingerprint;
 *   } __attribute__((packed)) zchg_gossip_msg_t;
 *
 * AFTER:
 */

typedef struct {
    uint32_t    source_ip;
    uint8_t     strand_weights[8];     /* phi-weighted strand authority (1-100) */
    uint32_t    storage_available;
    uint32_t    cluster_fingerprint;
    uint32_t    dn_aggregate;          /* Dₙ(r) 32-bit bitmask — genome key material */
    uint32_t    genome_fingerprint;    /* gossip_fingerprint from GenomeFabric */
} __attribute__((packed)) zchg_gossip_msg_t;

/*
 * Size change: 17 → 25 bytes (+8 bytes).
 * Wire-compatible with old peers: new fields are at the end.
 * Old peers send 17 bytes; receiver checks payload_len and reads
 * only what arrived — the two new fields default to zero, which is
 * safe (dn_aggregate=0 → genome convergence not asserted).
 *
 * Build note: rebuild all nodes together; the size change is backward-
 * compatible on receive but not on send (old sender → new receiver is fine;
 * new sender → old receiver sees 8 extra bytes as junk payload, harmless).
 */
