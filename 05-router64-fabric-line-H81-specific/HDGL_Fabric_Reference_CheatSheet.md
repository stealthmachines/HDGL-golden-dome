# HDGL Fabric — Expert Reference CheatSheet
## Quick lookup for critical values, addresses, and procedures

---

## QUICK CONSTANTS

| Symbol | Value | Meaning |
|--------|-------|---------|
| φ (PHI32) | 0x9E3779B9 | Knuth multiplicative hash |
| FIB32 | 0x9E3779B1 | Nearest prime to PHI32 |
| SQRT_PHI32 | 0xA17F0BCB | floor(√φ × 2³²) |
| PHI32_INV | 0x144CBC89 | Modular inverse |
| GOLDEN_ANGLE_RAD | 2.399963229728653 | 2π/φ² |
| GOLDEN_ANGLE_DEG | 137.5077640500378 | Degrees |

### EMA Alpha
```
φ / (φ + 1) ≈ 0.618
```

---

## CRITICAL MEMORY ADDRESSES

### Phi-Lattice (0x101000 range)
```
0x101010    phi_tick counter (advances each shell iteration)
0x101014    phi-lattice flags (bit 5 = FABRIC_READY)
0x101020-0x10102C    phi-lattice slots 0-3 (mean computation)
0x101020+127*4=0x1013FC    genome_fp cache (STABLE, boot-only)
0x101208    gossip_fingerprint (LIVE, updates each cycle)
0x101200    gossip_dn_ema (8 bytes)
0x101100-0x10110C    GenomeFabricConfig (16 bytes)
0x101110    phi_lattice_mean
0x101114    active_node_count
0x10100C    dn_aggregate

0x200000    OMEGA_BASE (OMEGA_MAX_NODES × 128B)
OMEGA_MAX_NODES = 64
```

### NIC State (0x105000 range)
```
0x105000    NIC_STATE_BASE (512 bytes)
  +0x00     NIC_OFF_TYPE    4B (0=e1000, 1=RTL8111, 2=RTL8169)
  +0x08     NIC_OFF_MMIO    8B (MMIO base, 64-bit)
  +0x10     NIC_OFF_IOBASE  4B (I/O port base, RTL only)
  +0x18     NIC_OFF_MACLO   4B (MAC bytes 0-3)
  +0x1C     NIC_OFF_MACHI   2B (MAC bytes 4-5)
  +0x20     NIC_OFF_TXHEAD  4B (TX ring head, e1000)
  +0x24     NIC_OFF_RX_HEAD 4B (RX ring head)
  +0x28     NIC_OFF_RTL_HDR 1B (RTL header offset: 0 or 4)
  +0x20     NIC_OFF_LOCAL_IP 4B (local IP, filled by discovery)

0x106000    TX_RING (16 descriptors × 16B = 256B)
0x107000    RX_RING (16 descriptors × 16B = 256B)
0x108000    TX_BUFFERS (16 × 2KB = 32KB)
0x110000    RX_BUFFERS (16 × 2KB = 32KB)

e1000 Registers (relative to MMIO):
  E1000_CTRL      0x0000
  E1000_STATUS    0x0008
  E1000_EERD      0x0014
  E1000_RCTL      0x0100
  E1000_TCTL      0x0400
  E1000_TDBAL     0x3800
  E1000_TDBAH     0x3804
  E1000_TDLEN     0x3808
  E1000_TDH       0x3810
  E1000_TDT       0x3818
  E1000_RDBAL     0x2800
  E1000_RDBAH     0x2804
  E1000_RDLEN     0x2808
  E1000_RDH       0x2810
  E1000_RDT       0x2818
  E1000_RAL       0x5400
  E1000_RAH       0x5404
```

### Peer Discovery (0x119800 range)
```
0x119800    PEER_TABLE_BASE (16 × 16B entries)
  Entry: [uint32 IP][uint16 port][uint8[10] pad]

0x119880    peer_count (4 bytes)
0x11A000    HEALTH frame buffer (used during discovery)
0x11B000    RX buffer (peer discovery polling)
```

### Store (0x11F000 range)
```
0x11F000    STORE_CURSOR_BASE (256 × 8B = 2KB)
0x11F800    STORE_INDEX_BASE (128 × 4B = 512B)
0x120000    STRAND_CACHE_BASE (256 × 512B = 128KB)
0x140100    DIRTY_FLAGS_BASE (256 × 1B)

Disk:
  Sector 68: peer_seed (512B, static peers)
  Sector 69: store_header (52B)
  Sectors 70+: strand data (64 sectors/strand × 512B)
```

### Arena
```
0x400000    ARENA_BASE (4MB)
0x420000    ARENA_BUMP (bump pointer)
```

---

## NIC TYPE DETECTION

### Intel e1000 Family
Vendors/Devices:
- 0x8086:100E (82571 Gigabit)
- 0x8086:10D3 (82574 Gigabit)
- 0x8086:1539 (82575 Gigabit)
- 0x8086:1521 (82573 Gigabit)
- 0x8086:1533 (82574B Gigabit)
- 0x8086:1091 (82574L Gigabit)
- 0x8086:1368 (82576 Gigabit)
- 0x8086:15F3 (82579 Gigabit - i218)
- 0x8086:153A (82579 Gigabit - i217-V) ← **H81-BTC PCH**
- 0x8086:1559 (82579 Gigabit - i218-V)

