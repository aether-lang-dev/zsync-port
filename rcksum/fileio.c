/*
 * SPDX-FileCopyrightText: 2026 Colin Phipps <cph@moria.org.uk>
 * SPDX-License-Identifier: Artistic-2.0
 *
 * Positional file I/O shim for the Aether zsync port.
 *
 * std.fs has only whole-file / sequential read+write today, but zsync
 * scatters reconstructed blocks into the output file at arbitrary
 * offsets as they are matched (pwrite) and reads them back (pread).
 * Rather than `extern` libc's pwrite/pread directly — which clashes
 * with glibc's size_t/off_t prototypes when Aether emits its own — we
 * expose uniquely-named wrappers here and extern those from Aether.
 *
 * Uniquely named (zsync_io_*) so there is zero chance of colliding with
 * a libc or stdlib prototype. A stdlib-wish has been filed to add
 * positional I/O to std.fs; this shim is the self-contained stopgap.
 */

/* _GNU_SOURCE for strptime / timegm (and pwrite/pread feature macros). */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include <string.h>

/* Counted strings: every string this shim hands to Aether is an
 * AetherString (Aether 0.818 ownership model, R1). */
#include "aether_string.h"

/* Open for read+write, create if absent, truncate. mode 0644.
 * Returns the fd, or -1 on failure. */
int zsync_io_open_rw_trunc(const char *path) {
    return open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
}

/* Open an existing file read-only. Returns fd or -1. */
int zsync_io_open_ro(const char *path) {
    return open(path, O_RDONLY);
}

/* Write `count` bytes of `buf` at absolute `offset`. Returns bytes
 * written, or -1. Loops to handle short writes. */
long zsync_io_pwrite(int fd, const char *buf, long count, long offset) {
    long total = 0;
    while (total < count) {
        ssize_t n = pwrite(fd, buf + total, (size_t)(count - total),
                           (off_t)(offset + total));
        if (n < 0) return -1;
        if (n == 0) break;
        total += n;
    }
    return total;
}

/* Read up to `count` bytes at absolute `offset`. Returns the bytes as a
 * fresh, length-aware AetherString the caller owns (declared `@heap` on
 * the Aether side, so binary data with NULs survives), and records the
 * byte count for zsync_io_last_read_len(). Returns NULL on error, with
 * the count set to -1. */
static long zsync_io_last_n = 0;
AetherString *zsync_io_pread_alloc(int fd, long count, long offset) {
    char *buf = (char *)malloc((size_t)count + 1);
    if (!buf) { zsync_io_last_n = -1; return NULL; }
    long total = 0;
    while (total < count) {
        ssize_t n = pread(fd, buf + total, (size_t)(count - total),
                          (off_t)(offset + total));
        if (n < 0) { free(buf); zsync_io_last_n = -1; return NULL; }
        if (n == 0) break;   /* EOF */
        total += n;
    }
    zsync_io_last_n = total;
    AetherString *s = string_new_with_length(buf, (size_t)total);
    free(buf);
    return s;
}
long zsync_io_last_read_len(void) {
    return zsync_io_last_n;
}

/* Truncate the file to `length` bytes. Returns 0 on success, -1. */
int zsync_io_ftruncate(int fd, long length) {
    return ftruncate(fd, (off_t)length);
}

int zsync_io_close(int fd) {
    return close(fd);
}

/* --- mutable byte-buffer helpers ---
 * The rcksum core needs a mutable byte array for the bithash table
 * (get / set-bit / test-bit). std.intarr is fixed-size-int only and
 * std has no mutable byte buffer, so expose a minimal one here. */

unsigned char *zsync_buf_alloc(long n) {
    unsigned char *b = (unsigned char *)calloc((size_t)n, 1);
    return b;
}

/* A zeroed, length-aware string of n bytes, owned by the caller. */
AetherString *zsync_buf_alloc_str(long n) {
    char *b = (char *)calloc((size_t)n + 1, 1);
    if (!b) return NULL;
    AetherString *s = string_new_with_length(b, (size_t)n);
    free(b);
    return s;
}

/* Copy the first `n` bytes of a raw byte buffer `b` (zsync_buf_alloc +
 * zsync_buf_set) into a fresh, length-aware string the caller owns. */
AetherString *zsync_buf_to_string(unsigned char *b, long n) {
    return string_new_with_length((const char *)b, (size_t)n);
}

int zsync_buf_get(unsigned char *b, long i) {
    return (int)b[i];
}

void zsync_buf_set(unsigned char *b, long i, int v) {
    b[i] = (unsigned char)(v & 0xff);
}

void zsync_buf_or(unsigned char *b, long i, int v) {
    b[i] |= (unsigned char)(v & 0xff);
}

void zsync_buf_free(unsigned char *b) {
    free(b);
}

/* Identity: re-types a container element that holds a counted string
 * (std.list / std.map store void*) as a string for the Aether side. */
char *zsync_buf_identity(unsigned char *b) {
    return (char *)b;
}

/* Format a Unix epoch time as RFC1123Z in UTC, e.g.
 * "Mon, 02 Jan 2006 15:04:05 +0000" — the exact shape zsync's MTime
 * header uses (Go's time.RFC1123Z formatted as UTC). Returns a fresh
 * string the caller owns, or NULL on failure. std has only an ISO-8601
 * "now" formatter, so this fills the gap for arbitrary epochs. */
AetherString *zsync_rfc1123z(long epoch) {
    time_t t = (time_t)epoch;
    struct tm tmv;
    if (!gmtime_r(&t, &tmv)) return NULL;
    char out[40];
    /* strftime's %z on a gmtime struct may emit "+0000" or be empty
     * depending on libc, so hard-code "+0000" for UTC. */
    size_t n = strftime(out, sizeof out, "%a, %d %b %Y %H:%M:%S +0000", &tmv);
    if (n == 0) return NULL;
    return string_new_with_length(out, n);
}

/* Parse an RFC1123/RFC1123Z date string back to a Unix epoch (UTC).
 * Returns -1 on parse failure. Used for If-Modified-Since round-trips. */
long zsync_parse_rfc1123(const char *s) {
    if (!s) return -1;
    struct tm tmv;
    memset(&tmv, 0, sizeof(tmv));
    /* Try RFC1123Z (with numeric zone) then RFC1123 (GMT). */
    char *r = strptime(s, "%a, %d %b %Y %H:%M:%S", &tmv);
    if (!r) return -1;
    return (long)timegm(&tmv);
}


/* fsync to flush before close/rename. Returns 0 / -1. */
int zsync_io_fsync(int fd) {
    return fsync(fd);
}

/* Set a file's modification time to `epoch` (Unix seconds, UTC), leaving
 * access time at "now" — mirrors Go's os.Chtimes(path, time.Now(), mtime)
 * used to stamp the finished download from the .zsync MTime header. std.fs
 * can read mtime but not set it, so this is the shim. Returns 0 / -1. */
int zsync_set_mtime(const char *path, long epoch) {
    if (!path) return -1;
    struct timeval tv[2];
    gettimeofday(&tv[0], NULL);   /* atime = now */
    tv[1].tv_sec = (time_t)epoch; /* mtime = epoch */
    tv[1].tv_usec = 0;
    return utimes(path, tv);
}
