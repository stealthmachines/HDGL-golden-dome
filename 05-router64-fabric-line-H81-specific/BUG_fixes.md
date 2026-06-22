# HDGL Router64 — Boot Chain Fix (MBR → Stage2 → Runtime64)

Three bugs were found and fixed by actually booting the chain in QEMU with
COM1 captured to a serial console, rather than by static read-through. All
three were invisible in the source and only surfaced at execution time.

## Bug 1 — MBR clobbers the boot drive number before reading the disk

`mbr.asm` prints an "AXIOM" banner over COM1 before issuing `int 0x13` to
load stage2. The banner code does `mov dx, 0x3F8` to address the UART, and
`.com1_tx` only saves/restores `dx` *within its own call* — it never
restores the BIOS-supplied boot drive number that arrives in `DL` at entry.
By the time `int 0x13` runs, `DL` has been left at `0xF8` (the low byte of
`0x3F8`) instead of `0x80`, so every disk read fails with an invalid-drive
error.

Confirmed by a throwaway debug MBR that printed `DL` and the `int 0x13`
error code directly: `DL` arrived correctly as `80` from BIOS, but was gone
by the time of the read in the original code.

**Fix:** latch `dl` into a `boot_drive` byte immediately on entry (before
any COM1 calls), and reload it into `dl` immediately before `int 0x13`.

## Bug 2 — `.com1_hex_dword` rotates the wrong register width

The hex-dword print routine did:

```asm
mov rbx, rax      ; 64-bit register
mov ecx, 8
.chd_loop:
    rol rbx, 4    ; rotates the 64-bit register
    ...
    loop .chd_loop
```

`mov eax, ...` zero-extends the upper 32 bits of `rax`, so a 32-bit value
loaded this way sits in the low half of a 64-bit register. Rotating that
64-bit register by 4 bits, 8 times, only rotates it 32 of its 64 positions
— half a turn. The actual data never reaches the low nibble in 8 steps, so
every digit read back as `0`. In context this silently printed `genome_fp`
as `00000000`, which reads exactly like a plausible (if uninteresting)
passing value rather than an obvious failure.

**Fix:** operate on `ebx`/`rol ebx, 4` (32-bit) instead of `rbx`/`rol rbx, 4`.

## Bug 3 — empty string literal aliases the next label

```asm
.msg_genome_fp    db ""   ; placeholder — computed inline
.msg_prompt       db "Router64> ", 0
```

`db ""` emits zero bytes. `.msg_genome_fp` therefore points to the exact
same address as `.msg_prompt` — it isn't an empty string, it's the *same*
string. The `lea rsi, [rel .msg_genome_fp] / call .com1_str` step printed
`"Router64> "` early, in the middle of the final banner, before the actual
hex digits.

**Fix:** the placeholder served no purpose (the hex value is printed
separately by `.com1_hex_dword` right after); removed the dead label and
the print call referencing it.

## Verified output (QEMU, e1000 NIC, COM1 → stdio, canonical 66-sector layout)

```
AXIOM
S2
RT
PM
LM
[Omega] Omegan+1=T(Omegan) axiom: RUNTIME
  [T1] phi constants (PHI32, PHI32_INV)... PASS
  [T2] zero-clear via mov not xor... PASS
  [T3] PCI NIC scan... (Intel e1000) PASS
  [T4] VGA split chrome... PASS
  [T5] IPC ring init... PASS
  [T6] phi-distance GENOME-LOCK... PASS
  [T7] phi_tick loop (3 ticks)...
    tick=1
    tick=2
    tick=3
PASS
  [T8] genome_fp -> 0x101208/0x1013FC/FABRIC_READY... PASS
[Fabric] ready  nic=1  store=open  genome_fp=0x779B1000
Router64> 
```

`genome_fp = 0x779B1000` matches the hand-derived expectation in the
source comments: `phi_fold(dn_aggregate=0, phi_lattice_mean=0x1000, seq=0)`
= lower 32 bits of `0x9E3779B1 * 0x1000` — and per Part II's
`PHI_FOLD_FORWARD` definition (`fold(x,key,seq) = x·PHI32 + key·FIB32 +
seq·SQRT_PHI32 mod 2³²`), which this matches exactly with `x=0,
key=phi_lattice_mean, seq=0`.

## Reconciled against the canonical spec (HDGL_CONSOLIDATED_V2.hdgl)

The three bugs above were found purely by booting the chain — independent
of any spec. Separately, this revision also reconciles the boot chain
against `HDGL_CONSOLIDATED_V2.hdgl` (Parts XII, XIII, IV, VIII), which the
original three sources had diverged from in three ways:

