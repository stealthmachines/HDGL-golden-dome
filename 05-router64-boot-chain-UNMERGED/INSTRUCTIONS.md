# HDGL Router64 Boot Chain — Full Instructions

Covers three things: setting up the toolchain on a fresh machine, rebuilding
and testing in QEMU, and writing the result to real boot media for
PCEngines APU / Protectli hardware. Read the "Hardware gotchas" section
before you write anything to a physical board — there are two settings
that will look like a dead board if you skip them.

---

## 1. Toolchain setup on a fresh machine

You need `nasm` (assembler) always, and `qemu-system-x86` only if you want
to test before touching real hardware.

### Debian/Ubuntu

```sh
sudo apt-get update
sudo apt-get install -y nasm qemu-system-x86
```

### Fedora/RHEL

```sh
sudo dnf install -y nasm qemu-system-x86
```

### macOS (Homebrew)

```sh
brew install nasm qemu
```

Verify:

```sh
nasm -v                    # NASM version 2.x
qemu-system-x86_64 --version
```

This was built and tested against NASM 2.16.01 and QEMU 8.2.2. Anything
from NASM 2.14+ and QEMU 6.0+ should behave identically — nothing here
uses bleeding-edge syntax.

---

## 2. Rebuild from source

Directory layout assumed below: `src/{mbr,stage2,runtime64}.asm` and an
output `bin/` directory.

```sh
mkdir -p bin
nasm -f bin src/mbr.asm       -o bin/mbr.bin
nasm -f bin src/stage2.asm    -o bin/stage2.bin
nasm -f bin src/runtime64.asm -o bin/runtime64.bin
```

