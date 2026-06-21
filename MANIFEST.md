# HDGL — Stand-Alone Source Tree (deduplicated, v0.1 → golden-dome)

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

## Not included here, on purpose

`old files/` (the legacy archive — SQL store, NGINX store, the original
larger Router64 kernel, DNA ENGINE V3, the EZ-by-zCHG MCP predecessor):
confirmed identical across every tag, so it isn't duplicated here. It's
documented in the prior turn of this conversation rather than re-shipped
as another zip-of-zips.

`README.md` per tag: deliberately excluded — every single tag has a
different one (8 distinct versions across 9 states, never reused), and
none of them are source. Available on request if you want the prose
history rather than the code history.

`fabric-v0.5.zip` / `fabric-v0.2.zip`: inert snapshots, contents already
represented by the deduplicated files above.
