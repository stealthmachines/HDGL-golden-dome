/*
 * hdgl_genome_fabric.c — Fabric genome engine for HDGL
 *
 * PHILOSOPHY:
 *   The Omega graph IS the genome. Every parameter derives from it.
 *   No FASTA. No hardcoded colors. No arbitrary constants.
 *   Only: φ, π, Fibonacci, primes, and the hardware itself.
 *
 * WHAT THIS FILE DOES:
 *   1. Encodes the Omega graph as a base-4 sequence (A/C/G/T ← node type)
 *   2. Computes genome statistics (GC, entropy, k-mers, codons, transitions)
 *   3. Derives ALL fabric parameters from those statistics
 *   4. Emits fabric points each tick via the golden-angle spiral
 *   5. Integrates with zchg_lattice gossip for cross-node genome convergence
 *
 * THREE PRIMITIVES (same as hdgl_analog_engine.c):
 *   Dₙ(r) = √(φ·Fₙ·2ⁿ·Pₙ·Ω)·rᵏ    ← continuous analog signal
 *   Kuramoto θᵢ locked phases          ← derived statistics / camera path
 *   A=0 C=1 G=2 T=3                    ← base-4 codec
 *
 * DEPENDENCIES (all native — no third party):
 *   hdgl_analog_engine.c   (included — Dₙ, Kuramoto, constants)
 *   <math.h>               (sin, cos, sqrt, log, fmod, ldexp)
 *   <stdint.h>, <string.h> (no libc beyond these)
 *
 * Licensed per https://zchg.org/t/legal-notice-copyright-applicable-ip-and-licensing-read-me/440
 */

#include "hdgl_genome_fabric.h"
#include "hdgl_analog_engine.c"   /* pulls in PHI, FIB, PRIMES, dn_r, Kuramoto */

#include <math.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>

/* ── Universal constants (already in analog_engine, aliased here for clarity) ── */
/* PHI, PHI_INV, SQRT_PHI, PI, DT, DIMS — all from hdgl_analog_engine.c */

#define GOLDEN_ANGLE_RAD   2.399963229728653   /* 2π/φ² */
#define MAX_NODES          256                 /* maximum Omega graph nodes */
#define MAX_STRAND_COUNT   256                 /* zchg_store max */
#define EMA_ALPHA          0.3                 /* gossip EMA — matches zchg_lattice */

/* Base-4 codec (information theory — not arbitrary) */
#define BASE_A  0   /* CPU, RUNTIME — adenine, purine, energetic */
#define BASE_C  1   /* MEM, default — cytosine, pyrimidine, structural */
#define BASE_G  2   /* IO — guanine, purine, bridging */
#define BASE_T  3   /* COMPILER, STORAGE — thymine, pyrimidine, templating */

/* Omega node types (from hdgl_bootstrap.c / hdgl_firmware.hdgl) */
#define OTYPE_ROOT      0
#define OTYPE_CPU       1
#define OTYPE_MEM       2
#define OTYPE_IO        3
#define OTYPE_COMPILER  4
#define OTYPE_RUNTIME   5
#define OTYPE_BOOTSTRAP 6
#define OTYPE_REPLICA   7
#define OTYPE_PCI       8
#define OTYPE_GPU       9
#define OTYPE_STORAGE   10
#define OTYPE_BOOT      11

/* ── Population count (no __builtin_popcount — portable) ── */
static int popcount32(uint32_t x) {
    x = x - ((x >> 1) & 0x55555555u);
    x = (x & 0x33333333u) + ((x >> 2) & 0x33333333u);
    x = (x + (x >> 4)) & 0x0F0F0F0Fu;
    return (int)((x * 0x01010101u) >> 24);
}

/* ── Next power of 2 (for strand_count clamping) ── */
static int next_pow2_clamped(int n, int lo, int hi) {
    int p = lo;
    while (p < n && p < hi) p <<= 1;
    return (p > hi) ? hi : (p < lo ? lo : p);
}

/* ============================================================================
 * BASE-4 ENCODING
 * Omega node type → DNA base, deterministic bijection.
 * ============================================================================ */