### Realtek Family
- 0x8168:8168 (RTL8168)
- 0x8169:8168 (RTL8169)
- 0x10C3:8168 (RTL8168B)

### Atheros (via Realtek)
- 0x1066:8131 (AR8131)

---

## REGISTER OFFSETS

### e1000 Registers (MMIO offsets)
```
E1000_CTRL      0x0000      # Reset: 0x04000000, SLU: 0x00000040
E1000_STATUS    0x0008
E1000_EERD      0x0014
E1000_RCTL      0x0100      # RCTL corrected: 0x8802
E1000_TCTL      0x0400      # TCTL: 0x4010A
E1000_MACM      0x0500
E1000_RAL       0x5400      # Read MAC low
E1000_RAH       0x5404      # Read MAC high

TX Ring:
E1000_TDBAL     0x3800
E1000_TDBAH     0x3804
E1000_TDLEN     0x3808
E1000_TDH       0x3810
E1000_TDT       0x3818

RX Ring:
E1000_RDBAL     0x2800
E1000_RDBAH     0x2804
E1000_RDLEN     0x2808
E1000_RDH       0x2810
E1000_RDT       0x2818
```

### RTL8111 Registers (I/O offsets)
```
RTL_IDR0        0x00        # Read MAC (6 bytes)
RTL_CMD         0x37        # Reset: 0x10, Enable: 0x0C
RTL_IMR         0x3C
RTL_ISR         0x3E
RTL_TCR         0x40        # TCR: 0x03000700
RTL_RCR         0x44        # RCR: 0x0F
RTL_TSAD0       0x20        # TX status
RTL_RBSTART     0x30        # RX buffer start
RTL_CAPR        0x38        # Current packet pointer
RTL_CBR         0x3A        # Current buffer pointer
RTL_TXSTS0      0x10        # TX status
```

---

## PHI-FOLD FORMULAS

### Core Functions
```
fold(x, key, seq) = (x·PHI32 + key·FIB32 + seq·SQRT_PHI32) mod 2³²

unfold(f, key, seq) = ((f - key·FIB32 - seq·SQRT_PHI32) · PHI32_INV) mod 2³²

mcast_ip = 0xEF000000 | (phi_fold(genome_fp, 0, 0) & 0x00FFFFFF)

slot_index = phi_fold(phi_addr & 0xFFFFFFFF, genome_fp, 0) mod 128
```

### Index Computation (Assembly)
```asm
; EAX = phi_addr (low 32 bits)
mov   ecx, 0x9E3779B9        ; PHI32
mul   ecx                    ; EAX = phi_addr * PHI32

mov   ecx, [0x1013FC]        ; genome_fp
mov   rdx, 0x9E3779B1        ; FIB32
imul  ecx, edx               ; ECX = genome_fp * FIB32

add   eax, ecx               ; EAX = result
and   eax, 127               ; mod 128
imul  eax, 4                 ; byte offset
add   eax, STORE_INDEX_BASE  ; final index
```

---

## BOOTSTRAP FLOW

```
1. DNA → GRAPH
   ├─ base4_codec EXECUTED
   └─ genome_engine CONFIGURED

2. NIC_INIT
   ├─ nic_timing EXECUTED
   ├─ e1000_wait_reset OR rtl_detect_version
   └─ NIC_READY

3. PEER_INIT
   ├─ peer_discover_all
   │   ├─ phi_seed (multicast)
   │   ├─ arp_probe (fallback)
   │   └─ sector_seed (static)
   └─ PEERS_KNOWN

4. STORE_INIT
   ├─ store_arena_init
   └─ STORE_OPEN

5. FABRIC_READY
   ├─ store_genome_fp_in_lattice (0x1013FC = 0x101208)
   ├─ mailbox_extend
   └─ RUNTIME
```

---

## SHELL COMMANDS

### Command Syntax
```
Router64> <command> [arguments]
```

### Available Commands
```
genome      # Omega graph stats
fabric      # Fabric engine status
ftick       # Advance phi_tick
fgossip     # Gossip update
fauth       # Strand authentication
nic2        # NIC state
store       # Store statistics
peers       # Peer table
send        # Send test frame to peer[0]
load        # Load by phi_addr: load <hex>
```

### Command Examples
```
Router64> peers
fabric peers:
  10.0.0.1:8090
  10.0.0.2:8090

Router64> load 9E3779B9
load: phi_addr=0x9E3779B9

Router64> store
fabric store:
  arena: 102400 / 4194304
  strand[1] cursor: 122880 bytes
```

---

## ERROR HANDLING (GOI)

