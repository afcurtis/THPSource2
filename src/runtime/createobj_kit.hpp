#pragma once

#include "create_parts.hpp"
#include "guest_view.hpp"
#include "create_spool.hpp"
#include "create_stitch.hpp"
#include "collision_kit.hpp"
#include "render_kit.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace opentony::runtime::createobj {

// Phase 4.15f: the create-a-skater objects and the leftovers of the career, trick and front-end code (the last functions of Phase 4 that had no native twin): the texture database and manager
// objects, the part database, the spooler, the `MySkater` object (the custom skater the editor builds: its parts, material checksums, textures and PSX regions), the part stitcher's weld,
// the custom definition holder, the keyboard bindings screen, the career loading screen and a few trick-manager and singleton constructors. Like the rest of Phase 4 the functions work on
// the retail memory at the retail addresses (`self` is the object of a method); everything outside them (the allocators, the PSX region and spool services, the font and window code,
// the texture-set lookups, the assertion routine) is a service reported by address (`engine_<address>`) or by name.

constexpr std::uint32_t kScratch = pcplatform::kScratchBase + 0x1000;     // where the port builds the locals the retail code keeps on the stack

// vtables
constexpr std::uint32_t kVtBase = 0x005184b4;                // the base class: every destructor ends by restoring it
constexpr std::uint32_t kVtTextureDatabase = 0x00518764;
constexpr std::uint32_t kVtTextureManager = 0x00518768;
constexpr std::uint32_t kVtPartDatabase = 0x0051876c;
constexpr std::uint32_t kVtMySkater = 0x00518770;
constexpr std::uint32_t kVtSpooler = 0x00518774;
constexpr std::uint32_t kVtPartStitcher = 0x00518778;
constexpr std::uint32_t kVtCustomDefinition = 0x00518848;
constexpr std::uint32_t kVtTrickManager = 0x00519828;
constexpr std::uint32_t kVtRecordBank = 0x00519734;
constexpr std::uint32_t kVtSingleton = 0x00519890;
constexpr std::uint32_t kVtKeyConfig = 0x00518744;

constexpr std::uint32_t kSpooler = 0x005601d0;               // the global spooler object
constexpr std::uint32_t kCachedManager = 0x00561cb4;         // the texture-set manager the front end keeps

class CreateObjKit final : public collision::CollisionKit {
public:
    using Word = std::uint32_t;
    CreateObjKit(RetailMemory& memory, RetailServices& services)
        : CollisionKit(memory, services), render_(memory, services), parts_(memory, services), spool_(memory, services), stitch_(memory, services) {}

    // ---- the texture database, the texture-set manager and the part database (createobj_small.cpp) ----
    Word texture_database_construct(Word self);                                         // 0x0041d790 (fastcall)
    Word texture_database_scalar_delete(Word self, Word flags);                         // 0x0041d7b0 (thiscall, one argument)
    void texture_database_destruct(Word self);                                          // 0x0041d7d0 (fastcall)
    Word texture_manager_construct(Word self);                                          // 0x0041db50 TextureSetManager_Initialize (fastcall)
    Word texture_manager_scalar_delete(Word self, Word flags);                          // 0x0041dbc0 (thiscall, one argument)
    void texture_manager_destruct(Word self);                                           // 0x0041dbe0 (fastcall)
    Word part_database_scalar_delete(Word self, Word flags);                            // 0x0041f350 (thiscall, one argument)
    void part_database_destruct(Word self);                                             // 0x0041f370 (fastcall)

    // ---- the spooler (createobj_small.cpp) ----
    Word spooler_construct(Word self);                                                  // 0x00424550 (fastcall)
    Word spooler_scalar_delete(Word self, Word flags);                                  // 0x004245a0 (thiscall, one argument)
    void spooler_destruct(Word self);                                                   // 0x004245c0 (fastcall)
    void spooler_clear(Word self);                                                      // 0x00424610 (fastcall)
    Word spooler_pump(Word self);                                                       // 0x00424630 (fastcall)

    // ---- the materials of the parts and the textures applied to them (createobj_texture.cpp) ----
    void collect_checksums(Word self, Word region, Word model, Word list, Word count);  // 0x0041f900 Part_CollectChecksums (thiscall, four arguments)
    Word select_material_checksum(Word self, Word name, Word largest);                 // 0x00421910 (thiscall, two arguments)
    Word find_material_checksum(Word self, Word wanted, Word other, Word third);       // 0x00421ba0 (thiscall, three arguments)
    Word select_slot_checksum(Word self, Word slot, Word flag);                        // 0x00422020 (thiscall, two arguments)
    void find_board_checksum(Word self, Word list, Word count);                        // 0x00420420 (thiscall, two arguments)
    void apply_texture(Word self, Word checksum, Word name, Word tone, Word slot, Word request_a, Word request_b);   // 0x004221a0 (thiscall, six arguments)
    void apply_preview_texture(Word self, Word checksum, Word name, Word kind, Word request);   // 0x00422340 (thiscall, four arguments)
    void attach_loaded_bitmap(Word self, Word data, Word checksum, Word slot, Word name, Word unused, Word flag);   // 0x00422b30 (thiscall, six arguments)
    void apply_body_group(Word self, Word kind, Word id, Word tone);                   // 0x004208a0 TextureSet_ApplyBodyGroup (thiscall, three arguments)
    void apply_preview_groups(Word self, Word torso, Word legs, Word shoes, Word tone);   // 0x00422d80 (thiscall, four arguments)

