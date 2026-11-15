# Native architecture

`hle/3ds/` supplies startup, guest RAM, filesystem, timing, coroutine contexts and input.
`hle_3ds_profile.h` defines compile-time renderer capacities for both models. Old 3DS reserves an 8 MiB linear heap;
normal heap availability still depends on the loaded image and the launcher's memory allowance.

The backend stores CPU vertices/batches; `hle_3ds_gfx.c` converts/submits them to Citro3D.
GPU buffers and cached textures cannot be rewritten or freed while queued work references them.
Texture cache invalidation on in-place edits/raster reuse remains an open issue in `docs/3DS_STATUS.md`.

The shared runtime requires a full 32 MiB guest RAM window. Smaller render buffers are bounded and can omit geometry;
they do not make guest RAM smaller. NDSP output remains unverified on hardware.

Old-model controls are implemented separately in `hle_3ds_padmap.c` and tested with synthetic HID bits.
