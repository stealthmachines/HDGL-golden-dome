/*
 * zchg_carrier.h — Phi-lattice steganographic carrier
 *
 * THREE CHANNELS, ONE KEY:
 *
 *   CH-0  frame.reserved  (4 bytes/frame)
 *         Phi-fold of (payload_hash ^ data32) written to the reserved
 *         uint32 in every zchg_frame_header_t.  At 200K frames/sec:
 *         800 KB/sec covert capacity inside normal transport traffic.
 *
 *   CH-1  gossip strand_weights LSB-2  (2 bytes/gossip cycle)
 *         Bottom 2 bits of each of the 8 strand_weight bytes carry
 *         payload bits (16 bits = 2 bytes per gossip message).
 *         EMA in zchg_lattice.c rounds to integer — sub-2-bit
 *         modulation is below the noise floor.  Carrier stripped
 *         before the EMA update runs; legitimate weights unaffected.
 *
 *   CH-2  /serve/<path> phi-sequence  (1 bit/request, request-paced)
 *         Sender issues GET /serve/zc/<seg0>/<seg1>.
 *         seg0 = framing word (same for both bit values).
 *         seg1 encodes payload bit: two phi-derived values, one per bit.
 *         All paths 404 — receiver decodes from path alone, not response.
 *         Cover story: cache warming / CDN miss traffic.
 *
 * KEY MATERIAL:
 *   genome_fp — gossip_fingerprint from GenomeFabric (32-bit).
 *               Derived from Omega hardware graph (CPUID, E820, PCI
 *               topology) on every boot.  Never stored.  Not transmitted
 *               directly — both sides derive it independently from the
 *               same hardware + phi-lattice primitives.
 *
 * PRIMITIVE — phi_fold32 (additive, no XOR crypto path):
 *   fold(x, key, seq) = (x·PHI32 + key·FIB32 + seq·SQRT_PHI) mod 2^32
 *   unfold(f, key, seq) = ((f - key·FIB32 - seq·SQRT_PHI) · PHI32_INV) mod 2^32
 *   where PHI32_INV · PHI32 ≡ 1 (mod 2^32)  [verified: 0x144CBC89]
 *
 * VERIFIED ROUND-TRIPS (see zchg_carrier_test.c):
 *   CH-0: 4-byte frame payload  ✓
 *   CH-1: 16-bit gossip word    ✓
 *   CH-2: bit-level path decode ✓
 */

#ifndef ZCHG_CARRIER_H
#define ZCHG_CARRIER_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>

/* ── Universal constants ── */
#define ZC_PHI32      0x9E3779B9u   /* floor(2^32 / φ) */
#define ZC_FIB32      0x9E3779B1u   /* nearest prime to ZC_PHI32 */
#define ZC_SQRT_PHI   0xA17F0BCBu   /* floor(√φ × 2^32) */
#define ZC_PHI32_INV  0x144CBC89u   /* ZC_PHI32 × ZC_PHI32_INV ≡ 1 (mod 2^32) */

/* CH-1 carrier: bits per strand weight byte, mask */
#define ZC_GOSSIP_CARRIER_BITS  2
#define ZC_GOSSIP_CARRIER_MASK  0x03u

/* CH-0: bytes per frame */
#define ZC_FRAME_PAYLOAD_BYTES  4

/* CH-2: path geometry */
#define ZC_SERVE_CARRIER_PREFIX "/serve/zc/"
#define ZC_PATH_SEG_LEN         8    /* 8 hex chars = 32-bit segment */
/* full path: /serve/zc/SSSSSSSS/SSSSSSSS  (27 bytes + NUL) */

/* ── Carrier state ── */
typedef struct {
    uint32_t genome_fp;
    uint32_t seq_frame;
    uint32_t seq_gossip;
    uint32_t seq_path;

    /* CH-0 reassembly */
    uint8_t  ch0_buf[256];
    uint16_t ch0_head;

    /* CH-1 reassembly */
    uint8_t  ch1_buf[256];
    uint16_t ch1_head;
} zchg_carrier_t;

/* ============================================================================
 * PRIMITIVE
 * ============================================================================ */

static inline uint32_t zc_fold(uint32_t x, uint32_t key, uint32_t seq) {
    return (x    * ZC_PHI32)
         + (key  * ZC_FIB32)
         + (seq  * ZC_SQRT_PHI);
}

static inline uint32_t zc_unfold(uint32_t f, uint32_t key, uint32_t seq) {
    uint32_t raw = f
                 - (key * ZC_FIB32)
                 - (seq * ZC_SQRT_PHI);
    return raw * ZC_PHI32_INV;
}

/* ============================================================================
 * INIT
 * ============================================================================ */

static inline void zchg_carrier_init(zchg_carrier_t *c, uint32_t genome_fp) {
    memset(c, 0, sizeof(*c));
    c->genome_fp = genome_fp;
}

