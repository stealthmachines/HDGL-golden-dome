/*
 * zchg_lattice_patch.c — Gap 3 patch 2/2
 *
 * Replaces zchg_lattice_apply_gossip in zchg_lattice.c.
 * Adds genome convergence update after the existing EMA weight update.
 *
 * Drop this file into NGINX-HDGL-0.6-c/src/ and add to the build.
 * Remove the original zchg_lattice_apply_gossip from zchg_lattice.c
 * (or #define ZCHG_LATTICE_APPLY_GOSSIP_OVERRIDE before including it).
 */

#include "zchg_lattice.h"
#include "zchg_carrier.h"        /* for zchg_carrier_gossip_decode */
#include "hdgl_genome_fabric.h"  /* for hdgl_genome_fabric_gossip_update */

/* External carrier state (from zchg_frame_patched.c / zchg_gossip_patched.c) */
extern zchg_carrier_t s_carrier;
extern int            s_carrier_ready;

/* External genome fabric state (initialised after boot by genome_boot_hook) */
extern GenomeFabric g_genome_fabric;
extern int          g_genome_fabric_ready;

/*
 * zchg_lattice_apply_gossip — extended version
 *
 * Changes from original (zchg_lattice.c):
 *   1. CH-1 carrier decode: strips bottom-2 bits from strand_weights
 *      BEFORE applying EMA. Legitimate weights are clean; carrier recovered.
 *   2. Genome gossip: if dn_aggregate and genome_fingerprint are present
 *      (payload_len >= 25), call hdgl_genome_fabric_gossip_update().
 *      This folds the peer's genome into the local EMA and updates
 *      strand_auth. Cross-node genome convergence happens here.
 *   3. Everything else: identical to original.
 */
int zchg_lattice_apply_gossip(zchg_lattice_t    *lattice,
                               uint32_t           peer_ip,
                               zchg_gossip_msg_t *msg)
{
    /* ── CH-1 CARRIER DECODE (strip before EMA) ── */
    if (s_carrier_ready) {
        uint8_t recovered[2];
        zchg_carrier_gossip_decode(&s_carrier, msg->strand_weights, recovered);
        /* recovered[] accumulated in s_carrier.ch1_buf for drain via API */
    }
    /* strand_weights[] now has clean top-6 bits; EMA proceeds on clean values */

    /* ── ORIGINAL PEER FIND/CREATE LOGIC ── */
    zchg_peer_t *peer = NULL;
    for (uint32_t i = 0; i < lattice->peer_count; i++) {
        if (lattice->peers[i].ip_addr == peer_ip) {
            peer = &lattice->peers[i];
            break;
        }
    }
    if (!peer && lattice->peer_count < zchg_MAX_PEERS) {
        peer = &lattice->peers[lattice->peer_count++];
        peer->ip_addr  = peer_ip;
        peer->port     = 8090;
        peer->is_healthy = 1;
    }
    if (!peer) return -1;

    /* ── ORIGINAL STRAND WEIGHT UPDATE ── */
    for (uint8_t i = 0; i < zchg_STRAND_COUNT; i++)
        peer->strands[i].authority_weight = msg->strand_weights[i];

    peer->cluster_fingerprint = msg->cluster_fingerprint;
    peer->last_gossip_in      = time(NULL);
    peer->failed_checks       = 0;

    /* ── GENOME CONVERGENCE (new) ── */
    if (g_genome_fabric_ready && msg->dn_aggregate != 0) {
        hdgl_genome_fabric_gossip_update(&g_genome_fabric,
                                          msg->dn_aggregate,
                                          msg->genome_fingerprint,
                                          0 /* verbose=0 in hot path */);
    }

    return 0;
}

/*
 * Extended gossip message creation — fills dn_aggregate and genome_fingerprint.
 * Replaces or supplements zchg_gossip_create_message in zchg_gossip_patched.c.
 *
 * Call this INSTEAD OF the original when g_genome_fabric_ready is true.
 */
void zchg_gossip_create_message_extended(const zchg_lattice_t *lattice,
                                          zchg_gossip_msg_t    *out_msg)
{
    if (!lattice || !out_msg) return;

    /* Base fields */
    memset(out_msg, 0, sizeof(*out_msg));
    out_msg->source_ip           = lattice->local_ip;
    out_msg->cluster_fingerprint = lattice->cluster_fingerprint;
    out_msg->storage_available   = (uint32_t)lattice->my_strands[0].storage_available;

    for (uint8_t i = 0; i < zchg_STRAND_COUNT; i++)
        out_msg->strand_weights[i] = (uint8_t)lattice->my_strands[i].authority_weight;

    /* Genome fields — the two new uint32s */
    if (g_genome_fabric_ready) {
        out_msg->dn_aggregate      = (uint32_t)(g_genome_fabric.gossip_dn_ema + 0.5);
        out_msg->genome_fingerprint = g_genome_fabric.gossip_fingerprint;
    }

    /* CH-1 carrier encode (from zchg_gossip_patched.c — kept here for completeness) */
    if (s_carrier_ready) {
        uint8_t chunk[2] = {0, 0};
        /* ch1_pop(chunk, 2) — pop from outbound CH-1 ring */
        zchg_carrier_gossip_encode(&s_carrier, out_msg->strand_weights, chunk);
    }
}
