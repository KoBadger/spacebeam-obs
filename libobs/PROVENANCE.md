# Vendored OBS headers

These are the libobs headers from **obs-studio 33.0.1**, the version this
plugin targets.

They are checked in because OBS Studio ships `obs.dll` but no SDK: to build a
plugin you need headers that match the DLL you link against. Vendoring them
means a clone builds without downloading the obs-studio source tree first.

Only the headers are committed. The `.c` files that came with the same
snapshot are ignored by `.gitignore`, because this plugin links against
`obs.dll` rather than compiling libobs.

`deps/include/obs-frontend-api.h` comes from the same tag (`frontend/api/`)
and is needed by the dock panel.

Headers are LGPL-2.1, as part of OBS Studio. See the OBS project for details:
https://github.com/obsproject/obs-studio

## Updating

When you move to a new OBS version, replace the contents of this folder with
the matching tag's headers (`libobs/*.h`, `libobs/graphics`, `libobs/util`,
`libobs/media-io`, `libobs/audio-monitoring`, `libobs/opus`, `libobs/rtmp`),
then rebuild with `.\build.ps1 -RegenerateImportLib` so the import library
matches the new `obs.dll`.
