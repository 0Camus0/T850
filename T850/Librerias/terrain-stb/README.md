# Terrain Image Decoder

Upstream: https://github.com/nothings/stb

Pinned revision: `2c980bb59875b0d32144a71867fbdebb2f77cd20`.

`stb_image.h` is vendored unmodified from that revision. Its MIT/public-domain
license is included in the upstream header. Terrain compiles it with static
symbols and memory-only input, independently of the legacy renderer's stb copy.
This preserves native 16-bit PNG/PNM elevation samples without changing texture IO.