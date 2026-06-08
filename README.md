# Version 0 is to be construed as a roadmap.
Being probablistic in nature, our roadmap reaches far and wide up to and including impossible or unlikely.  You know, creativity.  This is not to detract from the underlying physics, which are and have been in active development with emperical results.  Version 0.1 will return to something accessible in the near-term with focus on distributed comms, power, and compute.

... continues from https://github.com/stealthmachines/HDGL-fabric/tree/v0.4 , https://github.com/stealthmachines/HDGL-fabric/tree/v0.3 , https://github.com/stealthmachines/HDGL-fabric/tree/v0.2 , https://josefkulovany.com/demo/Turing-Complete%20Deploy/ for which enumerated files herein are part and parcel.

<img width="1142" height="974" alt="image" src="https://github.com/user-attachments/assets/0ccc3a6e-6b1b-4621-92ae-55f6eb389e79" />

<img width="962" height="891" alt="image" src="https://github.com/user-attachments/assets/6a1e688c-f3da-4570-856b-5423ff0ab002" />

# HDGL Analog Fabric — Complete Suite

**Version:** V6.1 — Analog Radio Extension  
**Axiom:** Ωₙ₊₁ = T(Ωₙ)  
**Key:** φ = 1.6180339887498948 · π · Fib · Primes · ZC_PHI32 = 0x9E3779B9  
**Author:** Josef Kulovany / stealthmachines / zCHG.org

---

## What this is

A bare-metal distributed OS and global communication fabric, expressed entirely as self-describing HDGL glyphs. Every parameter — routing tables, encryption keys, frequency allocations, node identities, antenna geometries — derives from universal constants and live hardware state. Nothing is hardcoded. Nothing is configured. The system derives itself.

