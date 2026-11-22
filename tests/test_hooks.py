import copy
import os

import pytest

from tony import hooks


def _quick_validate(manifest):
    errors, _notes = hooks.validate(
        manifest, check_executables=False, check_disassembly=False, check_header=False
    )
    return errors


def test_manifest_is_valid_and_header_current():
    errors, _notes = hooks.validate()
    assert errors == []
    assert hooks.generate(check=True)


def test_header_generation_is_deterministic():
    assert hooks.render_header() == hooks.render_header()


def test_every_hook_has_registered_native_and_fault_variants():
    registered = hooks._registered_natives()
    for hook in hooks.load_manifest()["hooks"]:
        if "kit" in hook:
            continue  # a Kit hook's adapter is generated (tests/test_kit_hooks.py)
        assert registered[("native", hook["name"])] == hook["native"]
        assert (("fault", hook["name"]) in registered) == bool(hook.get("fault"))


@pytest.mark.parametrize(
    "mutate, message",
    [
        (lambda hook: hook.update(expected="8b c1"), "expected has 2 bytes"),
        (lambda hook: hook.update(overwrite=4, expected="8b c1 56 8b"), "overwrite must be"),
        (lambda hook: hook.update(abi="cdecl_magic"), "unknown abi"),
        (lambda hook: hook.update(name="Math_Vector3Mul"), "re/symbols/functions.yml names"),
        (lambda hook: hook["builds"].update(original_2000=0x004c7c54), "disagrees with"),
        (lambda hook: hook.update(native="ot_Something"), "no OT_REGISTER_NATIVE"),
    ],
)
def test_manifest_rejects_inconsistent_hooks(mutate, message):
    manifest = copy.deepcopy(hooks.load_manifest())
    mutate(manifest["hooks"][0])
    assert any(message in error for error in _quick_validate(manifest))


def test_manifest_rejects_duplicate_hooks():
    manifest = copy.deepcopy(hooks.load_manifest())
    manifest["hooks"].append(copy.deepcopy(manifest["hooks"][0]))
    assert any("duplicate hook" in error for error in _quick_validate(manifest))


def test_native_status_drives_default(monkeypatch):
    statuses = hooks._native_statuses()
    first = hooks.load_manifest()["hooks"][0]
    address = first["builds"]["canonical"]
    promoted = {key: dict(value) for key, value in statuses.items()}
    for status, default in (("tested", "off"), ("trace-validated", "on"), ("integrated", "on")):
        promoted[address]["status"] = status
        monkeypatch.setattr(hooks, "_native_statuses", lambda promoted=promoted: promoted)
        assert f"/* {first['name']}: native status {status} -> default {default} */" in hooks.render_header()


def test_unhookable_native_status_is_rejected(monkeypatch):
    statuses = {key: dict(value) for key, value in hooks._native_statuses().items()}
    address = hooks.load_manifest()["hooks"][0]["builds"]["canonical"]
    statuses[address]["status"] = "modeled"
    monkeypatch.setattr(hooks, "_native_statuses", lambda: statuses)
    assert any("not hookable" in error for error in _quick_validate(hooks.load_manifest()))


def test_clock_sites_are_validated():
    manifest = copy.deepcopy(hooks.load_manifest())
    clock = manifest["services"]["lockstep_clock"]
    clock["delivery_sites"][0]["kind"] = "sometimes"
    clock["timer_state"]["builds"]["original_2000"] = 0x0069D1F4
    errors = _quick_validate(manifest)
    assert any("kind must be one of" in error for error in errors)
    assert any("timer_state: original_2000 address disagrees" in error for error in errors)


def test_clock_rendering_lists_sites_in_manifest_order():
    header = hooks.render_header()
    sites = hooks.load_manifest()["services"]["lockstep_clock"]["delivery_sites"]
    positions = [header.index(f'{{"{site["name"]}", ') for site in sites]
    assert positions == sorted(positions)
    assert '"Game_FrameTick", OT_CLOCK_SITE_PUMP' in header


