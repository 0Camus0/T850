# MikkTSpace

Vendored copy of the standard tangent-space generator by Morten S. Mikkelsen.

* Upstream: https://github.com/mmikk/MikkTSpace
* License: zlib/libpng (see `include/mikktspace.h` for the full notice)

The two source files (`include/mikktspace.h`, `src/mikktspace.c`) are
unmodified from upstream and compiled into `Framework` so the glTF
loader can produce industry-standard tangent spaces consistent with the
output of Blender, Substance Painter, Maya, etc.