    // ---- the part database and the skater object (createobj_object.cpp) ----
    Word part_database_construct(Word self, Word index, Word unused);                  // 0x0041f1e0 (thiscall, two arguments)
    void process_spooler_entry(Word self, Word file);                                  // 0x0041f4b0 MySkater__ProcessSpoolerEntry (thiscall, one argument)
    void find_animation_indexes(Word self, Word file);                                 // 0x0041f7c0 MySkater__Free2 (thiscall, one argument)
    void build_checksums(Word self, Word region);                                      // 0x0041fc80 (thiscall, one argument)
    Word skater_construct(Word self, Word index, Word unused, Word palette_buffer);    // 0x0041ff50 MySkater__Construct (thiscall, three arguments)
    void skater_destruct(Word self);                                                   // 0x004200d0 MySkater__Free (fastcall)
    Word skater_scalar_delete(Word self, Word flags);                                  // 0x004200b0 (thiscall, one argument)
    void skater_delete(Word skater);                                                   // 0x00422ec0 MySkater__Delete (cdecl)
    void release_part_database(Word self);                                             // 0x004206c0 (fastcall)
    void init_model_cache_slot(Word self);                                             // 0x00420fa0 Collision_InitModelCacheSlot (thiscall)
    void attach_region(Word self, Word skater_slot);                                   // 0x00420c10 (thiscall, one argument)
    void reset_texture_names(Word self);                                               // 0x00421410 (fastcall)
    void assign_model_part_order(Word self, Word object);                              // 0x00421460 (thiscall, one argument)
    void stitch_parts(Word self);                                                      // 0x00421690 (fastcall)
    void apply_body_scale(Word self, Word object);                                     // 0x00422e30 (thiscall, one argument)
    void build_object(Word skater, Word object);                                       // 0x00423be0 CustomSkater_BuildObject (cdecl)

    // ---- the part stitcher (createobj_weld.cpp) ----
    Word stitcher_construct(Word self, Word source, Word build, Word map);             // 0x00424900 (thiscall, three arguments)
    Word stitcher_scalar_delete(Word self, Word flags);                                // 0x00424a60 (thiscall, one argument)
    void stitcher_destruct(Word self);                                                 // 0x00424a80 (fastcall)
    void weld(Word name, Word source, Word build, Word map);                           // 0x004252c0 PartStitcher_Weld (four arguments)
    void weld_pair(Word object, Word source, Word build, Word part_a, Word part_b, Word map);   // 0x004254c0 PartStitcher_WeldPair (six arguments)

    // ---- the custom definition holder and the custom skaters (createobj_def.cpp) ----
    Word definition_construct(Word self);                                              // 0x0042f880 (fastcall)
    Word definition_scalar_delete(Word self, Word flags);                              // 0x0042f8b0 (thiscall, one argument)
    void definition_destruct(Word self);                                               // 0x0042f8d0 (fastcall)
    void load_definition_psx(Word self, Word slot, Word unused, Word index);           // 0x0042f950 (thiscall, three arguments)
    void load_all_definitions(Word self);                                              // 0x0042fa10 CustomSkaters_LoadAll (fastcall)
    void init_all_custom_skaters();                                                    // 0x004236d0 CustomSkaters_InitAll
    Word custom_skater_create(Word slot, Word index, Word spooling, Word palette_buffer, Word manager);   // 0x00423800 (cdecl)
    Word free_all_custom_skaters(Word count, Word skaters);                            // 0x00423b50 (cdecl)
    void reset_spool();                                                                // 0x00423bd0 Create_ResetSpool

