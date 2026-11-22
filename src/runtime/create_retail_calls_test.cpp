// Replays retail forced calls (Phase 4.9) through the native create-a-skater code:
//
//   opentony_create_retail_calls_test FIXTURE.txt [FIXTURE.txt ...]
//
// A fixture (scripts/make_career_calls_fixture.py, from re/gdb/probes/create_calls.py) holds the
// retail memory regions the ported code touches and, for every case, the state, the arguments and
// what the retail function did; see retail_calls_replay.hpp for the format and the comparison.
#include "create_color.hpp"
#include "create_palette.hpp"
#include "create_parts.hpp"
#include "create_records.hpp"
#include "create_spool.hpp"
#include "create_stitch.hpp"
#include "create_textures.hpp"
#include "retail_calls_replay.hpp"
#include "skater_manager.hpp"

#include <memory>

using namespace opentony::runtime;
using namespace opentony::runtime::replay;

namespace {

struct Ctx {
    RetailMemory mem;
    RetailServices svc;
    create::CreateColor color;
    create::TextureSets textures;
    skater::SkaterManager skaters;
    create::CustomSkaters customs;
    create::CreateParts parts;
    create::CreateSpool spool;
    create::CreatePalette palette;
    create::CreateStitch stitch;
    explicit Ctx(RetailMemory memory)
        : mem(std::move(memory)),
          color(mem, svc),
          textures(mem, svc),
          skaters(mem, svc),
          customs(mem, svc, skaters, textures),
          parts(mem, svc),
          spool(mem, svc),
          palette(mem, svc, color, textures),
          stitch(mem, svc) {}
    Ctx(const Ctx&) = delete;
    Ctx& operator=(const Ctx&) = delete;
};

using CreateAdapter = Adapter<Ctx>;

// A(expr): the function returns expr. V(expr): it returns nothing the callers use. `a` are the stack
// arguments, `self` is `this` for a thiscall function.
#define A(expr) [](Ctx& c, const Args& a, std::uint32_t self) -> std::uint32_t { (void)a; (void)c; (void)self; return U(expr); }
#define V(expr) [](Ctx& c, const Args& a, std::uint32_t self) -> std::uint32_t { (void)a; (void)c; (void)self; expr; return 0; }

std::uint8_t B(std::uint32_t v) { return static_cast<std::uint8_t>(v); }

Unspecified texture_gaps(Ctx& c) { return c.textures.unspecified(); }

const std::map<std::uint32_t, CreateAdapter>& adapters() {
    static const std::map<std::uint32_t, CreateAdapter> table = {
        // ---- colour (create_color.cpp) ----
        {0x00423c90, {"Color_Min3", 0xff, false, A(static_cast<std::uint32_t>(create::CreateColor::min3(B(a[0]), B(a[1]), B(a[2]))))}},
        {0x00423cb0, {"Color_Max3", 0xff, false, A(static_cast<std::uint32_t>(create::CreateColor::max3(B(a[0]), B(a[1]), B(a[2]))))}},
        {0x00423cd0, {"Color_RgbToHsv", 0, false, V(c.color.rgb_to_hsv(a[0], a[1], a[2], a[3], a[4], a[5]))}},
        {0x00423ea0, {"Color_HsvToRgb", 0, false, V(c.color.hsv_to_rgb(S(a[0]), S(a[1]), S(a[2]), a[3], a[4], a[5]))}},
        {0x0041f0c0, {"Color_PresetHsv", 0, false, V(c.color.preset_hsv(S(a[0]), a[1], a[2], a[3]))}},
        {0x00424280, {"Palette_GetHsv", 0, false, V(c.color.palette_hsv(S(a[0]), a[1], a[2], a[3], a[4]))}},
        {0x00424330, {"Palette_ShiftHsv", 0, false, V(c.color.shift_palette(a[0], a[1], S(a[2]), S(a[3]), S(a[4]), S(a[5])))}},
        {0x004241a0, {"Palette_Remember", 0, false, V(c.color.remember_palette(a[0], a[1]))}},
        // ---- the texture database and texture sets (cre_tex.cpp, cre_texset.cpp) ----
        {0x0041d860, {"TextureDatabase_Find", 0xffffffff, false, A(c.textures.find_texture(self, a[0])), texture_gaps}},
        {0x0041d9c0, {"TextureDatabase_Allocate", 0xffffffff, false, A(c.textures.allocate_texture(self, a[0])), texture_gaps}},
        {0x0041dc30, {"TextureSet_FindById", 0xffffffff, false, A(c.textures.find_by_id(self, S(a[0])))}},
        {0x0041dc60, {"TextureSet_FindByGroup", 0xffffffff, false, A(c.textures.find_by_group(self, S(a[0])))}},
        {0x0041dcb0, {"TextureSet_FindRoot", 0xffffffff, false, A(c.textures.find_root(self, S(a[0]), S(a[1])))}},
        {0x0041ddb0, {"TextureSet_Step", 0xffffffff, false, A(c.textures.step(self, S(a[0]), S(a[1]), S(a[2])))}},
        {0x0041ef50, {"TextureSet_Commit", 0, false, V(c.textures.commit(self, a[0])), texture_gaps}},
        {0x0041f060, {"TextureSet_Dump", 0, false, V(c.textures.dump(self))}},
        {0x0041dea0, {"TextureSetManager_LoadCretex", 0, false, V(c.textures.load_cretex(self, a[0])), texture_gaps}},
        // ---- the custom skaters' records (create.cpp, creedit.cpp, cresave.cpp) ----
        {0x00422f20, {"CustomSkater_Init", 0, false, V(c.customs.init_record(S(a[0]), a[1], a[2]))}},
        {0x00423370, {"CustomSkater_Randomize", 0, false, V(c.customs.randomize(S(a[0]), a[1], a[2]))}},
        {0x00422640, {"CustomSkater_SaveColours", 0, false, V(c.customs.save_colours(self, S(a[0])))}},
        {0x00422690, {"CustomSkater_LoadColours", 0, false, V(c.customs.load_colours(self, S(a[0])))}},
        {0x0042ef50, {"CustomSave_CopySlot", 0, false, V(c.customs.copy_slot(self, S(a[0]), S(a[1])))}},
        {0x0042ef30, {"CustomSave_CommitImage", 0, false, V(c.customs.commit_image(self))}},
        // ---- the part database (create.cpp) ----
        {0x0041f3d0, {"Part_GroupIndex", 0xffffffff, false, A(c.parts.group_index(a[0]))}},
        {0x0041fe50, {"PartDb_FindBodyGroup", 0xffffffff, false, A(c.parts.find_body_group(self, a[0], S(a[1])))}},
        {0x00421040, {"Part_Classify", 0xffffffff, false, A(c.parts.classify_part(self, S(a[0]), a[1]))}},
        {0x00421250, {"Part_BuildMap", 0, false, V(c.parts.build_part_map(self, a[0]))}},
        {0x004203c0, {"Part_CheckedRegion", 0xffffffff, false, A(c.parts.checked_region(self))}},
        {0x004214d0, {"Part_RegionRecord", 0xffffffff, false, A(c.parts.region_record(self))}},
        {0x00420860, {"Part_FreePalettes", 0, false, V(c.parts.free_palettes(self))}},
        // ---- the bitmap spool queue and the help-text layout (create_spool.cpp, create_ui.cpp) ----
        {0x00424870, {"Spool_Compact", 0, false, V(c.spool.compact(self))}},
        {0x004248e0, {"Spool_FindFree", 0xffffffff, false, A(c.spool.find_free(self))}},
        {0x00424780, {"Spool_Add", 0, false, V(c.spool.add(self, a[0], a[1], S(a[2]), a[3], a[4], a[5]))}},
        {0x00425f20, {"Text_NextWord", 0xffffffff, false, A(c.spool.next_word(a[0], a[1]))}},
        {0x00425ca0, {"Text_Layout", 0, false, V(c.spool.layout(self, a[0]))}},
        // ---- recolouring a part's palette (create.cpp) ----
        {0x004226e0, {"Bitmap_HueLookup", 0xff, false, A(c.palette.bitmap_hue(a[0], a[1], a[2], a[3]))}},
        {0x00422790, {"Palette_AverageHsv", 0, false, V(c.palette.average_hsv(self, S(a[0]), a[1], a[2], a[3], a[4]))}},
        {0x00422a80, {"Palette_SetShift", 0, false, V(c.palette.set_shift(self, S(a[0]), S(a[1]), S(a[2]), S(a[3]), a[4]))}},
        {0x00422c10, {"Palette_ApplyPreset", 0, false, V(c.palette.apply_preset(self, S(a[0]), a[1]))}},
        {0x004214f0, {"Palette_UpdateAnimation", 0, false, V(c.palette.update_animation(self, S(a[0]), a[1]))}},
        {0x00422460, {"Palette_SetHandle", 0, false, V(c.palette.set_palette_handle(self, S(a[0]), a[1]))}},
        // ---- stitching the body models (create_stitch.cpp) ----
        {0x00425110, {"Model_CountStitchVertices", 0xffffffff, false, A(c.stitch.count_stitch_vertices(a[0]))}},
        {0x00425180, {"Model_ByteSize", 0xffffffff, false, A(c.stitch.model_size(a[0]))}},
        {0x00425200, {"PartMap_Lookup", 0xffffffff, false, A(c.stitch.map_lookup(S(a[0]), a[1]))}},
        {0x00425270, {"PartMap_ReverseLookup", 0xffffffff, false, A(c.stitch.map_reverse(S(a[0]), a[1]))}},
        {0x004252a0, {"PartMap_Has", 0xffffffff, false, A(c.stitch.map_has(S(a[0]), a[1]))}},
        {0x00424a90, {"PartStitcher_LogStitches", 0, false, V(c.stitch.log_stitches(a[0], a[1]))}},
        {0x00424bb0, {"PartStitcher_CopyModels", 0, false, V(c.stitch.copy_models(a[0], a[1], a[2]))}},
        {0x00424e40, {"PartStitcher_ReorderStitches", 0, false, V(c.stitch.reorder_stitches(a[0], a[1], a[2], a[3]))}},
        {0x004257d0, {"PartStitcher_AddStitch", 0, false, V(c.stitch.add_stitch(a[0], a[1], a[2], a[3], a[4]))}},
    };
    return table;
}

}  // namespace

int main(int argc, char** argv) {
    Replay<Ctx> replay(adapters(), [](RetailMemory memory) { return std::make_unique<Ctx>(std::move(memory)); });
    return replay.run_files(argc, argv, "create retail calls");
}
