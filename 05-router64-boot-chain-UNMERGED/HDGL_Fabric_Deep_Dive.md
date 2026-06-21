# HDGL Analog Fabric — Deep Technical Dive
## Expert-Level Architecture & Implementation Details

---

## PHILOSOPHICAL FOUNDATION

**AXIOM**: Ωₙ₊₁ = T(Ωₙ) — The system bootstraps itself through recursive definition.

**GOVERNANCE BY DESIGN** (GOI):
- **Saturate, Don't Halt**: Errors return gracefully; system continues operating
- **Idempotent Operations**: Repeated calls produce consistent results
- **Phi-Fold as Primitive**: All addresses/identities derive from phi-fold function

---

## PHI-FOLD MATH ENGINE

### Core Algorithm
```
fold(x, key, seq) = (x·PHI32 + key·FIB32 + seq·SQRT_PHI32) mod 2³²
```

**Properties**:
- **Bijection**: Every (x, key, seq) triple maps uniquely
- **Full Avalanche**: Δkey=1 → Δfold≈2³¹ (FIB32≈0x9E3779B1)
- **Irrational Stride**: Δseq=1 → Δfold≈√φ·2³² (SQRT_PHI32≈0xA17F0BCB)

### Carrier Constants (Mathematical Facts)
| Symbol | Hex | Derivation |
|--------|-----|------------|
| PHI32 | 0x9E3779B9 | floor(2³²/φ) |
| FIB32 | 0x9E3779B1 | Nearest prime to PHI32 |
| SQRT_PHI32 | 0xA17F0BCB | floor(√φ × 2³²) |
| PHI32_INV | 0x144CBC89 | Modular inverse: PHI32 × INV ≡ 1 (mod 2³²) |

---

## COVERT CHANNELS (Three-Carrier System)

### Carrier CH-0: Reserved Field (4 bytes/frame)
**Capacity**: 800 KB/sec at 200K frames/sec
- Phi-fold of (payload_hash XOR data32) written to reserved uint32
- In every `zchg_frame_header_t`
- **Undetectable**: Appears as normal framing overhead

### Carrier CH-1: Gossip Strand LSBs (2 bytes/cycle)
**Capacity**: 16 bits per gossip message
- Bottom 2 bits of each 8 strand_weight bytes = 16 bits
- EMA rounds to integer → sub-2-bit modulation below noise floor
- Embedded in normal gossip protocol

### Carrier CH-2: HTTP Path Encoding (1 bit/request)
**Protocol**: GET /serve/zc/<seg0>/<seg1>
- seg0: framing word (identical for both bit values)
- seg1: encodes payload bit (two phi-derived values per bit)
- All paths return 404 → receiver decodes from path alone
- **Cover Story**: Cache warming / CDN miss traffic

---

## GENOME FABRIC ENGINE

### Omega Graph Derivation
All config derived from Omega graph via three primitives:

1. **Dₙ(r)** — Continuous phi-lattice signal
2. **Kuramoto θᵢ** — Locked phases (camera path synchronization)
3. **Base-4 Codec** — Sequence statistics → DNA sequence

### Memory Layout (Identity-Mapped)
```
0x101000-0x1010FF: Phi-lattice (128 × 4B slots + flags)
0x101100          GenomeFabricConfig (16B)
  ├─ points_per_tick
  ├─ strand_count
  ├─ strand_auth
  └─ n_cells

0x101110          phi_lattice_mean (4B)
0x101114          active_node_count (4B)
0x101200          gossip_dn_ema (8B)
0x101208          gossip_fingerprint (4B) ← LIVE, updates each cycle
0x1013FC          phi-lattice slot 127 = genome_fp cache (4B) ← STABLE, set at boot

0x200000          OMEGA_BASE (OMEGA_MAX_NODES × 128B)
```

