// Replays retail forced calls (Phase 4.13a) through the native PC platform layer (the pad, the PC input bindings, the options file and the PC file backends):
//
//   opentony_platform_retail_calls_test FIXTURE.txt [FIXTURE.txt ...]
//
// A fixture (scripts/make_career_calls_fixture.py, from re/gdb/probes/frontend_calls.py with the groups of platform_specs*.py) holds the retail memory regions the ported code touches
// and, for every case, the state, the arguments and what the retail function did; see retail_calls_replay.hpp for the format and the comparison.
#include "pad_input.hpp"
#include "pc_system.hpp"
#include "retail_calls_replay.hpp"

#include <memory>

using namespace opentony::runtime;
using namespace opentony::runtime::replay;
using namespace opentony::runtime::pcplatform;

namespace {

struct Ctx {
    RetailMemory mem;
    RetailServices svc;
    PadInput pad;
    PcSystem sys;
    explicit Ctx(RetailMemory memory) : mem(std::move(memory)), pad(mem, svc), sys(mem, svc, pad) {
        if (!mem.contains(kScratchBase, 0x10)) {
            mem.add_region(kScratchBase, kScratchBytes);         // (the stack locals of the retail functions)
        }
    }
    Ctx(const Ctx&) = delete;
    Ctx& operator=(const Ctx&) = delete;
};

using PlatformAdapter = Adapter<Ctx>;

// A(expr): the function returns expr. V(expr): it returns nothing the callers use. `a` are the stack arguments, `self` is `this`.
#define A(expr) [](Ctx& c, const Args& a, std::uint32_t self) -> std::uint32_t { (void)a; (void)c; (void)self; return U(expr); }
#define V(expr) [](Ctx& c, const Args& a, std::uint32_t self) -> std::uint32_t { (void)a; (void)c; (void)self; expr; return 0; }
#define P c.pad
#define Y c.sys

const std::map<std::uint32_t, PlatformAdapter>& adapters() {
    static const std::map<std::uint32_t, PlatformAdapter> table = {
        // ---- pad.cpp ----
        {0x004894a0, {"Pad_InitAtStart", 0, false, V(P.init_at_start())}},
        {0x004895c0, {"Pad_ClearAllOne", 0, false, V(P.clear_all_one(a[0]))}},
        {0x00489610, {"Pad_ClearAll", 0, false, V(P.clear_all())}},
        {0x00489630, {"Pad_Clear", 0, false, V(P.clear(a[0]))}},
        {0x00489670, {"Pad_ClearTriggers", 0, false, V(P.clear_triggers(a[0]))}},
        {0x00489690, {"Pad_Nothing", 0xffffffff, false, A(P.nothing())}},
        {0x004896a0, {"Pad_ActuatorOn", 0, false, V(P.actuator_on(a[0], a[1], a[2], a[3]))}},
        {0x00489850, {"Pad_ActuatorOff", 0, false, V(P.actuator_off(a[0], a[1]))}},
        {0x00489930, {"Input_AdvanceActionState", 0, false, V(P.advance_action_state(a[0], a[1]))}},
        {0x00489990, {"Pad_Analog", 0xffffffff, false, A(P.analog(S(a[0]), S(a[1]), S(a[2]), S(a[3])))}},
        {0x00489a10, {"Input_UpdateActionStates", 0xffffffff, false, A(P.update_action_states())}},
        {0x00489cd0, {"Pad_Restore", 0, false, V(P.restore())}},
        {0x00489e70, {"Pad_Remap", 0, false, V(P.remap())}},
        // ---- pc\pcinput.cpp ----
        {0x004e3e80, {"PCInput_ResetBindings", 0, false, V(P.reset_bindings())}},
        {0x004e3f80, {"PCInput_SetControlMode", 0, false, V(P.set_control_mode(a[0]))}},
        {0x004e4190, {"PCInput_SetPendingButtons", 0, false, V(P.set_pending_buttons(a[0]))}},
        {0x004e41b0, {"PCInput_KeyDown", 0xff, false, A(P.key_down(a[0]))}},
        {0x004e41d0, {"PCInput_FirstKeyDown", 0xffffffff, false, A(P.first_key_down(S(a[0])))}},
        {0x004e4230, {"PCInput_KeyChar", 0xffffffff, false, A(P.key_char(a[0]))}},
        {0x004e4250, {"PCInput_KeyName", 0xffffffff, false, A(P.key_name(a[0]))}},
        {0x004e4260, {"PCInput_FirstJoystickButton", 0xffffffff, false, A(P.first_joystick_button())}},
        {0x004e4290, {"Input_ClearPadTriggers", 0, false, V(P.clear_pad_triggers())}},
        {0x004e42b0, {"PCInput_EnableJoystickButtons", 0, false, V(P.enable_joystick_buttons(a[0]))}},
        {0x004e42c0, {"PCInput_BuildActionMask", 0, false, V(P.build_action_mask())}},
        {0x004e4650, {"PCInput_GetActionMask", 0xffffffff, false, A(P.get_action_mask())}},
        {0x004e4bf0, {"PCInput_MouseButtonMessage", 0, false, V(P.mouse_button_message(a[0]))}},
        {0x004e4d10, {"PCInput_LoadBindings", 0, false, V(P.load_bindings())}},
        {0x004e5460, {"PCInput_SaveBindings", 0, false, V(P.save_bindings())}},
        {0x004e4be0, {"PCInput_Nothing", 0, false, V()}},
        {0x004e4ce0, {"PCInput_NothingMotor", 0, false, V()}},
        // ---- pc\winmain.cpp ----
        {0x004f6660, {"WINMAIN_CanDoMode", 0xff, false, A(Y.can_do_mode(S(a[0]), S(a[1]), a[2]))}},
        {0x004f6d00, {"WINMAIN_ScreenDimensions", 0, false, V(Y.screen_dimensions(a[0], a[1]))}},
        {0x004f7d80, {"WINMAIN_Nothing", 0, false, V(Y.nothing())}},
        {0x004f86f0, {"WINMAIN_ShutDown", 0, false, V(Y.shut_down())}},
        {0x004f6850, {"Setup_ProbeConfiguredFile", 0xffffffff, false, A(Y.probe_configured_file(a[0], a[1]))}},
        {0x004f6920, {"WINMAIN_InstallSetup", 0, false, V(Y.install_setup())}},
        {0x004f6b90, {"WINMAIN_LoadOptions", 0, false, V(Y.load_options())}},
        {0x004f6c60, {"WINMAIN_SaveOptions", 0, false, V(Y.save_options())}},
        {0x004cf420, {"WINMAIN_BuildDisplayModeList", 0, false, V(Y.build_display_mode_list())}},
        {0x004cf030, {"VideoOptions_Load", 0, false, V(Y.load_video_options())}},
        {0x004cf300, {"VideoOptions_Save", 0, false, V(Y.save_video_options())}},
        {0x004f77f0, {"D3D_DumpDeviceCaps", 0, false, V(Y.dump_device_caps(a[0]))}},
        // ---- pc	hstub.cpp and the PC file backends ----
        {0x004f62c0, {"THSTUB_SaveCustomLevel", 0, false, V(Y.save_custom_level(a[0], a[1], a[2], a[3]))}},
        {0x004f6400, {"THSTUB_DeleteCustomLevel", 0, false, V(Y.delete_custom_level(a[0], a[1]))}},
        {0x004f6140, {"THSTUB_LoadCustomLevel", 0xffffffff, false, A(Y.load_custom_level(a[0], a[1], a[2], a[3]))}},
        {0x004e7900, {"PCcreate", 0xffffffff, false, A(Y.pc_create(a[0]))}},
        {0x004e7b60, {"PCwrite", 0xffffffff, false, A(Y.pc_write(S(a[0]), a[1], a[2]))}},
        {0x004e7500, {"PCremove", 0, false, V(Y.pc_remove(a[0]))}},
        {0x004e74f0, {"gameShutdown", 0, false, V(Y.game_shutdown())}},
        {0x004e7c50, {"PCnothing", 0, false, V()}},
        // ---- pc\d3d*.cpp and the timers ----
        {0x004d0a20, {"D3D_BitCount", 0xffffffff, false, A(Y.bit_count(a[0]))}},
        {0x004d0a40, {"D3D_LowestBit", 0xffffffff, false, A(Y.lowest_bit(a[0]))}},
        {0x004dab30, {"D3DTIMER_IsActive", 0xff, false, A(Y.timer_active())}},
        {0x004dae00, {"FrameRate_Update", 0, false, V(Y.frame_rate_meter())}},
        {0x004ec930, {"SDXMouse_SetBounds", 0, false, V(Y.mouse_set_bounds(self, S(a[0]), S(a[1]), S(a[2]), S(a[3])))}},
        {0x004d3600, {"D3D_SubmitViewportRect", 0, false, V(Y.submit_viewport_rect(a[0]))}},
        {0x004d0bc0, {"D3D_BeginScene", 0, false, V(Y.begin_scene(a[0], a[1]))}},
        {0x004d3a70, {"D3D_ReversePush", 0, false, V(Y.reverse_push(a[0]))}},
        {0x004d3a40, {"D3D_BuildReverseCommandList", 0, false, V(Y.build_reverse_command_list(a[0]))}},
        {0x004d3ab0, {"D3D_InitListHeap", 0, false, V(Y.init_list_heap(a[0], S(a[1])))}},
        {0x004d40d0, {"D3DPOLY_EndScene", 0, false, V(Y.end_scene())}},
        {0x004d6090, {"D3D_DrawUnitRectangle", 0, false, V(Y.draw_rectangle(self, 1))}},
        {0x004d60c0, {"D3D_DrawEightRectangle", 0, false, V(Y.draw_rectangle(self, 8))}},
        {0x004d60f0, {"D3D_DrawSixteenRectangle", 0, false, V(Y.draw_rectangle(self, 16))}},
        {0x004d7430, {"Init_PolyBuf", 0, false, V(Y.init_poly_buffer())}},
        {0x004d7480, {"Free_PolyBuf", 0, false, V(Y.free_poly_buffer())}},
        // ---- pc\d3dtex.cpp ----
        {0x004d8b20, {"D3DTex_DestroyAll", 0, false, V(Y.destroy_all_textures())}},
        {0x004dab20, {"D3DTex_Reset", 0, false, V(Y.texture_reset())}},
        {0x004d8c60, {"D3DTex_LoadBitmap", 0, false, V(Y.load_bitmap(a[0], a[1], a[2], a[3], a[4], a[5]))}},
        {0x004d9f10, {"D3DTex_Bmp4Unsupported", 0xffffffff, false, A(Y.bmp4_unsupported(a[3]))}},
        {0x004d9f40, {"D3DTex_RecordField20", 0xffffffff, false, A(Y.record_field_20(a[0]))}},
        {0x004da3e0, {"D3DTex_AttachToSceneMaterial", 0, false, V(Y.attach_to_material(a[0], a[1]))}},
        {0x004da400, {"D3DTex_ReleaseMaterialTexture", 0, false, V(Y.release_material_texture(a[0]))}},
        {0x004d8cd0, {"D3DTex_PrepareTexture", 0xffffffff, false, A(Y.prepare_texture(a[0], a[1], a[2], a[3]))}},
        {0x004d98d0, {"D3DTex_CreateExternalRecord", 0xffffffff, false, A(Y.create_external_record(a[0], a[1], a[2], a[3], a[4], a[5]))}},
        {0x004d7db0, {"D3D_CopyScreen", 0, false, V(Y.copy_screen(self, a[0], a[1]))}},
        {0x004d4120, {"D3D_BuildGammaTable", 0, false, V(Y.build_gamma_table(a[0]))}},
        {0x004d44d0, {"D3D_PolygonTooLarge", 0xff, false, A(Y.polygon_too_large(self, S(a[1]), a[0]))}},
        // ---- the video options window ----
        {0x004cebb0, {"VideoWin_SetOption", 0, false, V(Y.video_set_option(self, S(a[0]), S(a[1]), a[2]))}},
        {0x004cec70, {"VideoWin_Draw", 0, false, V(Y.video_draw(self))}},
        {0x004cece0, {"VideoWin_Step", 0, false, V(Y.video_step(self))}},
        {0x004ced80, {"VideoWin_BuildModeNames", 0, false, V(Y.video_build_mode_names(self, a[0]))}},
        {0x004cefb0, {"VideoWin_PadChoice", 0xff, false, A(Y.video_pad_choice(self, a[0]))}},
        // ---- small wrappers ----
        {0x0040b5c0, {"Camera_ResetA", 0, false, V(Y.camera_reset(0x0055f930))}},
        {0x0040b610, {"Camera_ResetB", 0, false, V(Y.camera_reset(0x0055f918))}},
        {0x0044b1c0, {"FallingText_ConstructStatic", 0, false, V(Y.falling_text_construct())}},
        {0x004bb220, {"Resource_VersionTag", 0xffffffff, false, A(Y.version_tag(a[0]))}},
    };
    return table;
}

}  // namespace

int main(int argc, char** argv) {
    Replay<Ctx> replay(adapters(), [](RetailMemory memory) { return std::make_unique<Ctx>(std::move(memory)); });
    return replay.run_files(argc, argv, "PC platform retail forced calls");
}