Expected sizes — if any of these don't match, something in the source
changed or `nasm` truncated on an error that didn't set a non-zero exit
code (rare, but check `nasm`'s stderr output if so):

| File             | Size (bytes) | Why                                            |
|------------------|--------------|-------------------------------------------------|
| `mbr.bin`        | 512          | fixed: one boot sector                          |
| `stage2.bin`     | 512          | canonical: exactly 1 sector (Part XII)          |
| `runtime64.bin`  | 32768        | `times (64*512)-($-$$) db 0` — 64 sectors        |

```sh
wc -c bin/*.bin
```

Concatenate in boot order — this *is* the disk image, sector for sector:

```sh
cat bin/mbr.bin bin/stage2.bin bin/runtime64.bin > bin/disk.img
wc -c bin/disk.img    # 33792 bytes = 66 sectors
```

That's the whole build. No linker, no Makefile dependencies beyond NASM
itself for this three-file chain.

---

## 3. Test in QEMU before touching hardware

### 3a. Why you need to pad the image first

QEMU's BIOS (SeaBIOS) derives a CHS (cylinder/head/sector) disk geometry
from the image size when no geometry is given explicitly. For a tiny
34816-byte raw image it can pick a degenerate geometry where the `int
0x13` reads this boot chain issues (hardcoded `CH=0`, `CL=2`/`5`, `DH=0`)
fall outside the sectors-per-track SeaBIOS thinks exist, and the read
fails — even though the code and the real disk layout are both correct.
Padding the image to a size SeaBIOS recognizes as a "normal" disk
(16 heads × 63 sectors/track is the classic default) sidesteps this. This
is **only a QEMU/SeaBIOS quirk** — a real disk controller and the real
machine's BIOS resolve the boot drive's actual geometry from the hardware
itself, so you do not pad before writing to physical media (see §4).

```sh
cp bin/disk.img bin/disk_test.img
truncate -s 10M bin/disk_test.img
```

### 3b. Boot it with serial captured to your terminal

```sh
qemu-system-x86_64 \
  -drive file=bin/disk_test.img,format=raw,if=ide \
  -nographic \
  -no-reboot \
  -serial mon:stdio
```

Flag-by-flag:
- `-drive file=...,format=raw,if=ide` — attach the image as an IDE hard
  disk, raw format (no qcow2/sparse interpretation).
- `-nographic` — no VGA window; this also redirects the QEMU monitor and
  serial console to your terminal by default, which is why...
- `-serial mon:stdio` — ...this explicitly multiplexes the COM1 serial
  port and the QEMU monitor onto stdio, so you see the firmware's COM1
  output (the `AXIOM`/`S2`/`PM`/`LM`/test banners) directly in your shell.
- `-no-reboot` — halts QEMU instead of resetting when the guest issues a
  reset, useful if a bug causes a triple-fault loop; you'll see it hang
  instead of silently rebooting forever.

Expected output, end to end:

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

It stops at the `Router64> ` prompt and spins (`jmp $`) — that's the
smoke-test design, not a hang. Press `Ctrl-A` then `X` to quit QEMU
cleanly (`Ctrl-A` is the QEMU escape prefix under `-nographic`; `X`
terminates the emulator).

### 3c. If you want an RTL8139 NIC path instead of e1000

The PCI scan test (T3) reports whichever NIC vendor it finds first. To
exercise the Realtek branch instead of Intel:

```sh
qemu-system-x86_64 \
  -drive file=bin/disk_test.img,format=raw,if=ide \
  -net nic,model=rtl8139 -net user \
  -nographic -no-reboot -serial mon:stdio
```

T3 should then report `(RTL)` instead of `(Intel e1000)`.

### 3d. Troubleshooting

| Symptom | Likely cause |
|---|---|
| `E.` right after `AXIOM`, nothing else | MBR disk read failed. If you're using the corrected `mbr.asm`, this means the geometry-padding step (§3a) was skipped. |
| Boots, prints `AXIOM` then nothing | Stage2 read failed inside the MBR's own retry path — check the image wasn't truncated by a partial `cat`. |
| Hangs after `LM` with no `[Omega]` banner | Stage2's CHS read for runtime64 (LBA 4, 63 sectors) is missing or `runtime64.bin` is shorter than expected — re-check the `wc -c` sizes from §2. |
| `(no NIC)` instead of a vendor name | Expected if you didn't attach a NIC device to the QEMU command line at all; QEMU's default `-net` config usually still provides one, so this is mostly only seen with `-nic none`. |
| Any test reports `FAIL` | This is a real regression — none of the corrected sources should produce a `FAIL` on T1–T7 as shipped. Worth a closer look before going near hardware. |

---

## 4. Writing to real boot media (H81-BTC-Pro / B85-BTC / Z87-BTC)

> **Correction from an earlier draft of this document:** this section
> previously named PCEngines APU and Protectli boards. The actual target
> hardware, per `HDGL_CONSOLIDATED_V2.hdgl`'s own header (line 8-10) and
> its Part XVII hardware notes, is a **Gigabyte H81-BTC-Pro** (or the
> closely related B85-BTC/Z87-BTC variants — Haswell-era LGA1150 mining
> boards with many PCIe x1 slots) with an **Intel i217-V NIC
> (PCI ID 8086:153A)** on the PCH. If you're actually targeting a
> PCEngines/Protectli board for a different build of this firmware, the
> general BIOS/serial guidance below still applies, but board-specific
> details (BIOS menu names, onboard NIC model) will differ — check that
> board's own manual.

### Hardware gotchas — read this before you write anything

1. **Serial console baud rate: 9600, not 115200.** `runtime64.asm`
   programs the UART divisor as `12`, and `115200 / 12 = 9600`. This
   matches the spec directly — Part XVII states the firmware's serial is
   "COM1 at 0x3F8, 9600 8N1." H81-BTC-Pro boards typically ship with
   American Megatrends UEFI, whose console-redirect default (if enabled
   at all) is usually 115200 8N1, and most boards in this family don't
   expose serial output during early POST by default — you may only see
   anything once this firmware's own COM1 init runs. Set your terminal
   (`screen`, `minicom`, `putty`, etc.) to **9600 8N1**. The board has a
   COM header (labeled `JCOM1` on the H81-BTC-Pro) for a USB-serial
   adapter — there is no onboard DB9 port on these mining boards.
2. **Legacy/CSM boot only — no UEFI.** This is a raw MBR boot sector
   (`0xAA55` signature, real-mode entry at `0x7C00`) that hand-rolls its
   own A20/protected-mode/long-mode transition. It will not be discovered
   by a pure-UEFI boot manager. Per Part XVII: go into the AMI UEFI setup
   and set **BIOS → Boot → CSM Support → Enabled**, then **BIOS → Boot →
   Boot Mode → Legacy**. (The spec also notes an `hdgl_uefi_stub.asm` that
   chainloads this legacy MBR from within UEFI if you need to keep CSM
   off for other reasons — not part of this deliverable's three files.)
3. **CHS reads, not LBA48.** The MBR and stage2 both issue classic
   `int 0x13, AH=02` CHS reads, not `AH=42` extended/LBA reads. This
   works on essentially all modern BIOS-CSM disk emulation (it's
   translated to LBA internally), but if you ever swap to a disk larger
   than ~8GB with unusual partitioning, double check the target media is
   being addressed in CHS-translatable range — for a sub-1GB boot device
   this is a non-issue.
4. **No padding step on the real write.** §3a's `truncate -s 10M` was a
   QEMU/SeaBIOS-only workaround. Do not pad before writing to physical
   media — write `bin/disk.img` (33792 bytes, 66 sectors — sector 0 MBR,
   sector 1 stage2, sectors 2-65 runtime64, per the spec's Part XII disk
   layout) directly. The real board's BIOS resolves the actual device
   geometry from the device itself, not from file size heuristics.
5. **NIC vendor ID on this board is `8086:153A`, not a generic e1000.**
   The i217-V is a PCH-integrated 82579 MAC+PHY combo that speaks the
   e1000 register set, so T3's vendor-ID check (`0x8086` → "Intel e1000")
   correctly identifies it — but if you later wire in the real driver
   (Part V), note the spec calls out two i217-V-specific register fixes
   that don't apply to a generic e1000: BAR0 is a 64-bit BAR (read BAR0+
   BAR1 and combine for the true MMIO base), and RCTL needs
   `0x04008802` (BSIZE=01b for 2KB buffers) rather than the older e1000
   default of `0x8002`. T3 in this build only confirms the vendor ID is
   visible on the bus — it doesn't program any NIC registers, so this
   gotcha doesn't affect the smoke test itself, only future driver work.

### Identify the target device — do this carefully

Writing to the wrong device will destroy data on your build machine.
Confirm the device node before running `dd`.

```sh
# Insert the target media (the board's boot drive — typically a small
# SATA/mSATA SSD or USB stick on this board family), then:
lsblk
# or, more verbose:
sudo fdisk -l
```

Identify the target by its **size** matching your media, not by guessing
from device order. On Linux it's commonly `/dev/sdX`; on macOS,
`/dev/diskN` (use the raw `/dev/rdiskN` for speed). **If you're unsure
which device is which, stop and check `lsblk` output again rather than
guess.**

### Write the image

Linux:

```sh
sudo dd if=bin/disk.img of=/dev/sdX bs=4M conv=fsync status=progress
sync
```

macOS (use the raw device for speed, and unmount first):

```sh
diskutil unmountDisk /dev/diskN
sudo dd if=bin/disk.img of=/dev/rdiskN bs=4m
sync
```

Replace `/dev/sdX` / `/dev/diskN` with the device you confirmed above —
never the path to a partition (`/dev/sdX1`) or your build machine's own
disk. The spec's own Part XVII notes the equivalent flash command as
`dd if=bin/hdgl_router64.img of=/dev/sdX bs=512 oflag=sync` — functionally
the same operation, `bs=4M` above is just faster for a full-disk write.

### Boot it

1. Insert the written media into the H81-BTC-Pro's boot drive (SATA/
   mSATA, whichever the board exposes — these boards are typically built
   for many PCIe risers plus one small boot drive).
2. Connect a serial console via the `JCOM1` header (USB-serial adapter)
   at **9600 8N1** — see gotcha #1 above.
3. Confirm CSM/Legacy boot is enabled (gotcha #2) and this media is first
   in boot order.
4. Power on. You should see the same `AXIOM` → `S2` → `RT` → `PM` → `LM`
   → `[Omega]...` sequence as the QEMU run, ending at `Router64> `.
5. T3 (PCI NIC scan) should report `(Intel e1000)` against the onboard
   i217-V's real vendor ID (`8086:153A`) — the same vendor-ID check
   exercised against QEMU's emulated e1000 in §3, now against real
   silicon. If you've added a Realtek NIC card in one of the board's
   PCIe x1 slots instead, it'll report `(RTL)`.

If nothing appears at all on the serial console after power-on, the
first two things to check are exactly the two gotchas above: baud rate
mismatch, and Legacy/CSM not enabled.
