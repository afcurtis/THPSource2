#include "auto_views.hpp"
#include "createobj_kit.hpp"

namespace opentony::runtime::createobj {

using namespace opentony::runtime::autoviews;

using Word = CreateObjKit::Word;

namespace {

constexpr Word kLog = kScratch + 0x1c00;         // the stitch log (0x78 words) built in a local
constexpr Word kGteVector = 0x006a3e58;          // the GTE's input vector (three shorts) and result (three words)
constexpr Word kGteResult = 0x006a3eb8;

constexpr Word kAllocStorage = 0x0047fd30;       // TRG_AllocObjectStorage(size)
constexpr Word kAnimatedConstruct = 0x00480610;  // Animated_Construct() on the storage
constexpr Word kFindRegion = 0x004b3230;         // PSX_FindLoadedRegionByName(name)
constexpr Word kSelectNamedRegion = 0x0047fe30;  // Object_SelectNamedPsxRegion(name) on the object
constexpr Word kPose = 0x00430920;               // Animation_GetDecompressedPose(object)

constexpr Word kMsgNoSourceRegion = 0x005267d0;
constexpr Word kMsgNoBuildRegion = 0x005267c0;
constexpr Word kMsgNoMapping = 0x00526794;
constexpr Word kMsgNoFilename = 0x005269a0;
constexpr Word kMsgNoPartMap = 0x005263a8;
constexpr Word kMsgRegionNotFound = 0x0052698c;
constexpr Word kMsgNoSkater = 0x005269ac;

// the distance of two coordinates (the code's sign-mask idiom: the absolute value of the 32-bit difference)
Word distance(Word a, Word b) {
    const std::int32_t d = static_cast<std::int32_t>(a - b);
    return static_cast<Word>(d < 0 ? -d : d);
}

}  // namespace

// 0x00424900 (thiscall, three arguments): the stitcher copies the source region's visible models into the build region, with the stitch vertices numbered and their ghosts moved
Word CreateObjKit::stitcher_construct(Word self, Word source, Word build, Word map) {
    set_word_at(m_, self, kVtPartStitcher);
    check(source != 0, kMsgNoSourceRegion);
    check(build != 0, kMsgNoBuildRegion);
    check(map != 0, kMsgNoMapping);
    stitch_.log_stitches(source, kLog);
    stitch_.copy_models(source, build, map);
    stitch_.reorder_stitches(source, build, kLog, map);
    return self;
}

// 0x00424a80 (fastcall)
void CreateObjKit::stitcher_destruct(Word self) { set_word_at(m_, self, kVtBase); }

// 0x00424a60 (thiscall, one argument)
Word CreateObjKit::stitcher_scalar_delete(Word self, Word flags) {
    stitcher_destruct(self);
    if ((flags & 1) != 0) {
        operator_delete(self);
    }
    return self;
}

// 0x004254c0 (cdecl, six arguments): vertices of the build parts `part_a` and `part_b` that sit on the same spot once each is carried by its bone of the pose become a stitch and its
// ghost. The source models give the positions (and their order), the build models take the flags.
void CreateObjKit::weld_pair(Word object, Word source, Word build, Word part_a, Word part_b, Word map) {
    check(object != 0, kMsgNoSkater);
    check(source != 0, kMsgNoSourceRegion);
    check(build != 0, kMsgNoBuildRegion);
    const Word pose = engine_value(kPose, {object});
    const Word source_a = static_cast<Word>(stitch_.map_reverse(s(part_a), map));
    const Word source_b = static_cast<Word>(stitch_.map_reverse(s(part_b), map));
    const Word build_a = word_at(m_, word_at(m_, build + 0x14) + part_a * 4);
    const Word build_b = word_at(m_, word_at(m_, build + 0x14) + part_b * 4);
    const Word model_a = word_at(m_, word_at(m_, source + 0x14) + source_a * 4);
    const Word model_b = word_at(m_, word_at(m_, source + 0x14) + source_b * 4);
    Word normal_a = build_a + 0x1c + static_cast<Word>(half_at(m_, build_a + 2)) * 8;
    Word position_a = model_a + 0x1c;
    Word stitch_a = position_a + (build_a - model_a);
    for (Word i = 0; i < half_at(m_, model_a + 2); ++i) {
        Word position_b = model_b + 0x1c;
        Word normal_b = build_b + 0x1c + static_cast<Word>(half_at(m_, build_b + 2)) * 8;
        Word ghost_b = position_b + (build_b - model_b);
        for (Word j = 0; j < half_at(m_, model_b + 2); ++j) {
            m3d().load_rotation_and_translation(pose + source_a * 0x18);
            set_global_half(m_, kGteVector, half_at(m_, position_a));
            set_global_half(m_, kGteVector + 2, half_at(m_, position_a + 2));
            set_global_half(m_, kGteVector + 4, half_at(m_, position_a + 4));
            math().rt();
            const Word x1 = global_word(m_, kGteResult);
            const Word y1 = global_word(m_, kGteResult + 4);
            const Word z1 = global_word(m_, kGteResult + 8);
            m3d().load_rotation_and_translation(pose + source_b * 0x18);
            set_global_half(m_, kGteVector, half_at(m_, position_b));
            set_global_half(m_, kGteVector + 2, half_at(m_, position_b + 2));
            set_global_half(m_, kGteVector + 4, half_at(m_, position_b + 4));
            math().rt();
            if (s(distance(x1, global_word(m_, kGteResult))) < 10 && s(distance(y1, global_word(m_, kGteResult + 4))) < 10 && s(distance(z1, global_word(m_, kGteResult + 8))) < 10) {
                stitch_.add_stitch(build, stitch_a, ghost_b, normal_a, normal_b);
            }
            position_b += 8;
            ghost_b += 8;
            normal_b += 8;
        }
        stitch_a += 8;
        position_a += 8;
        normal_a += 8;
    }
}

// 0x004252c0 (a thiscall passing its `this` on, four arguments): the welds of the two pairs of parts (0 with 1, 2 with 3) of the build region, on an animated object that carries the
// source region's pose
void CreateObjKit::weld(Word name, Word source, Word build, Word map) {
    check(name != 0, kMsgNoFilename);
    check(source != 0, kMsgNoSourceRegion);
    check(build != 0, kMsgNoBuildRegion);
    check(map != 0, kMsgNoPartMap);
    const Word storage = engine_value(kAllocStorage, {0x168});
    Word object = 0;
    if (storage != 0) {
        object = method(kAnimatedConstruct, storage);
    }
    check(engine_value(kFindRegion, {name}, text_at(name)) != 0xffffffffu, kMsgRegionNotFound);
    method(kSelectNamedRegion, object, {name}, text_at(name));
    weld_pair(object, source, build, 0, 1, map);
    weld_pair(object, source, build, 2, 3, map);
    if (object != 0) {
        delete_object(object);
    }
}

}  // namespace opentony::runtime::createobj
