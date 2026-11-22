#include "auto_views.hpp"
#include "bit_kit.hpp"

// Phase 4.15d: the cheat codes the game accepts while the pad's buttons are held (0x004af660, 0x004af6b0): the held buttons choose a pair of seed words, which are mixed into a 64-bit rolling
// hash the way the typed code's letters were; the hash that results is compared with the hash of each code. The routine sits at the end of shatter.cpp in the executable. The career, the skater and the
// front end it reaches are services.

namespace opentony::runtime::bitkit {

using namespace opentony::runtime::autoviews;

namespace {

using Word = std::uint32_t;

constexpr Word kHashA = 0x0056c574;
constexpr Word kHashB = 0x0056c578;
constexpr Word kGameMode = 0x00533f38;                // LevelEventGameModeSelector
constexpr Word kNetMode = 0x004de150;                 // NetGame_ModeUsesNet()
constexpr Word kToggleCheat = 0x00416c90;             // Career_ToggleCheat(id, on)
constexpr Word kCurrentCharacter = 0x004b0c30;
constexpr Word kFindTrickConfig = 0x00416050;
constexpr Word kSelectSlot = 0x004b18a0;
constexpr Word kClearGame = 0x00416790;
constexpr Word kFatten = 0x0048fd60;                  // thiscall(player, amount, 0, 0)
constexpr Word kSwitchOnEndLevel = 0x0044e0f0;
constexpr Word kClearGameWithEveryone = 0x00416890;
constexpr Word kPlayer0 = 0x0056a858;
constexpr Word kPlayer1 = 0x0056a85c;
constexpr Word kModeWord = 0x0056b7f4;
constexpr Word kLatch = 0x00568658;
constexpr Word kSecondaryState = 0x00568818;
constexpr Word kSelectionRecords = 0x0053a514;
constexpr Word kSelectionEnd = 0x0053c1d4;
constexpr Word kGapBits = 0x0056a23c;

constexpr Word rotate_left(Word v) { return (v << 1) | (v >> 31); }

}  // namespace

// 0x004af660: the first word is mixed into the high hash (rotated left, times 0x209), the second into the low one
void BitKit::cheat_hash(Word first, Word second) {
    const Word mixed = rotate_left(global_word(m_, kHashA) ^ first) * 0x209;
    set_global_word(m_, kHashA, mixed);
    const Word low = (mixed >> 8) ^ global_word(m_, kHashB) ^ second;
    set_global_word(m_, kHashB, rotate_left(low));
}

// 0x004af6b0 (cdecl, one argument): 0 when no code matched (or a net game / the demo mode forbids them), 1 when one did
Word BitKit::cheat_code(Word flag) {
    if ((engine_value(kNetMode) & 0xff) != 0 || global_word(m_, kGameMode) == 7) {
        return 0;
    }
    Word first;
    Word second;
    if (global_byte(m_, 0x0056b099) != 0) {
        first = 0x01234567;
        second = 0x34859f3a;
    } else if (global_byte(m_, 0x0056b089) != 0) {
        first = 0xab432901;
        second = 0x88d3a109;
    } else if (global_byte(m_, 0x0056b0a9) != 0) {
        first = 0x776643d1;
        second = 0x0901d3e8;
    } else if (global_byte(m_, 0x0056b079) != 0) {
        first = 0x93fe1682;
        second = 0x92551072;
    } else if (global_byte(m_, 0x0056aff9) != 0) {
        first = 0x31415926;
        second = 0x7720de42;
    } else if (global_byte(m_, 0x0056b019) != 0) {
        first = 0xdeadbeef;
        second = 0xfe3010f3;
    } else if (global_byte(m_, 0x0056b029) != 0) {
        first = 0xb87610db;
        second = 0xde098401;
    } else if (global_byte(m_, 0x0056b009) != 0) {
        first = 0x03185332;
        second = 0x80fe4187;
    } else {
        return 0;
    }
    cheat_hash(first, second);
    const Word a = global_word(m_, kHashA);
    const auto toggle = [&](Word id, Word on) { engine(kToggleCheat, {id, on}); };
    const auto flip = [&](Word address) { set_word_at(m_, address, word_at(m_, address) ^ 1); };
    const auto mode = [&](Word value) { set_global_word(m_, kModeWord, global_word(m_, kModeWord) != value ? value : 0); };
    const auto fatten = [&](Word amount) {
        engine(kFatten, {global_word(m_, kPlayer0), amount, 0, 0});
        if (global_word(m_, kPlayer1) != 0) {
            engine(kFatten, {global_word(m_, kPlayer1), amount, 0, 0});
        }
    };
    const auto second_is = [&](Word value) { return global_word(m_, kHashB) == value; };
    switch (a) {
    case 0x6dc02d1e: toggle(10, 0); return 1;
    case 0x33523575:
        if (!second_is(0x34ab2931) || flag == 0) {
            return 1;
        }
        fatten(0x96);
        return 1;
    case 0x2ab86343: {
        if (!second_is(0xe94e5fa1) || global_word(m_, kGameMode) != 1) {
            return 1;
        }
        const Word slot = engine_value(kSelectSlot, {0});
        engine(kClearGame, {slot});
        return 1;
    }
    case 0x17b941b1: mode(7); return 1;
    case 0x0517f426: {
        const Word character = engine_value(kCurrentCharacter);
        const Word record = engine_value(kFindTrickConfig, {character});
        set_word_at(m_, record + 4, word_at(m_, record + 4) + 5000);
        set_word_at(m_, record + 8, word_at(m_, record + 8) + 5000);
        return 1;
    }
    case 0x0e2b0dc3: toggle(0xd, 0); return 1;
    case 0x209fb3ad: toggle(9, 0); return 1;
    case 0x2b85a8df: toggle(0xf, 0); return 1;
    case 0x317f1f9d:
        if (!second_is(0x553dc48b) || flag != 0) {
            return 1;
        }
        for (Word record = kSelectionRecords; record < kSelectionEnd; record += 0x170) {
            set_word_at(m_, record - 4, 1);
            if ((word_at(m_, record) & 8) != 0) {
                set_word_at(m_, record, word_at(m_, record) & 0xfffffff7);
            }
            if ((word_at(m_, record) & 0x10) != 0) {
                set_word_at(m_, record, word_at(m_, record) & 0xffffffef);
            }
        }
        return 1;
    case 0x31e3487b: toggle(0xc, 0); return 1;
    case 0x5df28f1e:
        if (!second_is(0x20c49738)) {
            return 1;
        }
        flip(0x0056b7f0);
        return 1;
    case 0x5ee13e0d: toggle(5, 0); return 1;
    case 0x6083c9b6:
        if (flag == 0) {
            return 1;
        }
        engine(kSwitchOnEndLevel);
        set_global_word(m_, kLatch, 1);
        set_global_word(m_, kSecondaryState, 1);
        set_word_at(m_, global_word(m_, kPlayer0) + 0x16c, 100000);
        set_word_at(m_, global_word(m_, kPlayer0) + 0x2858, 0);
        return 1;
    case 0x5b445bb0: mode(5); return 1;
    case 0x46f27698: toggle(6, 0); return 1;
    case 0x39bedf78:
        if (!second_is(0xacae70a1)) {
            return 1;
        }
        toggle(3, flag != 0);
        return 1;
    case 0x45a930ef: flip(0x00533fa0); return 1;
    case 0x522a8b6c: mode(8); return 1;
    case 0xa9411ee9: toggle(7, 0); return 1;
    case 0x943ed118:
        if (second_is(0xddba0d98)) {
            flip(0x0056b7ec);
            return 1;
        }
        return 1;
    case 0x87ae8f70: toggle(0xe, 0); return 1;
    case 0x8112bf6a:
        if (!second_is(0x1a348ccc)) {
            return 1;
        }
        flip(0x0056a8e4);
        return 1;
    case 0x70153315: flip(0x0056a86c); return 1;
    case 0x763a0dae: mode(0xd); return 1;
    case 0x9b70c632: flip(0x0056a904); return 1;
    case 0xa3e2dce2:
        if (second_is(0x7a6115ce)) {
            toggle(0x10, 0);
            return 1;
        }
        return 1;
    case 0xa8a606ad:
        if (second_is(0x8786ef73)) {
            engine(kClearGameWithEveryone);
            return 1;
        }
        return 1;
    case 0xc0a8f966:
        if (!second_is(0xdfa4b839) || flag == 0) {
            return 1;
        }
        fatten(0x43);
        return 1;
    case 0xc0998c91: toggle(4, flag != 0); return 1;
    case 0xab93b12c: mode(9); return 1;
    case 0xb130bfc4:
        if (!second_is(0x7cff03f2)) {
            return 1;
        }
        for (Word k = 0; k < 0x21; ++k) {
            set_word_at(m_, kGapBits + 4 * k, 0xffffffff);
        }
        return 1;
    case 0xd7dda9e0:
        if (second_is(0x8f37160d)) {
            flip(0x0056b760);
        }
        return 1;
    case 0xdebf473a: toggle(0xb, flag != 0); return 1;
    case 0xf4301a14: mode(6); return 1;
    case 0xfb29f028: flip(0x0056a948); return 1;
    default: return 0;
    }
}

}  // namespace opentony::runtime::bitkit
