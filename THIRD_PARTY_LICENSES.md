# Third-party software and fonts

gfx itself is under the [MIT license](LICENSE). It builds on the software and fonts below, none of which is
committed to this repository. CMake finds each dependency on the system or downloads it at the pinned version
shown, checked against its hash (`cmake/GfxDependencies.cmake`), and the license texts come with each download
(`<build>/_deps/<name>-src`). License identifiers are [SPDX](https://spdx.org/licenses/) identifiers.

A program that links a gfx library contains the libraries listed for it below; distributing that program in
binary form means giving their copyright notices and license texts with it. The build-time tools, the tests and
the fonts are listed separately.

## In programs

### The graphics library (`gfx`)

| Component | Version | License | Copyright | Notes |
|---|---|---|---|---|
| [volk](https://github.com/zeux/volk) | vulkan-sdk-1.4.350.0 | MIT | 2018-2026 Arseny Kapoulkine | Vulkan backend: loads the system's Vulkan loader at run time (the loader is not linked) |
| [Vulkan Memory Allocator](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator) | 3.4.0 | MIT | 2017-2026 Advanced Micro Devices, Inc. | Vulkan backend |
| [SPIRV-Cross](https://github.com/KhronosGroup/SPIRV-Cross) | vulkan-sdk-1.4.350.0 | Apache-2.0 | the SPIRV-Cross authors | Shader reflection |
| [Vulkan-Headers](https://github.com/KhronosGroup/Vulkan-Headers) | vulkan-sdk-1.4.350.0 | Apache-2.0 OR MIT | 2015-2023 The Khronos Group Inc. | Headers only |
| [RenderDoc in-application API header](https://github.com/baldurk/renderdoc) | v1.45 | MIT | 2015-2026 Baldur Karlsson | Header only; RenderDoc is used only when it is installed |
| [glslang](https://github.com/KhronosGroup/glslang) | vulkan-sdk-1.4.350.0 | BSD-3-Clause, BSD-2-Clause, MIT, Apache-2.0 (its `LICENSE.txt` lists which files use which) | the glslang authors | **Debug builds only** (run-time GLSL and HLSL compilation, hot reload); Release builds do not link it |
| [SPIRV-Tools](https://github.com/KhronosGroup/SPIRV-Tools) | the system's (with glslang) | Apache-2.0 | the SPIRV-Tools authors | Debug builds only, as glslang's dependency |

On this machine, MSYS2 provides Vulkan-Headers, Vulkan Memory Allocator, SPIRV-Cross, glslang and SPIRV-Tools
(the same projects, licensed the same way); volk and the RenderDoc header are always fetched.

### The platform library (`gfx::platform`)

| Component | Version | License | Copyright | Notes |
|---|---|---|---|---|
| [SDL](https://github.com/libsdl-org/SDL) | 3.4.10 (or the system's 3.4) | Zlib | 1997-2026 Sam Lantinga | SDL3 backend, linked statically when fetched |
| [GLFW](https://github.com/glfw/glfw) | 3.4 | Zlib | 2002-2006 Marcus Geelnard, 2006-2019 Camilla Löwy | GLFW backend, linked statically when fetched |
| [stb_image](https://github.com/nothings/stb) | 2.30 (commit f1c79c0) | MIT OR Unlicense (public domain) | 2017 Sean Barrett | Window icons and clipboard images (PNG) |
| [nlohmann/json](https://github.com/nlohmann/json) | 3.12.0 (or the system's) | MIT | 2013-2025 Niels Lohmann | Reading saved input bindings |

### The UI library (`gfx::ui`)

| Component | Version | License | Copyright | Notes |
|---|---|---|---|---|
| [HarfBuzz](https://github.com/harfbuzz/harfbuzz) | 14.2.1 | MIT-Modern-Variant ("Old MIT"); `src/ms-use`: MIT | the HarfBuzz authors; Microsoft (Universal Shaping Engine tables) | Text shaping |
| [zstd](https://github.com/facebook/zstd) | 1.5.7 | BSD-3-Clause (dual-licensed with GPL-2.0; gfx uses the BSD license) | Meta Platforms, Inc. and affiliates | Decompresses atlas pages |
| [SheenBidi](https://github.com/Tehreer/SheenBidi) | 3.0.0 (or the system's) | Apache-2.0 | 2014-2026 Muhammad Tayyab Akram | Bidirectional text, script runs |
| [libunibreak](https://github.com/adah1972/libunibreak) | 7.0 (or the system's) | Zlib | Wu Yongwei, Tom Hacohen, Petr Filipsky, Andreas Röver and contributors | Line, word and grapheme boundaries |
| nlohmann/json | as above | MIT | as above | Reading saved dock layouts |

### The UI's platform adapter (`gfx::ui_platform`, Windows)

| Component | Version | License | Copyright | Notes |
|---|---|---|---|---|
| [AccessKit C bindings](https://github.com/AccessKit/accesskit-c) | 0.23.1 | MIT OR Apache-2.0; code derived from Chromium: BSD-3-Clause (`LICENSE.chromium`) | the AccessKit authors; 2015 The Chromium Authors | `UiAccessibilityAdapter`: the accessibility tree to UI Automation |

AccessKit's C bindings are published as prebuilt static libraries, which contain AccessKit's Rust crates
(accesskit 0.25.1, accesskit_consumer 0.39.1, accesskit_windows 0.35.1: MIT OR Apache-2.0), the crates.io crates
they depend on (among them windows 0.62.2 and windows-core 0.62.2, MIT OR Apache-2.0, and hashbrown 0.17.1) and
Rust's standard library (MIT OR Apache-2.0). The archive does not include those crates' license texts; its
`Cargo.lock` lists every crate, and a tool such as [cargo-about](https://github.com/EmbarkStudios/cargo-about) run
on it produces the notices a binary distribution needs.

### The runtime of the toolchain (Windows)

`gfx_deploy_runtime_dependencies()` copies the DLLs an executable needs next to it:

| Component | License | Notes |
|---|---|---|
| `libstdc++-6.dll`, `libgcc_s_seh-1.dll` (GCC 16.1) | GPL-3.0-or-later WITH GCC-exception-3.1 | The GCC Runtime Library Exception allows them in programs under any license |
| `libwinpthread-1.dll` (mingw-w64 winpthreads) | MIT | 2011 mingw-w64 project |
| `glslang.dll`, `libglslang-default-resource-limits.dll`, `libSPIRV-Tools*.dll` | as above | Debug builds only (MSYS2's shared glslang) |

## Build-time tools

The atlas tool (`gfx_ui_atlas_builder`, the library `gfx::ui_atlas_tools`, run by `gfx_add_ui_atlas()`) turns
fonts into `.uiatlas` files at build time. Programs contain the atlases it writes (data derived from the fonts:
see [Fonts](#fonts)), not its code, unless they link `gfx::ui_atlas_tools` themselves.

| Component | Version | License | Copyright | Notes |
|---|---|---|---|---|
| [FreeType](https://freetype.org) | 2.14.3 | FTL (FreeType is dual-licensed FTL or GPL-2.0; gfx uses the FTL) | The FreeType Project | Font decoding; its own copy of zlib (Zlib) reads WOFF fonts |
| [Brotli](https://github.com/google/brotli) (decoder) | 1.2.0 | MIT | 2009, 2010, 2013-2016 the Brotli authors | WOFF2 fonts, through FreeType |
| [msdfgen](https://github.com/Chlumsky/msdfgen) | 1.13 | MIT | 2014-2025 Viktor Chlumsky | Distance fields (core and FreeType extension; no Skia, SVG or PNG) |
| [msdf-atlas-gen](https://github.com/Chlumsky/msdf-atlas-gen) | 1.4 | MIT | 2020-2026 Viktor Chlumsky | Atlas packing |
| HarfBuzz (subset library) | 14.2.1 | as above | as above | Font subsetting |
| [stb_truetype](https://github.com/nothings/stb) | 1.26 (commit f1c79c0) | MIT OR Unlicense (public domain) | 2017 Sean Barrett | The second font decoder |
| zstd | 1.5.7 | as above | as above | Compresses atlas pages |

The FreeType License asks products that use FreeType to credit it in their documentation; for the atlas tool:

> Portions of this software are copyright © 2026 The FreeType Project (https://freetype.org). All rights reserved.

## Tests

| Component | Version | License | Copyright |
|---|---|---|---|
| [GoogleTest](https://github.com/google/googletest) | 1.17.0 (or the system's) | BSD-3-Clause | 2008 Google Inc. |

## Fonts

The tests and the examples build atlases from fonts downloaded when CMake configures (`gfx_require_ui_test_fonts()`,
into `<build>/ui-test-fonts`, next to their license texts: `NotoSans-OFL.txt`, `NotoSansHebrew-OFL.txt`,
`NotoSansArabic-OFL.txt`, `SourceSans3-LICENSE.md`).

| Font | Source | License | Copyright | Used by |
|---|---|---|---|---|
| Noto Sans (Latin, Greek, Cyrillic) | [google/fonts](https://github.com/google/fonts) commit 47d54ca1 | OFL-1.1, no Reserved Font Name | 2022 The Noto Project Authors | tests; atlases embedded in `gfx_windowed_example` and `gfx_ui_editor_example` |
| Noto Sans Hebrew | google/fonts commit 47d54ca1 | OFL-1.1, no Reserved Font Name | 2022 The Noto Project Authors | tests; an atlas embedded in `gfx_ui_editor_example` |
| Noto Sans Arabic | google/fonts commit 47d54ca1 | OFL-1.1, no Reserved Font Name | 2022 The Noto Project Authors | tests; an atlas embedded in `gfx_ui_editor_example` |
| Source Sans 3 (OpenType CFF, CFF2 variable, WOFF, WOFF2) | [adobe-fonts/source-sans](https://github.com/adobe-fonts/source-sans) commit 87b37a2d | OFL-1.1, Reserved Font Name "Source" | 2010-2024 Adobe | tests only |

An atlas holds the glyphs of a subset of its font, converted to distance fields, with the font's metrics, kerning
and (for shaping) the subset font itself; gfx treats it as a modified version of the font under the OFL. A program
that embeds atlases therefore comes with the OFL's conditions for each font: the font's copyright notice and the
license accompany the program, the font (or its atlas) is not sold by itself, and an atlas made from a font with a
Reserved Font Name does not use that name.

## Acknowledgements

No code of these projects is included; parts of the UI library follow their design.

- [Dear ImGui](https://github.com/ocornut/imgui) (MIT, 2014-2026 Omar Cornut): the immediate-mode API, the dark,
  light and classic themes' colors, the scoring of directional navigation, the submenu's "moving toward" triangle,
  the layout of tab bars and docking.
- [AccessKit](https://accesskit.dev): the shape of the accessibility tree and its updates.
