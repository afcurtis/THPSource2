"""Kit hooks (Phase 5.0): generated adapters for the src/runtime/ Kits, run live by opentony_native.dll."""

import copy

import pytest

from tony import capture, hooks, kit_hooks


def _kit_errors(manifest, check_disassembly=False):
    return kit_hooks.validate(manifest, hooks._symbols(), check_disassembly)


def _kit_hook(manifest, name="CBruce__StateTransition"):
    return next(hook for hook in manifest["hooks"] if hook["name"] == name)


def test_generated_kit_source_is_current_and_deterministic():
    manifest = hooks.load_manifest()
    assert kit_hooks.render_source(manifest) == kit_hooks.render_source(manifest)
    assert hooks.generate(check=True)


def test_every_kit_hook_is_canonical_only_and_reaches_only_live_calls():
    manifest = hooks.load_manifest()
    services = {kit_hooks.service_name(call) for call in manifest["services"]["live_calls"]}
    kit_hooks_in_manifest = [hook for hook in manifest["hooks"] if kit_hooks.is_kit_hook(hook)]
    assert kit_hooks_in_manifest
    for hook in kit_hooks_in_manifest:
        assert hook["kit"] in manifest["kits"]
        assert set(hook["builds"]) == {"canonical"}
        assert set(hook["reaches"]) <= services
    assert _kit_errors(manifest) == []


@pytest.mark.parametrize(
    "mutate, message",
    [
        (lambda hook: hook.update(kit="nokit"), "unknown kit"),
        (lambda hook: hook["builds"].update(original_2000=0x00400000), "canonical build only"),
        (lambda hook: hook.update(abi="thiscall_vec3_vec3_ref"), "has no abi/native"),
        (lambda hook: hook.update(call="state_transition(self, a[2])"), "reads a[2], stack_args is 2"),
        (lambda hook: hook.update(convention="cdecl"), "uses `self` but the convention is cdecl"),
        (lambda hook: hook.update(reaches=["engine_00400000"]), "not a live call"),
        (lambda hook: hook.update(selftest="words"), "needs a cdecl function"),
        (lambda hook: hook.update(returns="no"), "returns must be true or false"),
        (lambda hook: hook.update(default="on"), "default must be `off` or absent"),
    ],
)
def test_kit_rows_are_checked(mutate, message):
    manifest = copy.deepcopy(hooks.load_manifest())
    mutate(_kit_hook(manifest))
    assert any(message in error for error in _kit_errors(manifest))


def test_live_calls_are_checked():
    manifest = copy.deepcopy(hooks.load_manifest())
    calls = manifest["services"]["live_calls"]
    calls.append(dict(calls[0]))
    calls[1]["lead"] = [0, 1]
    errors = _kit_errors(manifest)
    assert any("duplicate service" in error for error in errors)
    assert any("lead must be at most one constant" in error for error in errors)


def test_a_variadic_live_call_must_be_cdecl():
    manifest = copy.deepcopy(hooks.load_manifest())
    call = next(call for call in manifest["services"]["live_calls"] if call["convention"] == "thiscall")
    call["variadic"] = True
    assert any("only a cdecl function can be variadic" in error for error in _kit_errors(manifest))


def test_conventions_are_checked_against_the_retail_ret():
    if not hooks._canonical_exe().is_file() or hooks._objdump() is None:
        pytest.skip("canonical executable or llvm-objdump not present")
    # CBruce__StateTransition: `ret 8` (ECX + two stack arguments); M3dColij_LineToSphere: `ret`
    assert kit_hooks.callee_pops(0x004900B0) == {8}
    assert kit_hooks.callee_pops(0x00464200) == {0}
    # Math_SinQ12 has no `ret`: it ends in `jmp _ftol`, which returns for it
    assert kit_hooks.callee_pops(0x004F39B0) == {0}
    # BmpImage_Destruct: `mov [ecx], vtable; jmp base` and the next listed function is 0x00457810, so a linear
    # scan of its range counts the `ret 4` of the unlisted function at 0x00457700 (5.1 batch d crash)
    assert kit_hooks.callee_pops(0x004576F0) == {0}
    assert kit_hooks.check_convention("x", 0x004900B0, "thiscall", 2) == []
    assert "pops 4 bytes" in kit_hooks.check_convention("x", 0x004900B0, "thiscall", 1)[0]
    assert "pops 24 bytes" in kit_hooks.check_convention("x", 0x00464200, "stdcall", 6)[0]