/* ============================================================================
 * CH-0: FRAME RESERVED FIELD
 *
 * payload_hash: additive running hash of the frame's payload bytes.
 *               Ties the reserved word to actual frame content —
 *               every frame gets a unique, deterministic carrier word.
 *               Observer cannot distinguish this from a checksum field.
 * ============================================================================ */

static inline uint32_t zchg_carrier_payload_hash(const uint8_t *data, size_t len) {
    uint32_t acc = ZC_PHI32;
    for (size_t i = 0; i < len; i++)
        acc = acc * 3u + data[i];    /* prismatic recursion, matches phi_tick */
    return acc;
}

/* Encode: write carrier into hdr->reserved.
 * carrier_data: up to 4 bytes to embed.  Returns bytes embedded. */
static inline uint8_t zchg_carrier_frame_encode(zchg_carrier_t *c,
                                                  uint32_t       *reserved_field,
                                                  uint32_t        payload_hash,
                                                  const uint8_t  *carrier_data,
                                                  uint8_t         carrier_len)
{
    uint8_t n = (carrier_len > ZC_FRAME_PAYLOAD_BYTES)
                 ? (uint8_t)ZC_FRAME_PAYLOAD_BYTES : carrier_len;

    uint32_t data32 = 0;
    for (uint8_t i = 0; i < n; i++)
        data32 |= (uint32_t)carrier_data[i] << (i * 8u);

    *reserved_field = zc_fold(payload_hash ^ data32, c->genome_fp, c->seq_frame++);
    return n;
}

/* Decode: extract carrier from reserved field.
 * Returns bytes recovered (always ZC_FRAME_PAYLOAD_BYTES = 4). */
static inline uint8_t zchg_carrier_frame_decode(zchg_carrier_t *c,
                                                  uint32_t        reserved_field,
                                                  uint32_t        payload_hash,
                                                  uint8_t        *out,
                                                  uint8_t         out_max)
{
    uint32_t plaintext = zc_unfold(reserved_field, c->genome_fp, c->seq_frame++);
    uint32_t data32    = plaintext ^ payload_hash;

    uint8_t n = (out_max > ZC_FRAME_PAYLOAD_BYTES)
                 ? (uint8_t)ZC_FRAME_PAYLOAD_BYTES : out_max;
    for (uint8_t i = 0; i < n; i++)
        out[i] = (uint8_t)(data32 >> (i * 8u));

    if (c->ch0_head + n <= (uint16_t)sizeof(c->ch0_buf)) {
        memcpy(c->ch0_buf + c->ch0_head, out, n);
        c->ch0_head += n;
    }
    return n;
}

/* ============================================================================
 * CH-1: GOSSIP STRAND WEIGHT LSB MODULATION
 *
 * 8 strand_weights × 2 carrier bits = 16 bits = 2 bytes per gossip message.
 * The fold is masked to 16 bits before packing — the unfold recovers exactly
 * those 16 bits.  The upper 16 bits of the fold output are discarded; the
 * bijection holds within the 16-bit subspace because the masking is symmetric.
 * ============================================================================ */

/* Encode: modulate strand_weights[] in-place before transmission.
 * strand_weights: the 8-byte array from zchg_gossip_msg_t.
 * carrier_data:   exactly 2 bytes. */
static inline void zchg_carrier_gossip_encode(zchg_carrier_t *c,
                                               uint8_t        *strand_weights,
                                               const uint8_t  *carrier_data)
{
    uint16_t raw16 = (uint16_t)carrier_data[0]
                   | ((uint16_t)carrier_data[1] << 8);

    /* Fold and mask to 16 bits */
    uint16_t folded16 = (uint16_t)(zc_fold((uint32_t)raw16,
                                            c->genome_fp,
                                            c->seq_gossip++) & 0xFFFFu);

    for (uint8_t i = 0; i < 8u; i++) {
        uint8_t carrier_bits = (uint8_t)((folded16 >> (i * 2u)) & ZC_GOSSIP_CARRIER_MASK);
        strand_weights[i] = (strand_weights[i] & (uint8_t)~ZC_GOSSIP_CARRIER_MASK)
                           | carrier_bits;
    }
}

/* Decode: extract carrier bits, strip them from strand_weights[] in-place.
 * The legitimate EMA update that follows sees clean weight values.
 * out: 2 bytes of recovered payload. */
