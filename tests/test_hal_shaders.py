"""Tests for the SDL_GPU shader staleness check (tony/hal/shaders.py,
docs/HOOKS.md, `tony hooks verify`). These never need glslang: they only
exercise the source-digest bookkeeping, not compilation."""

import hashlib

from tony.hal import shaders


def _sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def test_committed_shader_header_matches_committed_sources():
    # Regression test: catches a shader edited without `tony hal shaders`.
    assert shaders.check_staleness() == []


def test_source_digests_in_header_parses_the_macros():
    text = (
        '#define OT_GPU_SHADER_VIDEO_VERT_SHA256 "' + "a" * 64 + '"\n'
        '#define OT_GPU_SHADER_VIDEO_FRAG_SHA256 "' + "b" * 64 + '"\n'
    )
    digests = shaders.source_digests_in_header(text)
    assert digests == {"VIDEO_VERT": "a" * 64, "VIDEO_FRAG": "b" * 64}


def test_source_digests_in_header_missing_macro_is_absent():
    assert shaders.source_digests_in_header("no macros here") == {}


def test_check_staleness_reports_a_missing_header(tmp_path, monkeypatch):
    monkeypatch.setattr(shaders, "SHADER_DIR", str(tmp_path / "shaders"))
    monkeypatch.setattr(shaders, "HEADER_PATH", str(tmp_path / "sdl_video_gpu_shaders.h"))
    problems = shaders.check_staleness()
    assert len(problems) == 1
    assert "run `tony hal shaders`" in problems[0]


def test_check_staleness_passes_when_header_matches_sources(tmp_path, monkeypatch):
    shader_dir = tmp_path / "shaders"
    shader_dir.mkdir()
    header_path = tmp_path / "sdl_video_gpu_shaders.h"
    vert = shader_dir / "video.vert"
    frag = shader_dir / "video.frag"
    vert.write_text("vert v1")
    frag.write_text("frag v1")
    monkeypatch.setattr(shaders, "SHADER_DIR", str(shader_dir))
    monkeypatch.setattr(shaders, "HEADER_PATH", str(header_path))
    digests = {"VIDEO_VERT": _sha256(vert.read_bytes()), "VIDEO_FRAG": _sha256(frag.read_bytes())}
    header_path.write_text(shaders.render_header("0.0.0", digests, {"VIDEO_VERT": b"x", "VIDEO_FRAG": b"y"}))
    assert shaders.check_staleness() == []


def test_check_staleness_flags_a_changed_source(tmp_path, monkeypatch):
    shader_dir = tmp_path / "shaders"
    shader_dir.mkdir()
    header_path = tmp_path / "sdl_video_gpu_shaders.h"
    vert = shader_dir / "video.vert"
    frag = shader_dir / "video.frag"
    vert.write_text("vert v1")
    frag.write_text("frag v1")
    monkeypatch.setattr(shaders, "SHADER_DIR", str(shader_dir))
    monkeypatch.setattr(shaders, "HEADER_PATH", str(header_path))
    digests = {"VIDEO_VERT": _sha256(vert.read_bytes()), "VIDEO_FRAG": _sha256(frag.read_bytes())}
    header_path.write_text(shaders.render_header("0.0.0", digests, {"VIDEO_VERT": b"x", "VIDEO_FRAG": b"y"}))
    vert.write_text("vert v2 (edited, not recompiled)")
    problems = shaders.check_staleness()
    assert len(problems) == 1
    assert "video.vert" in problems[0]
    assert "run `tony hal shaders`" in problems[0]


def test_render_header_round_trips_through_source_digests_in_header():
    digests = {"VIDEO_VERT": "1" * 64, "VIDEO_FRAG": "2" * 64}
    text = shaders.render_header("16.6.0", digests, {"VIDEO_VERT": bytes([1, 2, 3]), "VIDEO_FRAG": bytes([4, 5])})
    assert shaders.source_digests_in_header(text) == digests
    assert "16.6.0" in text