    // ---- the record bank, the singleton, strings, the tricks (createobj_misc.cpp) ----
    Word record_bank_construct(Word self);                                             // 0x004b0b50 (fastcall)
    Word record_bank_scalar_delete(Word self, Word flags);                             // 0x004b0b90 (thiscall, one argument)
    void record_bank_destruct(Word self);                                              // 0x004b0bb0 (fastcall)
    Word singleton_construct(Word self);                                               // 0x004c43f0 (fastcall)
    Word singleton_construct_global();                                                 // 0x004c43b0
    void singleton_destruct(Word self);                                                // 0x004c43e0 (fastcall)
    Word singleton_scalar_delete(Word self, Word flags);                               // 0x004c4400 (thiscall, one argument)
    void string_to_lower(Word text);                                                   // 0x004b8340 (cdecl)
    Word is_blank(Word text);                                                          // 0x004b8360 (cdecl)
    void select_versus_players();                                                      // 0x004b8380 (cdecl)
    void add_viewport_message(Word player, Word message);                              // 0x004b83b0 (cdecl, two arguments)
    Word find_trick_config(Word character);                                            // 0x00416050 TrickConfig_FindByCharacter (cdecl)
    Word trick_manager_construct(Word self);                                           // 0x004bb4f0 Tricks_InitializeManager (thiscall)
    void trick_manager_destruct(Word self);                                            // 0x004bb790 (fastcall)
    Word trick_manager_scalar_delete(Word self, Word flags);                           // 0x004bb770 (thiscall, one argument)
    void tips_reset();                                                                 // 0x004c4570 TrickTips_Reset (cdecl)
    void trick_table_build(Word self, Word player, Word table);                        // 0x004bcf00 TrickTable_BuildForPlayer (thiscall, two arguments)
    void build_player_tables();                                                        // 0x004bd1e0 TrickRuntime_BuildPlayerTables (cdecl)

    mathkit::MathKit& math() { return render_.math(); }
    m3d::M3dKit& m3d() { return render_.m3d(); }

    // ---- the bindings screen and the career loading screen (createobj_screen.cpp) ----
    void copy_font_fields(Word dest, Word source);                                     // (the Font record copy the screens' constructors share)
    Word keyconfig_construct(Word self);                                               // 0x0041c4b0 Create_LoadPSH (fastcall)
    Word keyconfig_scalar_delete(Word self, Word flags);                               // 0x0041cda0 (thiscall, one argument)
    void keyconfig_destruct(Word self);                                                // 0x0041cdc0 (fastcall)
    void keyconfig_set_row(Word self, Word row, Word delta);                           // 0x0041ceb0 (thiscall, two arguments)
    void keyconfig_update(Word self);                                                  // 0x0041d060 (fastcall)
    void keyconfig_step(Word self);                                                    // 0x0041d0f0 (thiscall, slot 1 of the screen's vtable)
    void keyconfig_set_button(Word row, Word value);                                   // 0x0041d4f0 (cdecl, two arguments)
    void keyconfig_assign(Word row, Word column, Word key);                            // 0x0041d580 (cdecl, three arguments)
    Word keyconfig_poll(Word self, Word result);                                       // 0x0041d6f0 (thiscall, one argument)
    Word keyconfig_done(Word self);                                                    // 0x0041c4a0 (fastcall)
    Word keyconfig_run();                                                              // 0x0041c2d0 Game_MainLoop (cdecl)
    void career_loading_screen(Word mode);                                             // 0x00415b30 (cdecl, one argument)

    // The bytes of the last call's writes that are stack leftovers in retail (the replay does not compare them)
    [[nodiscard]] const std::vector<std::pair<std::uint32_t, std::uint32_t>>& skater_unspecified() const { return unspecified_; }

private:
    static Word u(std::int32_t v) { return static_cast<Word>(v); }
    static std::int32_t s(Word v) { return static_cast<std::int32_t>(v); }
    void operator_delete(Word block) { s_.note("operator_delete", {block}); }
    Word operator_new(Word size) {
        s_.note("operator_new", {size});
        return static_cast<Word>(s_.value("operator_new"));
    }
    void delete_block(Word block) { s_.note("mem_delete", {block}); }
    // FileIO_AllocLoadedBuffer(size, heap, 1, 0)
    Word alloc(Word size, Word heap) {
        s_.note("alloc", {size, heap, 1, 0});
        return static_cast<Word>(s_.value("alloc"));
    }
    // CRT string copy: the text and its terminator
    void copy_string(Word dest, Word source) {
        Word n = 0;
        while (guest::read_scalar<std::uint8_t>(m_, source + n) != 0) {
            ++n;
        }
        m_.copy(dest, source, n + 1);
    }
    // the bytes of a NUL-terminated string at a scratch address: the text of a reported call
    std::string text_at(Word address) { return m_.cstring(address, 0x100); }
    // a thiscall to a service: the object is the call's first argument
    Word method(Word target, Word self, std::vector<Word> args = {}, std::string text = {}) {
        args.insert(args.begin(), self);
        return engine_value(target, std::move(args), std::move(text));
    }
    // the virtual destructor of an object (slot 0, flag 1): the ports of this milestone run natively, any other class is a service
    void delete_object(Word object);
    // PlayerSpool_LoadPartManifest(file, names, textures, &count, 0x78)
    Word load_manifest(Word file, Word names, Word textures, Word count_address);
    void collect_named(Word database, Word region, Word name, Word list, Word count);

    renderkit::RenderKit render_;      // (the GTE and the matrix code of the renderer)
    create::CreateParts parts_;
    create::CreateSpool spool_;
    create::CreateStitch stitch_;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> unspecified_;
};

}  // namespace opentony::runtime::createobj
