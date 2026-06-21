/*
 * zchg_gossip_patched.c - zchg_gossip.c + CH-1 carrier integration
 *
 * Changes from original:
 *   - Include zchg_carrier.h
 *   - zchg_gossip_create_message: encode CH-1 carrier into strand_weights
 *     after computing legitimate weights, before transmission.
 *   - zchg_lattice_apply_gossip (called from zchg_handle_gossip_frame):
 *     decode CH-1 carrier from strand_weights, strip bits, then apply
 *     the clean weights to the EMA path as normal.
 *
 * The lattice convergence is unaffected — the EMA update sees weights
 * with their bottom 2 bits zeroed, which is within the ±1 integer
 * rounding noise already present from the phi-amplify computation.
 */

#include "zchg_transport.h"
#include "zchg_carrier.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ── Shared carrier state reference (defined in zchg_frame_patched.c) ── */
extern zchg_carrier_t s_carrier;
extern int            s_carrier_ready;

/* Pending outbound CH-1 data (separate ring from CH-0, same size) */
#define CH1_RING_SZ 1024
static uint8_t  s_ch1_out[CH1_RING_SZ];
static uint16_t s_ch1_head = 0;
static uint16_t s_ch1_tail = 0;

static inline uint16_t ch1_used(void) {
    return (uint16_t)((s_ch1_head - s_ch1_tail) & (CH1_RING_SZ - 1u));
}
static inline void ch1_push(const uint8_t *d, uint16_t n) {
    for (uint16_t i = 0; i < n; i++) {
        s_ch1_out[s_ch1_head & (CH1_RING_SZ - 1u)] = d[i];
        s_ch1_head++;
    }
}
static inline uint16_t ch1_pop(uint8_t *out, uint16_t max) {
    uint16_t used = ch1_used();
    uint16_t n = used < max ? used : max;
    for (uint16_t i = 0; i < n; i++) {
        out[i] = s_ch1_out[s_ch1_tail & (CH1_RING_SZ - 1u)];
        s_ch1_tail++;
    }
    return n;
}

/* Public: queue bytes for CH-1 gossip transmission */
void zchg_gossip_carrier_send(const uint8_t *data, size_t len) {
    if (len > CH1_RING_SZ - ch1_used() - 1u) return;
    ch1_push(data, (uint16_t)len);
}

/* Public: drain received CH-1 bytes */
size_t zchg_gossip_carrier_recv(uint8_t *out, size_t max) {
    return zchg_carrier_ch1_drain(&s_carrier, out, max);
}

/* ── Original helper (private) ── */
static int zchg_gossip_select_peers(const zchg_lattice_t *lattice,
                                    uint32_t *out_peers, int max_peers) {
    if (!lattice || !out_peers || max_peers <= 0) return 0;
    int out_count = 0;
    for (uint32_t i = 0; i < lattice->peer_count && out_count < max_peers; i++) {
        const zchg_peer_t *peer = &lattice->peers[i];
        if (!peer->is_healthy || peer->ip_addr == 0) continue;
        out_peers[out_count++] = peer->ip_addr;
    }
    return out_count;
}

/* ============================================================================
 * zchg_gossip_create_message  (+CH-1 encode)
 * ============================================================================ */
void zchg_gossip_create_message(const zchg_lattice_t *lattice,
                                 zchg_gossip_msg_t    *out_msg)
{
    if (!lattice || !out_msg) return;

    memset(out_msg, 0, sizeof(*out_msg));
    out_msg->source_ip          = lattice->local_ip;
    out_msg->cluster_fingerprint= lattice->cluster_fingerprint;
    out_msg->storage_available  = (uint32_t)lattice->my_strands[0].storage_available;

    /* Compute legitimate strand weights */
    for (uint8_t i = 0; i < zchg_STRAND_COUNT; i++)
        out_msg->strand_weights[i] = (uint8_t)lattice->my_strands[i].authority_weight;

    /* ── CH-1 ENCODE ─────────────────────────────────────────────────────
     * Pop 2 bytes from the outbound CH-1 ring and modulate the LSB-2
     * of strand_weights[].  If no pending data, encode zeros —
     * bottom 2 bits are already noise from the phi-amplify rounding,
     * so a zero carrier is indistinct from a non-carrying message.    */
    if (s_carrier_ready) {
        uint8_t chunk[2] = {0, 0};
        ch1_pop(chunk, 2);
        zchg_carrier_gossip_encode(&s_carrier, out_msg->strand_weights, chunk);
    }
    /* ── END CH-1 ENCODE ──────────────────────────────────────────────── */
}

