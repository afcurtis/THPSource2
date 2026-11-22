"""`tony hal shaders`: compiles src/hal/sdl/shaders/*.{vert,frag} to SPIR-V
with the pinned glslang (re/config/glslang.yml) and writes the committed
header src/hal/sdl/sdl_video_gpu_shaders.h (docs/HAL.md, "Video", 3.4d).

The header is committed so `tony hooks build` and a Windows MSVC `-A Win32`
build of src/hooks never need glslang themselves; `tony hooks verify` checks
(without glslang) that the header's recorded source digests still match
src/hal/sdl/shaders/*, and fails with "run tony hal shaders" if not."""

from __future__ import annotations

import subprocess
from pathlib import Path

from ..common import ROOT, resolve, sha256

SHADER_DIR = "src/hal/sdl/shaders"
HEADER_PATH = "src/hal/sdl/sdl_video_gpu_shaders.h"
FLAGS = ("-V", "--target-env", "vulkan1.0")

# (source file stem, GLSL source, C identifier stem)
SHADERS = (
    ("video.vert", "VIDEO_VERT", "OtGpuVideoVert"),
    ("video.frag", "VIDEO_FRAG", "OtGpuVideoFrag"),
)


class ShaderError(RuntimeError):
    pass


def source_path(name: str) -> Path:
    return resolve(f"{SHADER_DIR}/{name}")


def header_path() -> Path:
    return resolve(HEADER_PATH)


def compile_shader(glslang_binary, name: str) -> bytes:
    source = source_path(name)
    if not source.is_file():
        raise ShaderError(f"missing shader source: {source}")
    output = source.with_suffix(source.suffix + ".spv.tmp")
    command = [str(glslang_binary), *FLAGS, "-o", str(output), str(source)]
    completed = subprocess.run(command, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                               check=False)
    if completed.returncode != 0 or not output.is_file():
        output.unlink(missing_ok=True)
        raise ShaderError(f"glslang failed on {source.name}:\n{completed.stdout}")
    data = output.read_bytes()
    output.unlink()
    return data


def _format_bytes(data: bytes) -> str:
    lines = []
    for start in range(0, len(data), 16):
        chunk = data[start:start + 16]
        lines.append("    " + ", ".join(f"0x{byte:02x}" for byte in chunk) + ",")
    return "\n".join(lines)


def render_header(release: str, digests: dict, blobs: dict) -> str:
    parts = [
        "/* GENERATED FILE: run `tony hal shaders` to regenerate; do not edit by hand.",
        " *",
        " * SDL_GPU video backend shaders (docs/HAL.md, \"Video\", 3.4d), compiled from",
        f" * {SHADER_DIR}/{{video.vert,video.frag}} to SPIR-V by glslang {release}",
        f" * ({' '.join(FLAGS)}), byte-deterministic. Committed so neither `tony hooks",
        " * build` nor a Windows MSVC -A Win32 build of src/hooks needs glslang;",
        " * `tony hooks verify` checks the source digests below against the current",
        " * shader sources and fails with \"run tony hal shaders\" if they differ. */",
        "",
        "#ifndef OPENTONY_HAL_SDL_VIDEO_GPU_SHADERS_H",
        "#define OPENTONY_HAL_SDL_VIDEO_GPU_SHADERS_H",
        "",
        f'#define OT_GPU_SHADER_GLSLANG_VERSION "{release}"',
        f'#define OT_GPU_SHADER_FLAGS "{" ".join(FLAGS)}"',
    ]
    for _name, macro, _ident in SHADERS:
        parts.append(f'#define OT_GPU_SHADER_{macro}_SHA256 "{digests[macro]}"')
    parts.append("")
    for _name, macro, ident in SHADERS:
        data = blobs[macro]
        parts.append(f"static const unsigned char k{ident}Spv[] = {{")
        parts.append(_format_bytes(data))
        parts.append("};")
        parts.append(f"static const unsigned int k{ident}SpvSize = {len(data)}u;")
        parts.append("")
    parts.append("#endif /* OPENTONY_HAL_SDL_VIDEO_GPU_SHADERS_H */")
    parts.append("")
    return "\n".join(parts)


def generate() -> str:
    """Compiles both shaders and returns the header text (does not write it)."""

    from .. import glslang

    binary = glslang.ensure() / glslang.spec()["binary"]
    digests = {}
    blobs = {}
    for name, macro, _ident in SHADERS:
        digests[macro] = sha256(source_path(name))
        blobs[macro] = compile_shader(binary, name)
    return render_header(glslang.spec()["release"], digests, blobs)


def source_digests_in_header(text: str) -> dict:
    import re

    found = {}
    for _name, macro, _ident in SHADERS:
        match = re.search(rf'#define OT_GPU_SHADER_{macro}_SHA256 "([0-9a-f]{{64}})"', text)
        if match:
            found[macro] = match.group(1)
    return found


def check_staleness() -> list:
    """No glslang needed: compares the header's recorded source digests
    against the current shader sources. Returns a list of problems (empty =
    up to date)."""

    path = header_path()
    if not path.is_file():
        return [f"{HEADER_PATH} does not exist; run `tony hal shaders`"]
    recorded = source_digests_in_header(path.read_text(encoding="utf-8"))
    problems = []
    for name, macro, _ident in SHADERS:
        current = sha256(source_path(name))
        if recorded.get(macro) != current:
            problems.append(f"{SHADER_DIR}/{name} has changed since {HEADER_PATH} was generated; run `tony hal shaders`")
    return problems


def hal_shaders(args) -> int:
    if getattr(args, "check", False):
        problems = check_staleness()
        if problems:
            for problem in problems:
                print(f"FAIL {problem}")
            return 1
        try:
            fresh = generate()
        except Exception as exc:  # noqa: BLE001 - any compile failure (ShaderError, GlslangError, OSError) is a FAIL line, not a traceback
            print(f"FAIL {exc}")
            return 1
        current = header_path().read_text(encoding="utf-8") if header_path().is_file() else ""
        if fresh != current:
            print(f"FAIL {HEADER_PATH} does not match a fresh compile; run `tony hal shaders`")
            return 1
        print(f"OK   {HEADER_PATH} matches a fresh compile of {SHADER_DIR}/*.{{vert,frag}}")
        return 0
    try:
        text = generate()
    except Exception as exc:  # noqa: BLE001 - same as above: report the failure and exit 1
        print(f"FAIL {exc}")
        return 1
    header_path().write_text(text, encoding="utf-8")
    digest_summary = ", ".join(f"{macro}={sha256(source_path(name))[:12]}" for name, macro, _ident in SHADERS)
    print(f"wrote {header_path()} ({digest_summary})")
    return 0


def register(hal_sub) -> None:
    p = hal_sub.add_parser("shaders", help="compile src/hal/sdl/shaders/*.{vert,frag} to the committed SPIR-V header")
    p.add_argument("--check", action="store_true", help="verify the committed header matches a fresh compile; no write")
    p.set_defaults(func=hal_shaders)
