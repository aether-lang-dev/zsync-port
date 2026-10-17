# zsync 0.7.1

zsync is a file transfer program. It allows you to download a file from a
web server, where you have an older version of the file on your computer
already. zsync downloads only the new parts of the file. It uses the same
algorithm as rsync.

zsync does not require any special server software or a shell account on the
remote system at download time (rsync requires that you have an rsh or ssh
account, or that the remote system runs rsyncd). Instead, it uses a control
file - a .zsync file - that describes the file to be downloaded and enables
zsync to work out which blocks it needs. This file can be created by the admin
of the web server hosting the download, and placed alongside the file to
download - it is generated once, then any downloaders with zsync can use it.
Alternatively, anyone can download the file, make a .zsync and provide it to
other users.

The zsync web site is at http://zsync.moria.org.uk/ .

## Implementation

This zsync is written in [Aether](https://github.com/aether-lang-org/aether)
— a systems language that compiles to C. The rsync rolling-checksum engine,
the `.zsync` control-file format, the HTTP-range downloader and a native
test file server are all pure Aether (with a small Artistic-licensed C shim,
`rcksum/fileio.c`, for positional file I/O byte buffers). It uses Aether's
`std.http` client + server and `std.cryptography` (MD4/SHA-1). It does not
depend on Aether's MIT-licensed test/build tooling (aeocha/aeb) — the whole
thing stays under the Artistic License 2.0.

### How this port was made

The Go → Aether port was done by **Claude (Anthropic), Opus 4.8**, in a
single session of **roughly four hours**, with only a few points of human
arbitration from **Paul Hammant** (chiefly: deciding to file the MD4 gap
upstream rather than inline it, choosing full CLI flag-parity before
deleting the Go, and the branch/credit layout you're reading now). The work
was methodical and parity-gated — built leaf-first, every module verified
byte-exact against the original Go before the next was written, and three
gaps it surfaced in Aether's standard library (#637, #640, #641) were filed
and landed upstream along the way. The pristine original lives on the
`legacy_golang` branch; this `main` branch is the Aether port.

This is a derived work: the rsync algorithm, the `.zsync` format, the
optimisations, and the original C/Go implementation are the work of the
authors credited below, under their chosen licence (Artistic 2.0), which
this port preserves unchanged. The port adds nothing to the copyright or
licensing — it stands on their work.

## Installation

zsync is free software. There is no implied support, no implied fitness for
purpose, no warranty. You use it at your own risk. See the included LICENSE for
details.

To build zsync you need the Aether toolchain (`ae` / `aetherc`) on your PATH,
then run the shared bootstrap below. It also installs aeb; zsync’s
Makefile itself only needs ae, aetherc, make and a C compiler.

Install the released toolchain with the shared, version-pinned installer
(the same bootstrap used by selaenium):

```sh
curl -fsSL https://raw.githubusercontent.com/aether-lang-dev/aeb/main/get.sh \
  | AE_PIN=0.824.0 AEB_REF=v0.324 sh
export PATH="$HOME/.local/bin:$PATH"
```

This ensures `ae` 0.824.0 or newer and installs `aeb` v0.324 under
`~/.local` (`PREFIX` overrides it), using release binaries where available.
These bootstrap versions are separate from the project's minimum API floor.

```shell
make bins        # builds ./build/zsync, ./build/zsyncmake, ./build/fileserver
make test        # runs the unit suite (ranges, rcksum, control, download, ...)
make itest       # full client+server round-trip over HTTP (Range/206)
```

You can use `zsync` and `zsyncmake` without installing them. If you want to
install them then, as root, run:

```shell
install build/zsync build/zsyncmake /usr/local/bin
install -D man/zsync.1 man/zsyncmake.1 /usr/local/man/man1/
```

### Source layout

- `rcksum/` — the rolling-checksum engine: `ranges` (block-range set),
  `checksums` (Adler rsum + MD4), `rcksum` (hash tables + the rolling
  matcher + scatter-write), and `fileio.{ae,c}` (the positional-I/O +
  byte-buffer + RFC1123Z C shim).
- `zsync/` — `control` (the `.zsync` control-file parser) and `download`
  (the high-level download State; named `download` because `state` is a
  reserved word in Aether).
- `cmd/` — `zsync` (client), `mklib`+`zsyncmake` (generator), `clientlib`
  (the HTTP fetch core), and `fileserver` (a native Range-aware test server).
- `test/` — a tiny in-tree assert harness (Artistic-2.0, no aeocha/aeb dep);
  `itest/run.sh` — the end-to-end integration test.

## Status & handoff

This is a **complete, working port of the core protocol**. Everything is
parity-tested against the original Go implementation:

- `make test` → 7 test files, 94 assertions, all green. Reference values
  (rsums, MD4s, reconstructed-file SHA-1s) are generated from the actual Go
  code and asserted byte-exact, including the uint16-wraparound and MD4
  truncation edge cases.
- `make itest` → full round trip: `zsyncmake` builds a `.zsync`, the native
  `fileserver` serves it, the `zsync` client fetches the `.zsync` over HTTP,
  seeds from a deliberately-altered partial copy, fetches the missing blocks
  via HTTP `Range` requests (206), and the reconstructed output is
  byte-identical to the original (SHA-1 verified).
- `zsyncmake` output is **byte-for-byte identical** to the Go tool, MTime
  header included.

### Interoperability with the original C/Go zsync

Verified by building the Go `zsync`/`zsyncmake` from this repo's pre-port
history and cross-testing every meaningful combination. (Note: classic
zsync has **no dedicated server** — the "server" is any plain HTTP host that
supports `Range`. So "their server" means a standard Range-capable HTTP
server; our `fileserver` is one such, and is also handy for tests.)

| Case | Result |
|------|--------|
| `.zsync` produced by Aether `zsyncmake` vs Go `zsyncmake` | **byte-identical** — full format interop both directions |
| Aether client → Aether `fileserver` | OK (this is `make itest`) |
| **Go client → Aether `fileserver`** | **OK** — Go zsync downloads + verifies through our server |
| **Aether client → standard Range-capable HTTP server** | **OK** — works against any conformant host (Apache/nginx/…) |
| Either client → an HTTP server that ignores `Range` (returns 200) | both fail **identically** ("expected 206/partial content, got 200") — not an Aether issue; the host must support `Range` |

Bottom line: the two implementations are **wire-compatible** — same `.zsync`
format, same HTTP `Range`/`206` protocol. You can mix and match clients,
generators, and servers freely.

### Serving zsync downloads alongside other endpoints (shared listener)

The bundled `fileserver` owns its own listener for convenience, but the
file-serving is just an ordinary `std.http` handler — **it does not need a
dedicated listener.** You can register it as one route among many on a
single `http.server_create(port)`. The dispatcher walks routes in
registration order and takes the first match (`http_route_matches`: exact
match first, then `*` wildcard), so register specific routes before a
catch-all. Verified working — an API route and zsync file-serving on one
port:

```aether
server = http.server_create(port)
http.server_set_host(server, "127.0.0.1")
http.http_server_get(server, "/api/status", api_handler, null)     // specific first
http.http_server_get(server, "/files/*",   files_handler, ctx)     // catch-all serve_static
http.server_start(server)
```

`/api/status` returns JSON; `/files/iop.dat` serves the file with full
`Range`/`206` support; a real `zsync` client downloads through the `/files/`
route while the API route stays live on the same port.

**One gotcha for sub-path mounts:** `http.serve_static(req, res, base)` maps
the *full* request path under `base` — it does **not** strip the route
prefix. So a request for `/files/iop.dat` looks for `base/files/iop.dat`,
not `base/iop.dat`. Either mirror the directory layout (put files under
`base/files/`) or have the handler strip the prefix before calling
`serve_static`. (Mount at `/*` and there's nothing to strip.) The
closure-DSL below handles this for you.

### The whole server as a closure-DSL — "config IS code"

Every server-side feature composes in a single trailing block, the way
[`aether/docs/closures-and-builder-dsl.md`](https://github.com/aether-lang-org/aether/blob/main/docs/closures-and-builder-dsl.md)
intends: the `.ae` file **is** the config — no YAML, no separate parser —
and the block body is still full Aether (env lookups, conditionals, loops).
`cmd/serverdsl.ae` provides the surface; `cmd/server_dsl_example.ae` is a
runnable entry point. This is the verified, working shape:

```aether
import cmd.serverdsl

main() {
    srv = serverdsl.zsync_server("127.0.0.1", 8080) {
        serve("/files", "/var/www/downloads")   // zsync downloads (full Range/206)
        serve("/pub",   "/srv/public")           // a second mount
        health("/healthz")                       // 200 "ok" liveness probe
    }
    serverdsl.run(srv)                           // start the accept loop
}
```

`zsync_server(host, port)` creates the listener and pushes it on the
builder context stack; each `serve(...)` / `health(...)` call inside the
block auto-receives it via the `_ctx: ptr` convention (no parent-child
wiring), registers its route, and `run(srv)` starts accepting. `serve`
strips the route prefix and answers `Range` requests with `206` +
`Content-Range` (with a `..` traversal guard), so a real `zsync` client
downloads straight through it.

Because the block is ordinary Aether, the "config" can be dynamic
(`os_getenv` comes from `import std.os (*)`):

```aether
dir = os_getenv("ZSYNC_DIR")
if string_equals(dir, "") == 1 { dir = "/srv/zsync" }

srv = serverdsl.zsync_server("0.0.0.0", port) {
    serve("/files", dir)
    if string_equals(os_getenv("ENABLE_HEALTH"), "") == 0 {
        health("/healthz")
    }
}
```

Verified end-to-end: build with `make bins` (produces
`build/server_dsl_example`), and a `zsync` client downloads a file through
the DSL-composed server, byte-identical, with the health endpoint live on
the same port.

> Note: today `serve`/`health` are *immediate* `_ctx`-injected DSL calls.
> They could also be expressed as Aether `builder` functions (the
> `builder name(...) with <factory>` flavour) if you wanted per-route
> sub-blocks (e.g. `serve("/files") { dir("…"); max_age(3600) }`) — a
> natural next step for whoever extends this.

Porting it surfaced three gaps in Aether's stdlib, all filed and now landed
in Aether ≥ 0.218 — see
[aether-lang-org/aether](https://github.com/aether-lang-org/aether):
**#637** (MD4 + binary digest output), **#640** (`std.fs` positional I/O —
`pwrite`/`pread`/`ftruncate`/`fsync`), **#641** (`std.http` `serve_static`
honouring the `Range` header → 206/Content-Range).

### What is NOT yet ported (for whoever takes this over)

The omissions below are all correctness-preserving — they're performance,
deployment, and UX/defensive polish, not algorithm gaps. Roughly in
priority order:

**Performance**
1. **HTTP/2.** Go set `ForceAttemptHTTP2`. `std.http.client` is HTTP/1.1-only
   today (no client-side ALPN or h2 framing — nghttp2 is wired into the
   *server* only), so this is an **upstream gap**, not a port choice. Low
   value here: HTTP/1.1 works, and parallel range fetch (3 concurrent
   connections) already captures most of what h2 multiplexing would buy.

*(Done: **parallel range fetching** — like Go's `errgroup` + `SetLimit(3)`,
the client now fetches up to 3 ranges concurrently. The HTTP fetches run in
Fetcher actors; the submit back into the single-threaded reconstruction State
stays serialised inside one Coordinator actor's mailbox. See
`fetch_remaining_parallel` in `cmd/zsync.ae`.)*

**Real-world hosting / networking**
8. **Multi-URL failover ordering.** Go picked URLs randomly (`rand.Intn`) and
   marked failed ones; this port tries them in **deterministic order**
   (intentional — friendlier for tests — but a behaviour difference).

*(Done: **`--no-check-certificate`** — skips TLS peer/hostname verification
for hosts with self-signed or untrusted certs, via `client.set_insecure`
(aether#1012), applied per-connection to the control fetch and every range
fetch. `make itest-tls` proves both directions: the cert is *rejected* without
the flag and the download succeeds with it.)*

*(Done: **HTTP proxy** — `--proxy URL` pins an explicit forward proxy;
`--no-proxy` forces direct; by default the client follows `$HTTP_PROXY`/
`$HTTPS_PROXY`/`$NO_PROXY` (Go-compatible), via `client.use_env_proxy` /
`use_http_proxy` (aether#1012). `make itest-proxy` routes a full download
through an inline forward proxy and checks the proxy's request log.)*

*(Done: **`-k` resume via server `304`** — `fileserver` now emits
`Last-Modified` and answers a satisfied `If-Modified-Since` with `304`. The
client stamps the saved `-k` copy's mtime from `Last-Modified`, so a rerun
sends an accurate IMS and reuses the local copy on `304`. `make itest-304`
exercises the full round trip.)*

*(Done: **`-u` base URL + relative-URL `.zsync`** — `zsyncmake` with no `-u`
now emits a relative `URL:` (the input basename, like Go), and the client's
`-u <base>` resolves such a relative URL when the `.zsync` is a local file.
`make itest-localu` proves a local `.zsync` fails without `-u` and downloads
with it.)*

*(Done: **`-A` per-host auth map** — like Go's `authMap`, `-A host=user:pass`
is repeatable and each credential is applied only to requests whose host
matches (control-file fetch and every range fetch resolve auth from the URL's
host). Malformed specs warn and are skipped. See `auth_map_*` / `basic_auth_for`
/ `host_of` in `cmd/clientlib.ae`, unit-tested in `cmd/clientlib_test.ae`.)*

**Finalisation / UX / defensive polish**
9. **Old-file backup on completion.** Go renamed an existing target to
   `<name>.zs-old` (hardlink, falling back to rename) before writing the new
   file; this port overwrites in place. (Design divergence: the port
   reconstructs *in place*, using the existing target as a seed — the old
   bytes are consumed during reconstruction, so a pre-write `.zs-old` backup
   doesn't fit the model the way it does Go's temp-file-then-rename.)
*(Done: **restore mtime on the finished file** — like Go's `os.Chtimes`, the
client stamps the downloaded file's mtime from the `.zsync` `MTime` header
(best-effort; warns, never fatal). `fileio.set_mtime` (a `utimes` shim, since
std.fs reads but can't set mtime) + `fileio.parse_rfc1123`.)*

*(Done: **least-surprise / anti-traversal filename check** — like Go's
`getFilename`, a server-controlled `Filename:` header is never trusted
verbatim when no `-o` is given: any path component is stripped (so
`../../etc/passwd` → `passwd`) and the stripped name is accepted only if it
shares the source basename's alphanumeric prefix, else it's rejected and the
source-derived name is used. `resolve_output_name` / `base_name` /
`source_prefix` in `cmd/clientlib.ae`, unit-tested (incl. traversal cases) in
`cmd/clientlib_test.ae`.)*

*(Done: **`checkSuppliedFilename` `-k` guard** — like Go, zsync refuses to
overwrite an existing non-`.zsync` file with the control-file copy unless its
first bytes are `zsync:` (catches `-k` aimed at the wrong file before any
write). `check_supplied_filename` in `cmd/clientlib.ae`, unit-tested.)*
*(Done: **live progress meter** — like Go, a `\r`-overwritten
`<elapsed>s <rate> <pct>% of target obtained` line is printed to stderr as
ranges arrive (from the fetch Coordinator, so it works with the parallel
fetch), suppressed by `-q`. `clientlib.progress_line` (integer-only
formatting, unit-tested), `std.os.now_monotonic_ms` for elapsed/rate.)*

*(Done: **`zsyncmake` stdin input** — with no file argument `zsyncmake` now
reads the input from stdin and writes the `.zsync` to stdout (or `-o`/`<-f>.zsync`
if given). stdin has no mtime, so the `MTime:` header is simply omitted —
which also *fixes* the real Go bug where the stdin path dereferenced a nil
`FileInfo.ModTime()` and panicked. Byte-identical to the file path modulo
MTime; an itest step asserts this.)*

## Use

In its simplest form, as an end-user:

```sh
zsync https://cdimage.ubuntu.com/ubuntu/daily-live/current/resolute-desktop-amd64.iso.zsync
```

Someone has to make a .zsync file for the download before you can use zsync.

You have to have an older version of the file around - or a related file that
contains a lot of the same content - otherwise there is little point in using
zsync. zsync normally looks in the current directory for a file of the same
name as the one being downloaded to use as a source of data. If your local
older copy is in a different directory, or you have another file with relevant
source data for zsync to use, you can specify it with -i.

## Offering zsync downloads

Simple example:
Suppose you have `http://example.com/dl/some-image-0.2.iso` ; which is in
`/var/www/downloads/` on your server.

```sh
cd /var/www/downloads/
zsyncmake some-image-0.2.iso
```

This creates some-image-0.2.iso.zsync in the same directory, a zsync control
file. A zsync user can then download the original file by running:

```sh
zsync -i some-image-0.1.iso http://example.com/dl/some-image-0.2.iso.zsync
```

Users still need to access to the full download file - zsync merely allows then
to save time by only downloading parts of the file.) A user with v0.1 of the
same file can now use zsync to download only the new bits.

By default zsyncmake will include a relative URL in the zsync control file, so
the client program will access the full file from the same server and directory
as the .zsync file. You can instead specify a URL to include with `-u` if you
are putting the .zsync file in a different directory or on another server.

## Feedback, Support

The original C/Go zsync lives at https://github.com/cph6/zsync/ (Colin Phipps,
cph@moria.org.uk).

This Aether port is maintained as part of the Aether ecosystem; see the
"Status & handoff" section above for what's done and what's left, and the
linked aether-lang-org/aether issues (#637, #640, #641) for the stdlib work
it drove.

## Copyright, Author, Acknowledgements

zsync is based on the rsync algorithm, by Andrew Tridgell. It also incorporates
a number of optimisations, based on ideas in academic papers by Utku Irmak,
Svilen Mihaylov and Torsten Suel (primarily "Improved Single-Round Protocols
for Remote File Synchronization", Sept 2004).

zsync is copyright 2004,2005,2007,2009,2025,2026 Colin Phipps <cph@moria.org.uk>.
zsync is made available under the Artistic License 2.0 - see the file LICENSE
for details.

Also, thanks to Dennis Schridde, Timothy Lee, Richard Kiss, Érsek László, James
Montgomerie, James Antill, saul@alien-science.org, Kent Mein, Marc Lehmann,
Robert Lemmen, Mark Adler, Ricardo Correia, Karl Kalleberg, Michael Stone,
Richard Lucassen, Duncan Mac-Vicar, Jari Aalto, Marcin Mirosław, Jan Varho,
Loïc Minier, Gian Merlino and S Page for useful feedback and bug reports for
previous versions.