/* ============================================================================
 * zchg_handle_gossip_frame  (+CH-1 decode via apply_gossip hook)
 * ============================================================================ */

/* Internal: decode then apply gossip weights.
 * Called from zchg_handle_gossip_frame before applying to lattice. */
static void zchg_gossip_strip_and_decode(zchg_gossip_msg_t *msg) {
    if (!s_carrier_ready) return;

    uint8_t recovered[2];
    /* Modifies msg->strand_weights in-place: strips carrier bits */
    zchg_carrier_gossip_decode(&s_carrier, msg->strand_weights, recovered);
    /* recovered[] now in s_carrier.ch1_buf, drained via zchg_gossip_carrier_recv */
}

int zchg_gossip_broadcast(zchg_transport_server_t *server,
                           const zchg_gossip_msg_t *msg) {
    if (!server || !msg) return -1;

    uint32_t peers[3] = {0};
    int peer_count = zchg_gossip_select_peers(&server->lattice, peers, 3);
    if (peer_count <= 0) return 0;

    for (int i = 0; i < peer_count; i++) {
        if (peers[i] == 0 || peers[i] == server->local_ip) continue;

        zchg_frame_t frame;
        memset(&frame, 0, sizeof(frame));
        frame.header.version     = zchg_FRAME_VERSION;
        frame.header.type        = zchg_FRAME_GOSSIP;
        frame.header.source_ip   = server->local_ip;
        frame.header.payload_len = (uint32_t)sizeof(*msg);
        frame.header.timestamp   = (uint64_t)time(NULL) * 1000ULL;
        frame.payload_len        = sizeof(*msg);
        frame.payload            = (uint8_t *)malloc(sizeof(*msg));
        if (!frame.payload) continue;

        memcpy(frame.payload, msg, sizeof(*msg));
        zchg_frame_t *response = NULL;
        (void)zchg_client_send_frame(server, peers[i], &frame, &response);
        if (response) {
            if (response->payload) free(response->payload);
            free(response);
        }
        free(frame.payload);
    }
    return 0;
}

int zchg_gossip_cycle(zchg_transport_server_t *server) {
    if (!server) return -1;
    zchg_gossip_msg_t msg;
    zchg_gossip_create_message(&server->lattice, &msg);
    return zchg_gossip_broadcast(server, &msg);
}

int zchg_gossip_evict_dead_peers(zchg_lattice_t *lattice) {
    if (!lattice) return -1;
    time_t now = time(NULL);
    int removed = 0;
    for (uint32_t i = 0; i < lattice->peer_count; i++) {
        zchg_peer_t *peer = &lattice->peers[i];
        if (!peer->is_healthy) continue;
        if (peer->last_gossip_in > 0 &&
            (now - peer->last_gossip_in) > (time_t)(zchg_GOSSIP_INTERVAL * 4)) {
            peer->is_healthy = 0;
            removed++;
        }
    }
    return removed;
}

/* ── Hook for zchg_server_handle_frame (called when GOSSIP frame arrives) ──
 * In the original zchg_transport.c, zchg_handle_gossip_frame calls
 * zchg_lattice_apply_gossip directly.  Insert the strip step before it.
 *
 * Replacement drop-in for zchg_handle_gossip_frame:                        */
int zchg_handle_gossip_frame(zchg_transport_server_t *server,
                              zchg_frame_t            *frame)
{
    if (!server || !frame || !frame->payload) return -1;
    if (frame->payload_len < sizeof(zchg_gossip_msg_t)) return -1;

    zchg_gossip_msg_t msg;
    memcpy(&msg, frame->payload, sizeof(msg));

    /* ── CH-1 DECODE: strip carrier bits before lattice update ── */
    zchg_gossip_strip_and_decode(&msg);
    /* ── END CH-1 DECODE ─────────────────────────────────────── */

    zchg_lattice_apply_gossip(&server->lattice, msg.source_ip, &msg);
    return 0;
}
