#include "auto_views.hpp"
#include "render_kit.hpp"
#include "host_options.hpp"

// Phase 4.15e: the scene renderer's frame functions (`m3d.cpp` / `m3dzone.cpp` / `main.cpp` tail, 0x0045f3f0, 0x00460a90, 0x004667e0, 0x00466a00): the full-screen rectangle that closes a view, the object
// list submission of a view, the zone blockmap loader and the loading banner of a replay. The values the frame keeps in its globals (the viewport edges at 0x00563a38, the buckets at 0x0056433c, the
// light and colour matrices at 0x005620c0 / 0x005620e8) are copied to the GTE register file, which is memory at 0x006a3e00.

namespace opentony::runtime::renderkit {

using namespace opentony::runtime::autoviews;

namespace {

using Word = std::uint32_t;

constexpr Word kCursor = 0x00560fd0;
constexpr Word kCurrent = 0x00560fd4;
constexpr Word kBuckets = 0x0056433c;                // dword: the ordering table of the view being closed
constexpr Word kViewportEdges = 0x00563a38;          // dword: pointer to the view's edges (right, bottom, left, top, then the depth word at +8)
constexpr Word kScaleX = 0x029da394;
constexpr Word kScaleY = 0x029da398;
constexpr Word kSoftwareRenderer = 0x029da3a0;
constexpr Word kLightSource = 0x005620e8;            // the light matrix the frame keeps (nine shorts)
constexpr Word kColourSource = 0x005620c0;           // the colour matrix (nine shorts)
constexpr Word kGteLight = 0x006a3e80;
constexpr Word kGteColour = 0x006a3ec8;
constexpr Word kGteBackground = 0x006a3e48;
constexpr Word kRegionRecords = 0x0056d428;

}  // namespace

// 0x0045f3f0 M3d_RenderCleanup: closes the view with a rectangle the size of the view (or of the window for the software renderer), in display pixels, behind everything
void RenderKit::render_cleanup() {
    std::int16_t x, y, width, height;
    if (global_word(m_, kSoftwareRenderer) != 0) {
        width = static_cast<std::int16_t>(global_half(m_, 0x005620a0));
        height = static_cast<std::int16_t>(global_half(m_, 0x005620b8));
        x = 0;
        y = 0;
    } else {
        const Word edges = global_word(m_, kViewportEdges);
        x = static_cast<std::int16_t>(half_at(m_, edges + 4));
        y = static_cast<std::int16_t>(half_at(m_, edges + 6));
        width = static_cast<std::int16_t>(half_at(m_, edges) - half_at(m_, edges + 4));
        height = static_cast<std::int16_t>(half_at(m_, edges + 2) - half_at(m_, edges + 6));
    }
    const Word scale_x = global_word(m_, kScaleX);
    const Word scale_y = global_word(m_, kScaleY);
    const Word rect = kScratch;
    set_half_at(m_, rect, u(static_cast<std::int32_t>(x) << 9) / scale_x);
    set_half_at(m_, rect + 2, u(static_cast<std::int32_t>(y) * 240) / scale_y);
    set_half_at(m_, rect + 4, u(static_cast<std::int32_t>(width) << 9) / scale_x);
    set_half_at(m_, rect + 6, u(static_cast<std::int32_t>(height) * 240) / scale_y);
    const Word prim = global_word(m_, kCursor);
    engine(0x004e87f0, {prim, rect}, bytes_text(rect, 8));
    set_global_word(m_, kCursor, prim + 0x14);
    const Word buckets = global_word(m_, kBuckets);
    if (buckets == word_at(m_, global_word(m_, kCurrent) + 0x84)) {
        set_word_at(m_, prim + 0x10, 0xbf800000);
    } else {
        const float depth = static_cast<float>(static_cast<double>(half_at(m_, global_word(m_, kViewportEdges) + 8)) * static_cast<double>(float_at(0x00518d64)));
        set_word_at(m_, prim + 0x10, float_bits(depth));
    }
    set_word_at(m_, prim, word_at(m_, global_word(m_, kBuckets) + 0x7ff8));
    set_word_at(m_, global_word(m_, kBuckets) + 0x7ff8, prim);
}

// 0x00460a90 Render_SubmitObjectList(list) (cdecl): loads the frame's light and colour matrices into the GTE, culls the objects against the view and hands every visible one to the shadow / item renderer
void RenderKit::submit_object_list(Word list) {
    if (list == 0) {
        return;
    }
    for (Word k = 0; k < 18; k += 2) {
        set_half_at(m_, kGteLight + k, half_at(m_, kLightSource + k));
        set_half_at(m_, kGteColour + k, half_at(m_, kColourSource + k));
    }
    set_global_word(m_, 0x006a3e50, 0);
    set_global_word(m_, kGteBackground, u(static_cast<std::int16_t>(global_half(m_, 0x00563a96)) >> 1));
    set_global_word(m_, kGteBackground + 4, u(static_cast<std::int16_t>(global_half(m_, 0x00563a9e)) >> 1));
    m3d_.cull_objects_to_view(list, (global_word(m_, 0x0055fa3c) != 0 ? 0x3fu : 0u) + 1);
    set_global_half(m_, 0x00563a54, global_half(m_, 0x00563a44));
    set_global_half(m_, 0x00563a10, global_half(m_, 0x0056209c));
    set_global_half(m_, 0x00563a64, global_half(m_, 0x00564368));
    set_global_half(m_, 0x00563a7c, global_half(m_, 0x0056436a));
    set_global_half(m_, 0x00563a88, global_half(m_, 0x0056436c));
    set_global_word(m_, 0x00563a80, global_word(m_, 0x00563ae4));
    set_global_word(m_, 0x00563a14, global_word(m_, 0x00563b34));
    set_global_word(m_, 0x00563ae8, global_word(m_, 0x00563a60));
    set_global_half(m_, 0x00563b08, 0);
    Word object = list;
    do {
        if ((half_at(m_, object + 4) & 0x8000) == 0) {
            set_global_word(m_, 0x00563a8c, (half_at(m_, object + 4) >> 3) & 1);
            set_global_half(m_, 0x005620b6, u(s(global_word(m_, 0x00533f90)) >> 12) - half_at(m_, global_word(m_, 0x005620e0) + 8));
            engine(0x004604f0, {object});
        }
        object = word_at(m_, object + 0x20);
    } while (object != 0);
    set_global_word(m_, 0x0056437c, global_word(m_, 0x00564380));
}

// 0x004667e0 M3D_LoadZoneBlockmap(index, record) (cdecl): the zone grid of an environment: its bounds, the cell counts (at most 20 by 20) and the cell size; every cell's list of environment objects is
// pointed at inside the record, and the object numbers in the lists become the objects' addresses
void RenderKit::load_zone_blockmap(Word index, Word record) {
    check(index == 0, 0x00533e80);
    const Word base = index * 0x660;
    Word cursor = record;
    set_word_at(m_, base + 0x00567f84, word_at(m_, cursor));
    set_word_at(m_, base + 0x00567f88, word_at(m_, cursor + 4));
    set_word_at(m_, base + 0x00567f8c, word_at(m_, cursor + 8));
    set_word_at(m_, base + 0x00567f90, word_at(m_, cursor + 12));
    const Word counts = word_at(m_, cursor + 16);
    cursor += 20;
    set_half_at(m_, base + 0x00567f9c, counts);
    set_half_at(m_, base + 0x00567f9e, counts >> 16);
    set_word_at(m_, base + 0x00567f80, 1);
    const std::int32_t width = static_cast<std::int16_t>(half_at(m_, base + 0x00567f9c));
    set_word_at(m_, base + 0x00567f94, u((s(word_at(m_, base + 0x00567f8c)) - s(word_at(m_, base + 0x00567f84))) / width));
    check(static_cast<std::int16_t>(half_at(m_, base + 0x00567f9c)) <= 0x14, 0x00533e68);
    check(static_cast<std::int16_t>(half_at(m_, base + 0x00567f9e)) <= 0x14, 0x00533e50);
    for (std::int32_t row = 0; row < static_cast<std::int16_t>(half_at(m_, base + 0x00567f9e)); ++row) {
        for (std::int32_t column = 0; column < static_cast<std::int16_t>(half_at(m_, base + 0x00567f9c)); ++column) {
            const Word slot = 0x00567fa0 + (u(row) + index * 0x198) * 4 + u(column) * 0x50;
            std::int32_t items = s(word_at(m_, cursor + 8));
            cursor += 12;
            set_word_at(m_, slot, cursor);
            for (std::int32_t k = 0; k < items; ++k) {
                const Word region = word_at(m_, 0x00537c80 + index * 4);
                const Word objects = word_at(m_, 0x0056d438 + region * 0x44);
                set_word_at(m_, cursor, objects + word_at(m_, cursor) * 0x4c);
                cursor += 4;
            }
            cursor += 4;
        }
    }
}

// 0x00466a00: the banner a replay loading screen shows: the display set up for the text, and the lines naming the state, the level and the skater model(s), formatted into a local and handed to the text drawer
void RenderKit::loading_banner() {
    engine(0x004e8850, {1});
    engine(0x004e87c0, {0});
    engine(0x004e8890, {0, 0});
    const Word handle = engine_value(0x004e8880, {0x10, 0x78, 0x50, 0x40, 1, 0x400});
    (void)engine_value(0x004e8740, {0});
    const Word environment = kScratch + 0x100;
    engine(0x004e8720, {environment, 0, 0, 0x200, global_word(m_, 0x00527d54)});
    engine(0x004e8700, {environment});
    auto model_name = [&](Word slot) { return m_.cstring(word_at(m_, 0x0053a3b8 + slot * 0x170)); };
    const std::string state = m_.cstring(word_at(m_, 0x00533f40 + global_word(m_, 0x00561c74) * 4));
    const std::string level = m_.cstring(0x0053c1f8 + global_word(m_, 0x00561c90) * 0x1ac);
    std::string text;
    if (global_word(m_, 0x00533f3c) == 1) {
        const Word slot = engine_value(0x004b0c60, {0x0056c580, 0});
        text = pcplatform::crt::format(m_.cstring(0x005341a4), {state, level, model_name(slot)});
    } else {
        const Word second = engine_value(0x004b0c60, {0x0056c580, 1});
        const std::string second_model = model_name(second);
        const Word first = engine_value(0x004b0c60, {0x0056c580, 0});
        text = pcplatform::crt::format(m_.cstring(0x00534198), {state, level, model_name(first), second_model});
    }
    set_string(kScratch + 0x180, text);
    engine(0x004e8870, {kScratch + 0x180}, text);
    engine(0x004e8860, {handle});
}

// 0x00460ca0 M3d_RenderBackground(list) (cdecl): the sky / background objects of the list, each submitted with the view's matrix (the one at +0x74) turned by the object's own rotation: the product is
// built a column at a time with the GTE (the view matrix times each column of the object's matrix) and kept at 0x00563a18, with the region's colour table. An object with no rotation and no scale
// is drawn with the view matrix as it is. When the rotation is not zero the last fourteen bytes of the copy at 0x00563a18 are stack leftovers in retail.
void RenderKit::render_background(Word list) {
    constexpr Word kView = 0x005620e0;
    constexpr Word kRotation = 0x006a3e10;
    constexpr Word kIr = 0x006a3eb0;
    set_global_word(m_, 0x005620a4, 0x10);
    set_global_word(m_, 0x00563af8, 0);
    set_global_word(m_, 0x00563afc, 0);
    set_global_word(m_, 0x00563b00, 0);
    unspecified_.clear();
    if (list == 0) {
        return;
    }
    set_global_half(m_, 0x00563a54, 0x7fff);
    set_global_half(m_, 0x00563b08, 0);
    const Word product = kScratch + 0x110;           // the 32 bytes the product is built in (the view matrix's, then the product's nine shorts)
    const Word local = kScratch + 0x130;             // the object's matrix
    Word object = list;
    do {
        const Word region = byte_at(m_, object + 0x1f);
        if ((half_at(m_, object + 4) & 0x8001) == 0 && byte_at(m_, 0x0056d432 + region * 0x44) != 0) {
            const Word view = global_word(m_, kView);
            set_global_word(m_, 0x00564358, 0xffff0000);
            auto load_view = [&]() {
                for (Word k = 0; k < 18; k += 2) {
                    set_half_at(m_, kRotation + k, half_at(m_, view + 0x74 + k));
                }
            };
            auto columns = [&]() {
                // the view matrix times each column of the object's matrix; the results are the columns of the product
                for (Word column = 0; column < 3; ++column) {
                    for (Word row = 0; row < 3; ++row) {
                        set_half_at(m_, kIr + 2 * row, half_at(m_, local + 2 * column + 6 * row));
                    }
                    math().rtir();
                    for (Word row = 0; row < 3; ++row) {
                        set_half_at(m_, product + 2 * column + 6 * row, half_at(m_, kIr + 2 * row));
                    }
                }
                for (Word k = 0; k < 18; k += 2) {
                    set_half_at(m_, kRotation + k, half_at(m_, product + k));
                }
            };
            load_view();
            const bool rotated = half_at(m_, object + 0x14) != 0 || half_at(m_, object + 0x16) != 0 || half_at(m_, object + 0x18) != 0;
            if (!rotated) {
                for (Word k = 0; k < 32; k += 4) {
                    set_word_at(m_, product + k, word_at(m_, view + 0x74 + k));
                }
                m3d_.set_identity_matrix(local);
                if ((byte_at(m_, object + 5) & 2) != 0) {
                    m3d_.scale_object_matrix(object, local);
                    columns();
                }
            } else {
                m3d_.rotation_from_angles(object + 0x14, local);
                if ((byte_at(m_, object + 5) & 2) != 0) {
                    m3d_.scale_object_matrix(object, local);
                }
                load_view();
                columns();
                unspecified_.emplace_back(0x00563a18 + 18, 14);
            }
            for (Word k = 0; k < 32; k += 4) {
                set_word_at(m_, 0x00563a18 + k, word_at(m_, product + k));
            }
            const Word colours = word_at(m_, 0x0056d450 + region * 0x44);
            set_global_word(m_, 0x00563a58, colours);
            set_global_word(m_, 0x00563a84, colours);
            const Word model = word_at(m_, word_at(m_, 0x0056d43c + region * 0x44) + half_at(m_, object + 0x1a) * 4);
            submit_background_model(model);       // (0x00461bb0 for an unrotated object, 0x00461ad0 otherwise: the same code)
        }
        object = word_at(m_, object + 0x20);
    } while (object != 0);
    set_global_half(m_, 0x00563a54, global_half(m_, 0x00563a44));
}

// 0x004604f0 RenderSuperItemShadow(object) (cdecl): an item made of several models (a "super item": one model per bone of its region) drawn with the object's matrix at +0x118. The item's position
// relative to the view and its matrix times the view's are kept at 0x00563b10 (with the bones' pose entry at 0x00564340 for the model being submitted); every model of the item is submitted with
// the geometry state (the first one with 2, and 4 when the item's mask at +0x148 names it or its model asks for it). Two bytes of the saved matrix copy are stack leftovers in retail.
void RenderKit::render_super_item(Word object) {
    constexpr Word kView = 0x005620e0;
    constexpr Word kRotation = 0x006a3e10;
    constexpr Word kIr = 0x006a3eb0;
    const Word flags = half_at(m_, object + 4);
    const Word region = byte_at(m_, object + 0x1f);
    const Word record = 0x0056d428 + region * 0x44;
    set_global_word(m_, 0x00563ae0, byte_at(m_, record + 0x3a) != 0 && (flags & 0xc80) == 0 ? 1 : 0);
    check(byte_at(m_, record + 0xa) != 0, 0x00533984);
    const bool mirrored = (byte_at(m_, object + 0xd8) & 2) != 0;
    if (mirrored) {
        for (Word offset : {0x118u, 0x11eu, 0x124u}) {
            set_half_at(m_, object + offset, 0u - half_at(m_, object + offset));
        }
    }
    set_global_half(m_, 0x00563b08, mirrored ? 1 : 0);
    const Word view = global_word(m_, kView);
    for (Word k = 0; k < 18; k += 2) {
        set_half_at(m_, kRotation + k, half_at(m_, view + 0x74 + k));
    }
    const Word product = kScratch + 0x110;
    m3d_.multiply_by_rotation(object + 0x118, product);
    set_global_half(m_, kIr, u(s(word_at(m_, object + 8)) >> 12) - word_at(m_, view + 4));
    set_global_half(m_, kIr + 2, u(s(word_at(m_, object + 0xc)) >> 12) - word_at(m_, view + 8));
    set_global_half(m_, kIr + 4, u(s(word_at(m_, object + 0x10)) >> 12) - word_at(m_, view + 0xc));
    math().rtir();
    set_word_at(m_, product + 0x14, global_word(m_, 0x006a3eb8));
    set_word_at(m_, product + 0x18, global_word(m_, 0x006a3ebc));
    set_word_at(m_, product + 0x1c, global_word(m_, 0x006a3ec0));
    set_global_word(m_, 0x00563a48, 0);
    check(half_at(m_, object + 0x1a) == 0, 0x0053394c);
    const Word bones = half_at(m_, record + 0x38);
    Word models = word_at(m_, record + 0x14);
    const Word pose = (flags & 4) != 0 ? word_at(m_, object + 0x138) : engine_value(0x00430920, {object});
    const Word matrix = word_at(m_, object + 0x40);
    for (Word k = 0; k < 18; k += 2) {
        set_half_at(m_, kRotation + k, half_at(m_, matrix + k));
    }
    const Word columns[3][3] = {{0x118, 0x11e, 0x124}, {0x11a, 0x120, 0x126}, {0x11c, 0x122, 0x128}};
    for (const auto& column : columns) {
        for (Word k = 0; k < 3; ++k) {
            set_half_at(m_, kIr + 2 * k, half_at(m_, object + column[k]));
        }
        math().rtir();
    }
    const Word colours = word_at(m_, record + 0x28);
    set_global_word(m_, 0x00563a58, colours);
    set_global_word(m_, 0x00563a84, colours);
    unspecified_.clear();
    unspecified_.emplace_back(0x00563b10 + 0x12, 2);
    const bool hidden = ((word_at(m_, object + 0x88) >> 24) & 0x80) != 0;
    Word entry = pose;
    for (Word index = 0; index < bones; ++index, models += 4, entry += 0x18) {
        set_global_word(m_, 0x00563af4, ((1u << (index & 31)) & word_at(m_, object + 0x148)) != 0 ? 1 : 0);
        const Word model = word_at(m_, models);
        if (word_at(m_, object + 0x14c) != 0) {
            entry = pose + byte_at(m_, object + 0x150 + index) * 24;
        }
        if ((byte_at(m_, model) & 0x20) != 0) {
            set_global_word(m_, 0x00563af4, 1);
        }
        for (Word k = 0; k < 18; k += 2) {
            set_half_at(m_, kRotation + k, half_at(m_, object + 0x118 + k));
        }
        const Word picks[3][3] = {{0, 6, 0xc}, {2, 8, 0xe}, {4, 0xa, 0x10}};
        for (const auto& pick : picks) {
            for (Word k = 0; k < 3; ++k) {
                set_half_at(m_, kIr + 2 * k, half_at(m_, entry + pick[k]));
            }
            math().rtir();
        }
        for (Word k = 0; k < 24; k += 4) {
            set_word_at(m_, 0x00564340 + k, word_at(m_, entry + k));
        }
        for (Word k = 0; k < 32; k += 4) {
            set_word_at(m_, 0x00563b10 + k, word_at(m_, product + k));
        }
        if (!hidden) {
            Word state = index == 0 ? 2u : 0u;
            if (global_word(m_, 0x00563af4) != 0) {
                state |= 4;
            }
            engine(0x00461b10, {model, state});
        }
    }
    if ((byte_at(m_, object + 0xd8) & 2) != 0) {
        for (Word offset : {0x118u, 0x11eu, 0x124u}) {
            set_half_at(m_, object + offset, 0u - half_at(m_, object + offset));
        }
    }
}

// 0x004610f0 M3D_RenderModel(object) (cdecl): a skater (or any item of several models) drawn with the object's matrix at +0x118 and the pose of its bones. Like RenderSuperItemShadow the item's position relative
// to the view and its matrix times the view's are kept at 0x00563b10, but the bone loop here also lights the bone: a model that asks for it (bit 2 of its first byte), or an object with bit 7 of its flags set,
// has the lights of the object's matrix (or of the level's table, from level 10 on, except 13) loaded and its geometry packets submitted with the object's matrix times the bone's (a local matrix: nine shorts).
// The geometry state follows (RenderModelInSuperFast when the frame's flag at 0x00563ae0 says so); a bone that is not tiled, of an object with a shade or a +0xdc byte, then has its last vector turned by the object's matrix.
void RenderKit::render_model(Word object) {
    constexpr Word kView = 0x005620e0;
    constexpr Word kRotation = 0x006a3e10;
    constexpr Word kIr = 0x006a3eb0;
    constexpr Word kLevel = 0x0056a898;
    constexpr Word kLevelLights = 0x0053db80;
    const Word flags = half_at(m_, object + 4);
    const Word follow = word_at(m_, object + 0x14c) != 0 ? object : 0;
    const Word region = byte_at(m_, object + 0x1f);
    const Word record = 0x0056d428 + region * 0x44;
    set_global_word(m_, 0x00563ae0, byte_at(m_, record + 0x3a) != 0 && (flags & 0xc80) == 0 ? 1 : 0);
    check(byte_at(m_, record + 0xa) != 0, 0x00533984);
    set_global_half(m_, 0x00563b08, 0);
    const bool mirrored = (byte_at(m_, object + 0xd8) & 2) != 0;
    if (mirrored) {
        set_global_half(m_, 0x00563b08, 1);
        for (Word offset : {0x118u, 0x11eu, 0x124u}) {
            set_half_at(m_, object + offset, 0u - half_at(m_, object + offset));
        }
    }
    const Word view = global_word(m_, kView);
    for (Word k = 0; k < 18; k += 2) {
        set_half_at(m_, kRotation + k, half_at(m_, view + 0x74 + k));
    }
    const Word product = kScratch + 0x110;
    const Word local = kScratch + 0x154;              // the object's matrix times the bone's (the matrix the lit packets are submitted with)
    const Word columns[3][3] = {{0x118, 0x11e, 0x124}, {0x11a, 0x120, 0x126}, {0x11c, 0x122, 0x128}};
    // the object's matrix turned by the view's, a column at a time: row r of column c at (2 * c + 6 * r)
    const auto turn = [&](Word destination) {
        for (Word column = 0; column < 3; ++column) {
            for (Word k = 0; k < 3; ++k) {
                set_half_at(m_, kIr + 2 * k, half_at(m_, object + columns[column][k]));
            }
            math().rtir();
            for (Word k = 0; k < 3; ++k) {
                set_half_at(m_, destination + 2 * column + 6 * k, half_at(m_, kIr + 2 * k));
            }
        }
    };
    turn(product);
    set_global_half(m_, kIr, u(s(word_at(m_, object + 8)) >> 12) - word_at(m_, view + 4));
    set_global_half(m_, kIr + 2, u(s(word_at(m_, object + 0xc)) >> 12) - word_at(m_, view + 8));
    set_global_half(m_, kIr + 4, u(s(word_at(m_, object + 0x10)) >> 12) - word_at(m_, view + 0xc));
    math().rtir();
    set_word_at(m_, product + 0x14, global_word(m_, 0x006a3eb8));
    set_word_at(m_, product + 0x18, global_word(m_, 0x006a3ebc));
    set_word_at(m_, product + 0x1c, global_word(m_, 0x006a3ec0));
    set_global_word(m_, 0x00563a48, 0);
    for (Word k = 0; k < 32; k += 4) {
        set_word_at(m_, 0x00563b10 + k, word_at(m_, product + k));
    }
    unspecified_.clear();
    unspecified_.emplace_back(0x00563b10 + 0x12, 2);
    check(half_at(m_, object + 0x1a) == 0, 0x0053394c);
    const Word bones = half_at(m_, record + 0x38);
    Word models = word_at(m_, record + 0x14);
    const Word pose = (flags & 4) != 0 ? word_at(m_, object + 0x138) : engine_value(0x00430920, {object});
    const Word matrix = word_at(m_, object + 0x40);
    for (Word k = 0; k < 18; k += 2) {
        set_half_at(m_, kRotation + k, half_at(m_, matrix + k));
    }
    turn(local);
    const Word shade = word_at(m_, object + 0x88) >> 24;
    const Word colours = word_at(m_, record + 0x28);
    set_global_word(m_, 0x00563a58, colours);
    set_global_word(m_, 0x00563a84, colours);
    Word entry = pose;
    for (Word index = 0; index < bones; ++index, models += 4, entry += 0x18) {
        const Word model = word_at(m_, models);
        set_global_word(m_, 0x00563af4, ((1u << (index & 31)) & word_at(m_, object + 0x148)) != 0 ? 1 : 0);
        if (word_at(m_, object + 0x14c) != 0) {
            entry = pose + byte_at(m_, object + 0x150 + index) * 24;
        }
        for (Word k = 0; k < 18; k += 2) {
            set_half_at(m_, kRotation + k, half_at(m_, object + 0x118 + k));
        }
        const Word picks[3][3] = {{0, 6, 0xc}, {2, 8, 0xe}, {4, 0xa, 0x10}};
        for (const auto& pick : picks) {
            for (Word k = 0; k < 3; ++k) {
                set_half_at(m_, kIr + 2 * k, half_at(m_, entry + pick[k]));
            }
            math().rtir();
        }
        for (Word k = 0; k < 24; k += 4) {
            set_word_at(m_, 0x00564340 + k, word_at(m_, entry + k));
        }
        if ((byte_at(m_, model) & 4) != 0 || (flags & 0x80) != 0) {
            for (Word k = 0; k < 18; k += 2) {
                set_half_at(m_, 0x006a3ec8 + k, half_at(m_, matrix + 0x14 + k));
            }
            m3d_.set_background_colour(matrix + 0x28);
            const Word level = global_word(m_, kLevel);
            if (level >= 10 && level != 13) {
                const Word lights = kLevelLights + level * 0x34;
                for (Word k = 0; k < 18; k += 2) {
                    set_half_at(m_, 0x006a3e80 + k, half_at(m_, lights + k));
                    set_half_at(m_, 0x006a3ec8 + k, half_at(m_, lights + 0x14 + k));
                }
                m3d_.set_background_colour(lights + 0x28);
            }
            engine(0x004d11d0, {model + half_at(m_, model + 2) * 8 + 0x1c, half_at(m_, model + 4), local, index == 0 ? 1u : 0u}, bytes_text(local, 18));
        }
        if ((shade & 0x80) == 0) {
            Word state = 0;
            if (follow != 0 && (index == word_at(m_, follow + 0x2cd4) || index == word_at(m_, follow + 0x2cd8))) {
                state = 0x100;
            }
            if (index == 0) {
                state |= 2;
            }
            if (global_word(m_, 0x00563af4) != 0) {
                state |= 4;
            }
            engine(global_word(m_, 0x00563ae0) != 0 ? 0x00461b50u : 0x00461b10u, {model, state});
        }
        if (global_word(m_, 0x00563af4) == 0 && ((shade & 0x7f) != 0 || byte_at(m_, object + 0xdc) != 0)) {
            set_global_half(m_, kIr, half_at(m_, entry + 0x12));
            set_global_half(m_, kIr + 2, half_at(m_, entry + 0x14));
            set_global_half(m_, kIr + 4, half_at(m_, entry + 0x16));
            m3d_.load_rotation_and_rotate(object + 0x118);
        }
    }
    if ((byte_at(m_, object + 0xd8) & 2) != 0) {
        for (Word offset : {0x118u, 0x11eu, 0x124u}) {
            set_half_at(m_, object + offset, 0u - half_at(m_, object + offset));
        }
    }
}

// 0x0045f530 M3D_RenderObjects(list) (cdecl): the frame's objects (a linked list through +0x20): the light and colour matrices go into the GTE, the objects are culled against the view and each visible
// one is submitted. An item with bit 1 of its flags (a skater) goes to M3D_RenderModel, with a second pass for its shadow when bit 3 of its flags asks for one. Any other object picks the level of detail of
// its model by its distance (a model names the next, rougher model in +0x1a, and an object beyond the roughest is not drawn), turns the view matrix by the object's own rotation and scale into the matrix at
// 0x00563a18 (as M3d_RenderBackground does), loads the lights, and submits the model unrotated, turned or in the "variant" way, with the object's depth offsets (bytes at +0x1c, +0x1d, +0x1e, times 32)
// pushed into the frame's three depth words around the submission. The local position / matrix are stack words in retail: the fourth word of the position at 0x00563b04 and fourteen bytes of the matrix
// copy at 0x00563a18 (when the object is turned) are stack leftovers.
void RenderKit::render_objects(Word list) {
    constexpr Word kView = 0x005620e0;
    constexpr Word kRotation = 0x006a3e10;
    constexpr Word kIr = 0x006a3eb0;
    constexpr Word kEdges = 0x00563a38;
    constexpr Word kModelTables = 0x0056d43c;
    unspecified_.clear();
    if (list == 0) {
        return;
    }
    for (Word k = 0; k < 18; k += 2) {
        set_half_at(m_, kGteLight + k, half_at(m_, kLightSource + k));
        set_half_at(m_, kGteColour + k, half_at(m_, kColourSource + k));
    }
    set_global_word(m_, 0x006a3e50, 0);
    set_global_word(m_, kGteBackground, u(static_cast<std::int16_t>(global_half(m_, 0x00563a96)) >> 1));
    set_global_word(m_, kGteBackground + 4, u(static_cast<std::int16_t>(global_half(m_, 0x00563a9e)) >> 1));
    m3d_.cull_objects_to_view(list, list == global_word(m_, 0x0055f6bc) ? 1u : (global_word(m_, 0x0055fa3c) != 0 ? 0x3fu : 0u) + 1);
    const bool environment = list == global_word(m_, 0x0056db28);
    if (environment) {
        engine(0x00461cf0, {global_word(m_, 0x00537c80)});
        engine(0x00461cf0, {global_word(m_, 0x00537c84)});
    }
    set_global_half(m_, 0x00563a54, global_half(m_, 0x00563a44));
    set_global_half(m_, 0x00563a10, global_half(m_, 0x0056209c));
    set_global_half(m_, 0x00563a64, global_half(m_, 0x00564368));
    set_global_half(m_, 0x00563a7c, global_half(m_, 0x0056436a));
    set_global_half(m_, 0x00563a88, global_half(m_, 0x0056436c));
    set_global_word(m_, 0x00563a80, global_word(m_, 0x00563ae4));
    set_global_half(m_, 0x00563b08, 0);
    set_global_word(m_, 0x00563a14, global_word(m_, 0x00563b34));
    set_global_word(m_, 0x00563ae8, global_word(m_, 0x00563a60));
    set_global_word(m_, 0x00564380, global_word(m_, 0x0056437c));
    const Word matrix_home = kScratch + 0x130;       // the object's own matrix (nine shorts)
    const Word product = kScratch + 0x110;          // the view matrix, then the product (32 bytes)
    const Word position = kScratch + 0x154;          // the position relative to the view (three dwords and a fourth, unwritten)
    const auto reset_pushbacks = [&]() {
        set_global_half(m_, 0x00563a64, global_half(m_, 0x00564368));
        set_global_half(m_, 0x00563a7c, global_half(m_, 0x0056436a));
        set_global_half(m_, 0x00563a88, global_half(m_, 0x0056436c));
    };
    Word object = list;
    do {
        set_global_word(m_, 0x0056437c, global_word(m_, 0x00564380));
        const Word region = byte_at(m_, object + 0x1f);
        if ((byte_at(m_, object + 5) & 0x80) == 0 && byte_at(m_, 0x0056d432 + region * 0x44) != 0) {
            set_global_word(m_, 0x005620a4, environment ? 0x800u : 0u);
            set_global_word(m_, 0x00563a78, half_at(m_, object + 0x1a));
            const Word flags = half_at(m_, object + 4);
            Word state = 0xffff0000;
            if ((flags & 0x80) != 0) {
                state = (((flags & 0x400) != 0 ? 0x80008u : 0u) + 0xfff70004u);
            } else if ((flags & 0x400) != 0) {
                set_global_word(m_, 0x00563a74, word_at(m_, object + 0x24));
                state = 0xfffb0008;
            }
            state &= 0xfffffffd;
            state |= 0x210000;
            if ((byte_at(m_, object + 5) & 8) != 0) {
                state = (((word_at(m_, object + 0x30) ^ state) & 0x180) ^ state) | 0x40;
            }
            set_global_word(m_, 0x00564358, state);
            set_global_word(m_, 0x00563a8c, (byte_at(m_, object + 4) >> 3) & 1);
            if ((byte_at(m_, object + 4) & 2) != 0) {
                set_global_half(m_, 0x005620b6, u(s(word_at(m_, object == global_word(m_, 0x0056a960) ? 0x00533f90 : 0x00533f94)) >> 12) - half_at(m_, global_word(m_, kView) + 8));
                if (word_at(m_, object + 0x14c) != 0) {
                    set_global_word(m_, 0x005620a4, global_word(m_, 0x005620a4) | 0x80);
                }
                engine(0x004610f0, {object});
                if (global_word(m_, 0x00563a8c) != 0) {
                    const Word saved = half_at(m_, object + 4);
                    set_half_at(m_, object + 4, saved | 0x400);
                    const Word level = global_word(m_, 0x0056a898);
                    const Word colour = level == 0 ? (((0xau << 8) | 0xd) << 8) | 8 : (((0x22u << 8) | 0x20) << 8) | 0x20;
                    set_word_at(m_, object + 0x24, colour);
                    set_global_word(m_, 0x00533908, 0);
                    set_global_word(m_, 0x00563a74, word_at(m_, object + 0x24));
                    set_global_word(m_, 0x00564358, (state & 0xfffbffff) | 8);
                    const Word kept = byte_at(m_, object + 0xdc);
                    set_byte_at(m_, object + 0xdc, 0);
                    engine(0x004610f0, {object});
                    set_byte_at(m_, object + 0xdc, kept);
                    set_global_word(m_, 0x00533908, 0xffff);
                    set_half_at(m_, object + 4, saved);
                }
                object = word_at(m_, object + 0x20);
                continue;
            }
            Word model = word_at(m_, word_at(m_, kModelTables + region * 0x44) + half_at(m_, object + 0x1a) * 4);
            if ((half_at(m_, model) & 0x20) != 0) {
                object = word_at(m_, object + 0x20);
                continue;
            }
            if ((half_at(m_, model) & 0x200) != 0) {
                set_global_word(m_, 0x00563a4c, 1);
            }
            if ((half_at(m_, model) & 0x400) != 0) {
                set_global_word(m_, 0x0056437c, 1);
            }
            if ((global_byte(m_, 0x00562108) & 1) != 0) {
                set_global_word(m_, 0x00562098, global_word(m_, 0x005620ac));
                set_global_word(m_, 0x005620a4, global_word(m_, 0x005620a4) | 0x40);
                set_global_word(m_, 0x005620a8, global_word(m_, 0x00563b38));
            }
            const Word view = global_word(m_, kView);
            for (Word k = 0; k < 18; k += 2) {
                set_half_at(m_, kRotation + k, half_at(m_, view + 0x74 + k));
            }
            set_global_word(m_, 0x006a3ea0, 0);
            set_global_word(m_, 0x006a3ea4, 0);
            set_global_word(m_, 0x006a3ea8, 0);
            const Word x = u((s(word_at(m_, object + 8)) >> 12) - s(word_at(m_, view + 4)));
            const Word y = u((s(word_at(m_, object + 0xc)) >> 12) - s(word_at(m_, view + 8)));
            const Word z = u((s(word_at(m_, object + 0x10)) >> 12) - s(word_at(m_, view + 0xc)));
            set_word_at(m_, position, x);
            set_word_at(m_, position + 4, y);
            set_word_at(m_, position + 8, z);
            set_global_half(m_, 0x006a3e58, x);
            set_global_half(m_, 0x006a3e5a, y);
            set_global_half(m_, 0x006a3e5c, z);
            math().rtv0();
            Word mac1 = global_word(m_, 0x006a3eb8);
            Word mac2 = global_word(m_, 0x006a3ebc);
            std::int32_t depth = s(global_word(m_, 0x006a3ec0));
            // An object past the GTE's 16-bit reach only gets here when the host stretched the cull box (cull_reach, host_options.hpp); its halves above wrapped, so the
            // rotation the game uses (the level of detail's depth, the turned object's translation) is made again from the whole position, as rtv0 would with a wide vector
            const std::int32_t whole[3] = {s(x), s(y), s(z)};
            if (whole[0] != static_cast<std::int16_t>(whole[0]) || whole[1] != static_cast<std::int16_t>(whole[1]) || whole[2] != static_cast<std::int16_t>(whole[2])) {
                std::int32_t wide[3];
                for (Word r = 0; r < 3; ++r) {
                    std::int64_t sum = 0;
                    for (Word c = 0; c < 3; ++c) {
                        sum += static_cast<std::int64_t>(static_cast<std::int16_t>(half_at(m_, kRotation + 2 * (r * 3 + c)))) * whole[c];
                    }
                    wide[r] = static_cast<std::int32_t>(sum >> 12);
                }
                mac1 = u(wide[0]);
                mac2 = u(wide[1]);
                depth = wide[2];
            }
            set_global_word(m_, 0x00563af8, x);
            set_global_word(m_, 0x00563afc, y);
            set_global_word(m_, 0x00563b00, z);
            set_global_word(m_, 0x00563b04, word_at(m_, position + 12));
            unspecified_.emplace_back(0x00563b04, 4);
            // the level of detail: a model carries (+0x18) the distance at which the next model (+0x1a) replaces it, in units of the viewport's scale divided by 191; a negative distance scales by a frame factor
            const Word edges = global_word(m_, kEdges);
            const std::int32_t scale = half_at(m_, edges + 0xe);
            const auto next_model = [&](Word at) { return word_at(m_, word_at(m_, kModelTables + region * 0x44) + half_at(m_, at + 0x1a) * 4); };
            const auto quotient = [&](Word at) { return (static_cast<std::int32_t>(static_cast<std::int16_t>(half_at(m_, at + 0x18))) * scale) / 191; };
            const auto scaled_limit = [&](Word at) {
                const std::int32_t narrowed = static_cast<std::int16_t>(quotient(at));
                return -(s(u(narrowed) * global_word(m_, 0x00563a3c)) >> 10);
            };
            // HostOptions::detail_distance stretches every one of these distances (at the game's 100 percent nothing is computed). It gives way to the game's own distances while the polygon
            // arena is three quarters used, so keeping more objects detailed cannot fill it (an overflow stops the game).
            const auto reach = [&](std::int32_t limit) {
                if (host_options().detail_distance == 100) {
                    return limit;
                }
                const Word capacity = global_word(m_, 0x00561150);
                const Word used = global_word(m_, 0x00560fd0) - (global_word(m_, 0x00568628) - capacity);
                return used < capacity / 4 * 3 ? detail_reach(limit) : limit;
            };
            bool too_far = false;
            if (static_cast<std::int16_t>(half_at(m_, model + 0x18)) < 0) {
                while (depth > reach(scaled_limit(model)) && half_at(m_, model + 0x1a) != 0xffff) {
                    model = next_model(model);
                }
                const std::int32_t limit = static_cast<std::int16_t>(half_at(m_, model + 0x18)) < 0 ? scaled_limit(model) : quotient(model);
                too_far = depth > reach(limit) && half_at(m_, model + 0x1a) == 0xffff;
            } else {
                while (depth > reach(quotient(model)) && half_at(m_, model + 0x1a) != 0xffff) {
                    model = next_model(model);
                }
                too_far = depth > reach(quotient(model)) && half_at(m_, model + 0x1a) == 0xffff;
            }
            if (too_far) {
                object = word_at(m_, object + 0x20);
                continue;
            }
            const bool turned = half_at(m_, object + 0x14) != 0 || half_at(m_, object + 0x16) != 0 || half_at(m_, object + 0x18) != 0;
            const bool plain = half_at(m_, model + 2) <= 100 && (byte_at(m_, model + 1) & 1) == 0 && (half_at(m_, object + 4) & 0x480) == 0 &&
                               s(u(depth) - u(s(word_at(m_, model + 8)) >> 12)) > static_cast<std::int32_t>(half_at(m_, edges + 0xa) / 10);
            set_global_word(m_, 0x00563ae0, plain && !turned ? 1 : 0);
            const auto load_view = [&]() {
                for (Word k = 0; k < 18; k += 2) {
                    set_half_at(m_, kRotation + k, half_at(m_, view + 0x74 + k));
                }
            };
            // the object's matrix turned by the matrix in RT, a column at a time: row r of column c at (2 * c + 6 * r)
            const auto turn = [&](Word source, Word destination) {
                for (Word column = 0; column < 3; ++column) {
                    for (Word row = 0; row < 3; ++row) {
                        set_half_at(m_, kIr + 2 * row, half_at(m_, source + 2 * column + 6 * row));
                    }
                    math().rtir();
                    for (Word row = 0; row < 3; ++row) {
                        set_half_at(m_, destination + 2 * column + 6 * row, half_at(m_, kIr + 2 * row));
                    }
                }
            };
            const auto load_product = [&]() {
                for (Word k = 0; k < 18; k += 2) {
                    set_half_at(m_, kRotation + k, half_at(m_, product + k));
                }
            };
            if (!turned) {
                for (Word k = 0; k < 32; k += 4) {
                    set_word_at(m_, product + k, word_at(m_, view + 0x74 + k));
                }
                m3d_.set_identity_matrix(matrix_home);
                if ((byte_at(m_, object + 5) & 2) != 0) {
                    m3d_.scale_object_matrix(object, matrix_home);
                    turn(matrix_home, product);
                    load_product();
                }
            } else {
                m3d_.rotation_from_angles(object + 0x14, matrix_home);
                if ((byte_at(m_, object + 5) & 2) != 0) {
                    m3d_.scale_object_matrix(object, matrix_home);
                }
                load_view();
                turn(matrix_home, product);
                load_product();
                unspecified_.emplace_back(0x00563a18 + 18, 14);
            }
            for (Word k = 0; k < 32; k += 4) {
                set_word_at(m_, 0x00563a18 + k, word_at(m_, product + k));
            }
            const Word colours = word_at(m_, 0x0056d450 + region * 0x44);
            set_global_word(m_, 0x00563a58, colours);
            set_global_word(m_, 0x00563a84, colours);
            const Word item_matrix = word_at(m_, object + 0x40);
            if ((byte_at(m_, model) & 4) != 0 || (byte_at(m_, object + 4) & 0x80) != 0) {
                if (!turned) {
                    for (Word k = 0; k < 18; k += 2) {
                        set_half_at(m_, kGteLight + k, half_at(m_, item_matrix + k));
                    }
                } else {
                    for (Word k = 0; k < 18; k += 2) {
                        set_half_at(m_, kRotation + k, half_at(m_, item_matrix + k));
                    }
                    turn(matrix_home, kGteLight);
                }
                for (Word k = 0; k < 18; k += 2) {
                    set_half_at(m_, kGteColour + k, half_at(m_, item_matrix + 0x14 + k));
                }
                m3d_.set_background_colour(item_matrix + 0x28);
                engine(0x004d11d0, {model + half_at(m_, model + 2) * 8 + 0x1c, half_at(m_, model + 4), 0, 0});
            }
            const Word saved_width = half_at(m_, edges + 0xa);
            const bool fade = (byte_at(m_, object + 5) & 0x20) != 0;
            if (fade) {
                set_global_half(m_, 0x00563b08, 0);
                set_global_half(m_, 0x00563a54, 0x7fff);
                set_half_at(m_, edges + 0xa, 0x7fff);
                set_global_word(m_, 0x005620a4, global_word(m_, 0x005620a4) | 0x400);
            }
            const auto push_offsets = [&]() {
                const std::int32_t first = static_cast<std::int8_t>(byte_at(m_, object + 0x1c));
                const std::int32_t second = static_cast<std::int8_t>(byte_at(m_, object + 0x1e));
                const std::int32_t third = static_cast<std::int8_t>(byte_at(m_, object + 0x1d));
                if (first != 0) {
                    set_global_half(m_, 0x00563a64, u(first << 5));
                }
                if (second != 0) {
                    set_global_half(m_, 0x00563a7c, u(second << 5));
                }
                if (third != 0) {
                    set_global_half(m_, 0x00563a88, u(third << 5));
                }
                return first != 0 || second != 0 || third != 0;
            };
            const auto any_offset = [&]() { return byte_at(m_, object + 0x1c) != 0 || byte_at(m_, object + 0x1e) != 0 || byte_at(m_, object + 0x1d) != 0; };
            if (global_word(m_, 0x00563ae0) != 0) {
                // (the model is close and plain, the object unturned: the variant)
                if (any_offset()) {
                    push_offsets();
                    engine(0x00461a90, {model, position});
                    reset_pushbacks();
                } else {
                    engine(0x00461a90, {model, position});
                }
            } else if (!turned && (byte_at(m_, object + 5) & 2) == 0) {
                if (any_offset()) {
                    push_offsets();
                    engine(0x00461b70, {model, position});
                    reset_pushbacks();
                } else {
                    engine(0x00461b70, {model, position});
                }
            } else {
                set_global_word(m_, 0x006a3ea0, mac1);
                set_global_word(m_, 0x006a3ea4, mac2);
                set_global_word(m_, 0x006a3ea8, static_cast<Word>(depth));
                if (any_offset()) {
                    push_offsets();
                    engine(0x00461a50, {model});
                    reset_pushbacks();
                } else {
                    engine(0x00461a50, {model});
                }
            }
            if (fade) {
                set_global_half(m_, 0x00563a54, global_half(m_, 0x00563a44));
                set_global_half(m_, 0x00563b08, 0);
                set_half_at(m_, global_word(m_, kEdges) + 0xa, saved_width);
            }
        }
        object = word_at(m_, object + 0x20);
    } while (object != 0);
}

}  // namespace opentony::runtime::renderkit
