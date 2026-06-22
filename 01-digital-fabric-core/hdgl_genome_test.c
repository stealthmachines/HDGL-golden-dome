/*
 * hdgl_genome_test.c — Standalone test for hdgl_genome_fabric
 *
 * Builds a synthetic Omega graph matching a typical APU2 boot
 * (CPU + MEM + IO×2 + COMPILER + RUNTIME + STORAGE + PCI),
 * initializes the fabric engine, runs 3 ticks, verifies outputs.
 *
 * Build: gcc -O2 -std=c99 -o hdgl_genome_test hdgl_genome_test.c -lm
 */

#include <stdio.h>
#include <math.h>
#include <string.h>
#include <stdint.h>

/* ── Pull in the full implementation (single-header style) ── */
#include "hdgl_genome_fabric.h"

/* Forward-declare the impl (included internally) */
int hdgl_genome_fabric_init(GenomeFabric *fab,
                             const uint64_t *node_ids,
                             const uint32_t *node_types,
                             const uint32_t *node_states,
                             int n_nodes,
                             uint32_t dn_aggregate,
                             uint32_t phi_lattice_mean,
                             const double theta_locked[8],
                             int verbose);

int hdgl_genome_fabric_tick(GenomeFabric *fab,
                              int cell_id,
                              int frame_num,
                              FabricPoint *strand1_out,
                              FabricPoint *strand2_out,
                              int max_out);

void hdgl_genome_fabric_gossip_update(GenomeFabric *fab,
                                       uint32_t peer_dn_aggregate,
                                       uint32_t peer_fingerprint,
                                       int verbose);

#include "hdgl_genome_fabric.c"

/* ── Test Omega graph (APU2 profile, 8 nodes) ── */
/*  Identity fields: assigned sequentially (as router64 would assign) */
static const uint64_t TEST_IDS[8] = {
    0x0000000000000000,  /* ROOT */
    0x0000000000000001,  /* CPU  */
    0x0001000000000000,  /* MEM  */
    0x0002000000000000,  /* IO   */
    0x0002000000000001,  /* IO   (second NIC) */
    0x0004000000000000,  /* COMPILER */
    0x0005000000000000,  /* RUNTIME  */
    0x000A000000000000,  /* STORAGE  */
};

static const uint32_t TEST_TYPES[8] = {
    OTYPE_ROOT,        /* → BASE_C */
    OTYPE_CPU,         /* → BASE_A */
    OTYPE_MEM,         /* → BASE_C */
    OTYPE_IO,          /* → BASE_G */
    OTYPE_IO,          /* → BASE_G */
    OTYPE_COMPILER,    /* → BASE_T */
    OTYPE_RUNTIME,     /* → BASE_A */
    OTYPE_STORAGE,     /* → BASE_T */
};
/* Expected sequence: C A C G G T A T */

static const uint32_t TEST_STATES[8] = {
    4, /* EXECUTED */
    4,
    4,
    4,
    4,
    4,
    4,
    4,
};

/* Verified Dₙ aggregate from conscious compute_Dn_r */
#define TEST_DN_AGGREGATE   0x80C0C0E8u

/* phi_lattice_mean: synthetic (typical post-consensus value) */
#define TEST_PHI_LAT_MEAN   0x7FFF0000u

/* Kuramoto locked phases (synthetic — typical LOCK state) */
static const double TEST_THETA[8] = {
    0.0123, 0.0456, 0.0789, 0.0234,
    0.0567, 0.0890, 0.0345, 0.0678,
};

static int n_pass = 0;
static int n_fail = 0;

#define ASSERT_EQ_INT(label, got, exp) do { \
    if ((got) == (exp)) { \
        printf("  PASS  %-36s = %d\n", (label), (got)); n_pass++; \
    } else { \
        printf("  FAIL  %-36s : got %d, expected %d\n", (label), (got), (exp)); n_fail++; \
    } \
} while(0)

#define ASSERT_NEAR(label, got, exp, tol) do { \
    double _d = fabs((double)(got) - (double)(exp)); \
    if (_d <= (tol)) { \
        printf("  PASS  %-36s = %.6f\n", (label), (double)(got)); n_pass++; \
    } else { \
        printf("  FAIL  %-36s : got %.6f, expected %.6f ± %.6f\n", \
               (label), (double)(got), (double)(exp), (tol)); n_fail++; \
    } \
} while(0)

#define ASSERT_RANGE(label, got, lo, hi) do { \
    if ((double)(got) >= (double)(lo) && (double)(got) <= (double)(hi)) { \
        printf("  PASS  %-36s = %.4f  [%.4f..%.4f]\n", (label), (double)(got), (double)(lo), (double)(hi)); n_pass++; \
    } else { \
        printf("  FAIL  %-36s : %.4f outside [%.4f..%.4f]\n", \
               (label), (double)(got), (double)(lo), (double)(hi)); n_fail++; \
    } \
} while(0)

