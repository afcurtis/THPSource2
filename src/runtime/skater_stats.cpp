#include "skater_views.hpp"

// Phase 4.15a: the stats of a skater (physics.cpp, 0x0048f090..0x0048f590): the deck records, the stat levels a character starts with and the stat value the physics reads.

namespace opentony::runtime::skaterkit {

using namespace views;

namespace {

using Word = std::uint32_t;

inline std::int32_t sext8(Word v) { return static_cast<std::int8_t>(static_cast<std::uint8_t>(v)); }

}  // namespace

// 0x0048f090: the record of a deck. A set (one per character) has eight decks; a set with bit 1 of its flag byte also may use the twelve extra decks that follow all the sets
Word SkaterKit::deck_record(Word set, Word deck) {
    constexpr Word kMsgSet = 0x0052cf24;
    constexpr Word kMsgDeck = 0x0052cf10;
    check(s(set) >= 0 && s(set) < 0x14, kMsgSet);
    const Word base = set * 0x170;
    if ((byte_at(m_, base + 0x0053a514) & 2) == 0) {
        check(s(deck) >= 0 && s(deck) < 8, kMsgDeck);
        return globals::kSetRecords + base + (deck << 5);
    }
    check(s(deck) >= 0 && s(deck) < 0x14, kMsgDeck);
    if (s(deck) < 8) {
        return globals::kSetRecords + base + (deck << 5);
    }
    return globals::kExtraDecks + ((deck - 8) << 5);
}

// 0x0048f1d0: the skater's stat levels (bytes at +0x29b4 / 5 / 6 / 7) start at 1 and are raised to the best of the decks its character owns (the record's levels at +0xc / +0x10 / +0x14); the points those
// levels give go into the shorts at +0x29f8..+0x2a02 (10 for every level above the first, spread over the stats' slots by a fixed pattern)
void SkaterKit::init_stats(Word self) {
    constexpr Word kMsgDecks = 0x00536a84;
    constexpr Word kLogStats = 0x00536a4c;
    const Word player = static_cast<Word>(skater_at(m_, self).player_index());
    skater_at(m_, self).set_field_29b6(1);
    skater_at(m_, self).set_field_29b5(1);
    skater_at(m_, self).set_field_29b4(1);
    skater_at(m_, self).set_field_29b7(static_cast<std::uint8_t>(byte_at(m_, globals::kPlayerChoice + player)));
    Word decks = 0;
    for (Word k = 0; k < 0x14; ++k) {
        const Word record = engine_value(kFindTrickConfig, {static_cast<Word>(skater_at(m_, self).player_index())});
        if (((1u << (k & 31)) & word_at(m_, record + 0x34)) != 0) {
            ++decks;
        }
    }
    check(decks != 0, kMsgDecks);
    const Word deck = deck_record(static_cast<Word>(skater_at(m_, self).player_index()), decks - 1);
    if (sext8(skater_at(m_, self).field_29b4()) < int_at(m_, deck + 0xc)) {
        skater_at(m_, self).set_field_29b4(static_cast<std::uint8_t>(word_at(m_, deck + 0xc)));
    }
    if (sext8(skater_at(m_, self).field_29b5()) < int_at(m_, deck + 0x10)) {
        skater_at(m_, self).set_field_29b5(static_cast<std::uint8_t>(word_at(m_, deck + 0x10)));
    }
    if (sext8(skater_at(m_, self).field_29b6()) < int_at(m_, deck + 0x14)) {
        skater_at(m_, self).set_field_29b6(static_cast<std::uint8_t>(word_at(m_, deck + 0x14)));
    }
    log_line(kLogStats);
    for (const Word field : {0x29f8u, 0x29fcu, 0x2a00u, 0x2a04u, 0x2a08u}) {
        set_word_at(m_, self + field, 0);
    }
    const auto add10 = [this, self](Word field) { set_half_at(m_, self + field, half_at(m_, self + field) + 10); };
    std::int32_t level = sext8(skater_at(m_, self).field_29b5());
    if (level - 1 > 0) {
        add10(0x29fe);
    }
    if (level - 2 > 0) {
        add10(0x29f8);
    }
    if (level - 3 > 0) {
        add10(0x29fe);
        if (level - 4 > 0) {
            add10(0x29f8);
        }
    }
    level = sext8(skater_at(m_, self).field_29b4());
    if (level - 1 > 0) {
        add10(0x29fc);
    }
    if (level - 2 > 0) {
        add10(0x29fa);
    }
    if (level - 3 > 0) {
        add10(0x29fc);
        if (level - 4 > 0) {
            add10(0x29fa);
        }
    }
    const std::int32_t third = sext8(skater_at(m_, self).field_29b6());
    if (third > 0) {
        skater_at(m_, self).set_field_2a02(static_cast<std::uint16_t>(u((third * 10 - 10) / 2)));
    }
}

// 0x0048f3a0: stat `selector` (0..10) of the skater, in tenths of a level (10 per level). One player: the character's base level times ten plus the skater's bonus; more: the level the multi-player table gives
// plus the player's boost (never under 30); a level override replaces both. The cheats and power-ups (bits above 11 of +0x2f70) add a share, the stat's limit caps it, a skater riding the wrong way loses a
// share of it through stat 6 (balance), the bail lock adds a fixed amount up to its limit, stat 6 itself is capped at 100, and the tag mode lets the tag layer change the value. Cheat 12 makes stat 4 250.
std::int32_t SkaterKit::profile_value(Word self, Word selector) {
    constexpr Word kMsgStat = 0x00536a90;
    check(s(selector) >= 0 && s(selector) <= 10, kMsgStat);
    const Word record = engine_value(kFindTrickConfig, {skater_at(m_, self).skater_resource().address});
    std::int32_t value;
    if (global_word(m_, globals::kNumberOfPlayers) == 1) {
        value = sext8(byte_at(m_, record + 0x38 + selector)) * 10 + short_at(m_, self + 0x29f8 + 2 * selector);
    } else {
        const Word level = byte_at(m_, globals::kStatLevels + selector + skater_at(m_, self).skater_resource().address * 0x170);
        value = s((level + word_at(m_, globals::kPlayerBoost + 4 * static_cast<Word>(skater_at(m_, self).player_index())) + 2) * 10u);
        if (value < 0x1e) {
            value = 0x1e;
        }
    }
    if (global_word(m_, globals::kStatOverride) != 0) {
        value = global_int(m_, globals::kStatOverride) * 10;
    }
    if ((skater_at(m_, self).field_2f70() & 0xfffff000u) != 0 || (engine_value(kGetCheat, {6}) & 0xff) != 0) {
        value += s(word_at(m_, globals::kStatBonusScale + 4 * selector) * 30u) / 100;      // (32-bit products wrap)
    }
    if (value > int_at(m_, globals::kStatLimit + 4 * selector)) {
        value = int_at(m_, globals::kStatLimit + 4 * selector);
    }
    if (selector == 6) {
        if (value > 100 || global_word(m_, globals::kBailLocked) != 0) {
            value = 100;
        }
    } else {
        if (((word_at(m_, self + skater::kFlags) >> 1) & 1) != skater_at(m_, self).selectable()) {
            const std::int32_t balance = profile_value(self, 6);
            const std::int32_t reduced = s(u(balance + 100) * u(value) * 50u) / 10000;
            value += s(word_at(m_, globals::kStatFacingScale + 4 * selector) * u(reduced - value)) / 100;
        }
        if (global_word(m_, globals::kBailLocked) != 0) {
            value += int_at(m_, globals::kStatLockedBonus + 4 * selector);
            if (value >= int_at(m_, globals::kStatLockedLimit + 4 * selector)) {
                value = int_at(m_, globals::kStatLockedLimit + 4 * selector);
            }
        }
    }
    const Word mode = global_word(m_, globals::kGameMode);
    set_byte_at(m_, globals::kStatFlags + selector, 10);
    if (mode == 9) {
        value = s(engine_value(kTagStatModifier, {self, selector, u(value)}));
    }
    if ((engine_value(kGetCheat, {0xc}) & 0xff) != 0 && selector == 4) {
        return 0xfa;
    }
    return value;
}

}  // namespace opentony::runtime::skaterkit
