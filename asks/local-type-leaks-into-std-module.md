# A handler local's actor type leaks into a std module that has a local of the same name (Aether 0.931.0)

Found bringing zsync to 0.931.0. `cmd/zsync.ae` failed to compile with an
error that points inside std, at a line that has nothing to do with it:

```
error[E0302]: Unknown type 'Fetcher': the local 'g' is declared of a type the program does not define
  --> .../share/aether/std/cryptography/sha2/module.ae:273:9
273 |         g = f
```

`std/cryptography/sha2/module.ae` has an unrelated local `f` (`f = ctx.h5`).
The program has `f = spawn(Fetcher())` in an actor handler. The type the
program gave its `f` seems to be remembered by name and applied to the
std module's `f` and then `g = f`.

## Minimal repro

`m.ae`:

```aether
import std.cryptography

message Job { n: int }
actor Fetcher {
    state done = 0
    receive {
        Job(n) -> { done = done + n }
    }
}
actor Coord {
    state k = 0
    receive {
        Job(n) -> {
            f = spawn(Fetcher())
            f ! Job { n: n }
        }
    }
}
main() {
    println("ok")
}
```

```
$ aetherc m.ae /dev/null
error[E0302]: Unknown type 'Fetcher': the local 'g' is declared of a type the program does not define
```

Renaming the local `f` in the program to anything else (`w`, `fetcher`)
makes it compile. It built on 0.824.0 and 0.927.0's predecessors; first seen
here on 0.931.0.

## Workaround in this repo

`cmd/zsync.ae` names the local `fetcher`, with a comment pointing here. It is
a rename only; the right fix is in the compiler.
