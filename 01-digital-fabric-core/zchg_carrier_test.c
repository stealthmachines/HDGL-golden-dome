/*
 * zchg_carrier_test.c — End-to-end test for all three carrier channels
 *
 * Build: gcc -O2 -std=c99 -Wall -o zchg_carrier_test zchg_carrier_test.c
 * (header-only: no other sources needed for the test)
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <assert.h>

#include "zchg_carrier.h"

static int n_pass = 0;
static int n_fail = 0;

#define ASSERT(label, cond) do { \
    if (cond) { printf("  PASS  %s\n", label); n_pass++; } \
    else       { printf("  FAIL  %s\n", label); n_fail++; } \
} while(0)

#define ASSERT_EQ32(label, a, b) do { \
    if ((a)==(b)) { printf("  PASS  %-40s 0x%08X\n", label, (unsigned)(a)); n_pass++; } \
    else { printf("  FAIL  %-40s got 0x%08X expected 0x%08X\n", label, (unsigned)(a), (unsigned)(b)); n_fail++; } \
} while(0)

/* ── Test genome fingerprints ── */
#define GENOME_A  0xCAFEBABEu
#define GENOME_B  0xCAFEBABEu   /* same on both sides — shared hardware derivation */

/* ══════════════════════════════════════════════════════════════════════════
 * PRIMITIVE TESTS
 * ══════════════════════════════════════════════════════════════════════════ */

static void test_primitive(void) {
    printf("\n[ Primitive: phi_fold / phi_unfold ]\n");

    /* fold→unfold identity for a range of inputs */
    uint32_t keys[] = { 0x00000000u, 0xFFFFFFFFu, GENOME_A, 0x12345678u };
    uint32_t seqs[] = { 0, 1, 42, 0xFFFFu };

    for (int ki = 0; ki < 4; ki++) {
        for (int si = 0; si < 4; si++) {
            uint32_t x    = 0xDEAD0000u | (ki << 8) | si;
            uint32_t fold = zc_fold(x, keys[ki], seqs[si]);
            uint32_t back = zc_unfold(fold, keys[ki], seqs[si]);
            char label[64];
            snprintf(label, sizeof(label), "round-trip key[%d] seq[%d]", ki, si);
            ASSERT_EQ32(label, back, x);
        }
    }

    /* fold outputs are distinct for distinct inputs (collision check) */
    uint32_t f0 = zc_fold(0u, GENOME_A, 0u);
    uint32_t f1 = zc_fold(1u, GENOME_A, 0u);
    uint32_t f2 = zc_fold(0u, GENOME_A, 1u);
    ASSERT("fold: distinct for distinct x",     f0 != f1);
    ASSERT("fold: distinct for distinct seq",   f0 != f2);
}

/* ══════════════════════════════════════════════════════════════════════════
 * CH-0: FRAME RESERVED FIELD
 * ══════════════════════════════════════════════════════════════════════════ */