@pytest.mark.skipif(not hooks._canonical_exe().is_file(), reason="canonical executable not extracted")
def test_pe_reader_and_instruction_boundaries():
    image = hooks.PeImage(hooks._canonical_exe())
    assert image.timestamp == 0x3A54C1F6
    assert image.read(0x004CA9F0, 7) == bytes.fromhex("8bc1568b4c2408")
    if hooks._objdump() is None:
        pytest.skip("llvm-objdump not installed")
    assert hooks._check_boundaries(image.path, "Math_Vector3Add", 0x004CA9F0, 7) == []
    # 6 bytes would split `mov ecx, [esp+8]`.
    assert any("instruction boundary" in error for error in hooks._check_boundaries(image.path, "x", 0x004CA9F0, 6))
    # Math_Vector3Equal's prologue is followed by `jne`; covering it must fail.
    assert any("inside the patched prologue" in error for error in hooks._check_boundaries(image.path, "x", 0x004CAB50, 11))


def test_merge_overrides_replaces_only_the_named_dll():
    assert hooks._merge_overrides(None, "dinput=n,b") == "dinput=n,b"
    assert hooks._merge_overrides("mscoree,mshtml=;dinput=b", "dinput=n,b") == "mscoree,mshtml=;dinput=n,b"


def test_baseline_environment_forces_builtin_dinput(monkeypatch):
    monkeypatch.setenv("OPENTONY_HOOKS", "all")
    monkeypatch.delenv("WINEDLLOVERRIDES", raising=False)
    with hooks.hooks_environment(None):
        assert "OPENTONY_HOOKS" not in os.environ
        assert os.environ["WINEDLLOVERRIDES"] == "dinput=b"
    assert os.environ["OPENTONY_HOOKS"] == "all"
    assert "WINEDLLOVERRIDES" not in os.environ


def test_hooked_environment_stages_and_restores(monkeypatch, tmp_path):
    staged = []
    monkeypatch.setattr(hooks, "default_game_dir", lambda: tmp_path)
    monkeypatch.setattr(hooks, "staged_current", lambda directory: False)
    monkeypatch.setattr(hooks, "stage", lambda directory: staged.append(directory))
    monkeypatch.setattr(hooks, "runtime_dir", lambda: tmp_path / "runtime")
    monkeypatch.delenv("OPENTONY_CLOCK", raising=False)
    with hooks.hooks_environment("all,-Math_Vector3Sub", "lockstep-fast"):
        assert os.environ["OPENTONY_HOOKS"] == "all,-Math_Vector3Sub"
        assert os.environ["OPENTONY_CLOCK"] == "lockstep-fast"
        assert os.environ["WINEDLLOVERRIDES"].endswith("dinput=n,b")
        assert os.environ["OPENTONY_LOG_DIR"].startswith("Z:\\")
    assert staged == [tmp_path]
    assert "OPENTONY_CLOCK" not in os.environ


def test_clock_requires_hooks():
    with pytest.raises(hooks.HookError), hooks.hooks_environment(None, "lockstep"):
        pass


def test_stage_refuses_user_game_inputs(tmp_path):
    with pytest.raises(hooks.HookError):
        hooks.stage(hooks.resolve("game/variants/original-2000"))


def test_stage_never_overwrites_another_mods_proxy(tmp_path):
    foreign = tmp_path / "dinput.dll"
    foreign.write_bytes(b"MZ some other dinput proxy")
    with pytest.raises(hooks.HookError, match="another mod"):
        hooks.stage(tmp_path)
    assert foreign.read_bytes() == b"MZ some other dinput proxy"


def test_read_stats_parses_hook_lines(tmp_path):
    (tmp_path / "opentony.stats").write_text(
        "build canonical\r\nmaster 1\r\nstress_flips 0\r\nclock 2 deliveries 498\r\n"
        "hook Math_Vector3Add installed=1 enabled=1 fault=0 active=1 native_calls=812 retail_calls=0\r\n"
    )
    stats = hooks.read_stats(tmp_path)
    assert stats == {
        "Math_Vector3Add": {
            "installed": 1, "enabled": 1, "fault": 0, "active": 1, "native_calls": 812, "retail_calls": 0,
        }
    }


def test_parity_matrix_has_reference_repeat_and_negative_control():
    variants = hooks.default_parity_variants()
    labels = [variant.label for variant in variants]
    assert labels[:2] == ["retail-a", "retail-b"]
    assert all(variant.hooks == "none" for variant in variants[:2])
    assert any(variant.stress_ms for variant in variants)
    assert any(not variant.expect_equal and variant.hooks.startswith("fault:") for variant in variants)
