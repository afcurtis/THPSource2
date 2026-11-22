# Third-party notices

THPSource2's own code is under the MIT licence (`LICENSE`; `Licenses\THPSource2-LICENSE.txt` in a release). It is based on
**OpenTony** by Joao Matos (tritao), https://github.com/tritao/OpenTony, whose repository states no licence. The components
below are used by the platform HAL (`src/hal/`, docs/HAL.md) and the hook
DLLs; their licenses follow.

## SDL3

The SDL3 backend (`src/hal/sdl/`) compiles against the SDL3 headers and
loads SDL3.dll at run time. `tony setup sdl3` downloads the pinned official
release (re/config/sdl3.yml, SDL 3.4.16); `tony hooks stage` copies SDL3.dll
next to the game.

```
Copyright (C) 1997-2026 Sam Lantinga <slouken@libsdl.org>

This software is provided 'as-is', without any express or implied
warranty.  In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not
   claim that you wrote the original software. If you use this software
   in a product, an acknowledgment in the product documentation would be
   appreciated but is not required.
2. Altered source versions must be plainly marked as such, and must not be
   misrepresented as being the original software.
3. This notice may not be removed or altered from any source distribution.
```

## glslang

`tony hal shaders` (docs/HAL.md, "Video", 3.4d) uses the pinned glslang
16.6.0 Linux release (re/config/glslang.yml, Khronos's
`glslang-16.6.0-linux-x86_64-release.tar.gz`) to compile
`src/hal/sdl/shaders/*.{vert,frag}` to SPIR-V for the SDL_GPU video backend.
It is a **build-time tool only**: its output (the compiled SPIR-V byte
arrays) is committed to `src/hal/sdl/sdl_video_gpu_shaders.h`, and neither
glslang itself nor any of its files are ever copied into a build or an
install. The release archive has no `LICENSE`/`LICENSE.txt` file (unlike
glslang's source repository, whose `16.6.0` tag carries one); glslang is
BSD-3-Clause-style there:

```
Copyright (C) 2002-2005  3Dlabs Inc. Ltd.
Copyright (C) 2012-2016 LunarG, Inc.
Copyright (C) 2015-2018 Google, Inc.

All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

    Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.

    Redistributions in binary form must reproduce the above
    copyright notice, this list of conditions and the following
    disclaimer in the documentation and/or other materials provided
    with the distribution.

    Neither the name of 3Dlabs Inc. Ltd. nor the names of its
    contributors may be used to endorse or promote products derived
    from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.
```

## llvm-mingw (libc++, libunwind, compiler-rt, mingw-w64 runtime)

`opentony_standalone.dll`, `opentony_spike.exe`, `opentony.exe` (x86-64 and
32-bit), `opentony_native.dll` (the Kit DLL of the hooked build, `src/hooks/kits/`, Phase 5.0) and the
32-bit replay tests are built with the pinned llvm-mingw 20261006 release
(re/config/llvm_mingw.yml, `tony setup llvm-mingw`), which links its C++
runtime statically into them:

- LLVM's libc++, libc++abi, libunwind and compiler-rt: Apache License 2.0
  with LLVM Exceptions. The exception lets the runtime portions embedded in a
  compiled binary be redistributed without the attribution conditions
  (sections 4(a), 4(b), 4(d)); the full text is `LICENSE.TXT` at the root of
  the release.
- The mingw-w64 runtime (CRT start-up and support code): the mingw-w64
  runtime licence, which **requires its notices to accompany binary
  distributions**. They are in
  `<arch>-w64-mingw32/share/mingw32/COPYING.MinGW-w64-runtime.txt` of the
  release. `tony release package` (milestone 12) ships that file and LLVM's
  `LICENSE.TXT` in the release folder's `licenses\`.

## FFmpeg (movie and music decoding)

The game's movies and music are Bink files (`Movies/*.dat`, `Music/LTIX*.dat`, magic `BIKi`). The HAL's movie and music backend
(`src/hal/ffmpeg/`, Phase 5.3b, docs/HAL.md "Movie") decodes them with **FFmpeg 8.1.3** instead of RAD's `binkw32.dll`.

This software uses libraries from the FFmpeg project under the **LGPLv2.1 or later**. FFmpeg's source is
https://ffmpeg.org/releases/ffmpeg-8.1.3.tar.xz (SHA-256 `7138d28c96d9d3e3af4ee3d8cad72741f8ffb40da90c1112235dea3ecd3178a3`,
signed with the FFmpeg release key `FCF9 86EA 15E6 E293 A564  4F10 B432 2F04 D676 58D8`); the exact source of any binary
distributed with THPSource2 is that tarball, **unmodified**.

- **What is built** (`re/config/ffmpeg.yml`, `tony setup ffmpeg`): a decode-only, shared build (32-bit and x86-64) by the llvm-mingw toolchain with
  `--disable-everything` and then only the Bink demuxer, the three Bink decoders (`bink`, `binkaudio_dct`, `binkaudio_rdft`) and
  the `file` protocol; no encoders, no filters, no network, no assembly. It is configured **without** `--enable-gpl`,
  `--enable-nonfree` and `--enable-version3` and with no external library, so no GPL or non-free code is compiled in; `tony setup
  ffmpeg` refuses a build whose `config.h` turns one of them on.
