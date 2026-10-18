# REPLY: a glob import of std.string, std.list or std.map hides the builtin `free`

Fixed in aether PR #2966: https://github.com/aether-lang-dev/aether/pull/2966

Since 0.805, a module's merged body is kept from seeing the program's
imports. That check asked whether the program imported the name `free`,
not whether the import bound the symbol the module found, which was the
builtin. It now asks the second question. A module's bare `free` is also
no longer rewritten to the program's `string.free`. The same PR makes a
glob-imported constant work when written bare: before, it produced C
naming an undeclared identifier. zsync's selective imports can stay as
they are.