static uint8_t omega_type_to_base(uint32_t type, uint64_t identity) {
    switch (type) {
    case OTYPE_CPU:
    case OTYPE_RUNTIME:   return BASE_A;   /* energetic / executing */
    case OTYPE_MEM:       return BASE_C;   /* structural */
    case OTYPE_IO:        return BASE_G;   /* bridging */
    case OTYPE_COMPILER:
    case OTYPE_STORAGE:   return BASE_T;   /* templating / stable */
    case OTYPE_PCI:
    case OTYPE_GPU:
        /* PCI: deterministic from identity bits — no arbitrary assignment */
        return (uint8_t)((identity ^ (identity >> 16)) & 0x3);
    default:              return BASE_C;   /* structural by default */
    }
}

/* ============================================================================
 * GENOME STATISTICS
 * Computed from the base-4 sequence of the Omega graph.
 * Exactly mirrors the DNA Engine V3 analysis — adapted for Omega graph input.
 * ============================================================================ */

static void compute_genome_stats(GenomeFabricStats *s,
                                  const uint8_t *seq, int n)
{
    if (n <= 0) return;

    /* Base counts */
    int cnt[4] = {0, 0, 0, 0};
    for (int i = 0; i < n; i++) cnt[seq[i] & 3]++;

    double total = (double)n;
    s->gc_content    = (cnt[BASE_G] + cnt[BASE_C]) / total;
    s->purine_ratio  = (cnt[BASE_A] + cnt[BASE_G]) / total;
    s->at_ratio      = (cnt[BASE_A] + cnt[BASE_T]) / total;

    /* Shannon entropy: -Σ p·log₂(p) */
    s->shannon_entropy = 0.0;
    for (int i = 0; i < 4; i++) {
        if (cnt[i] > 0) {
            double p = cnt[i] / total;
            s->shannon_entropy -= p * (log(p) / log(2.0));
        }
    }

    /* Transition matrix: freq of base[i]→base[i+1] */
    double tcnt[4][4] = {{0}};
    for (int i = 0; i < n - 1; i++) {
        int from = seq[i] & 3;
        int to   = seq[i+1] & 3;
        tcnt[from][to] += 1.0;
    }
    for (int r = 0; r < 4; r++) {
        double row = tcnt[r][0]+tcnt[r][1]+tcnt[r][2]+tcnt[r][3];
        for (int c = 0; c < 4; c++)
            s->transition[r][c] = (row > 0) ? tcnt[r][c]/row : 0.25;
    }

    /* 4-mer (k=4) counts: 256 possible */
    for (int i = 0; i < 256; i++) s->kmer_counts[i] = 0;
    if (n >= 4) {
        for (int i = 0; i <= n - 4; i++) {
            int idx = (seq[i]<<6)|(seq[i+1]<<4)|(seq[i+2]<<2)|seq[i+3];
            s->kmer_counts[idx & 0xFF]++;
        }
    }

    /* 3-mer (codon) counts: 64 possible */
    for (int i = 0; i < 64; i++) s->codon_counts[i] = 0;
    if (n >= 3) {
        for (int i = 0; i <= n - 3; i++) {
            int idx = (seq[i]<<4)|(seq[i+1]<<2)|seq[i+2];
            s->codon_counts[idx & 0x3F]++;
        }
    }

    /* Genome hash: djb2 over the base-4 sequence */
    uint64_t hash = 5381;
    for (int i = 0; i < n; i++)
        hash = ((hash << 5) + hash) + seq[i];
    s->genome_hash = hash;

    /* Unique 4-mer count → compression ratio */
    int unique = 0;
    for (int i = 0; i < 256; i++) if (s->kmer_counts[i] > 0) unique++;
    s->compression_ratio = unique / 256.0;

    /* Autocorrelation lags 1..32 (periodicity detection) */
    for (int lag = 1; lag <= 32; lag++) {
        int matches = 0;
        for (int i = 0; i < n - lag; i++)
            if (seq[i] == seq[i+lag]) matches++;
        s->autocorr[lag-1] = (n - lag > 0) ? (double)matches / (n - lag) : 0.5;
    }
}

/* ============================================================================
 * COLOR PALETTE — from codon frequencies (HSV, device-independent)
 * ============================================================================ */