- **How it is linked**: dynamically. `avutil-60.dll`, `avcodec-62.dll` and `avformat-62.dll` keep FFmpeg's own names and are loaded at
  run time by `movie_source.cpp` (`OPENTONY_FFMPEG_DIR`, else beside the program); a player may replace them with their own build of
  the same major versions. THPSource2's code is not linked into them.
- **Obligations for a distribution** (met by `tony release package`, milestone 12): the release folder ships these DLLs with FFmpeg's
  `COPYING.LGPLv2.1` and `LICENSE.md` (`Licenses\`), names FFmpeg and its licence in this file and in its `README.md` (docs/PLAYING.md), and points to the matching
  source as above (the tarball is unmodified, so the pointer is the source offer). The DLLs are separate files a player can replace.
- This settles the licence of the code only. It says nothing about patents or the Bink format, and is not legal advice.

## zlib (compression of the game's package and resources)

The retail executable carries zlib 1.1.2; the standalone program (milestone 8c, Phase 5.3c) uses **zlib 1.3.1** instead, compiled from the unmodified release source
https://github.com/madler/zlib/releases/download/v1.3.1/zlib-1.3.1.tar.gz (SHA-256 `9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23`; pinned in
`re/config/zlib.yml`, fetched and checked by `tony hooks build`) and linked statically into `opentony_standalone.dll` and `THPSource2.exe` (the texture upscaler's PNG files). Only the library's own files are compiled (not the `gz*` file API).

  Copyright (C) 1995-2024 Jean-loup Gailly and Mark Adler

  This software is provided 'as-is', without any express or implied warranty. In no event will the authors be held liable for any damages arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose, including commercial applications, and to alter it and redistribute it freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not claim that you wrote the original software. If you use this software in a product, an acknowledgment in the product documentation would be appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.

## Dear ImGui (the THPSource2 launcher's interface)

`THPSource2.exe` (docs/LAUNCHER.md) draws its window with **Dear ImGui 1.92.9b**, compiled from the unmodified release source
https://github.com/ocornut/imgui/archive/refs/tags/v1.92.9b.tar.gz (SHA-256 `21d8a0a565e85dce943e375db00812c2f3f0ab21f3f0f7964e364a63422d7f99`; pinned in
`re/config/imgui.yml`, fetched and checked by `tony hooks build`): the library and its SDL3 and SDL_Renderer backends, linked statically. The game does not use it.

```
The MIT License (MIT)

Copyright (c) 2014-2026 Omar Cornut

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## PARTYMOD (the launcher's settings)

The THPSource2 launcher's settings (`src/launcher/thpsource2/core/settings.*`, `bindings.*`) are ported from **PARTYMOD for THPS2** by PARTYMANX
(https://github.com/PARTYMANX/partymod-thps2, `config/main.c` and `src/input.c`): the partymod.ini sections, keys and defaults, the fog-distance,
texture-filter and internal-resolution option lists, the resolution rules, and the SDL controller-button numbering (with its trigger values 21 and 22)
that the import of a partymod.ini reads. PARTYMOD's hook-point names and addresses are also recorded in re/xref/community.yml and
re/config/hal_boundary.yml.

```
MIT License

Copyright (c) 2024 PARTYMANX

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## Downloaded on request, not shipped: the texture upscaler

The launcher's Graphics > "Upscale textures..." downloads these on the player's PC into `Data\Upscaler\`, from the pinned URLs with size and SHA-256 in
`re/config/upscaler.yml`. None of them is part of a THPSource2 release; they upscale the player's own copy of the game's textures for that player.

- **upscayl-bin** (upscayl-ncnn), release 20251207-174704, AGPL-3.0, https://github.com/upscayl/upscayl-ncnn: run as a separate program; its `LICENSE` is kept
  beside it as `Data\Upscaler\LICENSE-upscayl-bin.txt`.
- **4x-UltraSharp** by Kim2091 (the ncnn conversion published by upscayl, https://github.com/upscayl/upscayl), CC-BY-NC-SA-4.0: the world textures' model.
  Non-commercial: the player downloads it for their own use.
- Other models the pins name (Real-ESRGAN x4plus and its anime model, BSD-3-Clause, xinntao; and others listed with their licences in `re/config/upscaler.yml`) are
  downloaded only if a later setting picks them.

## Reference projects

- **THPS2 Cumulative Patch** (GPL-3.0) and **thps2-demo-decomp** (no license):
  names, addresses and struct facts only; no code.
- **OpenTony** by Joao Matos (tritao), https://github.com/tritao/OpenTony (no licence stated): the project THPSource2
  is based on; THPSource2 continues its code, tools and recorded knowledge.
- **OpenAladdin** (https://github.com/tritao/OpenAlladin) and **Quicky** (https://github.com/tritao/quicky), both by
  tritao (no licence stated): workflow ideas only (docs/DEVELOPING.md "Credits in full"); no code.
- **thps2-tools** by JayFoxRox (https://github.com/JayFoxRox/thps2-tools): file-format research; `tests/test_pkr_crosscheck.py`
  runs its extractor, when present, as an independent check. Not shipped.

## The game

No file of Tony Hawk's Pro Skater 2 is included: the player supplies their own copy. Tony Hawk's Pro Skater is a trademark
of Activision. THPSource2 is a fan project, not affiliated with or endorsed by Activision, Neversoft or Tony Hawk.
