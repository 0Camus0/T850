# simplecpp

Unmodified core files from simplecpp 1.9.1:
https://github.com/cppcheck-opensource/simplecpp/tree/2499b51390e6ea74b8fbad91154f5529134de31a

License: 0BSD, reproduced in LICENSE. Only the library implementation/header are
vendored; no executable or Python test dependency is required by the engine.

The engine adapter accepts in-memory shader templates and feature defines.
It emits tokens without C #line markers. File includes and nondeterministic
predefined macros are deliberately unsupported in the initial shader contract.