def test_adapters_take_the_retail_calling_convention():
    source = kit_hooks.render_source(hooks.load_manifest())
    # thiscall: ECX is `self`, EDX ignored, stack arguments callee-popped (__fastcall is the same contract)
    assert ("std::uint32_t __fastcall ot_kit_CBruce__StateTransition(std::uint32_t self, std::uint32_t /*edx*/, "
            "std::uint32_t a0, std::uint32_t a1)") in source
    assert "std::uint32_t __cdecl ot_kit_Math_SqrtTruncated(std::uint32_t a0)" in source
    assert "return U(kit.sqrt_truncated(a[0]));" in source
    assert '{"assert", 0x004011e0u, Convention::Cdecl, 2, 1, 0u, false},  // M3dUtils_InBetween' in source
    # printf_log takes a variable argument list: the host passes as many arguments as the Kits reported
    assert "Convention::Cdecl, 1, 0, 0u, true},  // printf_log" in source


def test_a_struct_returned_by_value_hands_the_output_pointer_back_in_eax():
    # Camera_QuaternionAboutX fills the hidden output pointer (a[0]) and retail returns it: its callers use it
    # (a native that returned 0 crashed Camera_ComposeTransformQ12 in 5.1 batch b)
    source = kit_hooks.render_source(hooks.load_manifest())
    assert "kit.about_x(a[0], S(a[1]));\n        return U(a[0]);" in source
    manifest = copy.deepcopy(hooks.load_manifest())
    hook = next(h for h in manifest["hooks"] if h["name"] == "Camera_QuaternionAboutX")
    assert not any("return_value" in error for error in _kit_errors(manifest))
    hook["returns"] = True
    assert any("return_value goes with returns: false" in error for error in _kit_errors(manifest))
    hook["returns"] = False
    hook["return_value"] = "a[5]"
    assert any("reads a[5]" in error for error in _kit_errors(manifest))


def test_header_gives_kit_hooks_a_fillable_slot():
    header = hooks.render_header()
    assert "static void *ot_kit_CBruce__HandleFlips = 0;" in header
    assert "OT_ABI_KIT, 7u," in header
    assert "&ot_kit_CBruce__HandleFlips, 0," in header


def _call(caller, address="0x0048f3a0"):
    return {"address": address, "caller": caller}


def test_observations_of_calls_from_replaced_functions_are_set_aside():
    timer = {"delivery_source": "timer"}
    record = {
        "type": "frame",
        "events": [
            timer,
            _call("0x0049e831"),        # a retail call site: kept
            _call("0x0048f4d6"),        # nested in the next call (the observed function calling itself)...
            _call("0x0049d4a1"),        # ...made from inside a replaced function: both set aside
            _call("0x0048f4d6"),        # nested in a retail call site's call: kept with it
            _call("0x0049eb25"),
        ],
    }
    dropped = [0]
    result = capture._without_native_call_sites(record, [(0x0049D480, 0x0049D740)], lambda start: start + 0x1F0, dropped)
    assert result["events"] == [timer, _call("0x0049e831"), _call("0x0048f4d6"), _call("0x0049eb25")]
    assert dropped == [2]
    assert capture._without_native_call_sites(record, [], lambda start: start, [0]) is record


def test_a_hook_marked_default_off_stays_retail_until_a_scenario_reaches_it():
    assert hooks.default_on({}, "trace-validated")
    assert not hooks.default_on({"default": "off"}, "trace-validated")
    assert not hooks.default_on({}, "tested")
    off = [hook for hook in hooks.load_manifest()["hooks"] if hook.get("default") == "off"]
    # the loose-file overlay (5.2) is the one hand-written row that is off: the runtime turns it on when a mod has files
    assert off and all(kit_hooks.is_kit_hook(hook) or hook["name"] == "FileIO_BackendOpen" for hook in off)


def test_recorder_seams_are_refused():
    from tony import kit_hooks

    clash = {"hooks": [{"name": "PCInput_KeyDown", "overwrite": 9, "builds": {"canonical": 0x004E41B0}},
                       {"name": "Elsewhere", "overwrite": 6, "builds": {"canonical": 0x004E5000}}]}
    errors = kit_hooks.recorder_seam_errors(clash)
    assert len(errors) == 1 and "PCInput_KeyDown" in errors[0]


def test_no_hook_of_the_manifest_sits_on_a_recorder_seam():
    from tony import hooks, kit_hooks

    assert kit_hooks.recorder_seam_errors(hooks.load_manifest()) == []
