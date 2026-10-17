# Actor string state reads fail to compile with Clang (Aether 0.740.0)

Observed on macOS arm64, 2026-09-29, while rebuilding zsync's CLI.

```aether
message Done { error_text: string }
actor Worker {
    state err = ""
    receive { Done(error_text) -> { err = error_text } }
}
main() {
    worker = spawn(Worker())
    worker ! Done { error_text: "failed" }
    wait_for_idle()
    println(worker.err)
}
```

`ae build` emits an ordinary pointer field and then
`atomic_load(&worker->err)`. Clang rejects the latter because the operand
is not a pointer to an `_Atomic` type. The real zsync diagnostic was:
`address argument to atomic operation must be a pointer to _Atomic type
('void **' invalid)`.

Source investigation: `compiler/codegen/codegen_actor.c` uses an ordinary
`void*` for state assigned from a pointer/string message payload, while
`compiler/codegen/codegen_expr.c` applies `atomic_load` to every external
actor-state read except names ending in `_ref`. These policies disagree.
A compiler fix needs matching field declarations/read generation and a
regression built with Clang; merely deleting the atomic read would need
an explicit concurrency policy.

The downstream workaround stores the error in a heap-allocated FetchResult,
written exclusively by the coordinator and read only after wait_for_idle().
The regular HTTP integration test exercises successful parallel fetching;
its failure paths must remain covered as well. This is not an upstream fix.

## Status (2026-10-17): fixed upstream by 0.823.0

The repro above builds and prints `failed` with Aether 0.823.0 (Clang, macOS
arm64). The FetchResult workaround in `cmd/zsync.ae` still works and is kept.
It is a reasonable shape in its own right: one writer, read after
wait_for_idle. It no longer has to be there, though.
