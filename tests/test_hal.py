"""Tests for the platform boundary audit (tony/hal)."""

import pytest
import yaml

from tony.common import load_yaml, resolve
from tony.hal import com
from tony.hal.expr import linear, location, parse, render, substitute
from tony.hal.infer import Engine, call_shape, overrides_for, vtable_shape
from tony.hal.inventory import GENERATED_MARKER, SERVICES, STATUSES, VariantMap, _row, split_document

# ------------------------------------------------------------------ expressions


def test_parse_normalizes_constants_and_stack_addresses():
    assert parse("(0x10+0x20)") == ("const", 0x30)
    assert parse("*(0x6a05c8)") == ("g", 0x6A05C8)
    assert parse("*(&S-20)") == ("s", -20)
    assert parse("(&S-20+0x4)") == ("sa", -16)
    assert parse("-0x2") == ("const", 0xFFFFFFFE)
    assert parse("phi{0x1|0x1}") == ("const", 1)


def test_parse_keeps_unresolved_register_inputs():
    value = parse("*((in:(register, 0x18, 4)+0x4))")
    assert value[0] == "load" and value[1][1][0] == "unk"


def test_linear_splits_constant_and_terms():
    constant, terms = linear(parse("((0x29d6920+(arg0*0xa)*0x4)+0x4)"))
    assert constant == 0x29D6924 and len(terms) == 1


def test_location_kinds():
    function = 0x401000
    assert location(parse("0x29da3a8"), function) == ("G", 0x29DA3A8)
    assert location(parse("&S-20"), function) == ("L", function, -20)
    assert location(parse("(arg0+0x4)"), function) == ("F", function, ("arg", 0), 4)
    assert location(parse("(0x29d6920+(arg1*0x4))"), function) == ("T", 0x29D6920)
    assert location(parse("(G0x6a04e8+0x8)"), function) == ("F", None, ("g", 0x6A04E8), 8)


def test_substitute_maps_callee_parameters_into_caller_context():
    callee = parse("*((arg0+0x4))")
    assert substitute(callee, {0: ("const", 0x6A4500)}) == ("g", 0x6A4504)
    assert substitute(parse("S-4"), {0: ("const", 1)}) is None


def test_render_is_readable():
    assert render(parse("*((*(G0x29da3c0)+0x50))")) == "*((*([0x29da3c0])+0x50))"


# -------------------------------------------------------------------------- COM


def test_interface_layouts_match_the_sdk():
    assert com.method("IDirect3DDevice7", 0x50) == "SetRenderState"
    assert com.method("IDirect3DDevice7", 0x94) == "SetTextureStageState"
    assert com.method("IDirectDrawSurface7", 0x2C) == "Flip"
    assert com.method("IDirectDrawSurface7", 0x64) == "Lock"
    assert com.method("IDirectInputDeviceA", 0x24) == "GetDeviceState"
    assert com.method("IDirectSoundBuffer", 0x30) == "Play"
    assert com.method("IDirectPlay4A", 0x7C) == "SetSessionDesc"
    assert com.arity("IDirect3DDevice7", 0x64) == 5  # DrawPrimitive
    assert com.arity("IDirect3DDevice7", 0x28) == 6  # Clear (z is a float)
    assert com.arity("IDirectSoundBuffer", 0x2C) == 7  # Lock
    assert com.method("IDirectDraw7", 0x1000).startswith("<vtable")


def test_guid_names_roundtrip():
    for name, text in com.GUIDS.items():
        assert com.guid_name(com.guid_bytes(text)) == name


def test_producers_and_callbacks_name_real_methods():
    for (interface, method), (index, result) in com.PRODUCERS.items():
        assert method in com.INTERFACES[interface]
        assert index < dict(com.METHODS[interface])[method]
        assert isinstance(result, int) or result in com.INTERFACES
    for key, (callback, context, _parameter) in com.CALLBACKS.items():
        if isinstance(key, tuple):
            arity = dict(com.METHODS[key[0]])[key[1]]
            assert callback < arity and (context is None or context < arity)


