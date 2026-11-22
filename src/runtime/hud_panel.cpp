#include "auto_views.hpp"
#include "hud_panel.hpp"
#include "frame_pacing.hpp"

#include <string>
#include <vector>

namespace opentony::runtime::hud {

using namespace opentony::runtime::autoviews;

namespace {

std::int32_t wrap(std::uint32_t v) { return static_cast<std::int32_t>(v); }
std::uint32_t uns(std::int32_t v) { return static_cast<std::uint32_t>(v); }
// 32-bit signed multiply, low half (imul r32, r/m32).
std::int32_t mul32(std::int32_t a, std::int32_t b) { return wrap(uns(a) * uns(b)); }
// the multiply-by-reciprocal division the compiler emits for `x / 100` and `x / 10000` truncates toward zero
std::int32_t div_trunc(std::int32_t a, std::int32_t b) { return a / b; }

}  // namespace

// ---- the score stack ---------------------------------------------------------------------------------------

std::int32_t Panel::sum_entry_values(std::uint32_t panel) {
    std::int32_t total = 0;
    const std::int32_t count = get(panel + kStackCount);
    for (std::int32_t i = 0; i < count; ++i) {
        total = wrap(uns(total) + uns(get(entry(panel, i) + kEntValue)));
    }
    return total;
}

std::int32_t Panel::chain_value(std::uint32_t panel, std::int32_t index) {
    // back to the entry that begins the chain: the one after the nearest earlier entry of kind 1
    while (index > 0 && get(entry(panel, index - 1) + kEntKind) != 1) {
        --index;
    }
    const std::int32_t count = get(panel + kStackCount);
    std::int32_t acc = 0;
    while (index < count) {
        if (get(entry(panel, index) + kEntKind) == 1) {
            break;
        }
        acc = wrap(uns(acc) + uns(get(entry(panel, index) + kEntValue)));
        ++index;
    }
    if (index < count) {
        acc = wrap(uns(acc) + uns(get(entry(panel, index) + kEntValue)));
    }
    return acc;
}

std::int32_t Panel::chain_value_if_first(std::uint32_t panel, std::int32_t index) {
    if (get(entry(panel, index) + kEntKind) == 2) {
        return 0;
    }
    std::int32_t j = index;
    while (j > 0 && get(entry(panel, j - 1) + kEntKind) != 1) {
        --j;
    }
    if (j != index) {
        return 0;
    }
    return chain_value(panel, j);
}

std::int32_t Panel::compute_stack_sum(std::uint32_t panel) {
    std::int32_t total = 0;
    const std::int32_t count = get(panel + kStackCount);
    for (std::int32_t i = 0; i < count; ++i) {
        const std::uint32_t e = entry(panel, i);
        std::int32_t times = get(e + kEntHistory);
        if (get(e + kEntFlag) == 0) {
            times = 1;
        }
        check(times >= 0, kMsgTrickNotFound);
        times -= 1;
        if (times > 4) {
            times = 4;
        }
        const std::int32_t level = chain_value(panel, i);
        std::int32_t mult;
        if (level < 6) {
            mult = get(kChainMultTable + uns(level) * 4);
        } else {
            mult = wrap(uns(get(kChainMultBase)) + uns(level) * 4 - 0x14);
        }
        std::int32_t part = mul32(get(e + kEntPoints), wrap(uns(mult) + 2));
        part /= 2;
        part = mul32(part, get(kScoreMultTable + uns(times) * 4));
        total = wrap(uns(total) + uns(div_trunc(part, 100)));
    }
    const std::uint32_t skater = word_at(m_, panel + kOwner);
    if (get(skater + kSkLandQuality) != 0) {
        if (get(skater + kSkPerfect) != 0) {
            total = div_trunc(mul32(total, 150), 100);
        }
        if (get(skater + kSkSloppy) != 0) {
            total = div_trunc(mul32(total, 75), 100);
        }
    }
    return total;
}

std::int32_t Panel::compute_landed_value(std::uint32_t panel) {
    const std::int32_t count = get(panel + kStackCount);
    std::int32_t mult;
    if (count < 0xf) {
        mult = get(kStackMultTable + uns(count) * 4);
    } else {
        mult = wrap(uns(count) + uns(get(kStackMultBase)) - 0xe);
    }
    return mul32(compute_stack_sum(panel), mult) / 2;
}

void Panel::init_entry(std::uint32_t e, std::uint32_t node, std::uint32_t name, std::uint32_t points) {
    set_word_at(m_, e + kEntNode, node);
    set_word_at(m_, e + kEntPoints, points);
    set_word_at(m_, e + kEntName, name);
    set_word_at(m_, e + kEntCountdown, 0x12);
    set_word_at(m_, e + kEntValue, 0);
    set_word_at(m_, e + kEntKind, 0);
}

std::int32_t Panel::landing_quality(std::uint32_t skater) {
    std::int32_t quality = get(skater + kSkLandQuality);
    if (get(skater + 0x2dd4) == 0 && get(skater + 0x2c84) == 0) {
        const std::int32_t drift = get(skater + 0x3068);
        const std::int32_t magnitude = drift < 0 ? wrap(0u - uns(drift)) : drift;
        if (magnitude > 0x5dc) {
            quality += 1;
            put(skater + 0x3068, 0);
        }
    }
    return quality;
}

void Panel::push_entry(std::uint32_t panel, std::uint32_t name, std::int32_t points, std::int32_t kind, std::uint32_t flag,
                       std::uint32_t extra) {
    if (get(panel + kStackCount) >= static_cast<std::int32_t>(kStackSlots)) {
        // the stack is full: drop the oldest entry
        for (std::uint32_t i = 0; i + 1 < kStackSlots; ++i) {
            m_.copy(entry(panel, static_cast<std::int32_t>(i)), entry(panel, static_cast<std::int32_t>(i) + 1), kEntrySize);
        }
        put(panel + kStackCount, get(panel + kStackCount) - 1);
    }
    check(get(panel + kStackCount) < static_cast<std::int32_t>(kStackSlots), kMsgOutOfSlots);
    if (get(kTenfoldFlag) != 0) {
        points = mul32(points, 10);
    }
    check(points > 0, kMsgZeroPoints);
    const std::uint32_t skater = word_at(m_, panel + kOwner);
    if (get(skater + 0x314c) != 0) {
        points = div_trunc(mul32(points, 120), 100);
    }
    std::int32_t count = get(panel + kStackCount);
    if (count == 0) {
        const std::int32_t combo = get(panel + kComboTotal);
        const std::int32_t score = get(panel + kScore);
        put(panel + kComboTotal, 0);
        put(panel + kScore, wrap(uns(score) + uns(combo)));
        put(panel + kLandedAux, 0);
        put(panel + kStackTimer, 0);
    }
    init_entry(entry(panel, count), 0, name, uns(points));
    count = get(panel + kStackCount);
    if (count > 0 && get(entry(panel, count - 1) + kEntKind) == 0) {
        // the previous entry was never closed: close it with the landing quality
        const std::int32_t quality = landing_quality(skater);
        put(entry(panel, count - 1) + kEntValue, quality);
        put(skater + kSkLandQuality, 0);
    } else if (kind != 2 && kind == 1) {
        put(skater + kSkLandQuality, landing_quality(skater));
        count = get(panel + kStackCount);
        put(entry(panel, count) + kEntValue, get(skater + kSkLandQuality));
    }
    count = get(panel + kStackCount);
    const bool switched = (((word_at(m_, skater + 0xd8) >> 1) & 1) ^ byte_at(m_, skater + 0x163)) != 0;
    put(entry(panel, count) + kEntSwitch, switched ? 1 : 0);
    count = get(panel + kStackCount);
    const std::uint32_t e = entry(panel, count);
    const std::int32_t key = wrap(uns(get(e + kEntSwitch)) + uns(get(e + kEntValue)) * 2);
    s_.note(kSvcHistoryAdd, {skater, word_at(m_, e + kEntName), uns(key)});
    put(e + kEntHistory, s_.value(kSvcHistoryAdd));
    put(entry(panel, get(panel + kStackCount)) + kEntKind, kind);
    put(entry(panel, get(panel + kStackCount)) + kEntFlag, wrap(flag));
    put(entry(panel, get(panel + kStackCount)) + kEntExtra, wrap(extra));
    put(panel + kStackCount, get(panel + kStackCount) + 1);
    show_stack_text(panel);
}

void Panel::adjust_current_entry(std::uint32_t skater, std::int32_t delta) {
    if (get(skater + kSkStackCountMirror) > 0) {
        check(get(skater + kSkStackCountMirror) > 0, kMsgTweakNothing);
        const std::uint32_t at = skater + kSkStackBase + uns(get(skater + kSkStackCountMirror)) * kEntrySize;
        put(at, wrap(uns(get(at)) + uns(delta)));
        refresh_text(skater + kPanelInSkater);
    }
}

void Panel::expire_entries(std::uint32_t panel) {
    const std::int32_t count = get(panel + kStackCount);
    for (std::int32_t i = 0; i < get(panel + kStackCount) && i < count; ++i) {
        const std::uint32_t e = entry(panel, i);
        const std::uint32_t node = word_at(m_, e + kEntNode);
        if (static_cast<std::int32_t>(half_at(m_, node + 6)) >= get(e + kEntCountdown)) {
            put(e + kEntCountdown, 0);
        }
    }
    put(panel + kStackClock, get(kSimulationTime));
}

void Panel::commit_landed_stack(std::uint32_t panel) {
    put(panel + kZoomPending, 0);
    put(panel + kStackShown, 0);
    if (get(panel + kStackCount) > 0) {
        const std::uint32_t skater = word_at(m_, panel + kOwner);
        set_half_at(m_, panel + kTrickCount, half_at(m_, panel + kTrickCount) + static_cast<std::uint32_t>(get(panel + kStackCount)));
        const std::int32_t value = compute_landed_value(panel);
        put(panel + kLandedValue, value);
        put(panel + kComboTotal, wrap(uns(get(panel + kComboTotal)) + uns(value)));
        if (uns(value) > word_at(m_, panel + kBestScore)) {
            set_word_at(m_, panel + kBestScore, uns(value));
            set_half_at(m_, panel + kBestStackCount, half_at(m_, panel + kStackCount));
            for (std::uint32_t i = 0; i < 3; ++i) {
                const std::string text = m_.cstring(panel + kBestNames + i * kBestNameStride + 0x26e0, 0x100);
                std::vector<std::uint8_t> bytes(text.begin(), text.end());
                bytes.push_back(0);
                m_.write(panel + kBestNames + i * kBestNameStride, bytes);
            }
        }
        const std::uint32_t gap = word_at(m_, skater + kSkGapRecord);
        if (gap != 0) {
            s_.note(kSvcPrintfLog, {kFmtLanded});
            // the level's goal table: the first goal of kind 5 (a trick gap) names the trick to look for
            std::uint32_t goal = kLevelGoalTable + uns(get(kCurrentLevel)) * kLevelGoalStride;
            std::int32_t slot = 0;
            while (slot < 10) {
                if (get(goal) == 5) {
                    break;
                }
                goal += 0x18;
                ++slot;
            }
            check(slot < 10, kMsgGapNoMatch);
            const std::uint32_t trick_name = word_at(m_, goal + 8);
            check(trick_name != 0, kMsgNullGapName);
            for (std::int32_t i = 0; i < get(panel + kStackCount); ++i) {
                s_.note(kSvcFindNoCase, {word_at(m_, entry(panel, i) + kEntName), trick_name}, m_.cstring(word_at(m_, entry(panel, i) + kEntName)));
                if (s_.value(kSvcFindNoCase) != 0) {
                    s_.note(kSvcAwardTrickGap, {word_at(m_, skater + kSkGapRecord)});
                    break;
                }
            }
        }
        set_word_at(m_, skater + kSkGapRecord, 0);
        if (word_at(m_, skater + kSkGoalGapRecord) != 0) {
            s_.note(kSvcAwardGoalGap, {word_at(m_, skater + kSkGoalGapRecord)});
            set_word_at(m_, skater + kSkGoalGapRecord, 0);
        }
        put(panel + kStackCount, 0);
        put(panel + kStackTimer, 0x28);
        if (get(kCareerFlag) == 0) {
            put(panel + kStackTimer, 0);
        }
        const std::int32_t mode = get(kGameMode);
        if ((mode == 2 || mode == 4) && get(panel + kLandedValue) != 0) {
            put(panel + kScore, 0);
        }
    }
    s_.note(kSvcHighlightEnd, {word_at(m_, panel + kOwner), word_at(m_, panel + kComboTotal)});
}

void Panel::rollback_stack(std::uint32_t panel) {
    set_half_at(m_, panel + kBailCount, half_at(m_, panel + kBailCount) + 1);
    const std::int32_t value = compute_landed_value(panel);
    s_.note(kSvcHighlightEnd, {word_at(m_, panel + kOwner), uns(wrap(0u - uns(value)))});
    if (get(panel + kStackShown) != 0) {
        put(panel + kStackShown, 0);
        s_.note(kSvcEffectFinished, {panel + 0x66c});
        if (s_.value(kSvcEffectFinished) == 0) {
            s_.note(kSvcZoomStart, {panel + 0x66c});
            put(panel + kZoomPending, 1);
        }
    }
    const std::int32_t mode = get(kGameMode);
    if (mode != 2 && mode != 4) {
        const std::int32_t count = get(panel + kStackCount);
        for (std::int32_t i = 0; i < count; ++i) {
            const std::uint32_t e = entry(panel, i);
            const std::int32_t key = wrap(uns(get(e + kEntSwitch)) + uns(get(e + kEntValue)) * 2);
            s_.note(kSvcHistoryRemove, {word_at(m_, panel + kOwner), word_at(m_, e + kEntName), uns(key)});
        }
    }
    put(panel + kStackCount, 0);
}

// ---- the skater side ---------------------------------------------------------------------------------------

void Panel::advance_point_timer(std::uint32_t skater, std::int32_t share) {
    const std::int32_t step = get(kPointDelta);
    put(skater + kSkPointTimer, wrap(uns(get(skater + kSkPointTimer)) + uns(step)));
    const std::int32_t limit = get(skater + kSkPointLimit);
    if (limit != 0) {
        s_.note(kSvcProfileValue, {skater, 8});
        const std::int32_t profile = s_.value(kSvcProfileValue);
        const std::int32_t allowed = div_trunc(mul32(wrap(uns(profile) * 0x71 + 0x7d0), limit), 10000);
        if ((get(skater + kSkPointTimer) >> 12) > allowed) {
            put(skater + 0x50, 0);
            s_.note(kSvcBail, {skater, uns(static_cast<std::int32_t>(short_at(m_, skater + 0x29f0))), 0});
            return;
        }
    }
    adjust_current_entry(skater, framepacing::point_credit(skater, mul32(step, share)));   // retail's (step x share) >> 12; at 60 fps with the flag the half points are carried
}

void Panel::apply_gap(std::uint32_t skater, std::uint32_t name, std::int32_t value, std::int32_t kind, std::uint32_t extra) {
    if (get(skater + 0x3064) == 0) {
        return;
    }
    if (get(kGameMode) == 7) {
        put(skater + 0x2cc8, 0);
    }
    std::uint32_t positive = 1;
    if (value < 0) {
        value = wrap(0u - uns(value));
        positive = 0;
    }
    if (get(skater + 0x2dd4) != 0) {
        // a trick left pending by a script: drop its script flags (as `land_pending` does, but the state stays set)
        put(skater + 0x2f24, 0xfa0);
        put(skater + 0x29e0, 0);
        for (std::uint32_t i = 0; i < 0x20; ++i) {
            set_byte_at(m_, skater + 0x2a14 + 8 * i, 0);
        }
    }
    push_entry(skater + kPanelInSkater, name, value, kind, positive, extra);
}

void Panel::take_off(std::uint32_t skater) {
    if (get(skater + kSkState) == 5 || word_at(m_, skater + 0x2cc8) == 0) {
        return;
    }
    const std::int32_t state = get(skater + kSkState);
    put(skater + 0x3074, 0);
    put(skater + kSkPerfect, 0);
    put(skater + kSkSloppy, 0);
    put(skater + 0x3064, 1);
    put(skater + 0x2cbc, 0);
    put(skater + 0x2d90, 0xf);
    if (state != 2) {
        s_.note(kSvcResetTrickBuffers, {skater});
    }
    put(skater + 0x2c80, 0);
    put(skater + 0x2c84, 0);
    reset_trick_state(skater);
    const std::int32_t mode = get(kGameMode);
    if (mode == 2 || mode == 4) {
        s_.note(kSvcHistoryReset, {skater});
    }
}

// 0x0048f800 (this = skater; object): remember a trick object the skater touched this trick (a ring of 40).
void Panel::register_trick_object(std::uint32_t skater, std::uint32_t object) {
    check(object != 0, 0x00536aa8);
    const std::int32_t count = get(skater + 0x2d88);
    const std::int32_t known = count > 0x28 ? 0x28 : count;
    for (std::int32_t i = 0; i < known; ++i) {
        if (word_at(m_, skater + 0x2ce8 + 4 * uns(i)) == object) {
            return;
        }
    }
    set_word_at(m_, skater + 0x2ce8 + 4 * uns(count % 0x28), object);
    put(skater + 0x2d88, get(skater + 0x2d88) + 1);
}

// 0x0048f7c0 (this = skater): a trick that was left pending ends: clear the script flags and land.
void Panel::land_pending(std::uint32_t skater) {
    if (get(skater + 0x2dd4) == 0) {
        return;
    }
    put(skater + 0x29e0, 0);
    for (std::uint32_t i = 0; i < 0x20; ++i) {
        set_byte_at(m_, skater + 0x2a14 + 8 * i, 0);
    }
    put(skater + 0x2dd4, 0);
    land(skater);
}

void Panel::land(std::uint32_t skater) {
    const std::uint32_t panel = skater + kPanelInSkater;
    put(skater + 0x2c88, 0);
    put(skater + 0x3048, get(skater + 0x3048) + 1);
    put(skater + kSkLandQuality, landing_quality(skater));
    refresh_text(panel);
    const std::int32_t values = sum_entry_values(panel);
    if (values == 0 || (get(skater + kSkPrevState) == 2 && sum_entry_values(panel) == 1)) {
        put(skater + kSkPerfect, 0);
        put(skater + kSkSloppy, 0);
        refresh_text(panel);
    }
    if (get(skater + kSkPerfect) != 0) {
        s_.note(kSvcSfx, {0xbc, 0x2000, 0});
    }
    if (get(skater + kSkSloppy) != 0) {
        s_.note(kSvcSfx, {0xbd, 0x2000, 0});
    }
    const std::int32_t count = get(skater + kSkStackCountMirror);
    const bool open_entry = count == 0 || (count > 0 && get(skater + 0x900 + uns(count) * kEntrySize) != 0);
    if (open_entry) {
        const std::int32_t quality = get(skater + kSkLandQuality);
        if (quality != 0 && (get(skater + kSkPrevState) != 2 || quality > 1)) {
            s_.note(kSvcApplyGap, {skater, kNameOllie, uns(mul32(quality, 50)), 0, 0xffffffffu}, m_.cstring(kNameOllie));
            put(skater + 0x2c70, 1);
            put(skater + 0x31fc, 0);
        }
    }
    commit_for_skater(skater);
    put(skater + 0x2c84, 0);
    put(skater + 0x2c80, 0);
    if (get(skater + 0x2c70) != 0) {
        std::int32_t objects = get(skater + 0x2d88);
        if (objects != 0) {
            if (objects > 0x28) {
                objects = 0x28;
            }
            put(0x0056db84, 1);
            put(0x0056db80, 1);
            for (std::int32_t i = 0; i < objects; ++i) {
                s_.note(kSvcActivateTrickObject, {word_at(m_, skater + 0x2ce8 + 4 * uns(i)), skater, word_at(m_, skater + kSkLandedLast)});
            }
            put(skater + 0x2d88, 0);
        }
        if (get(skater + 0x2c70) != 0 && get(kGameMode) == 7) {
            put(skater + 0x2cc8, 0);
        }
    }
    s_.note(kSvcLandedTotal, {word_at(m_, skater + kSkLandedTotal)});
    if (get(skater + 0x2c70) != 0) {
        const std::uint32_t meter = word_at(m_, skater + 0x2f70);
        if ((meter & 0xfffff000u) == 0) {
            const std::uint32_t grown = word_at(m_, skater + 0x2f6c) + (word_at(m_, skater + kSkLandedTotal) << 12);
            set_word_at(m_, skater + 0x2f6c, grown);
            if (static_cast<std::int32_t>(grown & 0xfffff000u) >= 0xbb8000) {
                set_word_at(m_, skater + 0x2f6c, 0);
                set_word_at(m_, skater + 0x2f70, 0x2ee000);
            }
        } else {
            const std::uint32_t grown = ((word_at(m_, skater + kSkLandedTotal) / 5) << 12) + meter;
            set_word_at(m_, skater + 0x2f70, grown);
            if (static_cast<std::int32_t>(grown & 0xfffff000u) > 0x2ee000) {
                set_word_at(m_, skater + 0x2f70, 0x2ee000);
            }
        }
    }
    put(skater + 0x2c70, 0);
    put(skater + 0x2c74, 0);
    put(skater + kSkLandQuality, 0);
    put(skater + 0x3068, 0);
    put(skater + 0x3064, 0);
}

void Panel::show_stack_text(std::uint32_t panel) {
    put(panel + kStackShown, 1);
    s_.note("text_effect_disable", {panel + 0x548});
    check(get(panel + kStackCount) != 0, 0x005364ec);
    if (get(panel + kStackCount) != 0) {
        s_.note("effect_init", {panel + 0x66c});
        put(panel + 0x688, wrap(panel + 0x66c));
        refresh_text(panel);
    }
}

// 0x0048ddc0: build the strings the HUD draws for the stack. The names are joined with " + " into at most three
// lines of text (panel + 0x26f4, 0x64 bytes each, filled from the last), each as wide as the font lets it be.
void Panel::refresh_text(std::uint32_t panel) {
    const auto cstr = [&](std::uint32_t at) { return m_.cstring(at, 0x200); };
    const auto store = [&](std::uint32_t at, const std::string& text) {
        std::vector<std::uint8_t> bytes(text.begin(), text.end());
        bytes.push_back(0);
        m_.write(at, bytes);
    };
    const auto width_of = [&](const std::string& text) {
        s_.note(kSvcTextWidth, {panel + 0x2b0}, text);
        return s_.value(kSvcTextWidth);
    };
    for (std::uint32_t i = 0; i < 3; ++i) {
        set_byte_at(m_, panel + kTextNames + i * kBestNameStride, 0);
    }
    const std::uint32_t skater = word_at(m_, panel + kOwner);
    const std::uint32_t viewport = static_cast<std::uint32_t>(get(kViewportMode));
    const std::uint32_t selector = half_at(m_, skater + kSkPlayerNum) + viewport * 2 - 2;
    std::int32_t limit;
    bool wide;
    switch (selector) {
        case 0: case 1: case 4: case 5:
            limit = 0xdc;
            wide = false;
            break;
        default:
            limit = 0x1e0;
            wide = true;
            break;
    }
    const std::int32_t scale = (get(kNumPlayers) > 1 && get(kGameMode) != 7) ? 0x940 : 0xbc0;
    std::int32_t count = get(panel + kStackCount);
    if (count == 0) {
        set_byte_at(m_, panel + kTextNames, 0);
        return;
    }
    // the newest entry is still open: give it the landing quality now
    if (get(entry(panel, count - 1) + kEntKind) == 0) {
        const std::int32_t quality = get(skater + kSkLandQuality);
        if (get(entry(panel, count - 1) + kEntValue) != quality) {
            bool assign = true;
            if (get(skater + kSkState) == 2 || get(skater + kSkPrevState) == 2) {
                assign = quality > 1 || sum_entry_values(panel) > 0;
            }
            if (assign) {
                put(entry(panel, get(panel + kStackCount) - 1) + kEntValue, get(skater + kSkLandQuality));
            }
        }
    }
    std::string suffix;
    if (get(skater + kSkPerfect) != 0) {
        suffix = "~" + cstr(global_word(m_, kPerfectText)) + "]";
    } else if (get(skater + kSkSloppy) != 0) {
        suffix = "|" + cstr(global_word(m_, kSloppyText)) + "]";
    }
    check(get(panel + kStackCount) != 0, kMsgRefreshEmpty);
    put(panel + 0x2e0, scale);
    count = get(panel + kStackCount);
    std::int32_t index = count - 1;
    std::int32_t slots_left = 2;
    std::uint32_t current = panel + kTextNames + 2 * kBestNameStride;    // the line being filled
    std::uint32_t previous = current;                                    // the line before it, when this one overflows
    const auto suffix_text = [&]() -> std::string { return get(panel + kStackCount) > 1 ? " +" : ""; };
    if (index >= 0) {
        while (true) {
            const std::uint32_t e = entry(panel, index);
            const std::string name = cstr(word_at(m_, e + kEntName));
            const std::string held = cstr(current);
            check(name.size() + held.size() < 0xc0, kMsgTempBuffer);
            std::string line;
            const std::int32_t first = chain_value_if_first(panel, index);
            if (first == 0) {
                line = name;
            } else {
                line = std::to_string(mul32(chain_value_if_first(panel, index), 0xb4)) + " " + name;
            }
            if (index != get(panel + kStackCount) - 1) {
                line += " + ";
                line += cstr(current);
            }
            if (!suffix.empty() && (slots_left == 0 || index == 0) && wide) {
                limit -= width_of(suffix + " ... + ");
            }
            if (width_of(line) > limit) {
                check(cstr(previous).size() < 100, kMsgTooLong);
                slots_left -= 1;
                previous -= kBestNameStride;
                if (slots_left < (wide ? 0 : 1)) {
                    store(current, "... + " + cstr(current));
                    slots_left = 0;
                    break;
                }
                current = previous;
                const std::string tail = suffix_text();
                if (chain_value_if_first(panel, index) == 0) {
                    store(current, name + tail);
                } else {
                    store(current, std::to_string(mul32(chain_value_if_first(panel, index), 0xb4)) + " " + name + tail);
                }
            } else {
                store(current, line);
            }
            --index;
            if (index < 0) {
                break;
            }
        }
    }
    if (wide) {
        if (suffix.empty()) {
            return;
        }
        store(panel + kTextNames + static_cast<std::uint32_t>(slots_left) * kBestNameStride, suffix + " " + cstr(current));
    } else {
        store(panel + 0x2690 + static_cast<std::uint32_t>(slots_left) * kBestNameStride, suffix);
    }
}

}  // namespace opentony::runtime::hud