static void test_ch0(void) {
    printf("\n[ CH-0: frame.reserved carrier ]\n");

    zchg_carrier_t tx, rx;
    zchg_carrier_init(&tx, GENOME_A);
    zchg_carrier_init(&rx, GENOME_B);

    /* Simulate payload bytes */
    uint8_t payload[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE };
    uint32_t ph = zchg_carrier_payload_hash(payload, sizeof(payload));

    /* Message to embed */
    uint8_t message[] = "HDGL";   /* 4 bytes — exactly one frame's worth */
    uint32_t reserved_field = 0;

    uint8_t n = zchg_carrier_frame_encode(&tx, &reserved_field, ph, message, 4);
    ASSERT("CH-0 encode: returns 4", n == 4);
    ASSERT("CH-0 encode: reserved non-zero", reserved_field != 0);

    /* Decode on the other side */
    uint8_t recovered[4] = {0};
    uint8_t dn = zchg_carrier_frame_decode(&rx, reserved_field, ph, recovered, 4);
    ASSERT("CH-0 decode: returns 4", dn == 4);
    ASSERT("CH-0 decode: byte 0", recovered[0] == 'H');
    ASSERT("CH-0 decode: byte 1", recovered[1] == 'D');
    ASSERT("CH-0 decode: byte 2", recovered[2] == 'G');
    ASSERT("CH-0 decode: byte 3", recovered[3] == 'L');

    /* Drain from accumulation buffer */
    uint8_t drained[16];
    size_t nd = zchg_carrier_ch0_drain(&rx, drained, sizeof(drained));
    ASSERT("CH-0 drain: 4 bytes", nd == 4);
    ASSERT("CH-0 drain: correct", memcmp(drained, "HDGL", 4) == 0);

    /* Multi-frame: 32 bytes across 8 frames */
    printf("  --- multi-frame (8 × 4 bytes = 32 bytes) ---\n");
    zchg_carrier_init(&tx, GENOME_A);
    zchg_carrier_init(&rx, GENOME_B);

    const char *msg32 = "analog-over-digital-fabric-mesh!";   /* exactly 32 */
    for (int f = 0; f < 8; f++) {
        uint8_t pl[16];
        memset(pl, (uint8_t)(f * 7), sizeof(pl));
        uint32_t phl = zchg_carrier_payload_hash(pl, sizeof(pl));

        uint32_t res = 0;
        zchg_carrier_frame_encode(&tx, &res, phl,
                                   (const uint8_t *)msg32 + f*4, 4);

        uint8_t out[4];
        zchg_carrier_frame_decode(&rx, res, phl, out, 4);
    }

    uint8_t big[32];
    size_t got = zchg_carrier_ch0_drain(&rx, big, 32);
    ASSERT("CH-0 multi: 32 bytes received", got == 32);
    ASSERT("CH-0 multi: content correct",
           memcmp(big, "analog-over-digital-fabric-mesh!", 32) == 0);

    /* Zero-carrier frame: encode zeros, verify no decode corruption */
    uint8_t zeros[4] = {0, 0, 0, 0};
    uint32_t res_z = 0;
    zchg_carrier_frame_encode(&tx, &res_z, 0xAAAAAAAAu, zeros, 4);
    uint8_t out_z[4] = {0xFF, 0xFF, 0xFF, 0xFF};
    zchg_carrier_frame_decode(&rx, res_z, 0xAAAAAAAAu, out_z, 4);
    ASSERT("CH-0 zero-carrier: decodes to zeros",
           out_z[0] == 0 && out_z[1] == 0 && out_z[2] == 0 && out_z[3] == 0);
}

/* ══════════════════════════════════════════════════════════════════════════
 * CH-1: GOSSIP STRAND WEIGHT LSB MODULATION
 * ══════════════════════════════════════════════════════════════════════════ */

static void test_ch1(void) {
    printf("\n[ CH-1: gossip strand_weights LSB carrier ]\n");

    zchg_carrier_t tx, rx;
    zchg_carrier_init(&tx, GENOME_A);
    zchg_carrier_init(&rx, GENOME_B);

    /* Legitimate weights (from zchg_compute_strand_weight, typical values) */
    uint8_t legit_weights[8] = { 72, 68, 75, 70, 73, 69, 74, 71 };
    uint8_t tx_weights[8], rx_weights[8];
    memcpy(tx_weights, legit_weights, 8);
    memcpy(rx_weights, legit_weights, 8);  /* rx will receive these */

    /* Carrier: 2 bytes */
    uint8_t carrier_tx[2] = { 0xA5, 0x3C };
    zchg_carrier_gossip_encode(&tx, tx_weights, carrier_tx);

    /* Verify: legitimate weight top-6 bits unchanged (bottom 2 may differ) */
    for (int i = 0; i < 8; i++) {
        uint8_t top6_orig = legit_weights[i] & 0xFCu;
        uint8_t top6_mod  = tx_weights[i]    & 0xFCu;
        char label[64];
        snprintf(label, sizeof(label), "CH-1 encode: strand[%d] top-6 preserved", i);
        ASSERT(label, top6_orig == top6_mod);
    }

    /* Simulate gossip message transmission: rx receives tx_weights */
    memcpy(rx_weights, tx_weights, 8);

    /* Decode on receiver */
    uint8_t carrier_rx[2] = {0, 0};
    zchg_carrier_gossip_decode(&rx, rx_weights, carrier_rx);

    ASSERT("CH-1 decode: byte 0 correct", carrier_rx[0] == carrier_tx[0]);
    ASSERT("CH-1 decode: byte 1 correct", carrier_rx[1] == carrier_tx[1]);

    /* After decode, rx_weights should have clean bottom-2 bits */
    for (int i = 0; i < 8; i++) {
        char label[64];
        snprintf(label, sizeof(label), "CH-1 decode: strand[%d] LSB-2 stripped", i);
        ASSERT(label, (rx_weights[i] & ZC_GOSSIP_CARRIER_MASK) == 0);
    }

    /* Multi-cycle: 16 gossip rounds = 32 bytes */
    printf("  --- multi-cycle (16 rounds × 2 bytes = 32 bytes) ---\n");
    zchg_carrier_init(&tx, GENOME_A);
    zchg_carrier_init(&rx, GENOME_B);

    uint8_t payload32[32];
    for (int i = 0; i < 32; i++) payload32[i] = (uint8_t)(i * 7 + 3);

    for (int round = 0; round < 16; round++) {
        uint8_t w[8] = { 70, 72, 68, 74, 71, 73, 69, 75 };
        zchg_carrier_gossip_encode(&tx, w, payload32 + round * 2);

        uint8_t recovered[2];
        zchg_carrier_gossip_decode(&rx, w, recovered);
    }

    uint8_t got32[32];
    size_t ng = zchg_carrier_ch1_drain(&rx, got32, 32);
    ASSERT("CH-1 multi: 32 bytes received", ng == 32);
    ASSERT("CH-1 multi: content correct",   memcmp(got32, payload32, 32) == 0);

    /* Edge case: all-100 weights (EMA ceiling) */
    uint8_t max_w[8]; memset(max_w, 100, 8);
    uint8_t chunk[2] = { 0xFF, 0xFF };
    zchg_carrier_gossip_encode(&tx, max_w, chunk);
    for (int i = 0; i < 8; i++) {
        ASSERT("CH-1 max-weight: top-6 still 100-ish",
               (max_w[i] & 0xFCu) == (100u & 0xFCu));
    }
}