# ------------------------------------------------------------------ propagation


class _Image:
    def __init__(self, guids):
        self.guids = guids

    def read(self, address, size):
        return self.guids[address]


def _facts(functions):
    return {"iat": {"0x518000": "DDRAW.DLL!DirectDrawCreateEx"}, "thunks": {}, "functions": functions}


def _function(address, calls=(), stores=(), returns=(), params=()):
    return {"a": address, "name": f"FUN_{address:08x}", "calls": [list(c) for c in calls],
            "stores": [list(s) for s in stores], "ret": list(returns), "params": list(params)}


def _image():
    return _Image({0x51B4A8: com.guid_bytes(com.GUIDS["IID_IDirectDraw7"]),
                   0x51B4B8: com.guid_bytes(com.GUIDS["IID_IDirect3D7"])})


def test_factory_and_producers_type_globals_and_sites():
    init = _function(0x401000, calls=[
        (0x401010, "d", "0x518000", ["0x0", "0x29da3a8", "0x51b4a8", "0x0"]),
        (0x401020, "i", "*(*(G0x29da3a8))", ["G0x29da3a8", "0x51b4b8", "0x29da3bc"]),
        (0x401030, "i", "*((*(G0x29da3bc)+0x10))", ["G0x29da3bc", "0x51b4a8", "G0x29da3b0", "0x29da3c0"]),
    ])
    draw = _function(0x402000, calls=[(0x402010, "i", "*((*(G0x29da3c0)+0x50))", ["G0x29da3c0", "0x16", "0x1"])])
    result = Engine(_facts([init, draw]), _image()).run()
    by_site = {site.site: f"{site.interface}::{site.method}" for site in result.com_sites}
    assert by_site == {
        0x401020: "IDirectDraw7::QueryInterface",
        0x401030: "IDirect3D7::CreateDevice",
        0x402010: "IDirect3DDevice7::SetRenderState",
    }
    assert overrides_for(result)[0x402010] == (0x402000, 3)


def test_field_types_flow_to_callers_but_inherited_ones_do_not_flow_back():
    # 0x401000 creates the object into its own this->0x4 and is called with two globals.
    create = _function(0x401000, calls=[(0x401010, "d", "0x518000", ["0x0", "(arg0+0x4)", "0x51b4a8", "0x0"])])
    caller = _function(0x402000, calls=[(0x402010, "d", "0x401000", ["0x6a4500"])])
    # A generic helper called with the typed object and with an unrelated one.
    helper = _function(0x403000, calls=[(0x403010, "i", "*((*(*((arg0+0x4)))+0x8))", ["*((arg0+0x4))"])])
    good = _function(0x404000, calls=[(0x404010, "d", "0x403000", ["0x6a4500"])])
    other = _function(0x405000, calls=[(0x405010, "d", "0x403000", ["0x700000"])])
    user = _function(0x406000, calls=[(0x406010, "i", "*((*(G0x700004)+0xc))", ["G0x700004"])])
    result = Engine(_facts([create, caller, helper, good, other, user]), _image()).run()
    typed_sites = {site.site for site in result.com_sites}
    assert 0x403010 in typed_sites  # helper inherited the type from its typed caller
    assert 0x406010 not in typed_sites  # ...but it must not leak into the other caller's object
    assert ("G", 0x6A4504) in result.typed


def test_returns_are_typed_only_when_every_path_agrees():
    make = _function(0x401000, calls=[(0x401010, "d", "0x518000", ["0x0", "0x29da3a8", "0x51b4a8", "0x0"])])
    mixed = _function(0x402000, returns=["phi{G0x29da3a8|0xffffffff}"])
    clean = _function(0x403000, returns=["phi{G0x29da3a8|0x0}"])
    engine = Engine(_facts([make, mixed, clean]), _image())
    engine.run()
    assert ("R", 0x403000) in engine.typed and ("R", 0x402000) not in engine.typed