static void derive_color_palette(GenomeFabricConfig *cfg,
                                  const GenomeFabricStats *s)
{
    /* Find max codon frequency for normalization */
    uint32_t max_freq = 1;
    for (int i = 0; i < 64; i++)
        if (s->codon_counts[i] > max_freq) max_freq = s->codon_counts[i];

    for (int i = 0; i < 64; i++) {
        /* Hue: codon index spread evenly around the color wheel */
        cfg->palette[i].h = (float)((i / 64.0) * 360.0);

        /* Saturation: rare codons are more saturated (they encode specificity) */
        double freq = (double)s->codon_counts[i] / max_freq;
        cfg->palette[i].s = (float)(0.5 + (1.0 - freq) * 0.5);

        /* Value: from GC content of this codon
         * codon[i] encodes 3 bases: bits 5:4 = base0, 3:2 = base1, 1:0 = base2 */
        int gc = 0;
        for (int j = 0; j < 3; j++) {
            int base = (i >> (j * 2)) & 0x3;
            if (base == BASE_G || base == BASE_C) gc++;
        }
        cfg->palette[i].v = (float)(0.6 + gc / 3.0 * 0.4);
    }
}

/* ============================================================================
 * GEOMETRY DIMENSIONS — from k-mer complexity
 * ============================================================================ */

static void derive_geometry_dims(GenomeFabricConfig *cfg,
                                  const GenomeFabricStats *s, int n)
{
    for (int i = 0; i < 256; i++) {
        uint32_t count = s->kmer_counts[i];
        /* dim = floor(log₂(count+1)) + 1, clamped to [1, DIMS] */
        int dim = 1;
        uint32_t v = count + 1;
        while (v > 1 && dim < DIMS) { v >>= 1; dim++; }
        cfg->geometry_dims[i] = dim;
    }
    (void)n;
}

/* ============================================================================
 * DERIVE ALL CONFIG FROM STATS + Dₙ AGGREGATE
 * No hardcoded values. Only mathematical relationships.
 * ============================================================================ */

static void derive_genome_config(GenomeFabricConfig *cfg,
                                  const GenomeFabricStats *s,
                                  uint32_t dn_aggregate,
                                  uint32_t phi_lattice_mean,
                                  int n_nodes)
{
    /* points_per_tick: from phi-lattice mean after consensus
     * Typical post-consensus mean ≈ 0x7FFF0000/2 → scaled to 100..600 */
    {
        double mean_norm = (double)(phi_lattice_mean & 0xFFFF) / 65535.0;
        cfg->points_per_tick = (int)(100.0 + mean_norm * 500.0);
    }

    /* strand_count: GC-rich graph → more strands; must be power of 2 in [8,256] */
    {
        int raw = (int)(n_nodes * s->gc_content + 8.0);
        cfg->strand_count = next_pow2_clamped(raw, 8, MAX_STRAND_COUNT);
    }

    /* core_radius: from Dₙ aggregate popcount × φ / 32
     * aggregate 0x80C0C0E8 → popcount=13 → radius = 13×φ/32 ≈ 0.657 */
    cfg->core_radius = (double)popcount32(dn_aggregate) * PHI / 32.0;

    /* strand_sep: AT-rich graph → wider strand separation, bounded by φ⁻¹ */
    cfg->strand_sep = s->at_ratio * PHI_INV;

    /* num_geometries: from k-mer compression ratio */
    cfg->num_geometries = (int)(s->compression_ratio * DIMS) + 1;
    if (cfg->num_geometries > DIMS) cfg->num_geometries = DIMS;

    /* zchg strand authority mask: Dₙ bits 0-7 (strands A-H) */
    cfg->strand_auth = (uint8_t)(dn_aggregate & 0xFF);

    /* Color palette and geometry dims */
    derive_color_palette(cfg, s);
    derive_geometry_dims(cfg, s, n_nodes);
}

/* ============================================================================
 * FABRIC INITIALIZATION
 * Encodes Omega graph, computes stats, derives config.
 * Called once after the Omega graph is complete (post-REALIZE phase).
 * ============================================================================ */