| Condition | Action |
|-----------|--------|
| Arena full | Return 0 (saturate, don't halt) |
| NIC timeout | Continue anyway |
| No NIC found | Boot in store-only mode |
| Store lookup fail | Silent failure, don't crash |
| Peer not found | Retry later |

---

## SMOKE TEST CHECKLIST

Required strings in QEMU serial output:
- [ ] `[Omega] RUNTIME: Omega_n+1=T(Omega_n) complete`
- [ ] `[Analog] Dn(r) lattice:`
- [ ] `Kuramoto aphase: LOCK`
- [ ] `phi-lattice consensus state`
- [ ] `Router64>`
- [ ] `[Fabric] ready` (if NIC detected)
- [ ] `genome_fp`

---

## PORT CONFIGURATION

### QEMU (e1000)
```bash
qemu-system-x86_64 \
  -drive "file=bin/hdgl_router64.img,format=raw,if=ide" \
  -boot order=c \
  -m 64M \
  -serial "file:/tmp/hdgl.log" \
  -netdev "user,id=n0" \
  -device "e1000,netdev=n0" \
  -no-reboot \
  -display none
```

### QEMU (RTL8139)
```bash
-device "rtl8139,netdev=n0"
```

### Metal (H81-BTC)
```bash
# Flash image to USB
dd if=bin/hdgl_router64.img of=/dev/sdX bs=512 conv=fdatasync

# Boot via USB
# Capture serial:
minicom -D /dev/ttyS0 -b 115200
```

---

## CRITICAL FIXES (H81-BTC)

### Fix 1: BAR64 (i217-V)
```asm
; OLD (WRONG): movsx rax, eax
; NEW: Read BAR0 + BAR1, combine
mov   eax, ebx
shl   eax, 8
or    eax, 0x80000010
out   dx, eax
in    eax, dx              ; BAR0
test  esi, 0x04            ; Check 64-bit indicator
jnz   .nic_bar64           ; If set, read BAR1
; 32-bit path: mask and use directly
; 64-bit path: read BAR1, shift, OR together
```

### Fix 2: RCTL (82579)
```asm
; BSIZE interpretation differs by chip family
; 00b = 2KB on i210/i350
; 00b = 16KB on 82579 (WRONG for our needs!)
; Need 01b = 2KB on ALL chips
; RCTL corrected: 0x8002 → 0x8802
mov   dword [rax + E1000_RCTL], 0x04008802
  ; = EN | SBP | BAM | BSIZE_2KB | SECRC
```

---

## THREE CARriers (Covert Channels)

| Carrier | Location | Capacity | Detection Risk |
|---------|----------|----------|----------------|
| CH-0 | Reserved field (4B/frame) | 800 KB/sec | None (standard framing) |
| CH-1 | Strand weights LSB-2 | 16 bits/gossip | None (below noise floor) |
| CH-2 | HTTP paths | 1 bit/request | Low (all 404) |

---

## TEST FILES

### C Test Files (Referenced)
- `hdgl_genome_test.c` — Genome engine tests
- `hdgl_fabric_test.c` — Fabric engine tests
- `zchg_carrier_test.c` — Carrier channel tests

### Makefile Targets
```makefile
all         # Build complete image
carrier_test # Carrier channel tests
fabric_test # Fabric tests
metal       # Metal target (dd + minicom)
smoke       # QEMU smoke test
clean       # Remove build artifacts
```

---

## FILES STRUCTURE

```
HDGL-fabric-0.1/
├── fabric2/
│   └── HDGL_CONSOLIDATED.hdgl   ← MAIN FILE (this document)
├── fabric1/                     ← Original components
│   ├── hdgl_complete.hdgl
│   ├── hdgl_fabric.hdgl
│   ├── hdgl_genome.hdgl
│   ├── hdgl_nic.asm
│   ├── hdgl_peer_discovery.hdgl
│   ├── hdgl_genome_shell.hdgl
│   ├── hdgl_genome_fabric.c
│   ├── zchg_carrier.h
│   ├── zchg_store_metal.c
│   ├── hdgl_fabric_loader.c
│   └── Makefile
├── analog_fabric_plan.svg
├── README.md
└── LICENSE
```

---

## KEY INVARIANTS (Memorize)

1. **0x101208** = Live gossip_fingerprint (updates each cycle)
2. **0x1013FC** = Stable genome_fp cache (set once at boot)
3. **BAR64** = Bits 2:1 of BAR0 = 10b indicates 64-bit BAR
4. **RCTL** = 0x8802 for 2KB buffers (all e1000, including i217)
5. **EMA** = φ/(φ+1) ≈ 0.618 for natural weighting
6. **Peer Discovery** = Reads 0x101208 (live), 0x1013FC (stable)
7. **Phi-fold Bijection** = Every (x, key, seq) maps uniquely
8. **Store Index** = phi_fold mod 128 → 128 slots × 4B
9. **FABRIC_READY** = Bit 5 of 0x101014
10. **Genome FP** = FNV_PHI_SPIRAL(base4_seq, dn_aggregate)

---

## AXIOM

**Ωₙ₊₁ = T(Ωₙ)**

The system defines itself through recursive transformation. Every value, every address, every constant derives from the phi-fold primitive. The fabric IS the fabric's own definition.