def test_declared_roots_and_not_com_sites():
    function = _function(0x401000, calls=[(0x401010, "i", "*((*(*(arg0))+0x8))", ["*(arg0)"]),
                                          (0x401020, "i", "*((*(G0x600000)+0xc))", ["G0x600000"])])
    roots = [{"function": 0x401000, "value": "*(arg0)", "interface": "IDirectDrawSurface7"},
             {"site": 0x401020, "not_com": "thiscall"}]
    result = Engine(_facts([function]), _image(), roots).run()
    assert [(s.site, s.method) for s in result.com_sites] == [(0x401010, "Release")]
    assert [row[0] for row in result.not_com] == [0x401020]


@pytest.mark.parametrize("args, evidence, shape", [
    ([parse("G0x29da3c0"), ("const", 0x16)], None, "com"),
    ([("const", 0x16)], None, "thiscall"),
    ([], None, "thiscall"),
    ([("unk", "?eax")], None, "unknown"),
    ([("unk", "?eax")], ["ecx", "none"], "thiscall"),
    ([("unk", "?eax")], ["eax", "eax"], "com"),
])
def test_call_shape(args, evidence, shape):
    assert call_shape(args, parse("G0x29da3c0"), evidence) == shape


def test_vtable_shape():
    assert vtable_shape(parse("*((*(G0x29da3c0)+0x50))")) == (("g", 0x29DA3C0), 0x50)
    assert vtable_shape(parse("*(*(arg0))")) == (("arg", 0), 0)
    assert vtable_shape(parse("*((0x65f82c+(arg0*0x4)))")) is None


# -------------------------------------------------------------------- inventory


def test_generated_rows_render_addresses_as_hex():
    assert _row([0x4F8130, None, "WINMAIN_InitDirectDraw", 3]) == \
        '  - [0x004f8130, null, "WINMAIN_InitDirectDraw", 3]'


def test_split_document_keeps_curated_head():
    head, tail = split_document("version: 1\n\n" + GENERATED_MARKER + "\ngenerated: {}\n")
    assert head == "version: 1\n" and tail.startswith(GENERATED_MARKER)


def test_variant_map_interpolates_between_identical_neighbours():
    mapping = VariantMap({0x401000: 0x400F00, 0x401100: 0x401000}, {0x401050: 0x400FA0}, [0x401000, 0x401100], [])
    assert mapping.get(0x401000) == 0x400F00
    assert mapping.get(0x401050) == 0x400FA0  # data map
    assert mapping.get(0x401080) == 0x400F80  # same spacing on both sides
    uneven = VariantMap({0x401000: 0x400F00, 0x401100: 0x401010}, {}, [0x401000, 0x401100], [])
    assert uneven.get(0x401080) is None


def test_curated_boundary_is_consistent():
    document = load_yaml("re/config/hal_boundary.yml")
    curated = {int(item["address"]): item["name"] for item in load_yaml("re/symbols/functions.yml")["functions"]}
    seen = set()
    for hook in document["hook_points"]:
        assert hook["service"] in SERVICES
        assert curated.get(int(hook["canonical"])) == hook["name"], hook["name"]
        for address in [hook["canonical"], *hook.get("owns", [])]:
            assert address not in seen, hex(address)
            seen.add(address)
    for group in document["groups"]:
        assert group["status"] in STATUSES
    for root in document["roots"]["canonical"]:
        assert ("interface" in root) != ("not_com" in root)
        if "interface" in root:
            assert root["interface"] in com.INTERFACES