int hdgl_genome_fabric_init(GenomeFabric *fab,
                             const uint64_t *node_ids,
                             const uint32_t *node_types,
                             const uint32_t *node_states,
                             int n_nodes,
                             uint32_t dn_aggregate,
                             uint32_t phi_lattice_mean,
                             const double theta_locked[8],
                             int verbose)
{
    if (!fab || !node_ids || !node_types || n_nodes <= 0) return -1;
    if (n_nodes > MAX_NODES) n_nodes = MAX_NODES;

    /* 1. Encode Omega graph as base-4 sequence */
    for (int i = 0; i < n_nodes; i++)
        fab->base4_seq[i] = omega_type_to_base(node_types[i], node_ids[i]);
    fab->n_nodes = n_nodes;

    /* 2. Compute genome statistics */
    compute_genome_stats(&fab->stats, fab->base4_seq, n_nodes);

    /* 3. Derive all configuration from stats + Dₙ aggregate */
    derive_genome_config(&fab->config, &fab->stats,
                         dn_aggregate, phi_lattice_mean, n_nodes);

    /* 4. Store Kuramoto locked phases for camera path derivation */
    for (int i = 0; i < DIMS; i++)
        fab->theta_locked[i] = theta_locked[i];

    /* 5. Initialize cell array — one cell per active Omega node initially */
    fab->n_cells = 0;
    for (int i = 0; i < n_nodes && i < GENOME_MAX_CELLS; i++) {
        if (node_states[i] >= 2 /* CONFIGURED */) {
            FabricCell *cell = &fab->cells[fab->n_cells];
            cell->id     = i;
            cell->active = 1;
            cell->frame  = 0;

            /* Center offset from Kuramoto phase manifold */
            double theta = theta_locked[i % DIMS];
            double r     = fab->config.core_radius;
            cell->offset[0] = r * cos(theta);
            cell->offset[1] = r * sin(theta);
            cell->offset[2] = r * cos(theta * PHI);

            /* DNA properties from transition matrix rows */
            int b = fab->base4_seq[i];
            for (int j = 0; j < 4; j++)
                cell->dna_props[j]   = fab->stats.transition[b][j];
            for (int j = 0; j < 4; j++)
                cell->dna_props[4+j] = fab->stats.transition[(b+j)%4][b];

            fab->n_cells++;
        }
    }
    if (fab->n_cells == 0) {
        /* Fallback: at least one cell at origin */
        fab->cells[0].id = 0; fab->cells[0].active = 1; fab->cells[0].frame = 0;
        fab->cells[0].offset[0] = 0.0;
        fab->cells[0].offset[1] = 0.0;
        fab->cells[0].offset[2] = 0.0;
        fab->n_cells = 1;
    }

    /* 6. Gossip EMA: initialize with own values */
    fab->gossip_dn_ema = (double)dn_aggregate;
    fab->gossip_fingerprint = dn_aggregate ^ (uint32_t)fab->stats.genome_hash;

    if (verbose) {
        static const char *bnames[4] = {"A","C","G","T"};
        printf("[Genome] Omega sequence (%d nodes): ", n_nodes);
        for (int i = 0; i < n_nodes && i < 32; i++)
            printf("%s", bnames[fab->base4_seq[i]]);
        if (n_nodes > 32) printf("...");
        printf("\n");
        printf("[Genome] GC=%.3f  AT=%.3f  entropy=%.4f  compress=%.3f\n",
               fab->stats.gc_content, fab->stats.at_ratio,
               fab->stats.shannon_entropy, fab->stats.compression_ratio);
        printf("[Genome] points/tick=%d  strands=%d  radius=%.4f  sep=%.4f\n",
               fab->config.points_per_tick, fab->config.strand_count,
               fab->config.core_radius, fab->config.strand_sep);
        printf("[Genome] geometries=%d  strand_auth=0x%02X  cells=%d\n",
               fab->config.num_geometries, fab->config.strand_auth, fab->n_cells);
    }

    return 0;
}

/* ============================================================================
 * FABRIC TICK — emit one frame of fabric points
 *
 * Advances the golden-angle spiral. Called once per phi_tick.
 * All derived from genome stats — no hardcoded constants.
 *
 * Spiral equations (natural units — no pixels):
 *   progress  = frame / n_nodes           (fraction of genome traversed)
 *   r         = core_radius × (1 − progress^φ⁻¹)   (inward, phi-curved)
 *   θ         = GOLDEN_ANGLE_RAD × frame × (1 + autocorr[frame%32] × 0.1)
 *   z         = sin(frame × φ⁻¹) × φ + (frame/PPT) × 8
 *   x         = r × cos(θ + dim × GOLDEN_ANGLE_RAD) + offset.x
 *   y         = r × sin(θ + dim × GOLDEN_ANGLE_RAD) + offset.y
 *   (strand2) x = r × cos(θ − dim × GOLDEN_ANGLE_RAD) + sep + offset.x
 * ============================================================================ */

