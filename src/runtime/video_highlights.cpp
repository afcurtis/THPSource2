#include "auto_views.hpp"
#include "video_kit.hpp"

#include <array>

// Phase 4.15b: the highlights of the replay recorder (video.cpp, 0x004cb580..0x004cb6e0). While a run is recorded the game remembers its five best-scoring stretches (a combo, a long grind): a start
// frame is noted when the score starts to count, the end when it is banked, and the five records of the skater's state keep the best by score. When the replay is saved the ranges are sorted by
// start, overlapping neighbours merged, and written to the header.

namespace opentony::runtime::videokit {

using namespace opentony::runtime::autoviews;

namespace {

constexpr std::uint32_t kOpen = 0x0056e778;                // a highlight is being recorded
constexpr std::uint32_t kStartFrame = 0x0056e448;
constexpr std::uint32_t kEndFrame = 0x0056e444;
constexpr std::uint32_t kStartHeight = 0x0056e330;
constexpr std::uint32_t kEndHeight = 0x0056e334;
constexpr std::uint32_t kSkaterHeight = 0x0c;              // the skater's y (Q12)
constexpr std::uint32_t kRecords = 5;
constexpr std::uint32_t kRecordBytes = 8;                  // {start short, end short, score dword}

}  // namespace

// 0x004cb580
void VideoKit::highlight_start(Word skater) {
    const Word block = state_of(skater);
    if (global_word(m_, kOpen) == 0) {
        set_global_word(m_, kStartFrame, word_at(m_, block + state::kFrames));
        set_global_word(m_, kOpen, 1);
        set_global_word(m_, kStartHeight, word_at(m_, skater + kSkaterHeight));
    }
}

// 0x004cb5c0: the score is the one handed in, or, when that is 0, the height gained (twice the climb, in whole units); a negative one marks a bail. The new record goes into the first slot whose score it beats
void VideoKit::highlight_end(Word skater, std::int32_t score) {
    const Word block = state_of(skater);
    set_global_word(m_, kEndFrame, word_at(m_, block + state::kFrames));
    set_global_word(m_, kOpen, 0);
    const bool negative = score < 0;
    set_global_word(m_, kEndHeight, word_at(m_, skater + kSkaterHeight));
    if (score < 0) {
        score = -score;
    }
    const std::int32_t climb = s(global_word(m_, kEndHeight) - global_word(m_, kStartHeight));
    const Word mask = u(climb >> 31);
    const std::int32_t distance = s((mask ^ u(climb)) - mask);
    if (score == 0) {
        score = s(u(distance) * 2u) >> 12;
    } else {
        score = score + (distance >> 13);
    }
    for (Word slot = 0; slot < kRecords; ++slot) {
        const Word record = block + state::kHighlights + slot * kRecordBytes;
        if (int_at(m_, record + 4) < score) {
            // the records below it move down one
            for (Word k = kRecords - 1; k > slot; --k) {
                const Word to = block + state::kHighlights + k * kRecordBytes;
                set_word_at(m_, to, word_at(m_, to - kRecordBytes));
                set_word_at(m_, to + 4, word_at(m_, to - kRecordBytes + 4));
            }
            std::int32_t start = s(global_word(m_, kStartFrame)) - 0xf;
            if (start < 0) {
                start = 0;
            }
            set_global_word(m_, kStartFrame, u(start));
            std::int32_t end = s(global_word(m_, kEndFrame));
            end += negative ? 0x1e : 8;
            if (0xe0f < end) {
                end = 0xe0f;
            }
            set_global_word(m_, kEndFrame, u(end));
            set_half_at(m_, record, u(start));
            set_half_at(m_, record + 2, u(end));
            set_word_at(m_, record + 4, u(score));
            return;
        }
    }
}

// 0x004cb6e0: ranges are clamped to the replay's length, then picked in order of start (a record with no score is empty); neighbours that touch or overlap are merged and the list is packed
void VideoKit::prepare_highlights() {
    struct Entry {
        Word range = 0;
        Word score = 0;
    };
    std::array<Entry, kRecords> sorted{};
    const Word frames = global_word(m_, globals::kNumFrames);
    for (Word pass = 0; pass < kRecords; ++pass) {
        std::int32_t chosen = -1;
        std::int32_t best = 0x7fffffff;
        for (Word i = 0; i < kRecords; ++i) {
            const Word record = globals::kHighlightRanges + i * kRecordBytes;
            if (frames <= u(short_at(m_, record + 2))) {
                set_half_at(m_, record + 2, frames - 1u);
            }
            if (word_at(m_, record + 4) != 0 && short_at(m_, record) < best) {
                chosen = static_cast<std::int32_t>(i);
                best = short_at(m_, record);
            }
        }
        if (chosen != -1) {
            const Word record = globals::kHighlightRanges + u(chosen) * kRecordBytes;
            sorted[pass].range = word_at(m_, record);
            sorted[pass].score = word_at(m_, record + 4);
            set_word_at(m_, record + 4, 0);
        }
    }
    const auto start_of = [&](Word i) { return static_cast<std::int16_t>(sorted[i].range & 0xffff); };
    const auto end_of = [&](Word i) { return static_cast<std::int16_t>(sorted[i].range >> 16); };
    for (Word k = 4; k-- > 0;) {
        // two neighbours that touch (within 15 frames) and are in order become one: the later end, the scores added
        if (start_of(k + 1) <= end_of(k) + 0xf && start_of(k) < start_of(k + 1)) {
            std::uint32_t end = static_cast<std::uint16_t>(end_of(k));
            if (end_of(k) < end_of(k + 1)) {
                end = static_cast<std::uint16_t>(end_of(k + 1));
            }
            sorted[k].range = (sorted[k].range & 0xffffu) | (end << 16);
            sorted[k].score += sorted[k + 1].score;
            sorted[k + 1].range = 0;
            sorted[k + 1].score = 0;
        }
    }
    Word next = globals::kHighlightRanges;
    for (Word i = 0; i < kRecords; ++i) {
        const Word record = globals::kHighlightRanges + i * kRecordBytes;
        set_word_at(m_, record, 0);
        set_word_at(m_, record + 4, 0);
        if (sorted[i].score != 0) {
            set_word_at(m_, next, sorted[i].range);
            set_word_at(m_, next + 4, sorted[i].score);
            next += kRecordBytes;
        }
    }
}

}  // namespace opentony::runtime::videokit
