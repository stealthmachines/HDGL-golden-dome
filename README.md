# Manifest

note to self - this readme as manifest is slightly out of date, it is a copy of the previous version's, that's why.

# A version which supports node.js is added to the zip folder 6.23.26, scroll down to the bottom of this manifest for a table of file changes

# HDGL — Stand-Alone Source Tree (deduplicated, v0.2 → golden-dome)

42 files, one copy of each, organized by what they actually are rather
than which tag they happened to ship in. Built from byte-level (md5)
comparison across v0.1, v0.2, v0.3, v0.3b, v0.3c, v0.3d, v0.3e, v0.4, and
HDGL-golden-dome/main. No version-redundant copies — every file here is
the most current verified state of that file, full stop.

## 01-digital-fabric-core/  (22 files — frozen since v0.1, June 6)

The actual digital fabric: glyph layer, NIC driver, C transport/store,
test harness. Verified byte-identical across every tag that contains it
(v0.1/0.2/0.3/0.3b/0.4). Two files (`SUITE.hdgl`, plus nothing else in
this folder) are frozen since v0.2 rather than v0.1 — see the frozen-core
write-up from earlier in this thread for the full per-file breakdown.

**This entire folder is absent from HDGL-golden-dome.** Golden-dome's own
README and SUITE_V61.hdgl document these files by name and exact line
count as though they ship there — they don't. If golden-dome is ever
built standalone, this folder needs to sit alongside it or the includes
won't resolve.

## 02-analog-radio-extension/  (2 files)

`hdgl_analog_fabric_radio.hdgl` (new at v0.4) and `fabric_node_agent.mjs`
(new at v0.2). Both verified byte-identical between v0.4 and golden-dome
— these are the only two pieces of the fabric that golden-dome actually
carries forward unchanged, confirmed not just by filename but by content.

## 03-golden-dome-manifest/  (1 file)

`SUITE_V61.hdgl` — golden-dome's manifest. This is NOT a revision of
`SUITE.hdgl`; it's separately-written content (different size, different
structure, no shared lineage by hash). It documents the full fused system
(fabric + radio) as if 01- and 02- both ship alongside it, which is the
gap noted above.

## 04-golden-dome-speculative-tier/  (5 files + fine_tuned/ subfolder + 1 reduction)

`hdgl_unified_force.hdgl`, `hdgl_biofield_protection.hdgl`,
`hdgl_collective_mind.hdgl`, `hdgl_schumann_live.hdgl`,
`hdgl_akashic_antigravity_cloak-Einstein-was-wrong-skip-this-version.hdgl`.
golden-dome-only, no prior-version lineage at all. Internally chained:
schumann_live → biofield_protection → collective_mind → akashic/cloak,
with unified_force framed as the master derivation underneath all of it.

`fine_tuned/README.md` carries your own caveat on this tier verbatim in
spirit: these glyphs were generated after the fact, haven't been vetted
to actually load in the fabric (flagged cause: context drift), and their
empirical grounding claim rests on the bigg-fudge10 forum thread rather
than the fabric's own test harness. Kept that framing intact rather than
softening or asserting it either way.

`hdgl_unified_field_REDUCED.hdgl` (366 lines) is the single-glyph
reduction of all five source files above (4,210 lines combined, ~45
glyphs, 6 separate bootstrap glyphs) into one lattice operator, one
applications layer, one bootstrap. Every formula, numeric result, and
citation in it traces verbatim to one of the five source files — nothing
new was introduced, only the structural redundancy (repeated parent-chain
boilerplate, six copies of the self-load pattern) was collapsed. The
provenance caveat above applies to this file unchanged: reduction changes
the shape of the claim, not its evidence status.

## 05-router64-boot-chain-UNMERGED/  (9 files)

The 0.3e end-state of the Router64 line: `HDGL_CONSOLIDATED_V2.hdgl` (the
canonical spec), the deep-dive/cheatsheet reference docs,
`mbr.asm`/`stage2.asm`/`runtime64.asm` (the boot-tested assembly, 3 bugs
fixed and reconciled to canonical sector layout + genome_fp wiring,
targeting H81-BTC-Pro/i217-V), `BUG_fixes.md`, and `INSTRUCTIONS.md`.

Labeled UNMERGED deliberately: this branch forked from the digital fabric
at v0.3b, shares no code with golden-dome, and shares no code with the
older archived `hdgl_router64-0.3.zip` kernel either (that one's still
sitting in `old files`, untouched, if you ever want it mined). Three
genuinely separate things have all been called "Router64" across this
project's history — this folder is specifically the most recent,
smallest-scope, actively-debugged one.

**Open item carried over, not yet resolved:** `HDGL_CONSOLIDATED.hdgl`'s
Part V documents an i217-V RCTL register fix (`0x8002 → 0x8802` per that
doc; `0x04008802` per `BUG_fixes.md`) that, per `BUG_fixes.md` itself, was
never actually wired into `runtime64.asm`. Worth resolving before any
real H81-BTC-Pro bring-up — the spec and the boot-tested binary disagree.

