/*
 * hdgl_fabric_test.c — Fabric loader test suite
 * Build: gcc -O2 -std=c99 -Wall -o hdgl_fabric_test hdgl_fabric_test.c -lm
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>

/* Pull in dependencies as single-header includes */
#include "zchg_carrier.h"

/* Stub out the genome fabric header dependency for the test */
#ifndef HDGL_GENOME_FABRIC_H
#define HDGL_GENOME_FABRIC_H
/* zchg_carrier_payload_hash is already in zchg_carrier.h */
#endif

#include "hdgl_fabric_loader.h"
#include "hdgl_fabric_loader.c"

static int n_pass = 0, n_fail = 0;

#define ASSERT(label, cond) do { \
    if (cond) { printf("  PASS  %s\n", label); n_pass++; } \
    else       { printf("  FAIL  %s\n", label); n_fail++; } \
} while(0)

#define ASSERT_EQ(label, a, b) do { \
    if ((uint64_t)(a)==(uint64_t)(b)) { printf("  PASS  %-44s %llu\n", label, (unsigned long long)(a)); n_pass++; } \
    else { printf("  FAIL  %-44s got %llu expected %llu\n", label, (unsigned long long)(a), (unsigned long long)(b)); n_fail++; } \
} while(0)

#define GENOME_FP  0xCAFEBABEu
#define STRAND_COUNT 8

/* ── Synthetic payloads ── */

/* HDGL source — starts with "glyph" */
static const uint8_t PAYLOAD_HDGL[] =
    "glyph test_node\n"
    "    id    = TEST\n"
    "    class = RUNTIME\n"
    "    state = INIT\n"
    "end\n";

/* ELF header (minimal 64-byte ELF64, e_magic only matters) */
static uint8_t PAYLOAD_ELF[64];

/* MBR disk image (512 bytes, 0xAA55 at 510:511) */
static uint8_t PAYLOAD_DISK[512];