/* ══════════════════════════════════════════════════════════════════════════
 * CH-2: /serve/zc/ PHI-PATH SEQUENCE
 * ══════════════════════════════════════════════════════════════════════════ */

static void test_ch2(void) {
    printf("\n[ CH-2: /serve/zc/ phi-path carrier ]\n");

    zchg_carrier_t tx, rx;
    zchg_carrier_init(&tx, GENOME_A);
    zchg_carrier_init(&rx, GENOME_B);

    /* Encode 16 bits (2 bytes) one bit at a time */
    uint8_t msg2[2] = { 0xB7, 0x4A };
    char path_buf[64];
    int decoded_bits[16];

    for (int bit_idx = 0; bit_idx < 16; bit_idx++) {
        uint8_t byte_i = (uint8_t)(bit_idx / 8);
        uint8_t bit_i  = (uint8_t)(bit_idx % 8);
        uint8_t bit    = (msg2[byte_i] >> bit_i) & 1u;

        zchg_carrier_path_encode(&tx, bit, path_buf, sizeof(path_buf));
        decoded_bits[bit_idx] = zchg_carrier_path_decode(&rx, path_buf);
    }

    /* Verify all bits */
    int all_ok = 1;
    for (int bit_idx = 0; bit_idx < 16; bit_idx++) {
        uint8_t byte_i    = (uint8_t)(bit_idx / 8);
        uint8_t bit_i     = (uint8_t)(bit_idx % 8);
        uint8_t expected  = (msg2[byte_i] >> bit_i) & 1u;
        if (decoded_bits[bit_idx] != (int)expected) { all_ok = 0; break; }
    }
    ASSERT("CH-2: all 16 bits correct", all_ok);

    /* Verify path format */
    zchg_carrier_init(&tx, GENOME_A);
    zchg_carrier_path_encode(&tx, 0, path_buf, sizeof(path_buf));
    ASSERT("CH-2 path: starts /serve/zc/",
           strncmp(path_buf, "/serve/zc/", 10) == 0);
    ASSERT("CH-2 path: length is 27",
           strlen(path_buf) == 27);   /* /serve/zc/ + 8 + / + 8 */

    /* Sync mismatch: wrong seq on receiver → -1 */
    zchg_carrier_init(&tx, GENOME_A);
    zchg_carrier_init(&rx, GENOME_B);
    rx.seq_path = 5;   /* force out-of-sync */
    zchg_carrier_path_encode(&tx, 1, path_buf, sizeof(path_buf));
    int result = zchg_carrier_path_decode(&rx, path_buf);
    ASSERT("CH-2 sync mismatch: returns -1", result == -1);

    /* Non-carrier path: returns -1 */
    zchg_carrier_init(&rx, GENOME_B);
    result = zchg_carrier_path_decode(&rx, "/serve/somefile.json");
    ASSERT("CH-2 non-carrier path: returns -1", result == -1);

    /* Bit=0 and bit=1 generate distinct paths at same seq */
    zchg_carrier_init(&tx, GENOME_A);
    char path0[64], path1[64];
    zchg_carrier_path_encode(&tx, 0, path0, sizeof(path0));
    tx.seq_path--;   /* back up to same seq */
    zchg_carrier_path_encode(&tx, 1, path1, sizeof(path1));
    ASSERT("CH-2: bit=0 and bit=1 produce distinct paths",
           strcmp(path0, path1) != 0);
    /* seg0 (framing) must be identical */
    ASSERT("CH-2: seg0 identical for both bits",
           strncmp(path0 + 10, path1 + 10, 8) == 0);
}

