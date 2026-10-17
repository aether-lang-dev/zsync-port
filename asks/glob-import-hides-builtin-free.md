# A glob import of std.string, std.list or std.map hides the builtin `free` from every other module (Aether 0.809.0 to 0.823.0)

Found bringing zsync to 0.823.0. Every suite that imports `rcksum.checksums`
failed with 23 errors, all inside std itself:

```
error[E0301]: Undefined function 'free'
  --> .../share/aether/std/cryptography/module.ae:700:9
  --> .../std/cryptography/md4/module.ae:217:5   (and md5, sha1, hmac ...)
```

The trigger was `import std.string (*)` in zsync's own files.

## Minimal repro

`lib/mymod/module.ae`:

```aether
extern malloc(n: int) -> ptr
mk() -> ptr { return malloc(8) }
rel(p: ptr) { free(p) }
```

`m.ae`:

```aether
import std.string (*)
import mymod
main() {
    mymod.rel(mymod.mk())
    println("ok")
}
```

```
$ ae run m.ae --lib lib
error[E0301]: Undefined function 'free'      # 0.809.0 .. 0.823.0
ok                                           # 0.803.0 and earlier
```

`import std.list (*)` and `import std.map (*)` do the same thing. Plain
`import std.string`, a selective import, and the globs of std.os, std.math
and std.http do not. The three modules that cause it are the ones that define
a function called `free` (`string.free`, `list.free`, `map.free`). The glob
seems to bind the bare name `free` program-wide, so a module that calls the
builtin `free` without declaring it no longer finds it. Since std.cryptography
dropped its own `extern free` declaration, it is one such module.

The pass/fail split by version: 0.741, 0.766, 0.778, 0.791, 0.801 and 0.803
pass. 0.809, 0.810, 0.817 and 0.823 fail. The glob-import fixes #2632 and #2637
landed in that window.

## What zsync does meanwhile

Its 19 `import std.string (*)` (plus the std.list and std.map globs) are now
selective imports, naming what each file calls. That is clearer anyway, so
nothing needs undoing when this is fixed. Watch for one trap when converting:
std.string exports `keep` and `bytes`, and a selective list must not name
them where a file uses those words as local variables. `cmd/zsync.ae` has a
`keep` local for `-k`.