def test_hal_entry_sites_are_validated():
    import copy

    from tony import hooks

    manifest = hooks.load_manifest()
    hal = manifest["services"]["hal"]
    assert {site["role"] for site in hal["entry_sites"]} == {"startup", "shutdown", "window", "video", "draw_list", "texture"}
    assert hooks._validate_hal_sites(manifest, {}, hooks._symbols(), False) == []
    broken = copy.deepcopy(manifest)
    broken["services"]["hal"]["entry_sites"][0]["role"] = "teardown"
    broken["services"]["hal"]["entry_sites"][1]["name"] = "Game_FrameTick"
    broken["services"]["hal"]["onexit"]["builds"]["original_2000"] = 0x4FD214
    errors = hooks._validate_hal_sites(broken, {}, hooks._symbols(), False)
    assert any("role must be one of" in error for error in errors)
    assert any("is not the curated function Game_FrameTick" in error for error in errors)
    assert any("hal.onexit: original_2000 address disagrees" in error for error in errors)


def test_sdl3_pin_is_complete():
    pin = load_yaml("re/config/sdl3.yml")["sdl3"]
    assert pin["url"].endswith(f"SDL3-devel-{pin['release']}-VC.zip")
    assert len(pin["sha256"]) == 64 and int(pin["size"]) > 0
    assert pin["dll_x86"] == "lib/x86/SDL3.dll"


def test_generated_inventory_is_well_formed():
    generated = load_yaml("re/config/hal_boundary.yml")["generated"]
    summary = generated["summary"]
    assert summary["sites"] == len(generated["sites"])
    assert summary["functions"] == len(generated["functions"])
    assert summary["hook_points"] == len(generated["hook_points"])
    assert summary["instruction_import_references"] == summary["sites_by_kind"]["import"] + \
        summary["sites_by_kind"]["import-jump"]
    for site, _variant, _function, kind, target in generated["sites"]:
        assert kind in ("import", "import-jump", "com")
        if kind == "com":
            interface, method = target.split("::")
            assert method in com.INTERFACES[interface]
    text = resolve("re/config/hal_boundary.yml").read_text()
    assert yaml.safe_load(split_document(text)[0])["version"] == 1


# ------------------------------------------------------------------ interface headers

HAL_HEADERS = ("hal.h", "clock.h", "window.h", "input.h", "video.h", "audio.h", "movie.h", "fs.h")
# Word-level: the backend selector OT_HAL_SDL is fine, SDL_Window is not.
PLATFORM_TOKENS = ("windows.h", "SDL_[A-Za-z]", "SDL3", "HWND", "DWORD", "LPDIRECT", "IDirect", "D3D[A-Z]", "HRESULT", "dinput")


def test_hal_headers_are_platform_free():
    import re

    for name in HAL_HEADERS:
        text = resolve("src/hal") / name
        code = re.sub(r"/\*.*?\*/", "", text.read_text(), flags=re.DOTALL)  # comments may name retail APIs
        for token in PLATFORM_TOKENS:
            assert not re.search(token, code), f"{name} mentions {token}"
        assert "#include <" not in code.replace("#include <stdint.h>", ""), f"{name} includes a system header"


def test_hal_replacements_are_validated():
    import copy

    from tony import hooks

    manifest = hooks.load_manifest()
    hal = manifest["services"]["hal"]
    services = {entry["service"] for entry in hal["replacements"]}
    assert services == {"clock", "input", "video", "movie", "audio"}
    assert hooks._validate_hal_replacements(manifest, {}, hooks._symbols()) == []
    broken = copy.deepcopy(manifest)
    replacements = broken["services"]["hal"]["replacements"]
    replacements[0]["service"] = "input"  # D3DTimer_Init is a clock hook point
    replacements[1]["name"] = "Game_FrameTick"
    replacements[2]["builds"]["original_2000"]["address"] = 0x004F3604
    symbols = broken["services"]["hal"]["symbols"]
    symbols[0]["builds"]["original_2000"] = 0x004FD214
    next(entry for entry in symbols if entry["name"] == "ActionMask")["kind"] = "function"  # it is data
    symbols.append(dict(symbols[1]))
    errors = hooks._validate_hal_replacements(broken, {}, hooks._symbols())
    assert any("not a input hook point" in error for error in errors)
    assert any("is not the curated function Game_FrameTick" in error for error in errors)
    assert any("no OT_REGISTER_HAL_ADAPTER(Game_FrameTick" in error for error in errors)
    assert any("WINMAIN_Sleep: original_2000 address 0x004f3604 disagrees" in error for error in errors)
    assert any("_onexit: original_2000 address 0x004fd214 disagrees" in error for error in errors)
    assert any("is not the curated function ActionMask" in error for error in errors)
    assert any("duplicate symbol" in error for error in errors)