int hdgl_genome_fabric_tick(GenomeFabric *fab,
                              int cell_id,
                              int frame_num,
                              FabricPoint *strand1_out,
                              FabricPoint *strand2_out,
                              int max_out)
{
    if (cell_id < 0 || cell_id >= fab->n_cells) return -1;
    if (!fab->cells[cell_id].active) return -1;

    FabricCell *cell = &fab->cells[cell_id];
    const GenomeFabricConfig *cfg  = &fab->config;
    const GenomeFabricStats  *s    = &fab->stats;
    int N = cfg->points_per_tick;
    if (N > max_out) N = max_out;

    for (int i = 0; i < N; i++) {
        double tt  = (double)(frame_num * N + i);
        int    idx = (int)tt % fab->n_nodes;   /* position in Omega sequence */

        uint8_t base  = fab->base4_seq[idx];
        /* 4-mer index: 4 consecutive bases */
        int idx4      = idx;
        if (idx4 + 3 >= fab->n_nodes) idx4 = fab->n_nodes - 4;
        if (idx4 < 0) idx4 = 0;
        uint8_t kmer  = (uint8_t)((fab->base4_seq[idx4]  <<6)|
                                   (fab->base4_seq[idx4+1]<<4)|
                                   (fab->base4_seq[idx4+2]<<2)|
                                    fab->base4_seq[idx4+3]);
        /* Codon index: 3 consecutive bases */
        int idx3      = (idx >= 2) ? idx - 2 : 0;
        uint8_t codon = (uint8_t)((fab->base4_seq[idx3]  <<4)|
                                   (fab->base4_seq[idx3+1]<<2)|
                                    fab->base4_seq[idx3+2]) & 0x3F;

        int dim = cfg->geometry_dims[kmer];   /* 1..8, genome-derived */

        /* Spiral geometry — ALL from genome */
        double progress = tt / (double)fab->n_nodes;
        double r = cfg->core_radius * (1.0 - pow(progress, PHI_INV));
        if (r < PHI_INV * 0.1) r = PHI_INV * 0.1;   /* GOI/GUZ principle: floor */

        /* Theta: golden angle × autocorrelation modulation (periodicity signal) */
        int ac_idx = (int)tt % 32;
        double autocorr_mod = s->autocorr[ac_idx];
        double theta = GOLDEN_ANGLE_RAD * tt * (1.0 + autocorr_mod * 0.1);

        /* Z: helical with transition-driven pitch */
        double aa_trans = s->transition[BASE_A][BASE_A];
        double z_pitch  = 4.0 * (1.0 + aa_trans);
        double z = sin(tt * PHI_INV) * PHI + (tt / (double)N) * z_pitch;

        /* Strand 1: primary (+dim × golden_angle) */
        double a1 = (double)dim * GOLDEN_ANGLE_RAD;
        strand1_out[i].x = (float)(r * cos(theta + a1) + cell->offset[0]);
        strand1_out[i].y = (float)(r * sin(theta + a1) + cell->offset[1]);
        strand1_out[i].z = (float)(z + cell->offset[2]);

        strand1_out[i].color_h = cfg->palette[codon].h;
        strand1_out[i].color_s = cfg->palette[codon].s;
        strand1_out[i].color_v = cfg->palette[codon].v;
        strand1_out[i].dimension = dim;
        strand1_out[i].base      = base;
        strand1_out[i].kmer_index = kmer;
        strand1_out[i].codon_index = codon;
        strand1_out[i].omega_id  = (uint64_t)idx;

        /* Genome transition probabilities for this base → 4 property floats */
        for (int j = 0; j < 4; j++)
            strand1_out[i].genome_prop[j] = (float)s->transition[base][j];

        /* Strand 2: counter-rotating (−dim × golden_angle + strand_sep offset) */
        double a2 = -(double)dim * GOLDEN_ANGLE_RAD;
        double sep = cfg->strand_sep;
        strand2_out[i]   = strand1_out[i];
        strand2_out[i].x = (float)(r * cos(theta + a2) + sep + cell->offset[0]);
        strand2_out[i].y = (float)(r * sin(theta + a2) - sep + cell->offset[1]);
    }

    cell->frame++;

    /* Cell division check: Dₙ-inspired threshold
     * When frame delta causes progress to cross a Fₙ boundary → divide */
    if (fab->n_cells < GENOME_MAX_CELLS) {
        double progress = (double)(cell->frame * N) / (double)fab->n_nodes;
        int fib_idx = (int)(progress * 8.0) % 8;
        static const double FIB_NORM[8] = {1,1,2,3,5,8,13,21};
        double fib_threshold = FIB_NORM[fib_idx] / 21.0;  /* 0..1 */
        double tick_delta = fabs(progress - fib_threshold);
        if (tick_delta < (PHI_INV / (double)N)) {
            /* Divide: new cell at golden-angle offset from parent */
            FabricCell *child = &fab->cells[fab->n_cells];
            *child = *cell;
            child->id = fab->n_cells;
            child->frame = 0;
            double theta_div = GOLDEN_ANGLE_RAD * fab->n_cells;
            child->offset[0] = cell->offset[0] + cfg->core_radius * cos(theta_div) * PHI_INV;
            child->offset[1] = cell->offset[1] + cfg->core_radius * sin(theta_div) * PHI_INV;
            child->offset[2] = cell->offset[2] * PHI_INV;
            /* Dampen child dna_props by φ⁻¹ */
            for (int j = 0; j < 8; j++)
                child->dna_props[j] *= PHI_INV;
            fab->n_cells++;
        }
    }

    return N;
}

