/*
 * hdgl_genome_fabric.h — Fabric genome engine for HDGL
 *
 * Public API. Include this; link hdgl_genome_fabric.c.
 * hdgl_analog_engine.c is pulled in by hdgl_genome_fabric.c.
 */

#ifndef HDGL_GENOME_FABRIC_H
#define HDGL_GENOME_FABRIC_H

#include <stdint.h>
#include <stddef.h>

/* Maximum nodes in Omega graph (router64 typical: 8–32, max observed: 64) */
#define GENOME_MAX_NODES  256
/* Maximum fabric cells (derived from GC content × max_nodes) */
#define GENOME_MAX_CELLS  220
/* Palette size (3-mer = 64 codons) */
#define GENOME_PALETTE_SZ 64
/* Geometry table size (4-mer = 256 k-mers) */
#define GENOME_GDIM_SZ    256

/* ── HSV color entry (device-independent) ── */
typedef struct {
    float h, s, v;
} GenomeHSV;

/* ── Genome statistics derived from Omega base-4 sequence ── */
typedef struct {
    double gc_content;
    double purine_ratio;
    double at_ratio;
    double shannon_entropy;
    double compression_ratio;
    double transition[4][4];    /* base[i]→base[i+1] frequency */
    uint32_t kmer_counts[256];  /* 4-mer frequencies */
    uint32_t codon_counts[64];  /* 3-mer frequencies */
    double autocorr[32];        /* lag-1..32 autocorrelation */
    uint64_t genome_hash;       /* djb2 hash of base-4 sequence */
} GenomeFabricStats;

/* ── All derived configuration — zero hardcoded constants ── */
typedef struct {
    int      points_per_tick;           /* from phi_lattice_mean */
    int      strand_count;              /* from GC × n_nodes (power of 2) */
    double   core_radius;               /* from Dₙ aggregate popcount × φ/32 */
    double   strand_sep;                /* from AT ratio × φ⁻¹ */
    int      num_geometries;            /* from compression_ratio × DIMS */
    uint8_t  strand_auth;               /* bits 0-7: zchg strand A-H authority */
    GenomeHSV palette[GENOME_PALETTE_SZ];  /* HSV from codon frequencies */
    int      geometry_dims[GENOME_GDIM_SZ]; /* 1..8 from k-mer counts */
} GenomeFabricConfig;

/* ── A single fabric cell (maps to one Omega sub-graph / peer node) ── */
typedef struct {
    int    id;
    int    frame;
    int    active;
    double offset[3];       /* center position from Kuramoto θᵢ */
    double dna_props[8];    /* transition matrix properties */
} FabricCell;

/* ── A single emitted fabric point (one per strand per tick step) ── */
typedef struct {
    float    x, y, z;
    float    color_h, color_s, color_v;   /* HSV */
    int      dimension;                   /* 1..8, from k-mer */
    uint8_t  base;                        /* 0=A 1=C 2=G 3=T */
    uint8_t  kmer_index;                  /* 4-mer index */
    uint8_t  codon_index;                 /* 3-mer index */
    uint8_t  _pad;
    uint64_t omega_id;                    /* source Omega node identity */
    float    genome_prop[4];              /* transition[base][0..3] */
} FabricPoint;

/* ── The complete fabric state for one node ── */
typedef struct {
    /* Omega graph encoding */
    uint8_t          base4_seq[GENOME_MAX_NODES];
    int              n_nodes;

    /* Derived data */
    GenomeFabricStats  stats;
    GenomeFabricConfig config;

    /* Kuramoto locked phases (camera path) */
    double theta_locked[8];

    /* Cell array */
    FabricCell cells[GENOME_MAX_CELLS];
    int        n_cells;

    /* Gossip EMA state */
    double   gossip_dn_ema;
    uint32_t gossip_fingerprint;
} GenomeFabric;

/* ── API ── */

/*
 * hdgl_genome_fabric_init — initialize from Omega graph
 *
 * node_ids[]     : Omega identity fields (n_nodes entries)
 * node_types[]   : Omega type fields
 * node_states[]  : Omega state fields (INIT=0 .. EXECUTED=4)
 * n_nodes        : number of nodes
 * dn_aggregate   : 32-bit Dₙ aggregate from hdgl_analog_engine
 * phi_lattice_mean: mean of phi-lattice slots after consensus
 * theta_locked[] : Kuramoto locked phases (8 values)
 * verbose        : print derived params if non-zero
 *
 * Returns 0 on success, -1 on bad arguments.
 */
int hdgl_genome_fabric_init(GenomeFabric *fab,
                             const uint64_t *node_ids,
                             const uint32_t *node_types,
                             const uint32_t *node_states,
                             int n_nodes,
                             uint32_t dn_aggregate,
                             uint32_t phi_lattice_mean,
                             const double theta_locked[8],
                             int verbose);

/*
 * hdgl_genome_fabric_tick — emit one frame of fabric points
 *
 * cell_id     : index into fab->cells[]
 * frame_num   : current tick number (monotonically increasing)
 * strand1_out : primary strand output (max_out entries)
 * strand2_out : counter strand output (max_out entries)
 * max_out     : capacity of strand1_out / strand2_out arrays
 *
 * Returns number of points written (= config.points_per_tick, ≤ max_out),
 *         or -1 if cell_id is invalid/inactive.
 */
int hdgl_genome_fabric_tick(GenomeFabric *fab,
                              int cell_id,
                              int frame_num,
                              FabricPoint *strand1_out,
                              FabricPoint *strand2_out,
                              int max_out);

/*
 * hdgl_genome_fabric_gossip_update — integrate remote peer's genome data
 *
 * peer_dn_aggregate : the peer's Dₙ 32-bit aggregate (from gossip frame)
 * peer_fingerprint  : the peer's genome_fingerprint
 */
void hdgl_genome_fabric_gossip_update(GenomeFabric *fab,
                                       uint32_t peer_dn_aggregate,
                                       uint32_t peer_fingerprint,
                                       int verbose);

/* HSV to RGB conversion (device-independent) */
void hdgl_genome_hsv_to_rgb(float h, float s, float v,
                              float *r, float *g, float *b);

/* Accessors */
int      hdgl_genome_points_per_tick(const GenomeFabric *f);
int      hdgl_genome_strand_count(const GenomeFabric *f);
double   hdgl_genome_core_radius(const GenomeFabric *f);
uint8_t  hdgl_genome_strand_auth(const GenomeFabric *f);
int      hdgl_genome_n_cells(const GenomeFabric *f);
uint32_t hdgl_genome_fingerprint(const GenomeFabric *f);

#endif /* HDGL_GENOME_FABRIC_H */
