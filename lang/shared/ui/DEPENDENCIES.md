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

The renderer currently uses rounded rectangles only. SDL_ttf and a font atlas
are planned for text and are not dependencies of this build yet.