/* ============================================================================
 * HSV → RGB CONVERSION (device-independent intermediate)
 * Output range [0.0, 1.0] per channel.
 * ============================================================================ */

void hdgl_genome_hsv_to_rgb(float h, float s, float v,
                              float *r, float *g, float *b)
{
    float c  = v * s;
    float hh = h / 60.0f;
    float x  = c * (1.0f - fabsf(fmodf(hh, 2.0f) - 1.0f));
    float m  = v - c;

    float r1 = 0, g1 = 0, b1 = 0;
    int   hi = (int)hh % 6;
    switch (hi) {
    case 0: r1=c; g1=x; b1=0; break;
    case 1: r1=x; g1=c; b1=0; break;
    case 2: r1=0; g1=c; b1=x; break;
    case 3: r1=0; g1=x; b1=c; break;
    case 4: r1=x; g1=0; b1=c; break;
    default:r1=c; g1=0; b1=x; break;
    }
    *r = r1 + m;
    *g = g1 + m;
    *b = b1 + m;
}

/* ============================================================================
 * GOSSIP INTEGRATION
 * Called when a remote gossip frame arrives carrying peer's dn_aggregate.
 * EMA-folds peer genome data into local strand weights (α=0.3, matches zchg).
 * ============================================================================ */

void hdgl_genome_fabric_gossip_update(GenomeFabric *fab,
                                       uint32_t peer_dn_aggregate,
                                       uint32_t peer_fingerprint,
                                       int verbose)
{
    /* EMA fold: consensus_dn = α × peer + (1-α) × local */
    fab->gossip_dn_ema = EMA_ALPHA * peer_dn_aggregate
                        + (1.0 - EMA_ALPHA) * fab->gossip_dn_ema;

    /* Hamming distance check: < 4 bits difference → genome-converged cluster */
    uint32_t consensus = (uint32_t)(fab->gossip_dn_ema + 0.5);
    uint32_t local_agg = (uint32_t)(fab->config.core_radius * 32.0 / PHI + 0.5);
    int hamming = popcount32(consensus ^ local_agg);

    if (verbose) {
        printf("[Genome-Gossip] peer_dn=0x%08X  ema=0x%08X  hamming=%d  %s\n",
               peer_dn_aggregate, consensus, hamming,
               (hamming < 4) ? "GENOME-LOCK" : "converging");
    }

    /* Update strand authority: bits where EMA > √φ × 65535 */
    /* Each bit of gossip_dn_ema's rounded value → strand authority */
    fab->config.strand_auth = (uint8_t)(consensus & 0xFF);

    fab->gossip_fingerprint ^= peer_fingerprint;
    (void)peer_fingerprint;
}

/* ============================================================================
 * QUERY INTERFACE
 * Minimal surface — fabric is self-describing via genome stats.
 * ============================================================================ */

int      hdgl_genome_points_per_tick(const GenomeFabric *f) { return f->config.points_per_tick; }
int      hdgl_genome_strand_count(const GenomeFabric *f)    { return f->config.strand_count; }
double   hdgl_genome_core_radius(const GenomeFabric *f)     { return f->config.core_radius; }
uint8_t  hdgl_genome_strand_auth(const GenomeFabric *f)     { return f->config.strand_auth; }
int      hdgl_genome_n_cells(const GenomeFabric *f)         { return f->n_cells; }
uint32_t hdgl_genome_fingerprint(const GenomeFabric *f)     { return f->gossip_fingerprint; }