def test_video_adapters_take_over_the_entry_sites_they_replace():
    """With video=sdl the runtime leaves these entry sites unpatched (their
    function becomes `jmp adapter`); the adapters do the sites' work."""

    from tony import hooks
    from tony.common import resolve

    hal = hooks.load_manifest()["services"]["hal"]
    replaced = {
        entry["builds"]["canonical"]["address"]: entry["name"]
        for entry in hal["replacements"]
        if entry["service"] == "video"
    }
    overlapping = {site["name"]: site["role"] for site in hal["entry_sites"]
                   if site["builds"]["canonical"]["address"] in replaced}
    assert overlapping == {"WINMAIN_InitDirectDraw": "window", "D3D_DrawPolygonList": "draw_list"}
    source = (resolve("src/hooks/hal") / "video_hal.cpp").read_text()
    assert "ot_hal_adopt_main_window();" in source  # the window site's work
    assert "OT_VIDEO_SHADOW_SOLE" in source  # the draw_list site's walk, drawing for real
    runtime = (resolve("src/hooks/hal") / "hal_runtime.cpp").read_text()
    assert "site_replaced(" in runtime and "install_deferred_sites()" in runtime


def test_movie_service_follows_video():
    """video=sdl replaces the primary the full-screen movies decode into, so
    it turns movie=sdl on (and movie=sdl needs video=sdl's backend)."""

    import re

    from tony import hooks
    from tony.common import resolve

    hal = hooks.load_manifest()["services"]["hal"]
    movie = [entry["name"] for entry in hal["replacements"] if entry["service"] == "movie"]
    assert movie == ["PCMovie_PlayStartupMedia"]
    symbols = {entry["name"] for entry in hal["symbols"]}
    source = (resolve("src/hooks/hal") / "movie_hal.cpp").read_text()
    for name in re.findall(r"OT_HAL_SYM_(\w+)", source):
        assert name in symbols, name
    runtime = (resolve("src/hooks/hal") / "hal_runtime.cpp").read_text()
    assert "video=sdl needs movie=sdl" in runtime and "movie=sdl needs video=sdl" in runtime


def test_audio_service_is_a_stand_in_directsound():
    """audio=sdl replaces Audio_Init only; the stand-in keeps DirectSound's
    rules where retail and Bink can see them."""

    from tony import hooks
    from tony.common import resolve

    hal = hooks.load_manifest()["services"]["hal"]
    assert [entry["name"] for entry in hal["replacements"] if entry["service"] == "audio"] == ["Audio_Init"]
    source = (resolve("src/hooks/hal") / "audio_hal.cpp").read_text()
    assert source.count("OT_SLOT(buffer_") == 21 and source.count("OT_SLOT(device_") == 11
    assert "offset %= shared->size;" in source  # Bink locks at offset == size when the ring is full
    assert "kControlUnavailable" in source and "kInvalidParam" in source


def test_run_unmute_is_not_a_game_argument():
    from tony.cli import parse_args

    args = parse_args(["run", "--headless", "--unmute", "-level", "3"])
    assert args.headless and args.unmute
    assert args.game_args == ["-level", "3"]


