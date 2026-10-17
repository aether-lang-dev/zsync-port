# Notes to self (LLM on the Aether zsync port)

Not a CLAUDE.md. Skinny, opinionated, for a sibling Claude picking this up
mid-task. Re-read at session start.

## What this is

Pure-Aether port of zsync (rsync-over-HTTP: download a file fetching only
the changed blocks). ~3.4k lines Aether + one small C shim. Ported by a
sibling Claude in ~4h, parity-gated against the original Go. Artistic-2.0 (NOT
MIT). Keep zsync's import graph free of aeocha/aeb *modules* — but that's an
import-graph rule, NOT an artifact-license one: a build TOOL (aeb, like make or
gcc) doesn't relicense the output, so zsync could be built BY aeb and stay
Artistic. What's forbidden is `import`ing MIT aeb/aeocha modules into Artistic
zsync source — and aeb vendoring zsync *source* the other way. Linking is fine
both ways (Artistic-2.0 §7/§8): aeb may link a zsync `.so`.

- `main` = the Aether port (this). `legacy_golang` = pristine original Go,
  untouched, your parity oracle.
- Origin: `git@github.com:aether-lang-org/zsync-port.git`. `main` is default.

## Build / test — Makefile (ae) OR aeb

**Makefile path:** installed `ae` and `aetherc` on PATH. Override `AE` /
`AETHERC` explicitly for another toolchain. README has the pinned installer.
Verified with 0.823.0 on 2026-10-17 (macOS arm64, aether-linux arm64,
ae-x64 gcc -Werror): eight unit suites, four binaries, and all five
HTTP/TLS/proxy/304/local-URL integration suites. 0.818 is the floor: the
code uses `@keep ptr`. Since 0.818 every string is counted, so the C shim
(`rcksum/fileio.c`) hands back AetherStrings built with
`string_new_with_length`, and its externs say `@heap`. A raw `char*` typed
`string` is copied as a C string when it is bound, and binary block data
would be cut at its first NUL. `fileio.view(s, n)` is the byte[] view for the
slice-taking std calls (digests, fs.write_binary, io.fd_write). Do not
glob-import std.string, std.list or std.map: see
`asks/glob-import-hides-builtin-free.md`. The actor string-state limitation
in `asks/actor-string-state-atomic-load.md` is fixed as of 0.823.0.
Struct boxes use `heap.new`, not hard-coded byte counts.

