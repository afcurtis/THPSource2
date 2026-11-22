#include "core_views.hpp"
#include "host_options.hpp"
#include "seam_addresses_generated.hpp"

#include <bit>

// Phase 4.14c: the display half of main.cpp (0x00467c90 M3D_RenderStage, 0x00468520 Display) and the level launch (0x0046a8d0). Written from the machine code like core_main.cpp: the order of the
// stack arguments, the 16-bit stores and the x87 clamp of the depth scale decide the retail state. The level launch reports every call into the rest of the game as a service (the level loop,
// the clean-ups, the object creation, the front end): it is the top of the call tree.

namespace opentony::runtime::core {

using namespace views;

namespace {

using Word = std::uint32_t;

constexpr Word kScratch = pcplatform::kScratchBase;
constexpr Word kNormalizeVector = kScratch + 0x500;       // the vector handed to Math_NormalizeFixed12 (a stack local in retail)
constexpr Word kPlayer0 = globals::kPlayer0;
constexpr Word kPlayer1 = globals::kPlayer0 + 4;
constexpr Word kArenaCursor = 0x00560fd0;
constexpr Word kD3dSoftware = 0x029da3a0;
constexpr Word kFadeLevel = 0x0056a924;
constexpr Word kViewportMode = 0x0056a928;
constexpr Word kGameStatus = 0x0056a8d0;
constexpr Word kCurrentLevel = 0x0056a898;
constexpr Word kReplayPlayers = 0x0056b8d4;
constexpr Word kFont = 0x005619e0;

}  // namespace

// 0x00468520: one frame's drawing: the on-screen message, the camera objects, the close-camera toggle, the 3D stages for the viewport layout, the panel, the front end and the fade
void CoreKit::display() {
    if (global_word(m_, 0x0056a8cc) != 0) {
        engine(0x00473580, {0xff, 0, 0, 0});
        engine(0x004734f0, {0x100});
        engine(0x004734b0, {0});
        engine(0x004739e0, {0x100, 0x3c, global_word(m_, 0x0056a8cc), 0});
    }
    if (global_word(m_, 0x0056a86c) != 0) {
        set_global_word(m_, 0x00563a60, 0x80000000u);
        set_global_word(m_, 0x0056114c, 0);
        engine(0x0042fc70);
    }
    viewport_rect(m_, 0).set_field_0e(static_cast<std::uint16_t>(engine_value(0x0040f1f0, {global_word(m_, 0x0055fa38)})));
    // (the camera object update: its call site is the one place that can change the flag at 0x0056a900, which the probe scripts)
    auto update_cameras = [&]() {
        engine(0x00480ff0, {0x0055fa38});
        if (!s_.live()) {
            set_global_word(m_, 0x0056a900, static_cast<Word>(s_.value(pcplatform::engine_name(0x00480ff0))));
        }
    };
    bool toggle;
    if (global_word(m_, 0x0056a8e0) == 0) {
        update_cameras();
        toggle = global_word(m_, 0x0056a900) == 0;
    } else if (global_word(m_, 0x0056a900) == 0) {
        toggle = true;
    } else {
        update_cameras();
        toggle = global_word(m_, 0x0056a900) == 0;
    }
    if (toggle) {
        if (global_byte(m_, 0x0056b0e8) != 0) {
            if (global_word(m_, 0x00534084) != 0) {
                set_global_word(m_, 0x00534084, 0);
                const Word mode = global_word(m_, globals::kGameMode);
                if (global_word(m_, globals::kNumberOfPlayers) != 2 || mode == 7 || mode == 10) {
                    set_global_word(m_, 0x0056a860, global_word(m_, 0x0056a860) ^ 1);
                }
            }
        } else {
            set_global_word(m_, 0x00534084, 1);
        }
        if (global_byte(m_, 0x0056b255) != 0) {
            set_global_byte(m_, 0x0056b255, 0);
        }
    }
    set_global_word(m_, kViewportMode, 0);
    for (const Word name : {0x00524e74u, 0x00524e40u}) {
        const Word region = engine_value(0x004b3230, {name});
        if (region != 0xffffffffu) {
            engine(0x00461cf0, {region});
        }
    }
    engine(0x004dbea0);
    if (!s_.live()) set_global_word(m_, kViewportMode, static_cast<Word>(s_.value(pcplatform::engine_name(0x004dbea0))));         // (the mouse cursor draw can change the layout: scripted)
    const std::int32_t width = global_int(m_, 0x00527d50);
    const std::int32_t height = global_int(m_, 0x00527d54);
    const Word w2 = static_cast<Word>(width / 2);
    const Word h2 = static_cast<Word>(height / 2);
    auto camera = [&](Word dist, Word pitch, Word offset) {
        set_global_word(m_, 0x00524a40, dist);
        set_global_word(m_, 0x00524a44, pitch);
        set_global_word(m_, 0x00524a48, offset);
    };
    const bool free_camera = global_word(m_, 0x0056a8e0) != 0;
    switch (global_word(m_, kViewportMode)) {
    case 0:
        if (!free_camera) {
            if (global_word(m_, 0x0056a860) != 0) {
                camera(0x182, 0x1e, 0xffffff60u);
            } else {
                camera(0x122, 0x96, 0xffffff94u);
            }
        }
        render_stage(0x00533fa8, global_word(m_, 0x006a3d14));
        break;
    case 1:
        if (!free_camera) {
            camera(0x11e, 0x76, 0xffffff74u);
        }
        add_rect(w2 - 2, 0, 4, static_cast<Word>(height));
        render_stage(0x00533fc0, 0);
        render_stage(0x00533fd8, 1);
        break;
    case 2:
        if (!free_camera) {
            camera(0x1c2, 0x3c, 0xffffff88u);
        }
        add_rect(0, h2 - 2, static_cast<Word>(width), 4);
        render_stage(0x00533ff0, 0);
        render_stage(0x00534008, 1);
        break;
    case 3:
        if (!free_camera) {
            camera(0x282, 0x20, 0xffffff54u);
        }
        add_rect(w2, 0, w2, h2);
        add_rect(0, h2, w2, h2);
        render_stage(0x00534020, 0);
        render_stage(0x00534038, 1);
        break;
    case 4:
        if (!free_camera) {
            camera(0x262, 0, 0xffffff38u);
        }
        add_rect(w2 + 0x30, 0, w2 - 0x30, h2 - 0x20);
        add_rect(0, h2 + 0x20, w2 - 0x30, h2 - 0x20);
        render_stage(0x00534050, 0);
        render_stage(0x00534068, 1);
        break;
    default:
        break;
    }
    if (global_word(m_, 0x0056a868) == 0) {
        engine(0x0048d090);
    }
    engine(0x00489cd0);
    engine(0x0044eac0);
    engine(0x00489e70);
    engine(0x0044ca60);
    engine(0x004737c0);
    const Word second = global_word(m_, kPlayer1);
    const Word first = global_word(m_, kPlayer0);
    auto balance = [&](Word player, Word y) { engine(0x0048ac60, {skater_at(m_, player).field_2f14(), 0xfa0, 0x19, y, 0x78, 0xff, 0xff00}); };
    if (second == 0) {
        if (skater_at(m_, first).field_2dd4() != 0 && skater_at(m_, first).field_3210() != 0) {
            engine(0x0048ac60, {skater_at(m_, first).field_2f14(), 0xfa0, 0x1e, 0xc8, 0x78, 0xff, 0xff00});
        }
    } else {
        if (skater_at(m_, first).field_2dd4() != 0 && skater_at(m_, first).field_3210() != 0) {
            balance(first, 0x50);
        }
        if (skater_at(m_, second).field_2dd4() != 0 && skater_at(m_, second).field_3210() != 0) {
            balance(second, 0x150);
        }
    }
    // The host's grind meter (HostOptions::grind_meter): the PC game ported the PlayStation game's horizontal meter (0x0048abf0) but never calls it, so a rail's balance bar (+0x2ef0, a bail
    // at 4000) is invisible. Drawn above each grinding skater's trick names, at the spot of the HUD that names the viewport's layout (Panel::display's own coordinates); not with the HUD off or paused.
    if (host_options().grind_meter && !host_options().hide_hud && global_word(m_, 0x00533f9c) != 0 && global_word(m_, 0x00561c04) == 0) {
        const std::int32_t view = static_cast<std::int32_t>(global_word(m_, kViewportMode));
        const std::int32_t h2 = static_cast<std::int32_t>(global_word(m_, 0x00527d54)) / 2;
        for (const Word skater : {first, second}) {
            if (skater == 0 || word_at(m_, skater + 0x30b8) != 4) {
                continue;
            }
            const std::int32_t slot = static_cast<std::int32_t>(word_at(m_, skater + 0x170) & 1u) + view * 2;
            static constexpr std::int32_t kNameX[8] = {0x100, 0x100, 0x180, 0x80, 0x100, 0x100, 0x80, 0x180};
            const std::int32_t name_y[8] = {0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd - h2, 0xd7, 0xda - h2};
            if (slot > 7) {
                continue;
            }
            engine(0x0048abf0, {word_at(m_, skater + 0x2ef0), 4000, 0x30, static_cast<Word>(kNameX[slot]), static_cast<Word>(name_y[slot] - 0x14), 0xff, 0xff00});
        }
    }
    if (global_word(m_, 0x00561c04) == 0 || global_word(m_, 0x00561bfc) == 0x2a) {
        set_global_word(m_, kFadeLevel, 0);
    } else {
        draw_dim_overlay();
    }
    engine(0x004499b0);
    set_global_word(m_, 0x0056a908, (global_word(m_, kArenaCursor) & 0x7fffffffu) - (render_state(m_).arena_base() & 0x7fffffffu));
}

// 0x00467c90: one viewport's 3D scene: the world objects, the skaters (the lone player in front), the effects and the background, and the cleanup of the frame
void CoreKit::render_stage(Word viewport, Word index) {
    if ((engine_value(0x00416980, {9}) & 0xff) != 0) {
        set_global_word(m_, 0x0056437c, 1);
    }
    if (global_word(m_, globals::kNumberOfPlayers) == 2) {
        set_global_word(m_, 0x0056437c, 0);
    }
    set_global_word(m_, globals::kViewIndex, index);
    const Word shifted = index << 15;
    render_state(m_).set_viewport_table_alias(render_state(m_).viewport_table() + shifted);
    engine(0x0045e8e0, {globals::kViews + index * globals::kViewStride, viewport, render_state(m_).viewport_table() + shifted});
    const Word actor = word_at(m_, kPlayer0 + 4 * index);
    if (skater_at(m_, actor).position().y() < global_int(m_, 0x0052870c)) {
        engine(0x004328c0, {0, global_word(m_, 0x005613bc)});
    } else {
        engine(0x004328c0, {1, global_word(m_, 0x005613bc)});
    }
    set_global_word(m_, 0x0056a8c8, global_word(m_, 0x0056a8c4));
    engine(0x0045f530, {global_word(m_, globals::kEnvironmentList)});
    set_global_word(m_, 0x0056a8c8, 0);
    engine(0x0045f530, {global_word(m_, globals::kGameList)});
    const Word first = global_word(m_, kPlayer0);
    if ((word_at(m_, 0x0053a514 + skater_at(m_, first).skater_resource().address * 0x170) & 0x100) != 0) {
        set_global_word(m_, 0x0056437c, 0);
    }
    set_global_word(m_, 0x005685e4, 0);
    engine(0x0048ec00, {first});
    if (global_word(m_, kPlayer1) != 0) {
        engine(0x0048ec00, {global_word(m_, kPlayer1)});
    }
    engine(0x0046da70, {global_word(m_, kPlayer0)});
    if (global_word(m_, kPlayer1) != 0) {
        engine(0x0046da70, {global_word(m_, kPlayer1)});
    }
    const std::int16_t saved_a = global_short(m_, 0x00564368);
    const std::int16_t saved_b = global_short(m_, 0x0056436a);
    const std::int16_t saved_c = global_short(m_, 0x0056436c);
    set_global_half(m_, 0x00564368, 0xffc0);
    Word player = global_word(m_, kPlayer0);
    if (skater_at(m_, player).physics_state() == 0) {
        set_global_half(m_, 0x0056436a, 0);
    } else {
        set_global_half(m_, 0x0056436a, 0x80);
        if (skater_at(m_, player).field_2f64() != 0) {
            set_global_half(m_, 0x0056436a, 0);
        }
    }
    set_global_half(m_, 0x0056436c, 0x40);
    if (global_word(m_, 0x0056a86c) != 0) {
        skater_at(m_, player).set_draw_in_front(0);
        player = global_word(m_, kPlayer0);
    }
    const Word flag = skater_at(m_, player).draw_in_front();
    skater_at(m_, player).set_draw_in_front(0);
    const Word owner = global_word(m_, globals::kPlayerRecord);
    const Word next_of_owner = next_object(m_, owner);
    object_at(m_, owner).set_next(GuestPtr<guest::RuntimeListObject>{0});
    engine(0x0048fc90, {global_word(m_, kPlayer0), skater_at(m_, global_word(m_, kPlayer0)).field_31f0()});
    engine(0x0045f530, {global_word(m_, kPlayer0)});
    if (global_word(m_, kPlayer1) != 0) {
        engine(0x0048fc90, {global_word(m_, kPlayer1), skater_at(m_, global_word(m_, kPlayer1)).field_31f0()});
        engine(0x0045f530, {global_word(m_, kPlayer1)});
    }
    object_at(m_, global_word(m_, globals::kPlayerRecord)).set_next(GuestPtr<guest::RuntimeListObject>{next_of_owner});
    skater_at(m_, global_word(m_, kPlayer0)).set_draw_in_front(static_cast<std::uint8_t>(flag));
    if (global_word(m_, globals::kNumberOfPlayers) == 1 && flag != 0) {
        draw_player_in_front();
    }
    set_global_word(m_, 0x005685e4, 0);
    engine(0x0048f590, {global_word(m_, kPlayer0)});
    if (global_word(m_, kPlayer1) != 0) {
        engine(0x0048f590, {global_word(m_, kPlayer1)});
    }
    set_global_half(m_, 0x00564368, static_cast<Word>(static_cast<std::uint16_t>(saved_a)));
    set_global_half(m_, 0x0056436a, static_cast<Word>(static_cast<std::uint16_t>(saved_b)));
    set_global_half(m_, 0x0056436c, static_cast<Word>(static_cast<std::uint16_t>(saved_c)));
    engine(0x00407310);
    engine(0x0045f530, {global_word(m_, globals::kBaddyList)});
    engine(0x0045f530, {global_word(m_, globals::kPowerUpList)});
    if (global_word(m_, kViewportMode) == 0 && global_word(m_, 0x0056a86c) == 0) {
        engine(0x00460ca0, {global_word(m_, 0x0055f6b0)});
    }
    if (global_word(m_, globals::kGameMode) == 9 && global_word(m_, 0x0056db64) == 2 && global_word(m_, 0x0056db5c) == word_at(m_, kPlayer0 + 4 * index)) {
        versus_pointer();
    }
    engine(0x0045f3f0);
    if (global_word(m_, 0x0056a88c) != 0 && global_word(m_, globals::kNumberOfPlayers) == 1) {
        flip_primitives();
    }
    set_global_word(m_, 0x0056437c, 0);
}

// the lone player drawn in front of everything: the player's rotation matrix goes through the GTE one column at a time, the position is swapped for the one saved at +0x90, the model is submitted
// with a depth scale from +0x317c, and everything is put back
void CoreKit::draw_player_in_front() {
    const RuntimeMatrixTransformView local(m_, kScratch + 0x540);
    const RuntimeQ12MatrixView matrix = local.rotation();
    const RuntimeQ12MatrixView rotation = gte_registers(m_).rotation();
    const RuntimeShortVector3View ir = gte_registers(m_).ir();
    const RuntimeIntVector3View zero(m_, kScratch + 0x560);
    set_vector(zero, 0, 0, 0);
    math_.matrix_set_translation(local.address(), zero.address());
    Word player = global_word(m_, kPlayer0);
    matrix.set_elements(0, 0x1000);
    matrix.set_elements(1, 0);
    matrix.set_elements(2, 0);
    if (skater_at(m_, player).field_9e() == 0) {
        skater_at(m_, player).set_field_9e(1);
    }
    const std::int32_t lean = static_cast<std::int32_t>(static_cast<Word>(-static_cast<std::int32_t>(skater_at(m_, player).field_9c())) << 12) / static_cast<std::int32_t>(skater_at(m_, player).field_9e());
    matrix.set_elements(6, 0);
    matrix.set_elements(3, static_cast<std::int16_t>(lean));
    const std::int32_t roll = static_cast<std::int32_t>(static_cast<Word>(-static_cast<std::int32_t>(skater_at(m_, player).field_a0())) << 12) / static_cast<std::int32_t>(skater_at(m_, player).field_9e());
    matrix.set_elements(7, 0);
    matrix.set_elements(8, 0x1000);
    rotation.set_elements(0, 0x1000);
    rotation.set_elements(1, 0);
    rotation.set_elements(2, 0);
    rotation.set_elements(3, static_cast<std::int16_t>(lean));
    rotation.set_elements(4, 0);
    rotation.set_elements(6, 0);
    rotation.set_elements(7, 0);
    rotation.set_elements(8, 0x1000);
    matrix.set_elements(5, static_cast<std::int16_t>(roll));
    rotation.set_elements(5, static_cast<std::int16_t>(roll));
    // (a column at a time goes through the GTE: the element indices of the column, the same in the skater's matrix and in the local one)
    static const Word columns[3][3] = {{0, 3, 6}, {1, 4, 7}, {2, 5, 8}};
    for (int column = 0; column < 3; ++column) {
        player = global_word(m_, kPlayer0);
        const RuntimeQ12MatrixView source = skater_at(m_, player).model_matrix().rotation();
        for (Word row = 0; row < 3; ++row) {
            set_component(ir, row, source.elements(columns[column][row]));
        }
        math_.rtir();
        for (Word row = 0; row < 3; ++row) {
            matrix.set_elements(columns[column][row], static_cast<std::int16_t>(component(ir, row)));
        }
    }
    player = global_word(m_, kPlayer0);
    // the 32-byte copy: the nine shorts of the matrix, 2 bytes of padding the retail code never wrote (left as they are, and not compared), and the 12 bytes of the translation, which are zero
    const Word model = skater_at(m_, player).model_matrix_address();
    for (Word k = 0; k < 4; ++k) {
        set_word_at(m_, model + 4 * k, word_at(m_, local.address() + 4 * k));
    }
    set_half_at(m_, model + 16, half_at(m_, local.address() + 16));
    scrap_.emplace_back(model + 18, 2);
    for (Word k = 5; k < 8; ++k) {
        set_word_at(m_, model + 4 * k, word_at(m_, local.address() + 4 * k));
    }
    const RuntimeIntVector3View position = skater_at(m_, player).position();
    const RuntimeIntVector3View saved = skater_at(m_, player).saved_position();
    const Word x = ux(position);
    const Word y = uy(position);
    const Word z = uz(position);
    set_vector(position, ux(saved), uy(saved), uz(saved));
    engine(0x00461bf0, {global_word(m_, kPlayer0)});
    set_global_word(m_, 0x00564378, 1);
    const auto precision = math_.precision();
    long double scaled = mathkit::x87::round_to(precision, static_cast<long double>(skater_at(m_, global_word(m_, kPlayer0)).depth_scale_source()) * static_cast<long double>(std::bit_cast<float>(0x3b808081u)));
    if (scaled < 0.0L) {
        scaled = 0.0L;
    } else if (!(scaled <= 1.0L)) {
        scaled = 1.0L;
    }
    const float depth = static_cast<float>(mathkit::x87::round_to(precision, 1.0L - scaled));
    set_global_word(m_, 0x00563a5c, std::bit_cast<Word>(depth));
    if ((engine_value(0x004cde70) & 0xff) == 0) {
        const Word software = global_word(m_, kD3dSoftware);
        if (software == 0 || static_cast<std::int32_t>(software) >= 2) {
            engine(0x00460a90, {global_word(m_, globals::kPlayerRecord)});
        }
    }
    player = global_word(m_, kPlayer0);
    set_global_word(m_, 0x00564378, 0);
    set_vector(skater_at(m_, player).position(), x, y, z);
}

// the versus-mode pointer: the angle between the direction from the camera to the pointed-at player and the one between the two players, handed to the pointer object
void CoreKit::versus_pointer() {
    const Word a = global_word(m_, 0x0056db5c);
    const Word b = global_word(m_, 0x0056db60);
    const Word position = skater_at(m_, a).position_address();
    const Word camera = word_at(m_, a + 0x29b0) + 8;
    const Word work = kScratch + 0x600;
    auto direction = [&](Word from, Word to) {
        vector_subtract_out(work, from, to);
        const RuntimeIntVector3View source(m_, work);
        const RuntimeIntVector3View target(m_, kNormalizeVector);
        set_vector(target, ux(source), 0, uz(source));
        engine(0x00465f60, {kNormalizeVector});
        take_filled(kNormalizeVector, 12, pcplatform::engine_name(0x00465f60));
        return std::pair<std::int32_t, std::int32_t>{target.x(), target.z()};
    };
    const auto first = direction(position, camera);
    const auto second = direction(skater_at(m_, b).position_address(), position);
    const std::int32_t from_second = (0x400 - math_.ratan2(second.second, second.first)) & 0xfff;
    const std::int32_t from_first = (0x400 - math_.ratan2(first.second, first.first)) & 0xfff;
    const Word pointer = global_word(m_, 0x00568614);
    set_half_at(m_, pointer + 0x16, static_cast<Word>(from_second - from_first) & 0xfff);
    engine(0x00456580, {global_word(m_, 0x00568614)});
}

// ---- the level launch ----------------------------------------------------------------------------------------------------------------------------------------------------------

// 0x0046a8d0: set a level up, run it (again after a restart) and tear it down. Every call into the rest of the game is a service here: the level loop, the clean-ups, the object creation
void CoreKit::launch_game(Word level) {
    set_global_word(m_, kCurrentLevel, level);
    auto cheat = [&](Word number) { return (engine_value(0x00416980, {number}) & 0xff) != 0; };
    if (cheat(7)) {
        set_global_word(m_, 0x0056b7f4, 10);
    }
    if (cheat(3)) {
        set_global_word(m_, 0x0056a8ec, 1);
    }
    if (cheat(0xf)) {
        set_global_word(m_, 0x0056db8c, 1);
    }
    if (cheat(0x10)) {
        set_global_word(m_, 0x0056a88c, 1);
    }
    if (global_word(m_, globals::kGameMode) == 8) {
        set_global_word(m_, 0x0056db8c, 0);
    }
    engine(0x004586f0, {0, 0x10});
    engine(0x00458730);
    engine(seam::hal::WINMAIN_SwitchResolution, {1});
    const Word width_at = kScratch + 0x5e0;
    const Word height_at = kScratch + 0x5e4;
    engine(0x004f6d00, {width_at, height_at});
    const auto dimensions = pcplatform::engine_name(0x004f6d00);
    if (!s_.live()) {
        set_word_at(m_, width_at, static_cast<Word>(s_.value(dimensions)));
        set_word_at(m_, height_at, static_cast<Word>(s_.value(dimensions)));
    }
    const Word width = word_at(m_, width_at);
    const Word height = word_at(m_, height_at);
    // (the first five of the nine screen rectangles: the right and bottom edges, then the left and top)
    const auto narrow = [](Word v) { return static_cast<std::uint16_t>(v); };
    const auto half = [](Word v) { return static_cast<std::uint16_t>((v & 0xffff) >> 1); };
    viewport_rect(m_, 0).set_right(narrow(width));
    viewport_rect(m_, 1).set_right(half(width));
    viewport_rect(m_, 2).set_left(half(width));
    viewport_rect(m_, 2).set_right(narrow(width));
    viewport_rect(m_, 3).set_right(narrow(width));
    viewport_rect(m_, 4).set_right(narrow(width));
    const Word restart = global_word(m_, 0x0056a89c);
    viewport_rect(m_, 0).set_left(0);
    viewport_rect(m_, 0).set_top(0);
    viewport_rect(m_, 0).set_bottom(narrow(height));
    viewport_rect(m_, 1).set_left(0);
    viewport_rect(m_, 1).set_top(0);
    viewport_rect(m_, 1).set_bottom(narrow(height));
    viewport_rect(m_, 2).set_top(0);
    viewport_rect(m_, 2).set_bottom(narrow(height));
    viewport_rect(m_, 3).set_left(0);
    viewport_rect(m_, 3).set_top(0);
    viewport_rect(m_, 3).set_bottom(half(height));
    viewport_rect(m_, 4).set_left(0);
    viewport_rect(m_, 4).set_top(half(height));
    viewport_rect(m_, 4).set_bottom(narrow(height));
    set_global_word(m_, 0x0056a8e8, restart);
    set_global_word(m_, kGameStatus, 7);
    engine(0x004524a0);
    if (!s_.live()) set_global_word(m_, kGameStatus, static_cast<Word>(s_.value(pcplatform::engine_name(0x004524a0))));         // (the front end's level load leaves the exit code: scripted)
    if (global_word(m_, kGameStatus) == 9) {
        log_line(0x005344e4);
        engine(0x0044ada0, {0x005254e0});
        copy_system_font(engine_value(0x0044b000, {0x005254e0}));
        engine(0x0046a600, {kFont, global_word(m_, 0x0052e9d0)});
        engine(0x0044b0a0, {0x005254e0});
        engine(0x0045d680);
        const std::int32_t count = global_short(m_, kReplayPlayers);
        set_global_half(m_, kReplayPlayers, static_cast<Word>(count < 0 ? count : -count));          // (sign - (sign ^ x): the identity for a negative count, the negation of a positive one)
    } else {
        const Word shadow = engine_value(0x004b3680, {0x005344dc});
        check(shadow != 0, 0x005344cc);
        const Word texture = word_at(m_, shadow + 4);
        check(texture != 0, 0x005344b8);
        for (const Word off : {4u, 0xau, 9u, 0xbu}) {
            set_byte_at(m_, texture + off, byte_at(m_, texture + off) - 1);
        }
        if (cheat(0xb)) {
            log_line(0x00534498);
            set_global_byte(m_, 0x0056a3a2, 0);
            set_global_byte(m_, 0x0056a386, 0);
            engine(0x00416a00, {0xb, 1, 1});
        }
        if (cheat(4)) {
            log_line(0x0053447c);
            set_global_byte(m_, 0x0056a386, 0);
            engine(0x00416a00, {4, 1, 1});
        }
        std::uint8_t saved = static_cast<std::uint8_t>(engine_value(0x00416980, {8}));
        if (saved != 0x64) {
            engine(0x0048fd60, {global_word(m_, kPlayer0), saved, 0, 0});
            if (global_word(m_, kPlayer1) != 0) {
                engine(0x0048fd60, {global_word(m_, kPlayer1), saved, 0, 0});
            }
        }
        if (global_word(m_, 0x005618c8) == 0) {
            if (global_word(m_, globals::kGameMode) == 7) {
                engine(0x0044b470);
            }
            set_global_word(m_, 0x00568618, 0);
            set_global_word(m_, kGameStatus, 7);
            for (;;) {
                set_global_byte(m_, 0x0056b028, 1);
                set_global_byte(m_, 0x0056b029, 0);
                set_global_byte(m_, 0x0056b194, 1);
                set_global_byte(m_, 0x0056b195, 0);
                set_global_word(m_, 0x0056a860, host_options().default_camera != 0 ? 1u : 0u);   // the level's camera: the first position unless the host asks for the second
                set_global_word(m_, kViewportMode, 0);
                const Word mode = global_word(m_, globals::kGameMode);
                if (global_word(m_, globals::kNumberOfPlayers) == 2 && mode != 7 && mode != 10) {
                    set_global_word(m_, kViewportMode, 1);
                }
                const Word config = engine_value(0x00416050, {skater_at(m_, global_word(m_, kPlayer0)).skater_resource().address});
                if (mode != 1) {
                    saved = byte_at(m_, global_word(m_, kCurrentLevel) + config + 0x26);
                }
                engine(0x0046a3a0);
                if (!s_.live()) set_global_word(m_, kGameStatus, static_cast<Word>(s_.value(pcplatform::engine_name(0x0046a3a0))));         // (the level loop's exit code: scripted)
                if (global_word(m_, globals::kGameMode) != 1) {
                    set_byte_at(m_, global_word(m_, kCurrentLevel) + config + 0x26, saved);
                }
                if (global_word(m_, 0x0056a89c) != 0) {
                    set_global_word(m_, kGameStatus, 8);
                }
                set_global_word(m_, 0x00568a58, 0);
                set_global_word(m_, 0x00568a5c, 0);
                set_global_word(m_, 0x00568630, 0);
                if (global_word(m_, kGameStatus) != 7) {
                    break;
                }
                log_line(0x00534468);
                engine(0x00458500, {global_byte(m_, 0x005614c4) == 0 ? 1u : 2u});
                engine(0x00448c60);
                engine(0x004691e0);
                engine(0x004c8130);
                engine(0x004aa5d0);
                engine(0x004c4e30);
                if (global_word(m_, kGameStatus) != 7) {
                    break;
                }
            }
            log_line(0x00534438);
        } else {
            set_global_word(m_, 0x005618c8, 0);
            set_global_word(m_, 0x00561b9c, (global_byte(m_, 0x00561990) != 0 ? 3u : 0u) + 1);
        }
        if (global_byte(m_, 0x005614c4) == 0) {
            engine(0x0046a600, {kFont, global_word(m_, 0x0052e61c)});
        } else {
            Word index = global_word(m_, 0x005613bc);
            const Word table = word_at(m_, index * 0x44 + 0x0056d450);
            if (table != 0) {
                mem_delete(table);
                index = global_word(m_, 0x005613bc);
            }
            set_global_word(m_, 0x00563a58, 0);
            set_word_at(m_, index * 0x44 + 0x0056d450, 0);
        }
        engine(0x0048b100);
    }
    engine(0x004586f0, {0, 0x10});
    engine(0x00448c60);
    set_global_word(m_, globals::kNumberOfPlayers, 1);
    engine(seam::hal::WINMAIN_SwitchResolution, {2});
}

}  // namespace opentony::runtime::core
