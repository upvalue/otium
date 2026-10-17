# Native UI dependencies

Yoga 3.2.1 is downloaded from
https://github.com/facebook/yoga/archive/refs/tags/v3.2.1.tar.gz.
The archive SHA-256 is
`86b399ac31fd820d8ffa823c3fae31bb690b6fc45301b2a8a966c09b5a088b55`.
`setup.py` verifies it before installing the native sources and MIT license into
`shared/build/deps/yoga-3.2.1`. The build directory is ignored by Git.

The build links the installed SDL3 library, version 3.4.12 or later. SDL is
licensed under the zlib license. CMake discovers it with pkg-config. `glslc`
compiles the Vulkan shaders to SPIR-V; the Metal shader source is embedded for
SDL's Metal backend. `shaders/embed.py` embeds shader data during the build.

Text links SDL3_ttf 3.2.2 or later through pkg-config (`sdl3-ttf`). SDL_ttf is
zlib-licensed and uses FreeType and HarfBuzz for rasterization and shaping.
The renderer owns the glyph atlas and consumes SDL_ttf's custom text-engine API.

Bundled fonts are checked in under `fonts/`, with each font's unmodified SIL Open
Font License 1.1 and copyright notices. Retrieved October 17, 2026 from Google
Fonts; these bytes, rather than installed system fonts, define the UI defaults:

| File | Source | SHA-256 |
| --- | --- | --- |
| `NotoSans.ttf` | [Noto Sans variable font](https://github.com/google/fonts/blob/main/ofl/notosans/NotoSans%5Bwdth,wght%5D.ttf) | `bfb7bb691513f12e734dc346c03a03f784912432d7e3fa8e56efcf906fe86b3d` |
| `JetBrainsMono.ttf` | [JetBrains Mono variable font](https://github.com/google/fonts/blob/main/ofl/jetbrainsmono/JetBrainsMono%5Bwght%5D.ttf) | `48715a42ec242c21e9f02692891e147d022299a52e48d5e413e1a942193ffeda` |

CMake embeds the absolute font directory for this build, like the generated
native-library installation paths. Reconfigure after moving the checkout.
No font download is needed to build or run the UI.