**aeb path (2026-06):** zsync now also builds with aeb — `aeb cmd/zsync/.build.ae`
(+ zsyncmake/fileserver). aeb compiles each module `aetherc --emit=lib` (stricter
than the Makefile's whole-program compile). Build against the INSTALLED Aether
(`/usr/local/bin/ae`), which has the nested include layout aeb wants — building
against the dev TREE currently hits an aeb include-threading gap (aether_panic.h
not found; filed as aeb asks/aether-program-dev-tree-include-threading.md). aeb
is a build TOOL here — it does NOT relicense the Artistic-2.0 output (see top).
server_dsl_example is pre-broken (DSL closure form) in `make` too — not aeb's fault.

- `make test` → 7 test files, ~94 asserts, all parity-checked vs Go. Green = good.
- `make bins` → build/{zsync,zsyncmake,fileserver,server_dsl_example}.
- `make itest` → full client+server HTTP round-trip (needs network; run with
  sandbox disabled). Prints PASS.
- Pure-Aether leaf tests run via `ae run`; anything touching the C shim goes
  aetherc→cc→link (the Makefile's `build/%` rules do this).
- **Test fixtures are COMMITTED in `test/fixtures/`** (`ctltest.dat` +
  `.zsync`, `go_ref.zsync`, `go_mtime.txt`). control/download/zsyncmake tests
  read them by RELATIVE path (`test/fixtures/…`), so run from the repo root
  (make does). They used to be hand-made in `/tmp` — a wiped `/tmp` made those
  three tests read null buffers and **segfault before any assert printed**
  (looked like a protocol regression; wasn't). If they ever go missing again a
  test now fails readably (`assert.fatal`) instead of crashing. `ctltest.dat`
  is the same LCG the itest uses (seed 12345) → SHA-1 f5d0ac38…, the value
  hardcoded in control/download tests. Regenerate: `test/fixtures/regen.sh`
  (needs `go` + the `legacy_golang` branch).
- Regenerate a Go oracle: `git archive legacy_golang | tar -x -C /tmp/go &&
  cd /tmp/go && go build -buildvcs=false -o /tmp/go_zsync ./cmd/zsync` (etc).

## Layout

- `rcksum/` — the engine. `ranges` (block-range set), `checksums` (Adler
  rsum + MD4 + rhash + log2), `rcksum` (hash tables + bithash + rolling
  matcher + scatter-write), `fileio.{ae,c}` (the C shim: positional I/O,
  mutable byte buffers, dup16, slice/pad, RFC1123Z).
- `zsync/` — `control` (.zsync parser), `download` (high-level State; named
  `download` because `state` is reserved).
- `cmd/` — `zsync` (client) + `clientlib`; `zsyncmake` + `mklib` (generator);
  `fileserver` (native Range server); `serverdsl` + `server_dsl_example`
  (the config-IS-code closure-DSL surface).
- `test/assert.ae` — tiny in-tree harness (`new/ok/eq_int/eq_str/report`,
  exits 1 on fail). `itest/run.sh` — e2e.

## Aether gotchas that WILL bite (all learned the hard way here)

- **Call forms.** Raw externs: glob-import + unqualified — `import std.list (*)`
  then `list_new()`. Tuple-returning wrappers: import plain + qualified —
  `import std.cryptography` then `cryptography.md4_bytes(...)`. Getting it
  backwards gives "Undefined function 'list.list_new'" or a broken tuple
  destructure. Often you want BOTH imports of one module in a file.
- **Modules ref'd by LAST path component:** `import rcksum.ranges` → call
  `ranges.count()`, NOT `rcksum.ranges.count()` (→ "Undefined function '?'").
- **`as` only targets `*Struct` / `fn(...)` / `T[]`.** No `as ptr`, `as int`,
  `as string`. Scalars: rely on implicit int↔long widen, or a passthrough fn
  `f2i(x: float) -> int { return x }` for float→int. ptr↔string: just return
  it (both char* in C). Member access on a parenthesized cast can mis-parse —
  bind to a temp first (`s = z as *Foo; return s.x`).
- **Reserved words bite module FILENAMES too:** `state`, `match`, `message`,
  `after`. `import pkg.state` fails at the import line even if state.ae is
  trivial. (That's why it's `download.ae`.)
- **Top-level fn names must be globally unique across ALL transitively
  imported modules** — even called qualified. `state.new` vs `rcksum.new`
  collided → misleading "Expected actor, struct, function, or main" at the
  import line. Name port fns distinctly. Also watch collisions with stdlib
  glob symbols (`to_int` clashed → renamed `parse_num`).
- **`string_substring` / `string_concat` are strlen-based — TRUNCATE at the
  first NUL.** Useless on binary (blocks, digests). Use `fileio.slice` /
  length-explicit ops everywhere binary flows. `string_char_at_n(s, len, i)`
  reads binary fine (explicit length).
- **Heap-string tracker recycles AetherStrings stored in hand-malloc'd
  struct fields** — even after `string_retain`. Caused a deterministic +2
  shift in stored block MD4s (int fields fine, only the string field). Fix:
  for long-lived bytes in untracked structs, return a raw malloc'd char*
  typed as string from C (NOT via string_new_with_length) — no magic header,
  tracker leaves it alone. See `zsync_dup16`/`fileio.dup16`.
- **Don't `extern pwrite` (etc.) directly** — Aether emits a clashing
  prototype vs glibc. Sidecar C with uniquely-named wrappers (`zsync_io_*`),
  extern those. `_GNU_SOURCE` for strptime/timegm.
- **`@extern("c_name") fn(...)` — no `extern` keyword after the annotation.**
  And `@extern`-defined fns aren't reachable cross-module as `mod.fn`; wrap
  in a normal fn if another module needs them.
- **Parity tests need non-degenerate data.** First fixture `(i*37+11)%256`
  made every 1024-block byte-identical (period 256) and masked a real bug.
  Use an LCG/PRNG.

## std.http facts relevant here

- Server route dispatch: registration order, first match wins.
  `http_route_matches` = exact strcmp, then `*` wildcard. Register specific
  routes before a `/*` catch-all.
- `serve_static(req,res,base)` honours Range → 206 (#641) AND has a `..`
  traversal guard, but maps the FULL request path under base (no prefix
  strip). `serve_file(res,path)` takes no request → **ignores Range** (always
  200). So under a route prefix you must either mirror dirs or implement
  Range yourself — `serverdsl.serve_one` does the latter (parse Range, slice
  via fileio, 206 + Content-Range, own `..` guard).
- Client v2 (`std.http.client`): `request(m,url)` → `set_header` →
  `send_request` → `response_status`/`response_body_length`/`response_body`.
  Range = `set_header(req,"Range","bytes=a-b")`, expect 206. TLS verification
  skip and proxy controls are available and wired here via
  `client.set_insecure`, `client.use_env_proxy`, `client.use_http_proxy`, and
  the direct default (`cmd/clientlib.ae::apply_net_opts`).

## Upstream stdlib gaps this port filed (all landed ≥0.218)

#637 MD4 + binary digests · #640 std.fs positional I/O
(pwrite/pread/ftruncate/fsync) · #641 serve_static Range. If you hit a new
gap: file it on aether-lang-org/aether with a concrete spec (API shape,
call-site census, rationale) — that flow turns around same-day. Keep a
self-contained workaround in-tree meanwhile (the shim model).

- **#1012 client TLS-verify-skip + forward-proxy — LANDED (set_insecure 0.354,
  proxy 0.356), now WIRED IN.** cmd/clientlib.ae has a NetOpts struct
  {insecure, proxy_mode, proxy_url} + apply_net_opts(req, o) applied right after
  every client.request(); threaded from main through get_control and the
  fetch/Coordinator/FetchJob path (net: ptr in the message + coord state). CLI:
  `--no-check-certificate` → set_insecure; `--proxy URL` → use_http_proxy;
  `--no-proxy` → direct; DEFAULT = use_env_proxy (Go-compatible $HTTP_PROXY).
  Tested by make itest-tls (self-signed HTTPS: rejected w/o flag, works with)
  and itest-proxy (traffic verified through an inline forward proxy). The
  aether client is HTTP/1.1-only still (no client ALPN/h2) — HTTP/2 is an
  upstream gap, low value given parallel HTTP/1.1 fetch.

- **#644 streaming HTTP request bodies — LANDED ≥0.347 (PR #990), NO consumer
  in zsync yet.** Filed as the future-blocker for a zsync *write/upload*
  receive-path (multi-MB uploads without buffering the whole body in RAM).
  zsync has no such path today — the servers (`fileserver`, `serverdsl`) are
  Range *download* servers, and `fileio.c`'s `pwrite` is the client's local
  scatter-write, not HTTP. So this closed as pure bookkeeping; wire it in only
  if/when an upload receiver is built. Then: handler dispatches at
  headers-complete once the body > 16 KiB; loop `http.request_body_read(req,
  off, 65536)` (blocks on the wire), terminate on
  `http.request_body_complete(req)`, `fileio`/`fs.pwrite` each window to disk.
  `http.request_body(req)` still works whole-body (materializes on demand) but
  DON'T mix it with `request_body_read` on the same req (returns ""). Chunked
  `Transfer-Encoding` request bodies remain unsupported upstream. Lineage:
  #626 asked, #642 shipped the API surface, #644 did the parse-loop reshape.
  (Still NOT landed: TLS-verify-skip toggle → `--no-check-certificate` stays a
  no-op.)

## What's NOT done (full list + priority in README "What is NOT yet ported")

Core protocol is complete + byte-verified + interop-tested both directions.
Outstanding = HTTP/2 (UPSTREAM GAP — client is HTTP/1.1-only, no ALPN/h2; low
value given parallel HTTP/1.1 fetch) and random URL failover (intentional
divergence — deterministic order for tests). (old-file backup is a documented
design divergence — in-place reconstruction consumes the old bytes as seed, so
no pre-write `.zs-old`.) README has the prioritised list. **Essentially the
whole Go feature set is now ported + tested.** What's left is HTTP/2 (upstream
gap) or the two deliberate divergences.

- **DONE: `-k` server 304 + `-u` for local .zsync + zsyncmake relative URL.**
  (1) fileserver.ae now emits Last-Modified and answers a satisfied
  If-Modified-Since with 304 (resolves base+request_path with a `..` guard,
  stats mtime, compares to IMS via fileio.parse_rfc1123). Client get_control
  stamps the saved -k copy's mtime from the response Last-Modified (returned as
  a 5th value from http_get_auth) so reruns send accurate IMS. (2) `-u URL`
  client flag sets the base for resolving relative URLs (was: always the
  source; useless for a local .zsync). (3) zsyncmake with no -u now emits a
  RELATIVE URL (input basename) + a stderr warning, like Go — without this
  there was no relative-URL .zsync to exercise -u. Tested: make itest-304 /
  itest-localu (and itest-all runs every suite). Server helpers has_dotdot/
  join_path live in fileserver.ae (main module, itest-covered).

- **DONE: live progress meter.** cmd/zsync.ae Coordinator prints a
  `\r`-overwritten `<elapsed>s <KB/s> <pct>% of target obtained` line to
  STDERR on each successful FetchDone (so it works with parallel fetch),
  suppressed by -q; final `\n` in fetch_remaining_parallel. Formatting is
  clientlib.progress_line (integer-only — float→string is awkward; KB/s + 1dp
  elapsed/pct, unit-tested). Elapsed/rate via std.os.now_monotonic_ms (a real
  clock IS available in compiled binaries — the Date.now ban is workflow-only).

- **DONE: zsyncmake stdin.** cmd/zsyncmake.ae: no file arg → read_stdin()
  (loop io.fd_read_n(0, 64k) into a std.bytes growable buf, bytes.finish →
  binary-safe string) and write the .zsync to stdout via io.fd_write_n(1, …,
  len) (length-explicit — the table has NULs). Output goes to stdout only when
  neither -o nor -f is given (else -o / <fname>.zsync, matching Go). stdin has
  no mtime → no MTime header (this is ALSO the fix for Go's stdin panic on
  nil FileInfo.ModTime). -v goes to stderr so it can't corrupt a stdout
  .zsync. itest has a stdin-vs-file parity step (strip MTime with python, NOT
  grep — grep mangles the binary .zsync).

- **DONE: checkSuppliedFilename (-k guard).** cmd/clientlib.ae
  `check_supplied_filename(path)` (called in main before any control write):
  refuse to clobber an existing non-.zsync file unless its first bytes are
  "zsync:" (binary-safe has_prefix_bytes). Returns "" if safe, else an error
  string; main prints + exit(3). Unit-tested (5 branches).

- **DONE: least-surprise / anti-traversal output filename.** cmd/clientlib.ae
  `resolve_output_name(remote, source, quiet)` (+ base_name / source_base /
  source_prefix), called from cmd/zsync.ae when no -o. A server-controlled
  Filename: header is never trusted verbatim: strip path (../../etc/passwd →
  passwd), accept only if it shares the source basename's alnum prefix, else
  reject + use the source-derived name. Unit-tested incl. traversal cases;
  security itest confirms a malicious traversal Filename does NOT escape.

- **DONE: `-A` per-host auth map.** cmd/clientlib.ae `auth_map_new/add` (a
  std.map host->"user:pass"), `basic_auth_for(map, url)` (keys on `host_of(url)`
  = scheme+port+path/query/frag-stripped hostname, matching Go's url.Hostname),
  threaded through cmd/zsync.ae: control fetch + each range fetch resolve auth
  from their URL's host. `-A` is now repeatable (was: last-wins single cred for
  all hosts); malformed specs warn+skip. Unit-tested in cmd/clientlib_test.ae
  (new Makefile test-clientlib target; host_of edge cases + per-host selection).

- **DONE: restore mtime on finished output.** cmd/zsync.ae finalisation stamps
  the output file's mtime from the .zsync MTime header (Go's os.Chtimes),
  best-effort (warns, never fatal). Needed a shim — std.fs reads mtime but
  can't set it: `fileio.set_mtime(path, epoch)` = a `utimes()` wrapper in
  fileio.c (atime=now, mtime=epoch); header→epoch via the pre-existing
  `fileio.parse_rfc1123`. Covered by fileio_test (RFC1123Z parse + set + read
  back) and a network itest that asserts the output mtime == header epoch.

