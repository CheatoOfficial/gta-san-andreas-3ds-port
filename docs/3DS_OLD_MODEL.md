# Old 3DS / Old 3DS XL / original 2DS experiment

An Old-model resource and input profile exists, selected with `--model old3ds`.
It is **experimental and not established as playable**. Full builds and Old-mode Azahar city runs exist; original-model
hardware remains untested. New 3DS emulator measurements do not predict Old 3DS speed.

## Build and select the model

After the normal local conversion step, on an explicitly authorized machine with your own disc data:

```bash
python 3ds-port/ps2rc3ds.py build --model old3ds
python 3ds-port/ps2rc3ds.py build --model old3ds --mode profile --max-seconds 1500
python 3ds-port/ps2rc3ds.py stage-sd <SD_ROOT> --model old3ds --yes
python 3ds-port/ps2rc3ds.py azahar --model old3ds --azahar <DEDICATED_AZAHAR_EXECUTABLE>
```

Outputs are `3ds-work/dist/ps2rc-3ds-old3ds-release.3dsx` and `ps2rc-3ds-old3ds-profile.3dsx`.
Objects use `3ds-work/build-3ds-old3ds`, separate from New-model objects. Pass the same model to build, staging and Azahar.
An explicit `--3dsx` overrides automatic artifact selection. Launch that file from Homebrew Launcher.
Stop Azahar before changing models; this command updates its portable config's model setting even when the file already exists.
An emulator in Old-model mode still cannot prove hardware memory or performance behavior.

## Resource policy

| Resource | New-model default | Old-model profile |
|---|---:|---:|
| CPU/GPU world vertex capacity | 196,608 each | 65,536 each |
| Temporary mesh vertices | 32,768 | 8,192 |
| World batches | 4,096 | 2,048 |
| Overlay vertices / batches | 16,384 / 2,048 | 4,096 / 512 |
| Texture cache | 20 MiB / 384 slots | 2 MiB / 512 slots |
| Uploaded texels | RGBA8 | RGB565 / RGBA5551 / RGBA4 by alpha class |
| Texture detail ceiling | existing level policy, up to 512 | world: 64 pixels / 8 KiB; HUD: 128 pixels / 32 KiB |
| Static vertices converted per frame | unlimited release / 20,000 profile | 10,000 |
| Linear heap reservation | libctru automatic | 8 MiB |

The shared 32 MiB guest address window is preserved. Reduced buffers and draw budgets can omit meshes/textures;
they do not preserve full scene detail. `--vert-budget` overrides the static draw budget, including `0` to disable it.
Old-profile texture overrides cannot exceed the documented cache/slot cap.
Capture memory is allocated only in builds that enable captures.

Old-model textures use stored smaller levels or point decimation when necessary. Opaque images use RGB565, cutouts
use RGBA5551 and blended images use RGBA4. This trades detail/color precision for scene coverage; the New-model path
continues to use RGBA8. World and HUD copies are cached separately so a low-detail world image cannot replace a HUD atlas.
The Old-model decode scratch buffer is 69,632 bytes instead of 1,052,672 bytes. The 2 MiB cache cap and 8 MiB linear
reservation are unchanged. Neither cache resizing nor the smaller scratch proves that the whole game fits on hardware.

Raster creation/destruction, unlock and texture-read hooks now invalidate all matching 3DS cache copies. Already-used
snapshots survive until their GPU work completes; new draws may be refused temporarily rather than sampling stale data.
The static draw budget still skips whole meshes in draw order. A rejected static mesh no longer consumes the remaining
frame buffer, so later immediate/lit draws retain their space. These are separate limits from texture residency.

Available normal heap depends on the full linked code/data/BSS and the homebrew launcher's memory allowance.
Startup refuses a normal heap smaller than 36 MiB; this is only an initial floor, not a guarantee that later allocations fit.
If this fails or a later allocation fails, record the numeric resource summary and test a supported launch with a larger
application memory allowance. Do not infer compatibility from the console's physical RAM or remove bounds checks to force boot.
New-model builds refuse old hardware with an explanatory message. New 3DS speedup is requested only on new hardware.

## Controls without extra hardware

| Physical input | Guest action |
|---|---|
| Circle Pad | Left analog stick |
| D-pad | Right analog stick / camera |
| SELECT + D-pad | Guest digital D-pad; SELECT is consumed |
| L / R | L1 / R1 |
| SELECT + L / R | L2 / R2; SELECT and the first shoulder are consumed |
| SELECT + A / X | L3 / R3 stick clicks; SELECT and the face button are consumed |
| SELECT alone | SELECT |
| A / X / B / Y | Cross / Circle / Square / Triangle |
| START | START |

Camera movement is digital. Circle Pad Pro is not required and is not integrated by this profile.
Release builds are needed to test physical controls; profile builds use the scripted input route.

## Acceptance checks still required

1. Follow the latest full-build/emulator evidence in [3DS_STATUS.md](3DS_STATUS.md); repeat longer scenes and streaming
   transitions rather than extrapolating a short city run to the whole game.
2. On Old 3DS/Old 3DS XL/original 2DS, verify boot and sustained allocation headroom under the documented launcher setup.
3. Check every modifier and camera direction in a release build, then audio and sustained gameplay.
4. Share only the reviewed allowlisted timing summary from `summarize-log.py`; never upload the game build, captures or raw log.

Until these pass, describe this as an experimental compatibility profile, not confirmed Old 3DS support.
