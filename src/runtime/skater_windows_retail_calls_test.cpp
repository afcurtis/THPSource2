// Replays retail forced calls (Phase 4.10d) through the native skater-window helpers (commonfe.cpp) and the create-a-skater pages (perwin.cpp,
// appwin.cpp, statwin.cpp):
//
//   opentony_skater_windows_retail_calls_test FIXTURE.txt [FIXTURE.txt ...]
//
// A fixture (scripts/make_career_calls_fixture.py, from re/gdb/probes/frontend_calls.py with the skhelp, skpers, skapp and skstat groups) holds the
// retail memory regions the ported code touches and, for every case, the state, the arguments and what the retail function did; see
// retail_calls_replay.hpp for the format and the comparison.
#include "career.hpp"
#include "create_color.hpp"
#include "create_palette.hpp"
#include "create_textures.hpp"
#include "menu_toolkit.hpp"
#include "retail_calls_replay.hpp"
#include "skater_manager.hpp"
#include "skater_window_helpers.hpp"
#include "skater_windows.hpp"

#include <memory>

using namespace opentony::runtime;
using namespace opentony::runtime::replay;

namespace {

struct Ctx {
    RetailMemory mem;
    RetailServices svc;
    frontend::FontTools fonts;
    frontend::WindowBase window;
    frontend::MenuToolkit menu;
    skater::SkaterManager skaters;
    create::CreateColor color;
    create::TextureSets textures;
    create::CreatePalette palette;
    career::Career career_;
    frontend::SkaterWindowHelpers helpers;
    frontend::SkaterWindows windows;
    explicit Ctx(RetailMemory memory)
        : mem(std::move(memory)), fonts(mem, svc), window(mem, svc, fonts), menu(mem, svc, fonts, window), skaters(mem, svc), color(mem, svc),
          textures(mem, svc), palette(mem, svc, color, textures), career_(mem, svc, skaters), helpers(mem, svc, fonts, window, menu, color),
          windows(mem, svc, fonts, window, menu, helpers, skaters, textures, palette, career_) {
        menu.set_extra_dispatch([this](std::uint32_t object, std::uint32_t slot, const std::vector<std::uint32_t>& args) {
            return helpers.dispatch(object, slot, args) || windows.dispatch(object, slot, args);
        });
    }
    Ctx(const Ctx&) = delete;
    Ctx& operator=(const Ctx&) = delete;
};

using SkAdapter = Adapter<Ctx>;

// A(expr): the function returns expr. V(expr): it returns nothing the callers use. B(expr): it returns a bool in al.
#define A(expr) [](Ctx& c, const Args& a, std::uint32_t self) -> std::uint32_t { (void)a; (void)c; (void)self; return U(expr); }
#define V(expr) [](Ctx& c, const Args& a, std::uint32_t self) -> std::uint32_t { (void)a; (void)c; (void)self; expr; return 0; }
#define H c.helpers
#define W c.windows

const std::map<std::uint32_t, SkAdapter>& adapters() {
    static const std::map<std::uint32_t, SkAdapter> table = {
        // ---- commonfe.cpp: plain helpers ----
        {0x00418200, {"CommonFe_LightDefaults", 0, false, V(H.light_defaults(a[0]))}},
        {0x004182e0, {"CommonFe_ColourIndices", 0, false, V(H.colour_indices(a[0], a[1], a[2], a[3]))}},
        {0x00418330, {"CommonFe_FindRgbsChunk", 0xffffffff, false, A(H.find_rgbs_chunk(a[0]))}},
        {0x00418360, {"CommonFe_BusiestMaterial", 0xffffffff, false, A(H.busiest_material(S(a[0]), a[1]))}},
        {0x00418480, {"CommonFe_LoadFaceBitmap", 0, false, V(H.load_face_bitmap(a[0], a[1], a[2]))}},
        {0x00419420, {"CommonFe_ReplayLevelFlag", 0xff, false, A(H.in_replay_career())}},
        // ---- ColourText ----
        {0x00418540, {"ColourText_Construct", 0xffffffff, false, A(H.colour_text_construct(self, a[0], a[1], a[2], a[3], a[4], a[5], a[6]))}},
        {0x004185e0, {"ColourText_Delete", 0xffffffff, false, A(H.colour_text_delete(self, a[0]))}},
        {0x00418600, {"ColourText_Destroy", 0, false, V(H.colour_text_destroy(self))}},
        {0x00418610, {"ColourText_Draw", 0, false, V(H.colour_text_draw(self))}},
        {0x004186f0, {"ColourText_SetCustom", 0, false, V(H.colour_text_set_custom(self, a[0], a[1], a[2]))}},
        {0x00418720, {"ColourText_Swatch", 0, false, V(H.colour_text_swatch(self, a[0], a[1], a[2], a[3]))}},
        {0x004187d0, {"ColourText_Colour", 0, false, V(H.colour_text_colour(self, a[0], a[1], a[2]))}},
        {0x00418900, {"ColourText_SetIndex", 0, false, V(H.colour_text_set_index(self, S(a[0])))}},
        {0x00418970, {"ColourText_Index", 0xffffffff, false, A(H.colour_text_index(self))}},
        {0x00418980, {"ColourText_Step", 0, false, V(H.colour_text_step(self, a[0]))}},
        // ---- StepperRow ----
        {0x004189c0, {"StepperRow_Construct", 0xffffffff, false, A(H.stepper_construct(self, a[0], a[1], a[2], S(a[3]), a[4]))}},
        {0x00418b20, {"StepperRow_Delete", 0xffffffff, false, A(H.stepper_delete(self, a[0]))}},
        {0x00418b40, {"StepperRow_DestroyThunk", 0, false, V(H.control_destroy_thunk(self))}},
        {0x00418b50, {"StepperRow_Destroy", 0, false, V(H.stepper_destroy(self))}},
        {0x00418b60, {"StepperRow_SetPosition", 0, false, V(H.stepper_position(self, S(a[0]), S(a[1])))}},
        {0x00418b80, {"StepperRow_Draw", 0, false, V(H.stepper_draw(self))}},
        {0x00418d30, {"StepperRow_Step", 0, false, V(H.stepper_step(self, a[0]))}},
        // ---- NumberRow ----
        {0x00418d80, {"NumberRow_Construct", 0xffffffff, false,
                      A(H.number_construct(self, a[0], a[1], S(a[2]), S(a[3]), S(a[4]), a[5], a[6], a[7], a[8], a[9]))}},
        {0x00418e40, {"NumberRow_Delete", 0xffffffff, false, A(H.number_delete(self, a[0]))}},
        {0x00418e60, {"NumberRow_Destroy", 0, false, V(H.number_destroy(self))}},
        {0x00418e70, {"NumberRow_Value", 0xffffffff, false, A(H.number_value(self))}},
        {0x00418e80, {"NumberRow_SetValue", 0, false, V(H.number_set_value(self, S(a[0])))}},
        {0x00418f20, {"NumberRow_Step", 0, false, V(H.number_step(self, a[0]))}},
        // ---- ModelView ----
        {0x00418fc0, {"ModelView_Construct", 0xffffffff, false, A(H.model_view_construct(self, a[0], a[1], a[2], a[3], S(a[4]), S(a[5]), S(a[6])))}},
        {0x00419140, {"ModelView_Delete", 0xffffffff, false, A(H.model_view_delete(self, a[0]))}},
        {0x00419160, {"ModelView_Destroy", 0, false, V(H.model_view_destroy(self))}},
        {0x004191f0, {"ModelView_Update", 0, false, V(H.model_view_update(self))}},
        {0x00419220, {"ModelView_Draw", 0, false, V(H.model_view_draw(self))}},
        // ---- InitialsWindow ----
        {0x00419290, {"InitialsWindow_Construct", 0xffffffff, false, A(H.initials_construct(self, a[0], S(a[1]), S(a[2]), S(a[3]), S(a[4])))}},
        {0x00419390, {"InitialsWindow_Delete", 0xffffffff, false, A(H.initials_delete(self, a[0]))}},
        {0x004193b0, {"InitialsWindow_Destroy", 0, false, V(H.initials_destroy(self))}},
        {0x004193c0, {"InitialsWindow_SetLetters", 0, false, V(H.initials_set_letters(self, a[0], a[1], a[2]))}},
        // ---- perwin.cpp ----
        {0x004293b0, {"PersonalPage_Poll", 0xffffffff, false, A(W.personal_poll(self))}},
        {0x00429680, {"PersonalPage_Update", 0, false, V(W.personal_update(self))}},
        {0x00429910, {"PersonalPage_Draw", 0, false, V(W.personal_draw(self, S(a[0]), S(a[1])))}},
        {0x00429cd0, {"PersonalPage_HighlightFirst", 0, false, V(W.personal_highlight_first(self))}},
        {0x00429ce0, {"PersonalPage_IsTyping", 0xffffffff, false, A(W.personal_is_typing(self))}},
        {0x00429cf0, {"PersonalPage_ApplyPresetName", 0xff, false, A(W.personal_apply_preset_name(self))}},
        // ---- appwin.cpp ----
        {0x00429e00, {"AppearancePage_Construct", 0xffffffff, false, A(W.appearance_construct(self, a[0], S(a[1]), S(a[2])))}},
        {0x0042a0f0, {"AppearancePage_Delete", 0xffffffff, false, A(W.appearance_delete(self, a[0]))}},
        {0x0042a110, {"AppearancePage_Build", 0, false, V(W.appearance_build(self))}},
        {0x0042a8c0, {"AppearancePage_Destroy", 0, false, V(W.appearance_destroy(self))}},
        {0x0042a9b0, {"AppearancePage_Poll", 0xffffffff, false, A(W.appearance_poll(self))}},
        {0x0042aab0, {"AppearancePage_Update", 0, false, V(W.appearance_update(self))}},
        {0x0042ae30, {"AppearancePage_Camera", 0, false, V(W.appearance_camera(self, a[0], a[1]))}},
        {0x0042b070, {"AppearancePage_Draw", 0, false, V(W.appearance_draw(self, S(a[0]), S(a[1])))}},
        {0x0042b4d0, {"AppearancePage_Refresh", 0, false, V(W.appearance_refresh(self))}},
        {0x0042b880, {"AppearancePage_ReadColours", 0, false, V(W.appearance_read_colours(self))}},
        {0x0042b910, {"AppearancePage_PartVisible", 0xff, false, A(W.appearance_part_visible(self, a[0]))}},
        {0x0042b960, {"AppearancePage_TextureFlag", 0xffffffff, true, A(W.appearance_texture_flag(self, a[0]))}},
        {0x0042ba00, {"AppearancePage_LoadPreset", 0xff, false, A(W.appearance_load_preset(self))}},
        {0x0042bcf0, {"AppearancePage_Dump", 0, false, V(W.appearance_dump(self, a[0]))}},
        // ---- statwin.cpp ----
        {0x004b6180, {"StatPage_Poll", 0xffffffff, false, A(W.stat_poll(self))}},
        {0x004b6490, {"StatPage_Draw", 0, false, V(W.stat_draw(self, S(a[0]), S(a[1])))}},
        {0x004b64e0, {"StatPage_Update", 0, false, V(W.stat_update(self))}},
        {0x004b6560, {"StatPage_Build", 0, false, V(W.stat_build(self))}},
        {0x004b6bf0, {"StatPage_HighlightFirst", 0, false, V(W.stat_highlight_first(self))}},
        {0x004b6c00, {"StatPage_ResetSliders", 0, false, V(W.stat_reset_sliders(self, S(a[0])))}},
        {0x004b6d20, {"StatPage_Apply", 0, false, V(W.stat_apply(self))}},
        {0x004b6d90, {"StatPoints_Construct", 0xffffffff, false, A(W.points_construct(self, a[0], a[1], S(a[2]), S(a[3]), S(a[4]), S(a[5])))}},
        {0x004b7040, {"StatPoints_Delete", 0xffffffff, false, A(W.points_delete(self, a[0]))}},
        {0x004b7060, {"StatPoints_SetCharacter", 0, false, V(W.points_set_character(self, S(a[0])))}},
        {0x004b70a0, {"StatPoints_Destroy", 0, false, V(W.points_destroy(self))}},
        {0x004b70b0, {"StatPoints_Update", 0, false, V(W.points_update(self))}},
        {0x004b71c0, {"StatPoints_Adjust", 0, false, V(W.points_adjust(self, S(a[0])))}},
        {0x004b7290, {"StatPoints_Changed", 0xffffffff, false, A(W.points_changed(self))}},
        {0x004b7320, {"StatPoints_Text", 0xffffffff, false, A(W.points_text(self))}},
    };
    return table;
}

}  // namespace

int main(int argc, char** argv) {
    Replay<Ctx> replay(adapters(), [](RetailMemory memory) { return std::make_unique<Ctx>(std::move(memory)); });
    return replay.run_files(argc, argv, "skater window retail calls");
}
