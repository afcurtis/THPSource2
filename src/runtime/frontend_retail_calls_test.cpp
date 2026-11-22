// Replays retail forced calls (Phase 4.10a) through the native frontend text layer:
//
//   opentony_frontend_retail_calls_test FIXTURE.txt [FIXTURE.txt ...]
//
// A fixture (scripts/make_career_calls_fixture.py, from re/gdb/probes/frontend_calls.py) holds the retail
// memory regions the ported code touches and, for every case, the state, the arguments and what the retail
// function did; see retail_calls_replay.hpp for the format and the comparison.
#include "frontend_effects.hpp"
#include "frontend_mess.hpp"
#include "frontend_text.hpp"
#include "frontend_window.hpp"
#include "retail_calls_replay.hpp"

#include <memory>

using namespace opentony::runtime;
using namespace opentony::runtime::replay;

namespace {

struct Ctx {
    RetailMemory mem;
    RetailServices svc;
    frontend::FontTools fonts;
    frontend::Mess mess;
    frontend::WindowBase window;
    frontend::TextEffects effects;
    explicit Ctx(RetailMemory memory)
        : mem(std::move(memory)), fonts(mem, svc), mess(mem, svc, fonts), window(mem, svc, fonts), effects(mem, svc, fonts) {}
    Ctx(const Ctx&) = delete;
    Ctx& operator=(const Ctx&) = delete;
};

using FrontAdapter = Adapter<Ctx>;

// A(expr): the function returns expr. V(expr): it returns nothing the callers use. `a` are the stack
// arguments, `self` is `this` for a thiscall function.
#define A(expr) [](Ctx& c, const Args& a, std::uint32_t self) -> std::uint32_t { (void)a; (void)c; (void)self; return U(expr); }
#define V(expr) [](Ctx& c, const Args& a, std::uint32_t self) -> std::uint32_t { (void)a; (void)c; (void)self; expr; return 0; }

const std::map<std::uint32_t, FrontAdapter>& adapters() {
    static const std::map<std::uint32_t, FrontAdapter> table = {
        // ---- fonts (fonttools.cpp) ----
        {0x0044a860, {"Font_EscapeClass", 0xffffffff, false, A(frontend::FontTools::escape_class(a[0]))}},
        {0x0044a620, {"Font_GlyphIndex", 0xffffffff, false, A(c.fonts.glyph_index(self, a[0]))}},
        {0x0044abb0, {"Font_SetMapping", 0, false, V(c.fonts.set_mapping(self, a[0]))}},
        {0x0044ab70, {"Font_SetRgb", 0, false, V(c.fonts.set_rgb(self, a[0], a[1], a[2]))}},
        {0x0044ab90, {"Font_SetEffect", 0, false, V(c.fonts.set_effect(self, a[0]))}},
        {0x0044aba0, {"Font_GetEffect", 0xffffffff, false, A(c.fonts.effect(self))}},
        {0x0044aaa0, {"Font_CharAdvance", 0xffffffff, false, A(c.fonts.char_advance(self, a[0]))}},
        {0x0044a4d0, {"Font_TextWidth", 0xffffffff, false, A(c.fonts.text_width(self, a[0]))}},
        {0x0044a570, {"Font_TextTop", 0xffffffff, false, A(c.fonts.text_top(self, a[0]))}},
        {0x0044a5c0, {"Font_TextHeight", 0xffffffff, false, A(c.fonts.text_height(self, a[0]))}},
        {0x0044a540, {"Font_TextExtent", 0xffffffff, false, A(c.fonts.text_extent(self, a[0]))}},
        {0x0044a8e0, {"Font_ApplyEscape", 0, false, V(c.fonts.apply_escape(self, a[0]))}},
        {0x0044abe0, {"Font_Ellipsize", 0, false, V(c.fonts.ellipsize(self, a[0], S(a[1])))}},
        {0x0044a070, {"Font_Draw", 0, false, V(c.fonts.draw(self, S(a[0]), S(a[1]), a[2], a[3]))}},
        {0x0044ad30, {"Font_Unload", 0, false, V(c.fonts.unload(self))}},
        {0x0044af90, {"Font_IsLoaded", 0xff, false, A(c.fonts.is_loaded(a[0]))}},
        {0x0044ad80, {"Font_ClearTable", 0, false, V(c.fonts.clear_table())}},
        // ---- messages (mess.cpp) ----
        {0x00473510, {"Mess_SetSortMode", 0, false, V(c.mess.set_sort_mode(S(a[0])))}},
        {0x004734b0, {"Mess_SetTextJustify", 0, false, V(c.mess.set_text_justify(a[0]))}},
        {0x004734f0, {"Mess_SetScale", 0, false, V(c.mess.set_scale(S(a[0])))}},
        {0x00473500, {"Mess_GetScale", 0xffffffff, false, A(c.mess.get_scale())}},
        {0x00473580, {"Mess_SetRgb", 0, false, V(c.mess.set_rgb(a[0], a[1], a[2]))}},
        {0x004739e0, {"Mess_DrawText", 0xffffffff, false, A(c.mess.draw_text(S(a[0]), S(a[1]), a[2]))}},
        {0x004735b0, {"Mess_DrawPanelText", 0xffffffff, false, A(c.mess.draw_text(S(a[0]), S(a[1]), a[2]))}},
        {0x004739d0, {"Mess_TextWidth", 0xffffffff, false, A(c.mess.text_width(a[0]))}},
        {0x004735d0, {"Mess_DeleteMessage", 0, false, V(c.mess.delete_message(a[0]))}},
        {0x00473650, {"Mess_DeleteAll", 0, false, V(c.mess.delete_all())}},
        {0x00473690, {"Mess_Exists", 0xffffffff, false, A(c.mess.exists(a[0]))}},
        {0x004736c0, {"Mess_DeleteByText", 0, false, V(c.mess.delete_by_text(a[0]))}},
        {0x00473730, {"Mess_AllocNode", 0xffffffff, false, A(c.mess.alloc_node())}},
        {0x004736f0, {"Mess_AddMessage", 0xffffffff, false, A(c.mess.add_message(a[0], a[1], a[2], a[3], a[4]))}},
        {0x00473760, {"Mess_Update", 0, false, V(c.mess.update())}},
        {0x004737c0, {"Mess_Display", 0, false, V(c.mess.display())}},
        {0x00473a00, {"Mess_LoadFont", 0, false, V(c.mess.load_font(a[0], a[1], a[2]))}},
        {0x00473b80, {"Mess_UnloadFont", 0, false, V(c.mess.unload_font())}},
        {0x00473be0, {"Mess_SetCurrentFont", 0, false, V(c.mess.deprecated_notice())}},
        {0x00473bf0, {"Mess_RetiredNotice", 0, false, V(c.mess.deprecated_notice())}},
        // ---- the window base (screentools.cpp) ----
        {0x004ac430, {"ScreenElement_Construct", 0, false, V(c.window.element_construct(self, S(a[0]), S(a[1]), S(a[2]), S(a[3])))}},
        {0x004ac490, {"ScreenElement_Delete", 0xffffffff, false, A(c.window.element_delete(self, a[0]))}},
        {0x004ac4b0, {"ScreenElement_Destroy", 0, false, V(c.window.element_destroy(self))}},
        {0x004ac4c0, {"ScreenElement_NoResult", 0xffffffff, false, A(0xffffffffu)}},
        {0x004ac4d0, {"ScreenElement_Update", 0, false, V(c.window.element_update(self))}},
        {0x004ac560, {"ScreenElement_Nothing", 0, false, V((void)0)}},
        {0x004ac570, {"ScreenElement_SnapX", 0, false, V(c.window.element_snap_x(self))}},
        {0x004ac580, {"ScreenElement_SnapY", 0, false, V(c.window.element_snap_y(self))}},
        {0x004ac590, {"ScreenElement_GetX", 0xffffffff, false, A(c.window.element_x(self))}},
        {0x004ac5a0, {"ScreenElement_GetY", 0xffffffff, false, A(c.window.element_y(self))}},
        {0x004ac5b0, {"Window_Construct", 0xffffffff, false, A(c.window.window_construct(self, S(a[0]), S(a[1]), S(a[2]), S(a[3])))}},
        {0x004ac5f0, {"Window_Delete", 0xffffffff, false, A(c.window.window_delete(self, a[0]))}},
        {0x004ac610, {"Window_Destroy", 0, false, V(c.window.window_destroy(self))}},
        {0x004ac670, {"Window_AddElement", 0, false, V(c.window.window_add(self, a[0]))}},
        {0x004ac730, {"Window_Update", 0, false, V(c.window.window_update(self))}},
        {0x0047f1d0, {"NetMenu_UpdateJump", 0, false, V(c.window.window_update(self))}},
        {0x0047fa00, {"NetSummary_UpdateJump", 0, false, V(c.window.window_update(self))}},
        {0x004ac770, {"Window_Draw", 0, false, V(c.window.window_draw(self, S(a[0]), S(a[1])))}},
        {0x004ac7c0, {"Window_SnapX", 0, false, V(c.window.window_snap_x(self))}},
        {0x004ac800, {"Window_SnapY", 0, false, V(c.window.window_snap_y(self))}},
        {0x004ac840, {"TextScreenElement_Construct", 0xffffffff, false, A(c.window.text_construct(self, a[0], a[1], S(a[2]), S(a[3]), S(a[4]), S(a[5])))}},
        {0x004ac970, {"TextScreenElement_Delete", 0xffffffff, false, A(c.window.text_delete(self, a[0]))}},
        {0x004ac990, {"TextScreenElement_Destroy", 0, false, V(c.window.text_destroy(self))}},
        {0x004aca30, {"TextScreenElement_SetString", 0, false, V(c.window.text_set_string(self, a[0]))}},
        {0x004acb50, {"TextScreenElement_Draw", 0, false, V(c.window.text_draw(self, S(a[0]), S(a[1])))}},
        {0x004acbd0, {"TextScreenElement_Width", 0xffffffff, false, A(c.window.text_width(self))}},
        {0x004acbe0, {"SpriteScreenElement_Construct", 0xffffffff, false, A(c.window.sprite_construct(self, a[0], S(a[1]), S(a[2]), S(a[3]), S(a[4]), a[5], a[6]))}},
        {0x004acd60, {"SpriteScreenElement_Delete", 0xffffffff, false, A(c.window.sprite_delete(self, a[0]))}},
        {0x004acd80, {"SpriteScreenElement_Destroy", 0, false, V(c.window.sprite_destroy(self))}},
        {0x004ace20, {"SpriteScreenElement_Draw", 0, false, V(c.window.sprite_draw(self, S(a[0]), S(a[1])))}},
        {0x004ace60, {"SpriteScreenElement_Width", 0xffffffff, false, A(c.window.sprite_width(self))}},
        {0x004acec0, {"SpriteScreenElement_Height", 0xffffffff, false, A(c.window.sprite_height(self))}},
        {0x004bad00, {"TextWindow_SetLineColour", 0, false, V(c.window.textwin_set_line_colour(self, S(a[0]), S(a[1])))}},
        // ---- the text effects (texteffects.cpp) ----
        {0x004b94a0, {"TextEffect_Construct", 0xffffffff, false, A(c.effects.effect_construct(self))}},
        {0x004b94c0, {"TextEffect_Delete", 0xffffffff, false, A(c.effects.effect_delete(self, a[0]))}},
        {0x004b94e0, {"TextEffect_Destroy", 0, false, V(c.effects.effect_destroy(self))}},
        {0x004b94f0, {"TextEffect_Bind", 0, false, V(c.effects.effect_bind(self, a[0]))}},
        {0x004b95a0, {"TextEffect_Unbind", 0, false, V(c.effects.effect_unbind(self, a[0]))}},
        {0x004b9610, {"TextEffect_PositionDefault", 0, false, V(c.effects.effect_position_default(self, a[0], a[1]))}},
        {0x004b9690, {"TextEffect_ColourDefault", 0, false, V(c.effects.effect_colour_default(self, a[0], a[1], a[2]))}},
        {0x004b9710, {"TextEffect_GlyphDefault", 0, false, V((void)0)}},
        {0x004b9720, {"TextEffect_Disable", 0, false, V(c.effects.effect_disable(self))}},
        {0x004b9730, {"FallingTextEffect_Construct", 0xffffffff, false, A(c.effects.falling_construct(self))}},
        {0x004b9750, {"FallingTextEffect_Delete", 0xffffffff, false, A(c.effects.falling_delete(self, a[0]))}},
        {0x004b9770, {"FallingTextEffect_Destroy", 0, false, V(c.effects.falling_destroy(self))}},
        {0x004b9780, {"FallingTextEffect_SetBlood", 0, false, V(c.effects.falling_set_blood(self, a[0]))}},
        {0x004b9790, {"FallingTextEffect_Init", 0, false, V(c.effects.falling_init(self))}},
        {0x004b9820, {"FallingTextEffect_Update", 0, false, V(c.effects.falling_update(self))}},
        {0x004b9860, {"FallingTextEffect_GlyphDone", 0, false, V(c.effects.falling_glyph_done(self, S(a[0]), S(a[1]), S(a[2]), S(a[3])))}},
        {0x004b9ac0, {"FallingTextEffect_ClearClip", 0, false, V(c.effects.clear_clip())}},
        {0x004b9ae0, {"FallingTextEffect_ClearClipFor", 0, false, V(c.effects.clear_clip_for(a[0]))}},
        {0x004b9b30, {"FallingTextEffect_Position", 0, false, V(c.effects.falling_position(self, a[0], a[1]))}},
        {0x004b9c80, {"FallingTextEffect_Colour", 0, false, V(c.effects.falling_colour(self, a[0], a[1], a[2]))}},
        {0x004b9d50, {"FallingTextEffect_SetScale", 0, false, V(c.effects.falling_set_scale(self, a[0]))}},
        {0x004b9d60, {"FallingTextEffect_Bind", 0, false, V(c.effects.falling_bind(self, a[0]))}},
        {0x004b9de0, {"FallingTextEffect_Finished", 0xffffffff, false, A(c.effects.falling_finished(self))}},
        {0x004b9e00, {"FallingTextEffect_BloodUpdate", 0, false, V(c.effects.blood_sprite_update(self))}},
        {0x004b9ef0, {"FallingTextEffect_CreateSprite", 0, false, V(c.effects.blood_sprite_create())}},
        {0x004b9fb0, {"FallingTextEffect_DestroySprite", 0, false, V(c.effects.blood_sprite_destroy())}},
        {0x004ba020, {"ZoomTextEffect_Construct", 0xffffffff, false, A(c.effects.zoom_construct(self))}},
        {0x004ba040, {"ZoomTextEffect_Delete", 0xffffffff, false, A(c.effects.zoom_delete(self, a[0]))}},
        {0x004ba060, {"ZoomTextEffect_Destroy", 0, false, V(c.effects.zoom_destroy(self))}},
        {0x004ba070, {"ZoomTextEffect_Bind", 0, false, V(c.effects.zoom_bind(self, a[0]))}},
        {0x004ba430, {"ZoomTextEffect_Init", 0, false, V(c.effects.zoom_init(self))}},
        {0x004ba450, {"ZoomTextEffect_Update", 0, false, V(c.effects.zoom_update(self))}},
        {0x004ba480, {"ZoomTextEffect_Colour", 0, false, V(c.effects.zoom_colour(self, a[0], a[1], a[2]))}},
        {0x004ba760, {"ZoomTextEffect_Finished", 0xffffffff, false, A(c.effects.zoom_finished(self))}},
        {0x004ba780, {"ZoomTextEffect_Start", 0, false, V(c.effects.zoom_start(self))}},
    };
    return table;
}

}  // namespace

int main(int argc, char** argv) {
    Replay<Ctx> replay(adapters(), [](RetailMemory memory) { return std::make_unique<Ctx>(std::move(memory)); });
    return replay.run_files(argc, argv, "frontend retail calls");
}