/* ══════════════════════════════════════════════════════════════════════════
 * CROSS-CHANNEL ISOLATION
 * Check that operating one channel doesn't corrupt another's seq counter.
 * ══════════════════════════════════════════════════════════════════════════ */

static void test_isolation(void) {
    printf("\n[ Cross-channel isolation ]\n");

    zchg_carrier_t c;
    zchg_carrier_init(&c, GENOME_A);

    uint32_t seq_frame0  = c.seq_frame;
    uint32_t seq_gossip0 = c.seq_gossip;
    uint32_t seq_path0   = c.seq_path;

    /* Drive CH-0 10 times */
    for (int i = 0; i < 10; i++) {
        uint32_t r = 0;
        uint8_t d[4] = {1,2,3,4};
        zchg_carrier_frame_encode(&c, &r, 0xAAAAu, d, 4);
    }
    ASSERT("isolation: CH-0 advances seq_frame",  c.seq_frame  == seq_frame0  + 10);
    ASSERT("isolation: CH-0 leaves seq_gossip",   c.seq_gossip == seq_gossip0);
    ASSERT("isolation: CH-0 leaves seq_path",     c.seq_path   == seq_path0);

    /* Drive CH-1 5 times */
    for (int i = 0; i < 5; i++) {
        uint8_t w[8] = {50,50,50,50,50,50,50,50};
        uint8_t d[2] = {0xAB, 0xCD};
        zchg_carrier_gossip_encode(&c, w, d);
    }
    ASSERT("isolation: CH-1 advances seq_gossip", c.seq_gossip == seq_gossip0 + 5);
    ASSERT("isolation: CH-1 leaves seq_frame",    c.seq_frame  == seq_frame0  + 10);
    ASSERT("isolation: CH-1 leaves seq_path",     c.seq_path   == seq_path0);

    /* Drive CH-2 7 times */
    for (int i = 0; i < 7; i++) {
        char p[64];
        zchg_carrier_path_encode(&c, i & 1, p, sizeof(p));
    }
    ASSERT("isolation: CH-2 advances seq_path",   c.seq_path   == seq_path0   + 7);
    ASSERT("isolation: CH-2 leaves seq_frame",    c.seq_frame  == seq_frame0  + 10);
    ASSERT("isolation: CH-2 leaves seq_gossip",   c.seq_gossip == seq_gossip0 + 5);
}

/* ══════════════════════════════════════════════════════════════════════════
 * PAYLOAD HASH
 * ══════════════════════════════════════════════════════════════════════════ */

static void test_payload_hash(void) {
    printf("\n[ Payload hash ]\n");

    /* Empty payload */
    uint32_t h0 = zchg_carrier_payload_hash(NULL, 0);
    ASSERT("hash: empty payload returns PHI32", h0 == ZC_PHI32);

    /* Deterministic */
    uint8_t data[] = {0xDE, 0xAD, 0xBE, 0xEF};
    uint32_t h1 = zchg_carrier_payload_hash(data, 4);
    uint32_t h2 = zchg_carrier_payload_hash(data, 4);
    ASSERT("hash: deterministic", h1 == h2);

    /* Sensitive to content */
    uint8_t data2[] = {0xDE, 0xAD, 0xBE, 0xF0};   /* last byte differs */
    uint32_t h3 = zchg_carrier_payload_hash(data2, 4);
    ASSERT("hash: sensitive to content", h1 != h3);

    /* Sensitive to length */
    uint32_t h4 = zchg_carrier_payload_hash(data, 3);
    ASSERT("hash: sensitive to length", h1 != h4);
}

/* ══════════════════════════════════════════════════════════════════════════
 * MAIN
 * ══════════════════════════════════════════════════════════════════════════ */

int main(void) {
    printf("═══════════════════════════════════════════════════\n");
    printf("ZCHG Carrier — Three-Channel Self-Test\n");
    printf("genome_fp: 0x%08X\n", GENOME_A);
    printf("═══════════════════════════════════════════════════\n");

    test_payload_hash();
    test_primitive();
    test_ch0();
    test_ch1();
    test_ch2();
    test_isolation();

    printf("\n═══════════════════════════════════════════════════\n");
    printf("Results: %d PASS  %d FAIL\n", n_pass, n_fail);
    printf("═══════════════════════════════════════════════════\n");
    return (n_fail == 0) ? 0 : 1;
}