def test_every_hal_adapter_is_a_replacement():
    from tony import hooks

    names = {entry["name"] for entry in hooks.load_manifest()["services"]["hal"]["replacements"]}
    assert set(hooks._registered_hal_adapters()) == names


def test_generated_header_indexes_hal_symbols():
    from tony import hooks

    manifest = hooks.load_manifest()
    header = hooks.render_header(manifest)
    for index, entry in enumerate(manifest["services"]["hal"]["symbols"]):
        assert f"OT_HAL_SYM_{entry['name']} = {index}," in header
    assert "&ot_hal_adapter_PCInput_Poll," in header


def test_hal_parity_variants_compare_against_all_retail():
    from tony import hooks

    variants = hooks.hal_parity_variants("input=sdl,clock=sdl")
    assert variants[0].hal == "none" and variants[0].hooks == "none"
    assert all(variant.expect_equal for variant in variants)
    assert {variant.hal for variant in variants[1:]} == {"input=sdl,clock=sdl"}
    assert any(variant.stress_ms for variant in variants)


# ------------------------------------------------------------------ video oracle


def _otvs(records: list[tuple[int, int, bytes]], frame: int = 7) -> bytes:
    import struct

    body = b"".join(struct.pack("<HHI", kind, flags, len(payload)) + payload for kind, flags, payload in records)
    return b"OTVS" + struct.pack("<3I", 1, frame, len(records)) + body


def test_video_trace_decodes_states_and_fans(tmp_path):
    import struct

    from tony.hal import video

    vertex = struct.pack("<4fI2f", 10.0, 20.0, 0.5, 1.0, 0x80FF0000, 0.25, 0.75)
    draw = struct.pack("<5I", 6, 0x144, 3, 0, 28) + vertex * 3
    path = tmp_path / "frame_00007.otvs"
    path.write_bytes(_otvs([
        (4, video.SNAPSHOT, struct.pack("<2I", 23, 7)),
        (5, 0, struct.pack("<3I", 0, 1, 4)),
        (6, 0, struct.pack("<2IQ", 0, 1, 0x1234)),
        (7, 0, draw),
        (2, 0, b""),
    ]))
    frame = video.read_frame(path)
    assert frame.frame == 7 and not frame.truncated
    fan = frame.records[3].fields
    assert fan["primitive"] == "triangle_fan" and len(fan["vertices"]) == 3
    assert fan["vertices"][0]["color"] == 0x80FF0000 and fan["vertices"][2]["u"] == 0.25
    summary = video.summarize(frame)
    assert summary["primitives"] == {"triangle_fan": 1} and summary["textures"] == 1
    assert summary["states"]["z_func"] == {"7": 1} and summary["fvf"] == {"0x144": 1}
    path.write_bytes(path.read_bytes() + b"x")
    with pytest.raises(video.VideoTraceError):
        video.read_frame(path)


def test_video_image_oracle_measures_565_differences(tmp_path):
    import struct

    from tony.hal import video

    def image(pixel: int) -> video.Image:
        return video.Image(4, 2, 16, (0xF800, 0x07E0, 0x001F, 0), 0x40, struct.pack("<H", pixel) * 8)

    same = video.compare_images(image(0x1234), image(0x1234), tmp_path / "diff.png")
    assert same["psnr"] == 99.0 and same["within_tolerance"] == 1.0 and same["pass"]
    assert (tmp_path / "diff.png").read_bytes()[:8] == b"\x89PNG\r\n\x1a\n"
    off = video.compare_images(image(0x0000), image(0xFFFF))
    assert off["max_difference"] == 255 and not off["pass"]
    assert video.fvf_stride(0x144) == 28 and video.fvf_stride(0x1C4) == 32