### Config Derivation Rules
```asm
points_per_tick   = 100 + (phi_lattice_mean & 0xFFFF) * 500 / 65535
strand_count      = nearest_pow2(gc_content * n_nodes, 8, 256)
core_radius       = popcount(dn_aggregate) * φ / 32
strand_sep        = at_ratio * φ_inv
genome_fp         = FNV_PHI_SPIRAL(base4_seq, dn_aggregate)
```

**EMA Alpha**: φ/(φ+1) ≈ 0.618 — natural golden ratio weighting

---

## NIC DRIVER: COMPLETE IMPLEMENTATION

### Supported Hardware
**Intel e1000 Family**: 0x8086:100E, 0x8086:10D3, 0x8086:1539, 0x8086:1521, 0x8086:1533, 0x8086:1091, 0x8086:1368, 0x8086:15F3, 0x8086:153A (i217-V), 0x8086:1559

**Realtek**: RTL8111 (0x8168), RTL8168/8169 (0x8169/0x10C3)

**Atheros**: AR8131 (0x1066, via Realtek compat)

### Memory Map
```
0x105000  NIC_STATE_BASE   512 bytes (state)
0x106000  TX_RING          256 bytes (16 descriptors × 16B)
0x107000  RX_RING          256 bytes (16 descriptors × 16B)
0x108000  TX_BUFFERS       32KB (16 × 2KB)
0x110000  RX_BUFFERS       32KB (16 × 2KB)
```

### Critical H81-BTC PCH FIXES

**Fix 1: BAR64 Detection (i217-V)**
- i217-V (8086:153A) is PCH-integrated, uses 64-bit BAR0
- Bits 2:1 of BAR0 = 10b → 64-bit BAR
- **Old code**: `movsx rax, eax` (sign-extends 32-bit, zeros upper word = WRONG)
- **Fixed code**: Read BAR0 + BAR1, combine into 64-bit MMIO base

**Fix 2: RCTL Register (82579/i217)**
- BSIZE bit interpretation differs from older e1000
- BSIZE=00b = 2KB on i210/i350, but 16KB on 82579/i217
- Need BSIZE=01b for 2KB buffers
- **Old value**: 0x8002
- **Fixed value**: 0x8802 (adds BAM bit | BSIZE=01b | SECRC)

### e1000 Initialization Sequence
```asm
E1000_CTRL_RST   = 0x04000000 (bit 26)
E1000_CTRL_SLU   = 0x00000040 (bit 6, Set Link Up)
E1000_RCTL       = 0x04008802 (EN | SBP | BAM | BSIZE_2KB | SECRC)
E1000_TCTL       = 0x4010A (EN | PSP | CT=0x10 | COLD=0x40)
```

### RTL Version Detection
```asm
Read TXCFG register at IOBASE+0x40
Version = bits [27:16] (12 bits)
- RTL8169: version = 0x000 → legacy ring mode, 4-byte header
- RTL8111/8168: version ≠ 0x000 → descriptor mode, 0-byte header
```

---

## PEER DISCOVERY: THREE-PHASE PROTOCOL

### Phase 1: Phi-Seed Multicast
```
mcast_ip = 0xEF000000 | (phi_fold(genome_fp, 0, 0) & 0x00FFFFFF)
port     = 8090
```
- Identical genome_fp → same mcast_ip → automatic discovery
- Works on any L2 segment, IGMP not required (link-local)
- QEMU: `-netdev socket,mcast=<addr>`
- H81-BTC: native L2 broadcast

### Phase 2: ARP Probe (Fallback)
- Scan 10.0.0.1 through 10.0.0.254
- Send ARP REQUEST frames
- Collect replies → populate peer table

### Phase 3: Sector-68 Static Peers
- Disk sector 68: 8 × 16-byte entries
- Build-time generated from LN_SEED_PEERS environment variable
- Format: `{ uint32 ip_be, uint16 port_le, uint8[10] pad }`

