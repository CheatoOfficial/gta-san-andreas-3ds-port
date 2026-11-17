# Build-time texture conversion (design, not implemented)

Goal: the REGTA/librw idea of preparing textures on the PC instead of at run time - no texture decoding on the 3DS
and (with ETC1) 1/8 of the linear memory of today's RGBA8 textures on New 3DS, more textures resident on Old 3DS.
Status: design only (2026-11-17). Nothing below exists yet.

## What it would replace

Today every texture the game binds is decoded on first use (`hle_3ds_texture.c`: `raster_info` reads the RenderWare
raster at its guest address - pixel pointer +0x04, palette pointer +0x08, width/height, depth, format - then
`tc_convert` (PS2 swizzle / palette) and `decode_to_texture` (3DS tiling) into a linear-memory `C3D_Tex`; New 3DS
RGBA8, Old 3DS 565 / 5551 / 4444).
Measured (Azahar, New mode, scripted route): 0-1.4 s of decoding per 120-frame window in gameplay, mean ~3.2 ms per
frame, busiest window ~11.8 ms per frame, in bursts while streaming ("Citro3D texture decode time" profile line);
the texture cache holds 11-15 MB of linear memory on New 3DS (min linear free 5.9 MB); Old 3DS has ~124 KB linear
headroom and visible texture loss.

## Matching run-time rasters to pre-converted data

The game loads its texture dictionaries (TXD files inside the disc's IMG archives) into guest memory as-is; a raster's
pixel and palette pointers point into that loaded file data. So the bytes the run time sees are the bytes of the TXD
on the disc. Key = 64-bit hash (e.g. xxHash64) of [pixel bytes of all levels] + [palette bytes] + width, height,
depth, format. The PC computes it from the TXD payload; the 3DS computes it at the texture-cache miss (one pass over
the raster's bytes, far cheaper than the decode it replaces), looks it up, and loads the prepared data.

Anything not in the pack (render targets, the radar / font rasters the game edits, textures the game patches after
loading) misses and takes today's decode path, so correctness never depends on the pack being complete. The existing
raster invalidation (unlock / lifecycle hooks) keeps working: an invalidated raster is re-hashed on its next use.

## Pieces

1. PC tool (`3ds-port/scripts/texpack.py` + a host C helper): walk the IMG archives of the converted disc (already
   staged under `package/.../disc/MODELS`), parse RenderWare TXD chunks (Texture Native, platform PS2: raster struct,
   GS registers, image + palette packets), extract the byte ranges the run time hashes.
2. Decode with the same `texconv.c` the 3DS uses (host-compilable today: the host texture tests build it), then
   tile and encode: ETC1 (opaque) / ETC1A4 (alpha) for New 3DS; Old 3DS keeps 16-bit or ETC1 too. An ETC1 encoder:
   rg_etc1 (zlib licence; REGTA ships one) or a small own encoder - quality setting matters for build time
   (thousands of textures).
3. Pack file on the SD card (`sdmc:/3ds/ps2rc-sa/tex.pack` + `tex.idx`): sorted index of (hash, offset, size,
   format, dimensions) loaded at start-up (a few hundred KB), payloads read on demand with one SD read each.
4. Run time (`hle_3ds_texture.c`, miss path): hash -> binary search -> `C3D_TexInit(..., GPU_ETC1 / GPU_ETC1A4)` +
   one read into the texture memory; on any mismatch fall back to the decode. A profile line counts pack hits /
   misses / bytes read / read time.
5. Validation: host test (pack entry == run-time decode for a synthetic raster), Azahar captures (pixel diffs are
   expected: ETC1 is lossy - judge visually and by a PSNR threshold), the decode-time line, linear-memory headroom,
   and on hardware the SD read time (the trade: decode CPU vs SD reads; REGTA notes SD speed matters for streaming).

## Risks and open questions

* TXD parsing of the PS2 native format (mip levels, PSMT4/PSMT8 swizzles, CLUT layout) - the run-time decoder is the
  reference for every case it handles.
* Lossy ETC1 on UI / text textures: keep those uncompressed (flag by dictionary name or alpha class).
* Hash cost on big rasters vs the decode it saves; measure.
* Build time and pack size (all textures of the game: several hundred MB as RGBA8, ~1/8 of that as ETC1).
* Game data: the pack is derived from the disc, so like everything in `3ds-work/` it stays on the user's machine
  and SD card and is never committed or shared.

Effort: roughly 2-4 sessions (parser + encoder + pack + run time + validation). Priority after the first hardware
profile, which says how much the decode bursts and SD reads cost on a real console.