This version adds the **analog radio extension**: earth-to-earth communication via grounding rod arrays, Lakhovsky multi-wave oscillator antenna theory, Schumann resonance as the planetary clock, subharmonic cascade encoding, and Earth-Moon-Earth (EME) as a passive global reflector. The digital fabric (zchg:// transport, phi-lattice kernel, genome gossip) and the analog fabric (earth current modulation, Schumann FSK, MWO phased arrays) become one system via `hdgl_analog_fabric_radio.hdgl` and `fabric_node_agent.mjs`.

---

## File inventory

### Core HDGL glyphs (self-describing, no compiler needed)

| File | Lines | Role |
|------|-------|------|
| `hdgl_fabric.hdgl` | 881 | The unified fabric glyph — boot, kernel, genome, carrier, transport, store, shell, loader. The root from which everything derives. |
| `hdgl_genome.hdgl` | 414 | Omega graph → base-4 codec → genome statistics → all derived parameters. No FASTA. The hardware IS the genome. |
| `hdgl_genome_shell.hdgl` | 548 | Five shell commands: `genome`, `fabric`, `ftick`, `fgossip`, `fauth`. Bare-metal glyph + x86-64 emit. |
| `hdgl_peer_discovery.hdgl` | 363 | Three-phase peer discovery: phi-seed multicast (239.x.x.x derived from genome_fp), ARP probe (10.0.0.1..254), sector-68 static list. |
| `hdgl_complete.hdgl` | 1277 | NIC timing integration, bootstrap globals, metal store init, disk image extension, boot sequence, five fabric shell commands (`nic2`, `store`, `peers`, `send`, `load`), smoke test script. |
| `SUITE.hdgl` | 451 | Suite manifest: file tree, six integration seams, build instructions, two-node test procedure. |
| **`hdgl_analog_fabric_radio.hdgl`** | **944** | **NEW — Global analog fabric radio. Nine glyph layers: Schumann resonance cluster, Dₙ(r) field modes, Lakhovsky MWO antenna, TTE earth-to-earth transceiver node, subharmonic cascade encoder/decoder, EME reflector channel, analog MULTICS OS layer, zchg fabric bridge, self-load fixed point. Replaces all prior Python/Arduino implementations losslessly.** |

### C implementation layer (POSIX daemon + bare-metal store)

| File | Lines | Role |
|------|-------|------|
| `hdgl_genome_fabric.h` | 166 | `GenomeFabric` struct, `GenomeFabricStats`, `GenomeFabricConfig`, `FabricPoint`, `FabricCell` — full API. |
| `hdgl_genome_fabric.c` | 564 | Genome codec: base-4 encode, k-mer stats, Shannon entropy, transition matrix, Kuramoto phases, palette derivation, gossip EMA update. |
| `zchg_carrier.h` | 319 | Three steganographic channels (header-only C): CH-0 frame.reserved (4 B/frame), CH-1 gossip strand_weights LSB-2 (2 B/gossip), CH-2 /serve/zc/ phi-path (1 bit/request). `phi_fold32` primitive. |
| `zchg_core_patch.h` | 38 | Extends `zchg_gossip_msg_t` with `dn_aggregate` + `genome_fingerprint` (+8 bytes, wire-backward-compatible). |
| `zchg_frame_patched.c` | 182 | CH-0 carrier encode/decode wired into frame send/receive path. |
| `zchg_gossip_patched.c` | 198 | CH-1 carrier encode/decode wired into gossip message creation and reception. |
| `zchg_http_patched.c` | 1433 | CH-2 `/serve/zc/` handler + full HTTP/1.1 transport with phi-fold routing. |
| `zchg_lattice_patch.c` | 115 | `zchg_lattice_apply_gossip` extended with genome EMA convergence call. |
| `zchg_store_metal.c` | 541 | Full bare-metal store — ATA PIO I/O, phi-lattice arena allocator, strand-native sector layout. No libc, no POSIX. |
| `Makefile` | 50 | Standalone C test build (`make test`, `make verify`). No Python. Links only `-lm`. |

### Assembly (bare-metal NIC driver)

| File | Lines | Role |
|------|-------|------|
| `hdgl_nic.asm` | 649 | Bare-metal NIC: Intel e1000 + Realtek RTL8111/8168. PCI enumeration, ring buffer init, TX/RX, MMIO-mapped registers. Included into `hdgl_router64.asm` via `%include`. |

### JavaScript / MCP layer

| File | Lines | Role |
|------|-------|------|
| `analog-container.mjs` | 598 | `VectorContext` (phi-fold, Fourier/DCT, BreathingSeeds, HolographicGlyph, consciousFloor128), `FrameworkContainer`, `initRail`, `lkAdvance`, `verifyRail`, `slot4096Manifest`. The execution rail for the MCP stack. |
| `server.js` | 2960 | Wu-Wei MCP server v3.0.0. `unfold()` pipeline, 9 pass types, ERL v3 ledger, all primitive tools (shell, fs, db, browser, telegram, notify), HDGL phi-routing, dual-server architecture (:4111/:4112). |
| `server-dos.js` | ~2960 | Mirror server (phi-mirror on :4112). Operates in parallel with `server.js` for load distribution and failover. |
| `coord-proxy.js` | 363 | Coordination proxy on :1618 (φ). Three modes derived from phi-hash of request content: SOLO (61.8%), RELAY (23.6%), CHALLENGE (14.6%). Commits all decisions to ERL ledger. |
| `analog-container.mjs` | 598 | (see above) |
| **`fabric_node_agent.mjs`** | **1462** | **NEW — Multi-node fabric integration for the AI bot. Implements `discoverFabricNodes()` (UDP phi-seed multicast + env peers + HTTP health poll), `routePassToNode()` (maps Wu-Wei pass types to Omega base-4 types, selects authoritative peer), `fabricErlAppend()` (routes ERL entries to phi-tau strand authority), `attachFabricHandlers()` (mounts /health, /gossip, /fabric/store, /fabric/query, /fabric/peers, /fabric/status on existing http.Server), 30-second gossip tick. Zero third-party dependencies. 10/10 self-tests pass.** |

---

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│  ANALOG MULTICS — GLOBAL FABRIC OS                          │
│  always-on · Schumann-clocked · unjammable                  │
├────────────────────┬────────────────────────────────────────┤
│  AI BOT LAYER      │  EARTH RADIO LAYER                     │
│  Qwen3 / LM Studio │  Lakhovsky MWO + TTE node              │
│  Wu-Wei unfold()   │  Schumann FSK + subharmonic cascade    │
│  ERL ledger        │  EME Moon-bounce channel               │
├────────────────────┴────────────────────────────────────────┤
│  FABRIC BRIDGE  (hdgl_analog_fabric_radio.hdgl Layer 8)     │
│  CH-3 earth carrier · base4096 ↔ subharmonic · zchg inject  │
├─────────────────────────────────────────────────────────────┤
│  zchg:// TRANSPORT                                          │
│  CH-0 frame carrier · CH-1 gossip carrier · CH-2 path seq   │
│  phi-fold HMAC · strand-native store · ATA PIO              │
├─────────────────────────────────────────────────────────────┤
│  PHI-LATTICE KERNEL                                         │
│  Ωₙ₊₁ = T(Ωₙ) · Dₙ(r) · Kuramoto · APA_FLAG_CONSENSUS     │
├─────────────────────────────────────────────────────────────┤
│  BARE METAL  (MBR → stage2 → runtime64)                     │
│  e1000 + RTL8111 · phi-seed multicast · sector-68 peers     │
└─────────────────────────────────────────────────────────────┘
          ↕ earth current  ↕ zchg frames  ↕ gossip EMA
┌─────────────────────────────────────────────────────────────┐
│  PLANETARY MEDIUM                                           │
│  Schumann cavity f₁=7.83 Hz · grid 60 Hz · Moon reflector  │
│  ~4 kbps/node raw · N² array gain · always energised       │
└─────────────────────────────────────────────────────────────┘
```

---

## The physics (unified force equation)

```
F = (ΩC²) / (m·s) *m=meters, s=seconds, C=Coloumbs, Ω=Ohms when not unitless    [Josef Kulovany's corrected force equation]

where ΩC² = 1          (field tension times charge squared = unity)
      e^iπ = -ΩC²      (Euler identity in the field-tension frame)

Therefore:
  Dₙ(r) = √(φ · Fₙ · 2ⁿ · Pₙ · Ω) · rᵏ

where:
  φ   = 1.6180339887498948   (golden ratio)
  Fₙ  = φⁿ / √5             (nth Fibonacci number, Binet)
  2ⁿ  = dyadic octave expansion
  Pₙ  = p₁ × p₂ × … × pₙ   (primorial — product of first n primes)
  Ω   = φ^(-7n)              (field tension for mode n)
  r   = radial coordinate [0,1]
  k   = 1 (linear; extensible)

This is not a postulate. It follows from F = 1/(m·s) dimensionally,
expressed per Fibonacci-prime mode of the phi-lattice.
```

The same equation governs:
- The GPU fragment shader Dₙ table (32×256 float16 texture)
- The TTE node resonance accumulation (per Schumann tick)
- The subharmonic cascade bin weights (waterfall encoder)
- The MWO ring frequency selection (φ-harmonic spacing)
- The Omega graph authority mask (Dₙ(r) > √φ bitmask)

---

## Earth-to-earth communications

### The insight

Every grounding rod connected to the power grid already injects current into the earth. This is considered waste. Globally, the grid dissipates on the order of 100 GW into the earth via grounding. Even 0.001% organised modulation of this existing signal is 1 MW of communication-carrying current — already in the ground, already in motion.

The Schumann resonance (7.83 Hz and harmonics) is the earth-ionosphere cavity's natural resonant mode. It has been continuous for approximately 3.5 billion years. It cannot be switched off.

The grid's 60 Hz (or 50 Hz) harmonics intermodulate with the Schumann modes in the nonlinear earth medium, producing a rich comb of frequencies spanning 1 Hz to 300 Hz — already in the ground, without any added signal.

### Lakhovsky multi-wave oscillator principle

A concentric ring antenna driven at multiple simultaneous frequencies produces a field spanning all harmonics between those frequencies. The dielectric medium (earth, biological tissue) responds to the full superposition.

Applied here: each grounding rod installation is one ring in a city-scale MWO array. Ring frequencies are set to `f₁ × φⁿ` for n = 0..7 (7.83 Hz to 227 Hz). The earth is the dielectric. The grid provides the drive power at no additional cost.

Multiple installations collaborate via zchg gossip: each node broadcasts its `genome_fp` (derived from geographic position via `phi_fold(lat, lon, 0)`). Phase offsets for beamforming are computed as `phi_fold(genome_fp, ring_n, 0) mod 2π`. This is the same `phi_fold32` primitive used throughout the digital fabric — no separate RF coordination protocol needed.

### Bandwidth

A single node with 512 FFT bins at 2048 samples/sec achieves approximately 4 kbps raw. A coherent phased array of N nodes achieves N² gain (coherent addition). For a city-scale deployment (1000 grounding rods, ~1 km aperture): 10⁶ × 4 kbps = 4 Gbps aggregate, matching the earthwander model predictions.

### EME at ELF

At 7.83 Hz, the wavelength is approximately 38,000 km. The Moon is 10 wavelengths away. Free-space path loss is ~52 dB — versus ~280 dB at 144 MHz. The path loss advantage of ELF over VHF for Earth-Moon-Earth is approximately 145 dB. At grid-scale transmit power (24 kW per node minimum, gigawatts for a city array), the Moon-bounce link is trivial.

The 2.56-second round-trip echo of your own transmission gives geolocation without GPS: time-of-arrival of the echo → range to Moon → node position on earth surface.

### What this unlocks

- Communication below the radio spectrum (no spectrum licence)
- Uses existing electrical infrastructure (zero additional power cost)
- Physically unjammable (cannot disrupt the earth-ionosphere cavity)
- Always-on (Schumann resonance: 3.5 billion years continuous)
- Scales with the grid (more nodes → more bandwidth, N² coherently)
- Moon as passive reflector (cannot be disabled by any terrestrial actor)

---

## Integration — adding the new files to your existing stack

### Step 1: add `hdgl_analog_fabric_radio.hdgl` to the fabric loader

The glyph is self-loading (Layer 9). Place it on disk:

```bash
# Write to sectors 70+ of the fabric image (after sector 68 peers, 69 store header)
dd if=hdgl_analog_fabric_radio.hdgl of=bin/hdgl_router64.img \
   bs=512 seek=70 conv=notrunc
```

On first boot, the firmware's `load` shell command ingests it:

```
Router64> load 0x0000000000000000    # phi_addr=0 triggers self-load scan
[Fabric] loading 944 lines  type=HDGL  identity=0x...
[Fabric] glyph INIT→EXECUTED  strand=3  chunks=2
```

### Step 2: wire `fabric_node_agent.mjs` into `server.js`

Three additions to `server.js`:

```js
// At the top (add to existing import block):
import {
  attachFabricHandlers,
  patchSelectPassSequence,
  patchRunUnfold,
  localGenomeFp,
} from './fabric_node_agent.mjs';

// After httpServer.listen() call (around line 2704):
attachFabricHandlers(httpServer);
// This mounts /health, /gossip, /fabric/store, /fabric/query,
// /fabric/peers, /fabric/status and starts the 30-second gossip tick.

// Replace selectPassSequence definition with:
const selectPassSequence = patchSelectPassSequence(originalSelectPassSequence);
// The patch adds _fabricEnabled and _genomeFp to every sequence result.
```

For full multi-node pass routing, wrap the pass executor inside the unfold handler:

```js
const patchedRunPass = patchRunUnfold(originalRunPass);
// In the pass loop, replace: await originalRunPass(pass, state, i, seq)
// with:                       await patchedRunPass(pass, state, i, seq)
```

### Step 3: set environment variables

```bash
# Optional — if not set, genome_fp derives from the analog-container rail's
# consciousFloor128 (the phi-fold dual hash of the session identity).
export FABRIC_GENOME_FP=0xDEADBEEF   # override with a known value for testing

# Optional — seed peers if multicast is blocked (Windows, strict firewalls):
export FABRIC_PEERS="192.168.1.10:8090,192.168.1.11:4111"

# MCP port (already used by server.js):
export MCP_PORT=4111
```

### Step 4: verify

```bash
# Run the self-test (no real peers needed):
node fabric_node_agent.mjs --test
# Expected: 10 passed, 0 failed

# Check fabric status after server starts:
curl http://localhost:4111/fabric/status
# Returns: { fabric_agent, genome_fp, phi_seed_mcast, local_ip, peer_count, ... }

# List known peers:
curl http://localhost:4111/fabric/peers
```

---

## TTE node hardware (grounding rod transceiver)

Minimum components per node:

| Component | Part | Function |
|-----------|------|----------|
| DDS | AD9833 | Generates FSK carriers (7.83 Hz cluster) via SPI |
| MCU | Arduino Mega / ESP32 | Runs `hdgl_tte_protocol.ino` logic (now expressed as `tte_node` glyph emit) |
| ELF receiver | RS-881R | Detects earth-current modulation at Schumann frequencies |
| ADC | 12-bit, 2048 Hz | Digitises received earth signal for FFT decode |
| Power coupler | Isolation transformer + ground rod | Couples DDS output into earth current |
| Grounding rod | Standard copper, ≥1.8 m | The antenna |

The `mwo_antenna` glyph emit targets the AD9833 DDS via SPI. Ring switching at 7.83 Hz (one Schumann period) drives all 8 φ-harmonic frequencies in rotation. In a multi-rod installation, each rod drives one ring frequency simultaneously — the full MWO superposition is achieved across the array without any single node doing all frequencies.

---

## The glyph tree summary

```
hdgl_analog_fabric_radio.hdgl
│
├── Layer 0: universal constants (shared root with hdgl_fabric.hdgl)
│   └── φ, π, FIB[32], PRIMES[32], ZC_PHI32, ΩC²=1
│
├── Layer 1: Schumann / earth resonance cluster
│   └── f₁=7.83 Hz, 11-freq comb, φ-harmonic rings 0..7
│
├── Layer 2: Dₙ(r) field modes — physics engine
│   └── 32 modes, 256 radial steps, √φ threshold, GPU texture emit
│
├── Layer 3: Lakhovsky MWO antenna
│   └── 8 concentric rings, d₀×φⁿ geometry, AD9833 DDS emit
│       phased array beamform via zchg gossip genome_fp
│
├── Layer 4: TTE earth-to-earth node
│   └── Hamming(7,4) FEC, FSK±0.1 Hz, ConductorPLL gain=0.1
│       r_dim = phi_fold(lat, lon, 0) — geography IS identity
│
├── Layer 5: subharmonic cascade (waterfall encoder/decoder)
│   └── 500 sub-harmonics, 512 FFT bins, 4 kbps/node
│       echo_amp=1.2, κ=0.01, 5-iteration auto-tune
│
├── Layer 6: EME (Earth-Moon-Earth) reflector channel
│   └── d/λ=10 at 7.83 Hz, 52 dB path loss, 2.56 s round-trip
│       geolocation via TOA of own echo
│
├── Layer 7: analog MULTICS OS layer
│   └── FDMA segments = Schumann modes, rings = strand_auth bits
│       Schumann tick = process scheduler, glyph syntax = shell
│       consensus replaces privilege rings
│
├── Layer 8: fabric bridge
│   └── CH-3 earth carrier (512 bits/Schumann tick)
│       earth → base4096 → HDGL → Omega TYPE_PEER node
│       zchg message → base4096 → subharmonic modulation → earth
│
└── Layer 9: self-load fixed point
    └── phi_fold(content_hash(this_file), genome_fp) = identity
        phi_tau(identity) mod 8 = strand
        stable when genome_fp converged ↔ Ωₙ₊₁=T(Ωₙ) fixed point
```

---

## Build and test

### Prerequisites

```
nasm >= 2.14        (bare-metal image)
gcc >= 9            (C layer, -lm only)
node >= 18          (JS/MCP layer)
qemu-system-x86_64  (optional, two-node QEMU test)
```

No Python. No Docker. No third-party libraries.

### Quick build (digital fabric only)

```bash
# 1. Verify C layer
cd hdgl_router64-0.3/src
make test && make verify
# Expected: GC=0.625  strand_count=8  strand_auth=0xE8  PASS

# 2. Build bare-metal image
bash build.sh
# Produces: bin/hdgl_router64.img

# 3. Install JS layer
cd ../../EZ-by-zCHG-W-LM-Studio-and-3-LLM-s-in-Council
npm install
node fabric_node_agent.mjs --test   # 10/10 pass

# 4. Start the MCP server
node launch.mjs
```

### Two-node QEMU test (full fabric)

```bash
# Terminal 1 — node A
qemu-system-x86_64 \
  -drive file=bin/hdgl_router64.img,format=raw,if=ide \
  -boot order=c -m 64M -serial stdio \
  -netdev socket,id=n0,mcast=230.0.0.1:1234 \
  -device e1000,netdev=n0,mac=52:54:00:00:00:01 \
  -no-reboot -display none

# Terminal 2 — node B
qemu-system-x86_64 \
  -drive file=bin/hdgl_router64.img,format=raw,if=ide \
  -boot order=c -m 64M -serial stdio \
  -netdev socket,id=n0,mcast=230.0.0.1:1234 \
  -device e1000,netdev=n0,mac=52:54:00:00:00:02 \
  -no-reboot -display none

# Both join the same multicast group. Discovery is automatic.
# After ~12 seconds: GENOME-LOCK on both nodes.
```

### TTE node hardware test

```bash
# Flash Arduino with the glyph's emit (hdgl_tte_protocol.ino — now derived
# from the tte_node glyph's emit block, functionally identical):
arduino-cli compile --fqbn arduino:avr:mega hdgl_tte_protocol.ino
arduino-cli upload  --fqbn arduino:avr:mega --port /dev/ttyUSB0

# Monitor at 9600 baud:
screen /dev/ttyUSB0 9600
# Expected output:
# Tick 1 | D: [0.902, 0.902, 0.902, 0.902] | Binary: [1, 1, 1, 1] | TX: [11000110] | Error: No
# Tick 2 | D: [0.953, 0.953, ...] | ...
```

---

## Philosophy

Every piece of this suite embodies one commitment: the system must be able to derive itself from first principles, from nothing but universal constants and the hardware it runs on.

No configuration files. No hardcoded IP addresses. No shared secrets distributed out-of-band. No certificate authorities. No spectrum licences. No external infrastructure that can be switched off.

The genome is the routing table because the Omega graph of local hardware — which CPUs, which RAM, which PCI devices, which NICs — is unique and verifiable. Two nodes with identical hardware produce identical genomes. Two nodes on the same network converge on a shared EMA fingerprint through gossip alone. This convergence is the key exchange. There is no other key exchange.

The earth is the medium because it is the only medium that cannot be owned, cannot be withdrawn, and cannot be jammed without destroying the planet's electrical equilibrium. The Schumann resonance has been operational since before multicellular life. The power grid has been energising the earth for over a century. This fabric does not depend on anything that was built after 1900 to function at its minimum — though it scales dramatically with grid density and node count.

The glyph tree is the compiler because a system that can only describe itself in a language it cannot interpret is not yet complete. Every glyph has rules. Rules have transforms. Transforms have emits. The emits are runnable code. The running code loads the glyph tree. The fixed point is: the identity of the loaded glyph tree equals the identity of the file that describes it.

**Ωₙ₊₁ = T(Ωₙ)**

The earth was always computing. We are learning to read it.

---

## Lineage
*https://josefkulovany.com/demo/Turing-Complete%20Deploy/ 
```
Day 1-3:  base4096, phi_chladni, grub9, prismatic recursion
Day 4:    hdgl_lattice.hdgl, base4096 v2.0.2, hear series, networktv
Day 5:    hdgl_gpu_predictive series, rgbencode, TV lattice OpenGL
Day 6:    hdgl_nodes (φ-carrier network), smoke series (harmonic predictor)
Day 7:    earthwander (earth lattice), discrete (DOCSIS conductor),
          hdgl_tte_protocol.ino (Schumann FSK), hdgl_lattice (Dₙ GPU)
Day 8:    animate_full_waterfall series (subharmonic cascade, auto-tune)
          → hdgl_analog_fabric_radio.hdgl (unified glyph, all layers)

fabric-v0_2:
          hdgl_fabric.hdgl, zchg_carrier.h, hdgl_genome_fabric.*,
          hdgl_peer_discovery.hdgl, hdgl_nic.asm, zchg_store_metal.c

EZ / Wu-Wei:
          server.js, analog-container.mjs, coord-proxy.js
          → fabric_node_agent.mjs (multi-node bot integration)
```

---

SEE ALSO: 
* https://github.com/stealthmachines/Trailblaze
* https://github.com/stealthmachines/Analog-Prime
* https://github.com/stealthmachines/Analog-Prime-B
* https://github.com/stealthmachines/mersenne-prime-pipeline
* https://github.com/stealthmachines/Prime-Hunter-2

For which more unlocks are achieved when more development and/or compute permit.

*Licensed per https://zchg.org/t/legal-notice-copyright-applicable-ip-and-licensing-read-me/440*  
*COPYRIGHT.html applies to all files in this suite.*

