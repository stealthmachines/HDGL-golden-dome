/*
 * zchg_frame_patched.c - zchg_frame.c + CH-0 carrier integration
 *
 * Changes from original:
 *   - Include zchg_carrier.h
 *   - Add zchg_frame_carrier_state (static per-process; replace with
 *     per-peer state when connection tracking is in place)
 *   - zchg_frame_serialize: encode carrier into header.reserved
 *   - zchg_frame_deserialize: decode carrier from header.reserved
 *   - zchg_frame_carrier_init / zchg_frame_carrier_send / _recv added
 *
 * All original logic untouched.  Two insertion points only.
 */

#include "zchg_core.h"
#include "zchg_carrier.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

/* ── Static carrier state (replace with per-peer map for production) ── */
static zchg_carrier_t  s_carrier;
static int             s_carrier_ready = 0;

/* Pending outbound carrier data (ring buffer, 4KB) */
#define CARRIER_RING_SZ  4096
static uint8_t  s_out_ring[CARRIER_RING_SZ];
static uint16_t s_out_head = 0;
static uint16_t s_out_tail = 0;

static inline uint16_t ring_used(void) {
    return (uint16_t)((s_out_head - s_out_tail) & (CARRIER_RING_SZ - 1u));
}
static inline void ring_push(const uint8_t *data, uint16_t n) {
    for (uint16_t i = 0; i < n; i++) {
        s_out_ring[s_out_head & (CARRIER_RING_SZ - 1u)] = data[i];
        s_out_head++;
    }
}
static inline uint16_t ring_pop(uint8_t *out, uint16_t max) {
    uint16_t used = ring_used();
    uint16_t n = used < max ? used : max;
    for (uint16_t i = 0; i < n; i++) {
        out[i] = s_out_ring[s_out_tail & (CARRIER_RING_SZ - 1u)];
        s_out_tail++;
    }
    return n;
}

/* Public: initialise carrier with genome fingerprint */
void zchg_frame_carrier_init(uint32_t genome_fp) {
    zchg_carrier_init(&s_carrier, genome_fp);
    s_carrier_ready = 1;
}

/* Public: queue bytes for transmission via CH-0 */
void zchg_frame_carrier_send(const uint8_t *data, size_t len) {
    if (len > CARRIER_RING_SZ - ring_used() - 1u) return; /* drop if full */
    ring_push(data, (uint16_t)len);
}

/* Public: drain received CH-0 bytes */
size_t zchg_frame_carrier_recv(uint8_t *out, size_t max) {
    return zchg_carrier_ch0_drain(&s_carrier, out, max);
}

/* ============================================================================
 * Frame Serialization (+ CH-0 encode)
 * ============================================================================ */

int zchg_frame_serialize(zchg_frame_t *frame, uint8_t **out_buf, size_t *out_len) {
    if (!frame || !out_buf || !out_len) return -1;

    size_t total_size = zchg_FRAME_HEADER_SIZE + frame->payload_len;
    uint8_t *buf = (uint8_t *)malloc(total_size);
    if (!buf) return -1;

    /* ── CH-0 ENCODE ────────────────────────────────────────────────────
     * Compute payload hash, pop up to 4 bytes from the outbound ring,
     * encode into header.reserved.  If no carrier data pending, encode
     * zeros (reserved field still looks like a valid fold — indistinct). */
    if (s_carrier_ready) {
        uint32_t ph = zchg_carrier_payload_hash(frame->payload, frame->payload_len);
        uint8_t  carrier_chunk[ZC_FRAME_PAYLOAD_BYTES] = {0, 0, 0, 0};
        uint8_t  n = (uint8_t)ring_pop(carrier_chunk, ZC_FRAME_PAYLOAD_BYTES);
        zchg_carrier_frame_encode(&s_carrier,
                                   &frame->header.reserved,
                                   ph,
                                   carrier_chunk, n > 0 ? 4u : 4u);
        /* Note: always encode 4 bytes (zero-padded) so every frame
         * carries a well-formed fold — no observable traffic pattern
         * that distinguishes "carrier present" from "carrier absent". */
    }
    /* ── END CH-0 ENCODE ─────────────────────────────────────────────── */

    memcpy(buf, &frame->header, zchg_FRAME_HEADER_SIZE);
    if (frame->payload && frame->payload_len > 0)
        memcpy(buf + zchg_FRAME_HEADER_SIZE, frame->payload, frame->payload_len);

    *out_buf = buf;
    *out_len = total_size;
    return 0;
}

/* ============================================================================
 * Frame Deserialization (+ CH-0 decode)
 * ============================================================================ */

int zchg_frame_deserialize(uint8_t *buf, size_t len, zchg_frame_t *out_frame) {
    if (!buf || len < zchg_FRAME_HEADER_SIZE || !out_frame) return -1;

    memcpy(&out_frame->header, buf, zchg_FRAME_HEADER_SIZE);

    if (out_frame->header.payload_len > zchg_FRAME_MAX_PAYLOAD) return -1;
    if (zchg_FRAME_HEADER_SIZE + out_frame->header.payload_len != len) return -1;

    if (out_frame->header.payload_len > 0) {
        out_frame->payload = (uint8_t *)malloc(out_frame->header.payload_len);
        if (!out_frame->payload) return -1;
        memcpy(out_frame->payload, buf + zchg_FRAME_HEADER_SIZE,
               out_frame->header.payload_len);
    } else {
        out_frame->payload = NULL;
    }
    out_frame->payload_len  = out_frame->header.payload_len;
    out_frame->created_at   = time(NULL);

    /* ── CH-0 DECODE ────────────────────────────────────────────────────
     * Compute the same payload hash the sender used, unfold reserved,
     * recover 4 carrier bytes, accumulate in the inbound buffer.       */
    if (s_carrier_ready) {
        uint32_t ph = zchg_carrier_payload_hash(out_frame->payload,
                                                 out_frame->payload_len);
        uint8_t recovered[ZC_FRAME_PAYLOAD_BYTES];
        zchg_carrier_frame_decode(&s_carrier,
                                   out_frame->header.reserved,
                                   ph,
                                   recovered,
                                   ZC_FRAME_PAYLOAD_BYTES);
    }
    /* ── END CH-0 DECODE ─────────────────────────────────────────────── */

    return 0;
}

/* ============================================================================
 * Frame Pool (unchanged from original)
 * ============================================================================ */

#include "zchg_core.h"

zchg_frame_t* zchg_frame_alloc(zchg_frame_pool_t *pool) {
    if (!pool) return NULL;
    for (uint32_t i = 0; i < zchg_FRAME_POOL_SIZE; i++) {
        if (!pool->in_use[i]) {
            pool->in_use[i] = 1;
            pool->reused_count++;
            return &pool->frames[i];
        }
    }
    return NULL;
}

void zchg_frame_free(zchg_frame_pool_t *pool, zchg_frame_t *frame) {
    if (!pool || !frame) return;
    for (uint32_t i = 0; i < zchg_FRAME_POOL_SIZE; i++) {
        if (&pool->frames[i] == frame) {
            pool->in_use[i] = 0;
            if (frame->payload) { free(frame->payload); frame->payload = NULL; }
            frame->payload_len = 0;
            return;
        }
    }
}

int zchg_timestamp_is_valid(uint64_t timestamp) {
    uint64_t now = (uint64_t)time(NULL) * 1000;
    int64_t diff = (int64_t)(now - timestamp);
    if (diff < 0) diff = -diff;
    return (diff <= zchg_REPLAY_WINDOW_SEC * 1000) ? 1 : 0;
}
