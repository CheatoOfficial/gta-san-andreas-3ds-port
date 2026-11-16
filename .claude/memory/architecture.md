# Native architecture

`hle/3ds/` supplies startup, guest RAM, filesystem, timing, coroutine contexts and input.
`hle_3ds_profile.c` selects immutable runtime capacities before renderer allocation. Auto startup reserves
32 MiB linear only when at least 88 MiB remains in the process allowance, otherwise 8 MiB. After services initialize,
actual hardware and heap sizes choose full or compact resources. New hardware with a small allowance keeps New controls
but uses compact resources. Forced regression targets remain. Heap availability depends on loaded image and launcher.

The backend stores CPU vertices/batches; `hle_3ds_gfx.c` converts/submits them to Citro3D.
GPU buffers and cached textures cannot be rewritten or freed while queued work references them.
Raster lifecycle/unlock hooks mark cached world/HUD copies dirty and retire them only after GPU completion.
Writes that bypass those hooks still need investigation if stale images are observed.

The shared runtime requires a full 32 MiB guest RAM window. Smaller render buffers are bounded and can omit geometry;
they do not make guest RAM smaller. NDSP output remains unverified on hardware.

Old textures use 16-bit formats with bounded lower-resolution world images and separate HUD copies, retaining the
2 MiB total cache limit. New textures retain RGBA8. The static-budget check must run before frame-buffer reservation.

Old-model controls are implemented separately in `hle_3ds_padmap.c` and tested with synthetic HID bits.