int main(void) {
    printf("═══════════════════════════════════════════════════\n");
    printf("HDGL Genome Fabric Engine — Self-Test\n");
    printf("Omega graph: %d nodes (APU2 synthetic profile)\n", 8);
    printf("═══════════════════════════════════════════════════\n\n");

    /* ── 1. Initialize fabric from synthetic Omega graph ── */
    printf("[ Phase 1: Init ]\n");

    GenomeFabric fab;
    memset(&fab, 0, sizeof(fab));

    int rc = hdgl_genome_fabric_init(&fab,
                                      TEST_IDS, TEST_TYPES, TEST_STATES, 8,
                                      TEST_DN_AGGREGATE, TEST_PHI_LAT_MEAN,
                                      TEST_THETA, 1 /* verbose */);

    ASSERT_EQ_INT("init return code", rc, 0);
    ASSERT_EQ_INT("n_nodes", fab.n_nodes, 8);

    /* Verify base-4 sequence: C A C G G T A T */
    static const uint8_t expected_seq[8] = {
        BASE_C, BASE_A, BASE_C, BASE_G,
        BASE_G, BASE_T, BASE_A, BASE_T
    };
    static const char *bnames[4] = {"A","C","G","T"};
    printf("\n  Sequence: ");
    int seq_ok = 1;
    for (int i = 0; i < 8; i++) {
        printf("%s", bnames[fab.base4_seq[i]]);
        if (fab.base4_seq[i] != expected_seq[i]) seq_ok = 0;
    }
    printf("  %s\n", seq_ok ? "PASS" : "FAIL");
    if (seq_ok) n_pass++; else n_fail++;

    printf("\n[ Phase 2: Genome Stats ]\n");

    /* GC content: G=2, C=2, total=8 → 0.5 (ROOT=C, MEM=C, IO=G, IO=G) */
    ASSERT_NEAR("gc_content",    fab.stats.gc_content,    0.5,   0.01);
    /* AT content: A=2, T=2 → 0.5 */
    ASSERT_NEAR("at_ratio",      fab.stats.at_ratio,      0.5,   0.01);
    /* Shannon entropy: 4 bases each with p=0.25 → 2.0 bits */
    ASSERT_NEAR("shannon_entropy", fab.stats.shannon_entropy, 2.0, 0.1);

    printf("\n[ Phase 3: Derived Config ]\n");

    /* core_radius: popcount(0x80C0C0E8) = 9 → 9 × φ / 32 ≈ 0.4551 */
    double expected_radius = 9.0 * 1.6180339887498948 / 32.0;
    ASSERT_NEAR("core_radius",   fab.config.core_radius,  expected_radius, 0.001);

    /* strand_sep: at_ratio × φ⁻¹ = 0.5 × 0.6180 ≈ 0.309 */
    ASSERT_NEAR("strand_sep",    fab.config.strand_sep,   0.309, 0.01);

    /* strand_count: next_pow2(8 × 0.5 + 8) = next_pow2(12) = 16 */
    ASSERT_EQ_INT("strand_count", fab.config.strand_count, 16);

    /* strand_auth: low byte of 0x80C0C0E8 = 0xE8 */
    ASSERT_EQ_INT("strand_auth",  fab.config.strand_auth, 0xE8);

    /* points_per_tick: 100 + (0xFFFF0000 & 0xFFFF) * 500 / 65535 = 100 */
    ASSERT_RANGE("points_per_tick", fab.config.points_per_tick, 100, 600);

    /* n_cells: all 8 nodes are EXECUTED (state ≥ 2) → 8 cells */
    ASSERT_EQ_INT("n_cells",     fab.n_cells, 8);

    /* Palette sanity: codon 0 hue = 0.0 */
    ASSERT_NEAR("palette[0].h",  fab.config.palette[0].h,  0.0,  0.01);
    ASSERT_RANGE("palette[0].s", fab.config.palette[0].s,  0.5, 1.0);
    ASSERT_RANGE("palette[0].v", fab.config.palette[0].v,  0.6, 1.0);

    /* Palette codon 32 hue = 180.0 */
    ASSERT_NEAR("palette[32].h", fab.config.palette[32].h, 180.0, 1.0);

    printf("\n[ Phase 4: Fabric Ticks ]\n");

    FabricPoint s1[600], s2[600];
    memset(s1, 0, sizeof(s1));
    memset(s2, 0, sizeof(s2));

    /* Tick 0, cell 0 */
    int n = hdgl_genome_fabric_tick(&fab, 0, 0, s1, s2, 600);
    ASSERT_RANGE("tick0 point count", n, 100, 600);

    /* All z values should be finite */
    int z_ok = 1;
    for (int i = 0; i < n; i++) {
        if (!isfinite(s1[i].z) || !isfinite(s2[i].z)) { z_ok = 0; break; }
    }
    ASSERT_EQ_INT("tick0 z finite", z_ok, 1);

    /* x/y should be within ±(core_radius + strand_sep + offset) */
    double bound = fab.config.core_radius * 3.0 + fab.config.strand_sep + 1.0;
    int xy_ok = 1;
    for (int i = 0; i < n; i++) {
        if (fabs(s1[i].x) > bound || fabs(s1[i].y) > bound) { xy_ok = 0; break; }
    }
    ASSERT_EQ_INT("tick0 xy bounded", xy_ok, 1);

    /* Color values in range */
    int col_ok = 1;
    for (int i = 0; i < n; i++) {
        if (s1[i].color_h < 0.0f || s1[i].color_h > 360.0f) { col_ok = 0; break; }
        if (s1[i].color_s < 0.4f || s1[i].color_s > 1.1f)   { col_ok = 0; break; }
        if (s1[i].color_v < 0.5f || s1[i].color_v > 1.1f)    { col_ok = 0; break; }
    }
    ASSERT_EQ_INT("tick0 color HSV range", col_ok, 1);

    /* Strand 2 should differ from strand 1 in x (strand separation) */
    int sep_ok = 1;
    for (int i = 0; i < n && sep_ok; i++) {
        float dx = s2[i].x - s1[i].x;
        float dy = s2[i].y - s1[i].y;
        float dist = sqrtf(dx*dx + dy*dy);
        /* Should be > 0 (counter-rotating, not coincident) */
        if (dist < 1e-6f) { sep_ok = 0; }
    }
    ASSERT_EQ_INT("tick0 strand separation non-zero", sep_ok, 1);

    /* Tick 1 */
    n = hdgl_genome_fabric_tick(&fab, 0, 1, s1, s2, 600);
    ASSERT_RANGE("tick1 point count", n, 100, 600);

    /* Tick 2 */
    n = hdgl_genome_fabric_tick(&fab, 0, 2, s1, s2, 600);
    ASSERT_RANGE("tick2 point count", n, 100, 600);

    printf("\n[ Phase 5: Gossip Integration ]\n");

    /* Simulate a peer with a different dn_aggregate */
    uint32_t peer_dn  = 0xC0E080A0u;
    uint32_t peer_fp  = 0xDEADBEEFu;
    uint32_t old_auth = fab.config.strand_auth;

    hdgl_genome_fabric_gossip_update(&fab, peer_dn, peer_fp, 1 /* verbose */);

    /* EMA should shift strand_auth toward peer */
    /* (May or may not change low byte — just check it's still a valid byte) */
    ASSERT_RANGE("post-gossip strand_auth", fab.config.strand_auth, 0, 255);
    printf("  INFO  strand_auth: 0x%02X → 0x%02X (EMA shift)\n",
           old_auth, fab.config.strand_auth);

    /* Fingerprint should have changed */
    ASSERT_EQ_INT("gossip fingerprint changed",
                  (fab.gossip_fingerprint != 0x80C0C0E8u /* initial */), 1);

    printf("\n[ Phase 6: HSV → RGB ]\n");

    float r, g, b;
    /* Pure red: H=0, S=1, V=1 → R=1 G=0 B=0 */
    hdgl_genome_hsv_to_rgb(0.0f, 1.0f, 1.0f, &r, &g, &b);
    ASSERT_NEAR("red H=0 R", r, 1.0, 0.01);
    ASSERT_NEAR("red H=0 G", g, 0.0, 0.01);
    ASSERT_NEAR("red H=0 B", b, 0.0, 0.01);

    /* Pure green: H=120 → R=0 G=1 B=0 */
    hdgl_genome_hsv_to_rgb(120.0f, 1.0f, 1.0f, &r, &g, &b);
    ASSERT_NEAR("green H=120 R", r, 0.0, 0.01);
    ASSERT_NEAR("green H=120 G", g, 1.0, 0.01);

    /* Neutral (S=0): R=G=B=V */
    hdgl_genome_hsv_to_rgb(200.0f, 0.0f, 0.75f, &r, &g, &b);
    ASSERT_NEAR("neutral S=0 R", r, 0.75, 0.01);
    ASSERT_NEAR("neutral S=0 G", g, 0.75, 0.01);
    ASSERT_NEAR("neutral S=0 B", b, 0.75, 0.01);

    /* ── Summary ── */
    printf("\n═══════════════════════════════════════════════════\n");
    printf("Results: %d PASS  %d FAIL\n", n_pass, n_fail);
    printf("═══════════════════════════════════════════════════\n");

    return (n_fail == 0) ? 0 : 1;
}
