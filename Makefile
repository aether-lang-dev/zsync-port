# SPDX-FileCopyrightText: 2026 Colin Phipps <cph@moria.org.uk>
# SPDX-License-Identifier: Artistic-2.0
#
# Build + test driver for the pure-Aether zsync port. Uses the installed
# toolchain on PATH; override AE/AETHERC to test another installation.
# No aeb dependency — plain make + ae.

AE      ?= ae
AETHERC ?= aetherc
CFLAGS_AE := $(shell $(AE) cflags)
CC      ?= cc

# Sidecar C objects (positional I/O shim).
C_OBJS := rcksum/fileio.o

# Imported modules affect each whole-program compile, too.
AE_SOURCES := $(wildcard rcksum/*.ae zsync/*.ae cmd/*.ae test/*.ae)

# The shim builds counted strings (aether_string.h), so it compiles with
# `ae cflags`' include flags, less the link-only words.
CFLAGS_SHIM := $(filter-out -L% -l% -pthread,$(CFLAGS_AE))

%.o: %.c
	$(CC) $(CFLAGS_SHIM) -c $< -o $@

# Run a single .ae test through `ae run`, linking sidecar C via the
# build pipeline. For tests that need the C shim we go through aetherc
# + cc; pure-Aether tests can use `ae run` directly.

.PHONY: test test-pure test-fileio clean

# Pure-Aether leaf tests (no sidecar C needed).
test-pure:
	$(AE) run rcksum/ranges_test.ae
	$(AE) run rcksum/checksums_test.ae

# Tests that link the C shim: compile .ae -> .c, then cc with the shim.
# $(AETHERC) emits a .c next to the source; we compile+link it with the
# sidecar object and the aether runtime.
# Generic rules: build any *_test.ae that needs the C shim, then run it.
build/%: rcksum/%.ae $(C_OBJS) $(AE_SOURCES)
	@mkdir -p build
	$(AETHERC) $< build/$*.c
	$(CC) build/$*.c $(C_OBJS) $(CFLAGS_AE) -o $@

build/%: cmd/%.ae $(C_OBJS) $(AE_SOURCES)
	@mkdir -p build
	$(AETHERC) $< build/$*.c
	$(CC) build/$*.c $(C_OBJS) $(CFLAGS_AE) -o $@

build/%: zsync/%.ae $(C_OBJS) $(AE_SOURCES)
	@mkdir -p build
	$(AETHERC) $< build/$*.c
	$(CC) build/$*.c $(C_OBJS) $(CFLAGS_AE) -o $@

test-fileio: build/fileio_test
	./build/fileio_test

test-rcksum: build/rcksum_test
	./build/rcksum_test

test-control: build/control_test
	./build/control_test

test-state: build/download_test
	./build/download_test

test-mk: build/zsyncmake_test
	./build/zsyncmake_test

test-clientlib: build/clientlib_test
	./build/clientlib_test

test: test-pure test-fileio test-rcksum test-control test-state test-mk test-clientlib

# The three shipping binaries.
bins: build/zsync build/zsyncmake build/fileserver build/server_dsl_example

# Full end-to-end integration test (client + native server over HTTP).
itest: bins
	./itest/run.sh

# TLS integration test: --no-check-certificate vs a self-signed HTTPS
# server (needs openssl + python3).
itest-tls: bins
	./itest/run_tls.sh

# Proxy integration test: --proxy routes through a forward proxy
# (needs python3).
itest-proxy: bins
	./itest/run_proxy.sh

# Conditional-GET test: fileserver Last-Modified/304 + client -k reuse
# (needs curl).
itest-304: bins
	./itest/run_304.sh

# -u base URL resolving a relative URL in a local .zsync (needs python3).
itest-localu: bins
	./itest/run_localu.sh

# All integration tests (each needs network; run sandbox disabled).
itest-all: itest itest-tls itest-proxy itest-304 itest-localu

clean:
	rm -f rcksum/*.o build/*