## 05-router64-fabric-line-H81-specific/  (9 files)

CORRECTION to earlier framing in this thread: this is NOT "the" Router64
branch and the system below is NOT its replacement. This folder is a
real, separate, narrower-purpose effort that forked from the digital
fabric at v0.3b and was purpose-built for one specific board (Gigabyte
H81-BTC-Pro, Intel i217-V NIC) — the canonical spec
(`HDGL_CONSOLIDATED_V2.hdgl`), reference docs, and the boot-tested
`mbr.asm`/`stage2.asm`/`runtime64.asm` smoke-test chain (boots, prints
phi constants, reaches GENOME-LOCK, does not yet program real NIC
registers). Genuinely real and boot-verified for what it is — real
hardware bring-up for one board — just much smaller in scope than 06.

## 06-router64-universal-VERIFIED-BOOTED/  (23 files)

This is `stealthmachines/hdgl_router64` v0.4, a separate GitHub repo.
EARLIER MISCHARACTERIZATION CORRECTED: two turns ago I described the
`hdgl_router64-0.3.zip` sitting in `old files` as an abandoned, dormant,
code-disconnected kernel. That was wrong — it's literally an earlier tag
(v0.3) of this same actively-developed repo, now at v0.4. I only had the
archive snapshot at the time, with no commit history to place it against,
and called real ongoing work a dead end.

Built with the repo's own `build.sh` (zero errors, all 5 binaries +
UEFI stub assembled, the C self-test passed 8/8 including a live Kuramoto
convergence to LOCK in 21 steps) and boot-tested for real in QEMU —
`VERIFIED_BOOT_TRANSCRIPT.txt` is the actual captured serial output, not
a description of expected output. Confirmed by typing real commands at
the live prompt: `dna` read the actual CPUID vendor string off the
running CPU, `phi` computed real per-node lattice depths, `attest`/`seal`
produced real crypto output, and `cat 66` made the booted system read its
own embedded `hdgl_firmware.hdgl` source back off disk and print it live
— genuine self-describing behavior, not asserted.

One real bug found in the process, same pattern as everything else this
audit has surfaced: the `ls` command's own built-in help text claims the
firmware source lives at sector 18; `build.sh` actually places it at
sector 66. `cat 18` shows raw runtime kernel bytes, not source text —
stale documentation baked into the shell's own output.

Status against the six required properties, each backed by the evidence
above rather than asserted: freestanding (verified), CLI-complete
(verified — real serial RX, real string-compare dispatch, computed not
static responses), self-describing (verified — live read-back of own
source), self-hosting (strong alignment between the firmware's own design
statement — "there is no compiler separate from firmware" — and the live
boot trace literally executing a `T_COMPILE_SELF` step, but a full
self-modify-and-reload round-trip via `exec` hasn't been exercised yet),
universally bootable (BIOS/IDE path verified; El Torito CD path and UEFI
path both assembled clean but not yet boot-tested via those specific
paths), fully programmable (substantial real command surface — `exec`,
`xform`, `glyph`, the `seal`/`unseal`/`attest`/`cap` crypto subsystem —
not established as Turing-complete, which is a deliberately lower claim
than "fully").

`hdgl-zero-0.7` (uploaded alongside this, not yet folded into the
package) is a closely related sibling repo: byte-identical
`hdgl_firmware.hdgl` and `hdgl_compiler.hdgl`, same architecture, slightly
different file naming (`hdgl_runtime64.asm` vs `hdgl_router64.asm`).
Confirmed identical in full to its own `old files` archive copy, so
nothing new there beyond confirming the same firmware genome is shared
across both repos.

## Not included here, on purpose

`old files/` (the legacy archive — SQL store, NGINX store, the original
larger Router64 kernel, DNA ENGINE V3, the EZ-by-zCHG MCP predecessor):
confirmed identical across every tag, so it isn't duplicated here. It's
documented in the prior versions rather than re-shipped
as another zip-of-zips.

README.md - Every folder has a
different one (8 distinct versions across 9 states, never reused), and
none of them are source. Available on request if you want the prose
history rather than the code history.

# Table of node.js addition (zip folder only)

| File                 | Folder                                | Subfolder |
| -------------------- | ------------------------------------- | --------- |
| hdgl_router64.asm    | 06-router64-universal-VERIFIED-BOOTED | src/      |
| hdgl_router64.bin    | 06-router64-universal-VERIFIED-BOOTED | bin/      |
| hdgl_router64.img    | 06-router64-universal-VERIFIED-BOOTED | bin/      |
| hdgl_mbr.bin         | 06-router64-universal-VERIFIED-BOOTED | bin/      |
| BOOTX64.EFI          | 06-router64-universal-VERIFIED-BOOTED | uefi/     |
| README-npm.md        | 06-router64-universal-VERIFIED-BOOTED | (root)    |
| analog-container.mjs | 02-analog-radio-extension             | (root)    |
| npm-bridge.mjs       | 02-analog-radio-extension             | (root)    |
