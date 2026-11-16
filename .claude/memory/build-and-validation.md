# Build and validation routes

`3ds-port/ps2rc3ds.py` is the user entry point. Conversion uses PSP tools; 3DS builds use devkitARM/libctru/Citro3D.
Read `docs/3DS_SETUP.md` for OS-specific prerequisites and `docs/3DS_OLD_MODEL.md` for model selection.

Host tests and native-checks use source and synthetic inputs. Native-checks compiles ARM11 units with SDK headers;
it does not link translated functions or establish game execution. A full build needs private local conversion outputs.

Universal objects live in `3ds-work/build-3ds-auto` and artifacts use `ps2rc-3ds-auto-<mode>.3dsx`.
This is the build/staging default. `azahar --hardware old3ds|new3ds` selects the emulated hardware independently.
Forced New-model objects live in `3ds-work/build-3ds`; old-model objects use `3ds-work/build-3ds-old3ds`.
Release/profile are diagnostic modes, separate from the model selection. Staging defaults to the selected model's newest artifact.

Portable Azahar configuration lives next to the emulator executable, outside the source checkout.
Use a dedicated emulator copy and stop it before switching model configuration.