def test_video_ingame_oracle_pairs_retail_and_hal_frames(tmp_path, capsys):
    import argparse
    import struct

    from tony.hal import commands

    def ottx(path, bits, masks, pixels):
        header = struct.pack("<4s11I", b"OTTX", 1, 4, 2, bits, *masks, 0x40, 0, 0)
        path.write_bytes(header + pixels)

    retail = tmp_path / "retail"
    hal = tmp_path / "hal"
    retail.mkdir()
    hal.mkdir()
    ottx(retail / "frame_00010.ottx", 16, (0xF800, 0x07E0, 0x001F, 0), struct.pack("<H", 0xF800) * 8)  # red
    ottx(hal / "frame_00010.hal.ottx", 32, (0xFF0000, 0xFF00, 0xFF, 0), struct.pack("<I", 0xFFFF0000) * 8)
    ottx(retail / "frame_00011.ottx", 16, (0xF800, 0x07E0, 0x001F, 0), struct.pack("<H", 0x001F) * 8)  # blue
    ottx(hal / "frame_00011.hal.ottx", 32, (0xFF0000, 0xFF00, 0xFF, 0), struct.pack("<I", 0xFFFF0000) * 8)
    ottx(retail / "frame_00012.ottx", 16, (0xF800, 0x07E0, 0x001F, 0), struct.pack("<H", 0) * 8)
    arguments = argparse.Namespace(action="ingame", directory=str(retail), hal=str(hal), frame=None, json=False)
    assert commands.hal_video(arguments) == 1
    output = capsys.readouterr().out
    assert "PASS    frame    10" in output and "FAIL    frame    11" in output and "MISSING frame    12" in output
    assert "1/3 frames pass" in output
    assert (hal / "frame_00010.hal.png").is_file() and (hal / "frame_00011.hal.diff.png").is_file()
    arguments.frame = 10
    assert commands.hal_video(arguments) == 0


def test_video_shadow_compare_tool(tmp_path):
    """The primitive-stream oracle tool agrees with a hand-made pair."""

    import struct
    import subprocess

    from tony.common import resolve

    tool = resolve("build/native/opentony_video_shadow_compare")
    if not tool.is_file():
        pytest.skip("opentony_video_shadow_compare not built")
    vertex = struct.pack("<4fI2f", 1.0, 2.0, 0.5, 1.0, 0xFF808080, 0.0, 0.0)
    stream = _otvs([
        (4, 0, struct.pack("<2I", 27, 1)),   # alpha blend
        (4, 0, struct.pack("<2I", 19, 5)),   # SRCALPHA
        (4, 0, struct.pack("<2I", 20, 6)),   # INVSRCALPHA
        (4, 0, struct.pack("<2I", 7, 0)),    # no z
        (5, 0, struct.pack("<3I", 0, 1, 3)),  # colour op: diffuse
        (7, 0, struct.pack("<5I", 6, 0x44, 3, 0, 20) + vertex[:20] * 3),  # untextured fan, 20-byte vertices
    ])
    retail = tmp_path / "frame_00001.otvs"
    retail.write_bytes(stream)
    hal_vertex = struct.pack("<4fII2f", 1.0, 2.0, 0.5, 1.0, 0xFF808080, 0xFF000000, 0.0, 0.0)
    flags = 0x80 | 0x200  # blend, perspective
    state = struct.pack("<II4BI", 0, flags, 4, 5, 0, 0, 0)
    draw = state + struct.pack("<2I", 2, 3) + hal_vertex * 3
    shadow = tmp_path / "frame_00001.otvh"
    shadow.write_bytes(b"OTVH" + struct.pack("<3I", 1, 1, 1) + struct.pack("<HHI", 1, 0, len(draw)) + draw)
    same = subprocess.run([str(tool), str(retail), str(shadow)], capture_output=True, text=True, check=False)
    assert same.returncode == 0, same.stdout
    shadow.write_bytes(shadow.read_bytes()[:-4] + struct.pack("<f", 0.25))  # last v differs
    different = subprocess.run([str(tool), str(retail), str(shadow)], capture_output=True, text=True, check=False)
    assert different.returncode == 1 and "vertex 2" in different.stdout