### Peer Table Layout (0x119800)
```
16 entries × 16 bytes each:
  [0:3]   uint32  IP address (LE)
  [4:5]   uint16  port (LE)
  [6:15]  uint8[10] padding
```

---

## BARE-METAL STORE: IMPLEMENTATION

### Sector Layout
```
Sector 69: Store Header (52 bytes)
  ├─ Magic: 0x4E525453 ('NRTS' LE = 'STRN')
  ├─ strand_count: 4 bytes (default 8)
  └─ disk_cursor[256]: 2048 bytes (8 bytes per strand)

Sectors 70+: Strand Data
  64 sectors per strand × 512 bytes = 32KB per strand
  Total: 8 strands × 32KB = 256KB minimum
```

### Memory Layout
```
0x400000  ARENA_BASE              4MB bump-allocated arena
0x420000  ARENA_BUMP              Bump pointer location

0x11F000  STORE_CURSOR_BASE       2KB (256 × 8B) — disk cursors
0x11F800  STORE_INDEX_BASE        512B (128 × 4B) — RAM hash index
0x120000  STRAND_CACHE_BASE       128KB (256 × 512B) — sector write-back cache
0x140100  DIRTY_FLAGS_BASE        256B — per-strand dirty flags
```

### Phi-Addr Indexing
```
strand = phi_addr & (strand_count - 1)
  e.g., for 8 strands: strand = phi_addr & 7

slot_index = phi_fold(phi_addr & 0xFFFFFFFF, genome_fp, 0) mod 128
  → maps to STORE_INDEX_BASE + slot × 4
```

### Store Operation: PUT
1. Build 52-byte frame header in scratch buffer
2. Write header + payload to appropriate strand (via write-back cache)
3. Update RAM index at computed slot
4. Async flush on sector boundary

### Store Operation: GET
1. Look up phi_addr in RAM index
2. If found: lazy-load from write-back cache or disk
3. Copy to caller buffer

---

## BASE-4 CODEC: DNA-INSPIRED ENCODING

### Type → DNA Mapping
| Type | DNA Base | Category | OTYPE |
|------|----------|----------|-------|
| CPU/RUNTIME | A | Purine, high-energy | 1, 5 |
| MEM | C | Pyrimidine, structural | 2 |
| IO | G | Purine, bridging | 3 |
| COMPILER/STORAGE | T | Pyrimidine, templating | 4, 10 |
| DEFAULT | C | Structural | - |
| PCI child | XOR_MOD4 | Dynamic | 8 |

**Formula**: `(vendor_id XOR device_id) mod 4`

### Codon Structure
```
Codon = 3 consecutive Omega nodes
index = (b0 << 4 | b1 << 2 | b2) → 12 combinations

K-mer = 4 consecutive nodes
index = (b0 << 6 | b1 << 4 | b2 << 2 | b3) → 256 possibilities
```

---

## SHELL COMMANDS (Fabric Extensions)

### nic2
```
Router64> nic2
fabric NIC state:
  type: e1000
  MAC: 00:11:22:33:44:55
  TX head: 15
  RX head: 3
```

### store
```
Router64> store
fabric store:
  arena: 102400 / 4194304
  strand[0] cursor: 0 bytes
  strand[1] cursor: 122880 bytes
  ...
```

### peers
```
Router64> peers
fabric peers:
  10.0.0.1:8090
  10.0.0.2:8090
```

### send
```
Router64> send
sent
```
- Sends HEALTH frame (52 bytes) to first peer

### load
```
Router64> load <phi_addr_hex>
load: phi_addr=0x9E3779B9
```
- Look up phi_addr in store index

---

## BOOTSTRAP SEQUENCE

### Phase Order
1. **DNA → GRAPH**: Omega graph initialized from base-4 codec
2. **NIC_INIT**: NIC detection, initialization, timing
3. **PEER_INIT**: Three-phase discovery, peer table populated
4. **STORE_INIT**: Arena zeroed, cursors restored from sector 69
5. **FABRIC_READY**: 
   - Store genome_fp in lattice slot 127 (0x1013FC)
   - Set FABRIC_READY_BIT (bit 5) in flags (0x101014)
   - Extend mailbox with fabric fields