**Disk sector layout.** Part XII (`disk_image`) specifies sector 0 = MBR,
sector 1 = stage2 (exactly one sector), sectors 2–65 = runtime64 (64
sectors). The original sources had stage2 at 3 sectors and runtime64
starting at LBA 4. Stage2's real code footprint is 446 bytes — it fits
the canonical single sector with room to spare. `mbr.asm` now reads only
1 sector for stage2; `stage2.asm` now reads runtime64 from LBA 2 (CHS
sector 3), 64 sectors, matching Part XII exactly. `disk.img` is now
33792 bytes (66 sectors) instead of 34816.

**genome_fp memory wiring.** Parts IV/VIII/XIII define a real memory map
for this value: `0x101208` = `gossip_fingerprint` (live, written by fabric
init), `0x1013FC` = phi-lattice slot 127 (boot-stable cache, written once
by `.store_genome_fp_in_lattice`), and bit 5 of `0x101014` = the
`FABRIC_READY` flag. The original `runtime64.asm` computed `genome_fp` in
a register and printed it directly — it never touched any of those three
addresses. `runtime64.asm` now writes the computed value to `0x101208`,
calls a `.store_genome_fp_in_lattice` routine reproduced verbatim from
Part VIII's emit block (copies `0x101208` → `0x1013FC`, sets the
`FABRIC_READY` bit), and the final banner now reads `genome_fp` back from
`0x101208` rather than a register — matching `.boot_complete_init` (Part
XIII) exactly. A new **T8** test verifies all three writes landed
correctly.

One pre-existing, harmless overlap: T7's `phi_tick` counter uses
`0x101010` as an ad-hoc scratch qword (not a canonical address) that
happens to span into `0x101014` where `FABRIC_READY` lives. T7 completes
before T8 runs, so this is inert, and it's commented in the source so
it isn't mistaken for a real collision later.

**Target hardware.** The spec's actual target is a **Gigabyte H81-BTC-Pro
/ B85-BTC / Z87-BTC** (Haswell PCH mining board) with an **Intel i217-V
NIC (8086:153A)** — not PCEngines APU or Protectli. See `INSTRUCTIONS.md`
for the corrected hardware section (BIOS settings, serial, NIC notes).

**Not wired in — flagged, not attempted.** Part V defines the real e1000
register-level driver, including two hardware fixes specific to the
i217-V/82579 family: BAR64 handling (its BAR0 is a 64-bit BAR — read
BAR0+BAR1 and combine for the true MMIO base) and an RCTL correction
(`0x8002` → `0x04008802`, BSIZE=01b for 2KB buffers on this NIC family,
versus older e1000 parts). T3 in this smoke test only confirms a NIC's
vendor ID is visible on the PCI bus via config-space read — it does not
program any NIC registers. Implementing the real driver is a
substantially larger task than the boot-chain fixes here and wasn't
attempted; flagging it explicitly so it isn't mistaken for done.



```
src/mbr.asm          corrected MBR (Bug 1 fix + canonical 1-sector stage2 read)
src/stage2.asm        canonical 1-sector stage2 — A20/PM/LM, loads runtime64
                       from canonical LBA 2 (was LBA 4)
src/runtime64.asm     corrected runtime (Bug 2 + Bug 3 fixes) + genome_fp
                       wired into canonical memory map (0x101208/0x1013FC/
                       FABRIC_READY) + new T8 verification test
bin/mbr.bin           512 bytes, assembled
bin/stage2.bin        512 bytes, assembled (canonical 1 sector)
bin/runtime64.bin     32768 bytes, assembled
bin/disk.img          33792 bytes — cat of the three .bin files in boot
                       order (66 sectors, matches Part XII exactly)
```

## Rebuilding and testing

```sh
nasm -f bin src/mbr.asm       -o bin/mbr.bin
nasm -f bin src/stage2.asm    -o bin/stage2.bin
nasm -f bin src/runtime64.asm -o bin/runtime64.bin
cat bin/mbr.bin bin/stage2.bin bin/runtime64.bin > bin/disk.img

# Pad to give SeaBIOS a standard CHS geometry under headless QEMU; not
# needed in your existing image/Makefile if it already targets a larger
# raw image or uses -drive ...,cyls=,heads=,secs= explicitly.
truncate -s 10M bin/disk.img

qemu-system-x86_64 -drive file=bin/disk.img,format=raw,if=ide \
    -nographic -no-reboot -serial mon:stdio
```

On real H81-BTC-Pro/B85-BTC/Z87-BTC hardware (the spec's actual target —
see `INSTRUCTIONS.md` §4) the boot-drive issue (Bug 1) would have
manifested identically, since it's a BIOS-contract bug, not a QEMU
artifact — worth flagging as the highest-priority fix of the three for
metal bring-up.