- **DONE: parallel range fetch** (was the highest-value item). `cmd/zsync.ae`
  `fetch_remaining_parallel` + Fetcher/FetchCoordinator actors. Up to 3 ranges
  in flight (const FETCH_CONCURRENCY), mirroring Go's errgroup SetLimit(3).
  Actor gotchas learned: **actors MUST live in the module that owns `main()`**
  — the compiler only emits `spawn_<Actor>` for actors in the main compilation
  unit, so they can't sit in an imported lib (that's why the actors are in
  cmd/zsync.ae, not cmd/clientlib.ae). **State fields can't be type-annotated**
  (`state x: ptr` is a parse error) and infer their type from the initializer,
  so a ptr-valued field must init `= null`, NOT `= 0` (else it's `atomic_int`
  and truncates the pointer — silent 64-bit corruption, only a C warning).
  Buffers cross the mailbox as a raw malloc'd `ptr` (clientlib.http_get_range_raw
  — no heap-string-tracker header, survives the message copy), retyped with
  fileio.buf_as_string and buf_free'd inside the Coordinator. submit_target_data
  stays serialised because only the one Coordinator mailbox calls it. Verified:
  make test green, itest PASS, a 200KB/40-scattered-range stress run byte-
  identical, and the error path (404) fails fast instead of hanging wait_for_idle.

## Credit / license — do not muddy

Derived work. LICENSE untouched (Colin Phipps, Artistic-2.0). Ported `.ae`
keep original `SPDX-FileCopyrightText: ... Colin Phipps` headers. Port claims
no new copyright. Keep it that way.

## Stray

`.golangci.yaml` is leftover Go-lint config, now inert — fine to delete if
you're tidying (ask Paul first; it's harmless).
