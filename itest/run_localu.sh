#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Colin Phipps <cph@moria.org.uk>
# SPDX-License-Identifier: Artistic-2.0
#
# `-u` (base URL for a local .zsync) integration test. A .zsync generated
# without -u carries a *relative* URL (the input basename, like Go). When
# the client is handed such a .zsync as a LOCAL FILE, it needs a `-u` base
# to turn that relative URL into a fetchable one.
#
#   1. WITHOUT -u: local .zsync + relative URL -> resolves to a local path,
#      which is not fetchable -> failure.
#   2. WITH -u <base>: relative URL resolves against the base and the
#      download succeeds + reconstructs byte-identical.
#
# Needs python3 + network; run with the sandbox disabled.

set -u
HERE="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$HERE/build"
PORT=${PORT:-8099}
WORK="$(mktemp -d)"
trap 'kill -9 "$SRV" 2>/dev/null; rm -rf "$WORK"' EXIT
fail() { echo "FAIL: $1"; exit 1; }

python3 - "$WORK/data.bin" <<'PY'
import sys
s=9; out=bytearray()
for i in range(15000):
    s=(s*1103515245+12345)&0x7fffffff
    out.append((s>>16)&0xff)
open(sys.argv[1],'wb').write(bytes(out))
PY
mkdir -p "$WORK/srv"; cp "$WORK/data.bin" "$WORK/srv/data.bin"

# Generate a .zsync with NO -u -> a relative "URL: data.bin".
( cd "$WORK/srv" && "$BIN/zsyncmake" -b 1024 -o data.bin.zsync -f data.bin data.bin ) 2>/dev/null \
    || fail "zsyncmake"
grep -aq '^URL: data.bin' "$WORK/srv/data.bin.zsync" || fail "expected a relative 'URL: data.bin'"
cp "$WORK/srv/data.bin.zsync" "$WORK/local.zsync"

python3 - "$WORK/data.bin" "$WORK/seed.bin" <<'PY'
import sys
d=bytearray(open(sys.argv[1],'rb').read())
for blk in range(0,15,2):
    o=blk*1024
    for i in range(o, min(o+1024,len(d))): d[i]^=0x5a
open(sys.argv[2],'wb').write(d)
PY

nohup "$BIN/fileserver" "$PORT" "$WORK/srv" >"$WORK/srv.log" 2>&1 & SRV=$!
sleep 1

# 1. without -u: the relative URL resolves to a local path -> not fetchable.
if "$BIN/zsync" -q -o "$WORK/o1.bin" -i "$WORK/seed.bin" "$WORK/local.zsync" >"$WORK/error.log" 2>&1; then
    fail "download unexpectedly succeeded without -u"
fi
# The resolver's verdict on the local path differs by platform (macOS:
# connection failed; glibc: could not resolve host). Either way the whole
# line must arrive intact, which is what this checks.
grep -Exq 'download failed: (connection failed|could not resolve host)' "$WORK/error.log" || fail "download error did not survive the worker message lifetime"
echo "PASS: local .zsync with a relative URL is not fetchable without -u"

# 2. with -u: resolves against the base and downloads.
"$BIN/zsync" -q -u "http://127.0.0.1:$PORT/" -o "$WORK/o2.bin" -i "$WORK/seed.bin" \
    "$WORK/local.zsync" || fail "download failed with -u"
cmp -s "$WORK/data.bin" "$WORK/o2.bin" || fail "output differs from original"
echo "PASS: -u resolves the relative URL in a local .zsync; reconstructed byte-identical"