/* zchg frame stream (version=1, type=GOSSIP=2) */
static const uint8_t PAYLOAD_FRAME[] = { 0x01, 0x02, 0x00, 0x00, 0x00, 0x00,
                                          0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

/* Raw data */
static const uint8_t PAYLOAD_RAW[] = "Hello, phi-lattice fabric!";

static void setup_payloads(void) {
    /* ELF magic */
    memset(PAYLOAD_ELF, 0, sizeof(PAYLOAD_ELF));
    PAYLOAD_ELF[0] = 0x7F; PAYLOAD_ELF[1] = 'E';
    PAYLOAD_ELF[2] = 'L';  PAYLOAD_ELF[3] = 'F';

    /* MBR magic */
    memset(PAYLOAD_DISK, 0x90, sizeof(PAYLOAD_DISK));  /* NOPs */
    PAYLOAD_DISK[510] = 0x55;
    PAYLOAD_DISK[511] = 0xAA;
}

/* ============================================================================
 * Tests
 * ============================================================================ */

static void test_type_detection(void) {
    printf("\n[ Type detection ]\n");

    ASSERT_EQ("HDGL detected",  fabric_detect_type(PAYLOAD_HDGL, sizeof(PAYLOAD_HDGL)), FTYPE_HDGL);
    ASSERT_EQ("ELF detected",   fabric_detect_type(PAYLOAD_ELF,  sizeof(PAYLOAD_ELF)),  FTYPE_ELF);
    ASSERT_EQ("DISK detected",  fabric_detect_type(PAYLOAD_DISK, sizeof(PAYLOAD_DISK)), FTYPE_DISK);
    ASSERT_EQ("FRAME detected", fabric_detect_type(PAYLOAD_FRAME,sizeof(PAYLOAD_FRAME)),FTYPE_FRAME);
    ASSERT_EQ("RAW detected",   fabric_detect_type(PAYLOAD_RAW,  sizeof(PAYLOAD_RAW)),  FTYPE_RAW);
}

static void test_content_hash(void) {
    printf("\n[ Content hash — prismatic, deterministic ]\n");

    uint64_t h1 = fabric_content_hash(PAYLOAD_RAW, sizeof(PAYLOAD_RAW));
    uint64_t h2 = fabric_content_hash(PAYLOAD_RAW, sizeof(PAYLOAD_RAW));
    ASSERT("hash deterministic", h1 == h2);

    uint64_t h3 = fabric_content_hash(PAYLOAD_HDGL, sizeof(PAYLOAD_HDGL));
    ASSERT("hash sensitive to content", h1 != h3);

    /* Verify it's not zero for interesting inputs */
    ASSERT("hash non-zero (HDGL)", h3 != 0);
    ASSERT("hash non-zero (RAW)",  h1 != 0);
}

static void test_phi_tau_strand(void) {
    printf("\n[ Phi-tau strand routing ]\n");

    /* All strands should be in [0, STRAND_COUNT) */
    int counts[8] = {0};
    for (int i = 0; i < 256; i++) {
        uint64_t addr = (uint64_t)i * 0x9E3779B9ULL + 0xDEADBEEFULL;
        uint8_t s = fabric_phi_tau_strand(addr, STRAND_COUNT);
        ASSERT("strand in [0,8)", s < STRAND_COUNT);
        counts[s]++;
    }
    /* Distribution: all 8 strands should get some hits */
    int all_covered = 1;
    for (int i = 0; i < 8; i++)
        if (counts[i] == 0) { all_covered = 0; break; }
    ASSERT("all strands covered by 256 addresses", all_covered);

    /* Deterministic */
    uint64_t addr = 0xABCD1234EF000000ULL;
    ASSERT("strand deterministic",
           fabric_phi_tau_strand(addr, 8) == fabric_phi_tau_strand(addr, 8));
}

static void test_identity_uniqueness(void) {
    printf("\n[ Identity uniqueness ]\n");

    /* Different content → different identity */
    hdgl_fabric_exec_result_t r1 = {0}, r2 = {0}, r3 = {0};

    hdgl_fabric_load(PAYLOAD_HDGL, sizeof(PAYLOAD_HDGL), GENOME_FP, 8, 0, &r1);
    hdgl_fabric_load(PAYLOAD_RAW,  sizeof(PAYLOAD_RAW),  GENOME_FP, 8, 0, &r2);
    ASSERT("identity: HDGL ≠ RAW", r1.identity != r2.identity);

    /* Same content, different genome_fp → different identity */
    hdgl_fabric_load(PAYLOAD_HDGL, sizeof(PAYLOAD_HDGL), 0xDEADBEEFu, 8, 0, &r3);
    ASSERT("identity: same content, diff key → diff identity", r1.identity != r3.identity);

    /* Same content, same genome_fp → same identity */
    hdgl_fabric_exec_result_t r1b = {0};
    hdgl_fabric_load(PAYLOAD_HDGL, sizeof(PAYLOAD_HDGL), GENOME_FP, 8, 0, &r1b);
    ASSERT("identity: deterministic", r1.identity == r1b.identity);
}

static void test_load_all_types(void) {
    printf("\n[ Load all payload types ]\n");

    hdgl_fabric_exec_result_t r = {0};
    int rc;

    rc = hdgl_fabric_load(PAYLOAD_HDGL, sizeof(PAYLOAD_HDGL), GENOME_FP, 8, 1, &r);
    ASSERT("load HDGL: rc=0",               rc == 0);
    ASSERT_EQ("load HDGL: type=HDGL",       r.payload_type, FTYPE_HDGL);
    ASSERT_EQ("load HDGL: status=PARSED",   r.exec_status, FABRIC_EXEC_HDGL_PARSED);

    r = (hdgl_fabric_exec_result_t){0};
    rc = hdgl_fabric_load(PAYLOAD_ELF, sizeof(PAYLOAD_ELF), GENOME_FP, 8, 1, &r);
    ASSERT("load ELF: rc=0",                rc == 0);
    ASSERT_EQ("load ELF: type=ELF",         r.payload_type, FTYPE_ELF);
    ASSERT_EQ("load ELF: status=VALID",     r.exec_status, FABRIC_EXEC_ELF_VALID);

    r = (hdgl_fabric_exec_result_t){0};
    rc = hdgl_fabric_load(PAYLOAD_DISK, sizeof(PAYLOAD_DISK), GENOME_FP, 8, 1, &r);
    ASSERT("load DISK: rc=0",               rc == 0);
    ASSERT_EQ("load DISK: type=DISK",       r.payload_type, FTYPE_DISK);

    r = (hdgl_fabric_exec_result_t){0};
    rc = hdgl_fabric_load(PAYLOAD_FRAME, sizeof(PAYLOAD_FRAME), GENOME_FP, 8, 1, &r);
    ASSERT("load FRAME: rc=0",              rc == 0);
    ASSERT_EQ("load FRAME: type=FRAME",     r.payload_type, FTYPE_FRAME);

    r = (hdgl_fabric_exec_result_t){0};
    rc = hdgl_fabric_load(PAYLOAD_RAW, sizeof(PAYLOAD_RAW), GENOME_FP, 8, 1, &r);
    ASSERT("load RAW: rc=0",                rc == 0);
    ASSERT_EQ("load RAW: type=RAW",         r.payload_type, FTYPE_RAW);
}

static void test_chunk_addressing(void) {
    printf("\n[ Chunk phi-addressing ]\n");

    /* Large payload: verify chunk count and addressing */
    size_t big_size = FABRIC_CHUNK_SIZE * 3 + 100;  /* 3 full + 1 partial */
    uint8_t *big = (uint8_t *)malloc(big_size);
    if (!big) { printf("  SKIP  (malloc failed)\n"); return; }
    for (size_t i = 0; i < big_size; i++) big[i] = (uint8_t)(i * 7 + 3);

    hdgl_fabric_exec_result_t r = {0};
    int rc = hdgl_fabric_load(big, big_size, GENOME_FP, 8, 1, &r);
    ASSERT("large payload: rc=0",            rc == 0);
    ASSERT_EQ("large payload: chunk_count",  r.chunk_count, 4);  /* 3 + 1 partial */
    free(big);

    /* Small payload: single chunk */
    r = (hdgl_fabric_exec_result_t){0};
    hdgl_fabric_load(PAYLOAD_RAW, sizeof(PAYLOAD_RAW), GENOME_FP, 8, 0, &r);
    ASSERT_EQ("small payload: chunk_count",  r.chunk_count, 1);
}

static void test_self_load(void) {
    printf("\n[ Self-load ]\n");

    /* Read hdgl_fabric.hdgl and load it as itself */
    FILE *f = fopen("hdgl_fabric.hdgl", "rb");
    if (!f) {
        printf("  SKIP  (hdgl_fabric.hdgl not found — run from build dir)\n");
        return;
    }
    fseek(f, 0, SEEK_END);
    long flen = ftell(f);
    rewind(f);
    uint8_t *fbuf = (uint8_t *)malloc((size_t)flen);
    if (!fbuf) { fclose(f); return; }
    size_t nread = fread(fbuf, 1, (size_t)flen, f);
    (void)nread;
    fclose(f);

    int rc = hdgl_fabric_self_load(fbuf, (size_t)flen, GENOME_FP, 1);
    ASSERT("self-load: rc=0",           rc == 0);

    /* Verify it detected as HDGL */
    uint8_t t = fabric_detect_type(fbuf, (size_t)flen);
    ASSERT_EQ("self-load: detected HDGL", t, FTYPE_HDGL);

    /* Verify identity is consistent */
    hdgl_fabric_exec_result_t r1 = {0}, r2 = {0};
    hdgl_fabric_load(fbuf, (size_t)flen, GENOME_FP, 8, 0, &r1);
    hdgl_fabric_load(fbuf, (size_t)flen, GENOME_FP, 8, 0, &r2);
    ASSERT("self-load: identity deterministic", r1.identity == r2.identity);

    /* Different genome_fp → different identity (key sensitivity) */
    hdgl_fabric_exec_result_t r3 = {0};
    hdgl_fabric_load(fbuf, (size_t)flen, GENOME_FP ^ 0xFFFFFFFFu, 8, 0, &r3);
    ASSERT("self-load: key-sensitive identity", r1.identity != r3.identity);

    free(fbuf);
}

static void test_reconstruct(void) {
    printf("\n[ Reconstruct ]\n");

    /* Load, then reconstruct */
    hdgl_fabric_exec_result_t r = {0};
    hdgl_fabric_load(PAYLOAD_RAW, sizeof(PAYLOAD_RAW), GENOME_FP, 8, 0, &r);

    uint8_t out[256] = {0};
    size_t got = hdgl_fabric_reconstruct(r.identity, out, sizeof(out));
    ASSERT_EQ("reconstruct: length", got, sizeof(PAYLOAD_RAW));
    ASSERT("reconstruct: content",
           memcmp(out, PAYLOAD_RAW, sizeof(PAYLOAD_RAW)) == 0);

    /* Non-existent identity → 0 bytes */
    got = hdgl_fabric_reconstruct(0xDEADBEEFCAFEBABEULL, out, sizeof(out));
    ASSERT_EQ("reconstruct: not-found returns 0", got, 0);
}

static void test_fabric_invariants(void) {
    printf("\n[ Fabric invariants ]\n");

    /* strand is always in [0, strand_count) */
    for (int sc = 8; sc <= 256; sc *= 2) {
        hdgl_fabric_exec_result_t r = {0};
        hdgl_fabric_load(PAYLOAD_HDGL, sizeof(PAYLOAD_HDGL), GENOME_FP, sc, 0, &r);
        char label[64];
        snprintf(label, sizeof(label), "strand_count=%d: strand in range", sc);
        ASSERT(label, r.strand < sc);
    }

    /* Empty payload → error */
    int rc = hdgl_fabric_load(NULL, 0, GENOME_FP, 8, 0, NULL);
    ASSERT("empty payload returns -1", rc == -1);

    /* genome_fp=0 still works (degenerate key) */
    hdgl_fabric_exec_result_t r = {0};
    rc = hdgl_fabric_load(PAYLOAD_RAW, sizeof(PAYLOAD_RAW), 0, 8, 0, &r);
    ASSERT("genome_fp=0: rc=0", rc == 0);
    ASSERT("genome_fp=0: identity non-zero", r.identity != 0);
}

int main(void) {
    printf("═══════════════════════════════════════════════════\n");
    printf("HDGL Fabric Loader — Self-Test\n");
    printf("genome_fp: 0x%08X  strand_count: %d\n", GENOME_FP, STRAND_COUNT);
    printf("═══════════════════════════════════════════════════\n");

    setup_payloads();

    test_type_detection();
    test_content_hash();
    test_phi_tau_strand();
    test_identity_uniqueness();
    test_load_all_types();
    test_chunk_addressing();
    test_reconstruct();
    test_self_load();
    test_fabric_invariants();

    printf("\n═══════════════════════════════════════════════════\n");
    printf("Results: %d PASS  %d FAIL\n", n_pass, n_fail);
    printf("═══════════════════════════════════════════════════\n");
    return n_fail == 0 ? 0 : 1;
}