### Mailbox Extension (0x50000 + 104)
```
+104  uint64  genome_fp       (from lattice slot 127)
+112  uint32  peer_count      (from 0x119880)
+120  uint32  fabric_flags    (bit 5 of 0x101014)
+124  uint64  reserved        (padding)
```

---

## QEMU SMOKE TEST

### Test Targets
1. **[Omega] RUNTIME**: Omega_n+1=T(Omega_n) complete
2. **[Analog] Dn(r) lattice**: Lattice initialization
3. **Kuramoto aphase: LOCK**: Phase synchronization
4. **phi-lattice consensus state**: Distributed consensus
5. **Router64>**: Shell prompt
6. **[Fabric] ready**: Fabric subsystem (if NIC found)
7. **genome_fp**: Genome fingerprint present

### Execution
```bash
qemu-system-x86_64 \
  -drive "file=hdgl_router64.img,format=raw,if=ide" \
  -boot order=c \
  -m 64M \
  -serial "file:/tmp/hdgl_smoke.log" \
  -netdev "user,id=n0" \
  -device "e1000,netdev=n0" \
  -no-reboot \
  -display none
```

---

## KEY INVARIANTS

1. **Address Invariant**: 
   - Peer discovery reads live gossip from 0x101208
   - Store index reads stable cache from 0x1013FC
   
2. **Phi-fold Bijection**: Every (x, key, seq) maps uniquely → no collisions

3. **BAR64 Detection**: Bits 2:1 of BAR0 determine 32-bit vs 64-bit BAR

4. **RCTL BSIZE Interpretation**: Varies by e1000 family (00b=2KB on i210, 16KB on i217)

5. **EMA Weighting**: φ/(φ+1) ≈ 0.618 — natural golden ratio decay

6. **Store Idempotency**: PUT operations are safe to repeat

---

## IMPLEMENTATION NOTES

### NASM Constraints
- **No C, No Python**: Entire fabric expressed in NASM x86-64 assembly
- **Identity-Mapped Memory**: All regions directly addressable from RAM
- **Phi-Tick as Clock**: 0x101010 advances every shell iteration

### Error Handling (GOI Compliance)
- Return 0 on success, negative errno on failure
- Arena full → return 0 (saturate, don't halt)
- NIC timeout → continue anyway (don't block boot)

### Self-Reference
- `hdgl_complete.hdgl` embedded in disk image
- System can read and modify itself at runtime
- Phi-fold ensures safe self-modification

---

## TESTING STRATEGIES

### QEMU Targets
1. **e1000**: Default test (covers Intel path)
2. **rtl8139**: Extended test (covers Realtek path)
3. **metal**: Serial capture on H81-BTC PCH (full bare-metal)

### Metal Targets
- **H81-BTC-Pro**: Gigabyte, Haswell PCH, Intel i217-V (8086:153A)
- e1000 with BAR64, RCTL fixes
- Full identity-mapped space access

---

## SECURITY CONSIDERATIONS

### Covert Channels
- **CH-0**: Reserved field (no bandwidth impact)
- **CH-1**: LSB modulation (below noise floor)
- **CH-2**: HTTP path encoding (all paths 404)

### Phi-Verification
- All data phi-verified (not checksummed)
- No traditional integrity verification
- Relies on phi-fold bijection for detection

---

## FUTURE EXTENSIONS

1. **Divergence Detection**: Compare twin-flame evals across ports
2. **Model Drift**: Store baseline answers, detect hash mismatches
3. **Carrier Expansion**: Additional covert channel vectors
4. **Store Scaling**: Extend beyond 128 slots as needed
5. **Cross-NIC Discovery**: Direct cross-NIC phi-seed (same physical network)
