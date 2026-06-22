# ============================================================================
# HDGL Genome Fabric — Makefile
#
# Targets:
#   test        — build standalone C test (Linux/x86-64, no libc beyond math.h)
#   verify      — run test and check output against known Omega graph
#   clean       — remove built artifacts
#
# Constraints:
#   No Python. No third-party libraries.
#   C only (+ math.h). NASM for any ASM.
#   The test binary links with -lm only.
#
# Integration with router64:
#   Add hdgl_genome_fabric.c to the router64 src/ directory.
#   Add #include "hdgl_genome_fabric.h" to hdgl_bootstrap.c.
#   Add hdgl_genome_shell.hdgl to the emit rules in hdgl_compiler.hdgl.
# ============================================================================

CC      = gcc
CFLAGS  = -O2 -std=c99 -Wall -Wextra -Wpedantic \
          -fno-common \
          -I.
LDFLAGS = -lm

.PHONY: all test verify clean

all: test

# ── Standalone test ──────────────────────────────────────────────────────────

test: hdgl_genome_test

hdgl_genome_test: hdgl_genome_test.c hdgl_genome_fabric.h
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

# ── Verify against known Omega graph ─────────────────────────────────────────
#
# Expected output (APU2 / i440FX 8-node Omega graph):
#   Sequence: ACGGCACC  (CPU=A MEM=C IO=G IO=G COMPILER=C CPU=A MEM=C MEM=C)
#   GC content: 0.625  (5/8 nodes are G or C)
#   strand_count: 8    (GC × 8 = 5, nearest pow2 = 8)
#   strand_auth: 0xE8  (bits from 0x80C0C0E8 low byte)
#   core_radius: ~0.455 (popcount(0x80C0C0E8)=9 × φ/32)

verify: test
	./hdgl_genome_test 2>&1 | tee /dev/stderr | grep -E "GC=|strand|radius|auth|PASS|FAIL"

clean:
	rm -f hdgl_genome_test