static inline void zchg_carrier_gossip_decode(zchg_carrier_t *c,
                                               uint8_t        *strand_weights,
                                               uint8_t        *out)
{
    uint16_t folded16 = 0;
    for (uint8_t i = 0; i < 8u; i++) {
        folded16 |= (uint16_t)((strand_weights[i] & ZC_GOSSIP_CARRIER_MASK) << (i * 2u));
        strand_weights[i] &= (uint8_t)~ZC_GOSSIP_CARRIER_MASK;
    }

    uint16_t raw16 = (uint16_t)(zc_unfold((uint32_t)folded16,
                                           c->genome_fp,
                                           c->seq_gossip++) & 0xFFFFu);
    out[0] = (uint8_t)(raw16 & 0xFFu);
    out[1] = (uint8_t)(raw16 >> 8u);

    if (c->ch1_head + 2u <= (uint16_t)sizeof(c->ch1_buf)) {
        c->ch1_buf[c->ch1_head++] = out[0];
        c->ch1_buf[c->ch1_head++] = out[1];
    }
}

/* ============================================================================
 * CH-2: /serve/zc/ PHI-PATH SEQUENCE
 *
 * Path layout: /serve/zc/<seg0_hex8>/<seg1_hex8>
 *   seg0 = zc_fold(genome_fp, genome_fp, seq_path)   — framing / sync word
 *   seg1 = zc_fold(genome_fp, seq_path, bit+1)        — bit=0 → +1, bit=1 → +2
 *
 * Receiver: parse seg0 → verify sync, parse seg1 → recover bit.
 * All paths 404.  Receiver reads only the request path, not the response.
 * ============================================================================ */

/* Encode: build the next carrier path.
 * out must hold at least 32 bytes. */
static inline void zchg_carrier_path_encode(zchg_carrier_t *c,
                                             uint8_t         bit,
                                             char           *out,
                                             size_t          out_len)
{
    uint32_t seg0 = zc_fold(c->genome_fp, c->genome_fp, c->seq_path);
    uint32_t seg1 = zc_fold(c->genome_fp, c->seq_path, (uint32_t)(bit & 1u) + 1u);
    c->seq_path++;

    snprintf(out, out_len, "/serve/zc/%08x/%08x", seg0, seg1);
}

/* Decode: parse path, recover bit.
 * Returns 0 or 1 on success, -1 if path is not a valid carrier path or
 * the framing word doesn't match the expected seq position (sync lost). */
static inline int zchg_carrier_path_decode(zchg_carrier_t *c, const char *path)
{
    const char *pfx = ZC_SERVE_CARRIER_PREFIX;     /* "/serve/zc/" */
    const size_t plen = 10u;
    if (strncmp(path, pfx, plen) != 0) return -1;

    const char *s0 = path + plen;
    /* Expect 8 hex + '/' + 8 hex */
    if (s0[8] != '/') return -1;
    const char *s1 = s0 + 9;

    /* Parse two 8-hex-char segments */
    uint32_t seg0_rx = 0, seg1_rx = 0;
    for (int i = 0; i < 8; i++) {
        char h0 = s0[i], h1 = s1[i];
        uint32_t n0, n1;
        if      (h0 >= '0' && h0 <= '9') n0 = (uint32_t)(h0 - '0');
        else if (h0 >= 'a' && h0 <= 'f') n0 = (uint32_t)(h0 - 'a' + 10);
        else return -1;
        if      (h1 >= '0' && h1 <= '9') n1 = (uint32_t)(h1 - '0');
        else if (h1 >= 'a' && h1 <= 'f') n1 = (uint32_t)(h1 - 'a' + 10);
        else return -1;
        seg0_rx = (seg0_rx << 4) | n0;
        seg1_rx = (seg1_rx << 4) | n1;
    }

    /* Verify framing word */
    uint32_t expected_seg0 = zc_fold(c->genome_fp, c->genome_fp, c->seq_path);
    if (seg0_rx != expected_seg0) return -1;

    /* Recover bit */
    uint32_t exp1_b0 = zc_fold(c->genome_fp, c->seq_path, 1u);
    uint32_t exp1_b1 = zc_fold(c->genome_fp, c->seq_path, 2u);
    c->seq_path++;

    if (seg1_rx == exp1_b0) return 0;
    if (seg1_rx == exp1_b1) return 1;
    return -1;
}

/* ============================================================================
 * REASSEMBLY
 * ============================================================================ */

static inline size_t zchg_carrier_ch0_drain(zchg_carrier_t *c,
                                              uint8_t *out, size_t max)
{
    size_t n = c->ch0_head < max ? c->ch0_head : (uint16_t)max;
    if (!n) return 0;
    memcpy(out, c->ch0_buf, n);
    memmove(c->ch0_buf, c->ch0_buf + n, c->ch0_head - n);
    c->ch0_head -= (uint16_t)n;
    return n;
}

static inline size_t zchg_carrier_ch1_drain(zchg_carrier_t *c,
                                              uint8_t *out, size_t max)
{
    size_t n = c->ch1_head < max ? c->ch1_head : (uint16_t)max;
    if (!n) return 0;
    memcpy(out, c->ch1_buf, n);
    memmove(c->ch1_buf, c->ch1_buf + n, c->ch1_head - n);
    c->ch1_head -= (uint16_t)n;
    return n;
}

#endif /* ZCHG_CARRIER_H */
