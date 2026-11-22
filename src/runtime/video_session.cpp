#include "auto_views.hpp"
#include "video_kit.hpp"

// Phase 4.15b: starting and ending a recording or a playback (video.cpp, 0x004cb810..0x004cbf30, 0x004cdc20..0x004cde60). A replay file starts with a header: the frame count, the five highlight
// ranges, how many skaters, which skater and costume each is, the level and the game mode, the money mask, and a 0x80-byte block of custom-skater data per skater; the frames follow.

namespace opentony::runtime::videokit {

using namespace opentony::runtime::autoviews;

namespace {

using Word = std::uint32_t;

constexpr Word kHeaderHighlights = 0x04;           // ten dwords: the five {start short, end short, score dword} records
constexpr Word kHeaderSkaterCount = 0x2c;          // short
constexpr Word kHeaderSelection = 0x2e;            // byte per skater: the entry of the skater table
constexpr Word kHeaderCostume = 0x30;              // byte per skater
constexpr Word kHeaderLevel = 0x32;                // short
constexpr Word kHeaderGameMode = 0x34;             // short
constexpr Word kHeaderZero = 0x36;                 // short
constexpr Word kHeaderCompetition = 0x38;          // byte
constexpr Word kHeaderCareer = 0x39;               // byte
constexpr Word kHeaderMoneyMaskLow = 0x3c;         // dword
constexpr Word kHeaderMoneyMaskHigh = 0x40;        // dword
constexpr Word kHeaderPlayerWordA = 0x44;          // dword per skater
constexpr Word kHeaderPlayerWordB = 0x4c;          // dword per skater
constexpr Word kHeaderCustomInfo = 0x80;           // 0x80 bytes of custom-skater data, 0x100 apart
constexpr Word kCustomInfoStride = 0x100;
constexpr Word kCustomInfoBytes = 0x80;

constexpr Word kSelectionOfSkater = 0x2cc0;        // skater: the entry of the skater table it plays
constexpr Word kPlayerNumber = 0x2cc4;             // skater: 0 or 1
constexpr Word kSkaterTable = 0x0053a514;          // SkaterSelectionRecords + 4: 0x170 bytes per entry, bit 1 of the first byte marks a custom skater
constexpr Word kSkaterRecordBytes = 0x170;
constexpr Word kCustomTable = 0x0056a488;          // the custom skaters' data, 0x80 bytes each
constexpr Word kCostumeA = 0x0055fb36;             // byte per skater
constexpr Word kCostumeB = 0x0055fb38;             // byte per skater
constexpr Word kCompetitionByte = 0x00568a24;
constexpr Word kCareerByte = 0x0056a838;
constexpr Word kMoneyMaskLow = 0x00568608;
constexpr Word kMoneyMaskHigh = 0x0056860c;
constexpr Word kSkaterManager = 0x0056c580;        // the skater manager object (SkaterManager_GetCostume is its method)

constexpr Word kGetCostume = 0x004b0cd0;           // SkaterManager_GetCostume(index) (thiscall)
constexpr Word kCustomSlotIndex = 0x004b1810;      // Skater_CustomSlotIndex(entry)
constexpr Word kAllocBytes = 0x80000;              // one stream buffer
constexpr Word kHeaderEnd = 0x200;                 // where the header sits in a stream buffer while recording
constexpr Word kFramesAt = 0x4800;                 // where the frames start
constexpr Word kBoardOn = 0x0046c630;              // CBruce__BoardOn (fastcall on the skater)
constexpr Word kBuildReturnName = 0x0047b7d0;      // Replay_BuildReturnBufferName(buffer)
constexpr Word kLoadValidated = 0x0047b400;        // Replay_LoadValidatedStream(first, second, name)
constexpr Word kReplayPlaying = 0x00452de0;        // Front_ReplayPlaying()
constexpr Word kResetExtraAnimations = 0x00492b60;
constexpr Word kIsKnownAnimation = 0x00492c70;
constexpr Word kRegisterAnimation = 0x00492ba0;
constexpr Word kEndCause = 0x0056a8d0;
constexpr Word kLoadedHeaderCopy = 0x0056b8ec;     // four dwords of the loaded header's per-skater words
constexpr Word kNameScratch = kScratch;            // the 0x80-byte name Replay_BuildReturnBufferName fills (a stack local)

constexpr Word kMsgBadSkaterNumber = 0x005482b8;
constexpr Word kMsgCustomTooBig = 0x0052ce8c;
constexpr Word kMsgNoStream1 = 0x00548308;
constexpr Word kMsgBadValue = 0x00548300;
constexpr Word kMsgNoStream2 = 0x005482f4;
constexpr Word kMsgNullSkater = 0x0054833c;
constexpr Word kMsgIncorrectMode = 0x0054832c;
constexpr Word kMsgNullBlockStart = 0x00548314;
constexpr Word kMsgNotReplayMode = 0x005484d4;
constexpr Word kMsgNoReplayBuffer = 0x005484c0;
constexpr Word kMsgIllegalSkaters = 0x005484e4;

}  // namespace

// 0x004cb810: `header` is where the header goes, `player` which state block to take the frame count and highlights from (0 or 1)
void VideoKit::write_header(Word header, std::int32_t player) {
    const Word block = player == 0 ? globals::kState0 : globals::kState1;
    set_word_at(m_, header, word_at(m_, block + state::kFrames));
    for (Word k = 0; k < 10; ++k) {
        set_word_at(m_, header + kHeaderHighlights + 4 * k, word_at(m_, block + state::kHighlights + 4 * k));
    }
    set_half_at(m_, header + kHeaderSkaterCount, global_word(m_, globals::kNumberOfPlayers));
    if (player < 0 || 1 < player) {
        check(false, kMsgBadSkaterNumber);
    } else {
        for (Word k = 0; k < 2; ++k) {
            const Word skater = word_at(m_, globals::kPlayerOne + 4 * k);
            if (skater == 0) {
                continue;
            }
            const Word entry = word_at(m_, skater + kSelectionOfSkater);
            set_byte_at(m_, header + kHeaderSelection + k, byte_at(m_, skater + kSelectionOfSkater));
            set_byte_at(m_, header + kHeaderCostume + k, engine_value(kGetCostume, {kSkaterManager, k}));
            set_word_at(m_, header + kHeaderPlayerWordA + 4 * k, byte_at(m_, kCostumeA + k));
            set_word_at(m_, header + kHeaderPlayerWordB + 4 * k, byte_at(m_, kCostumeB + k));
            if ((byte_at(m_, kSkaterTable + entry * kSkaterRecordBytes) & 2) != 0) {
                const Word slot = engine_value(kCustomSlotIndex, {entry});
                log_line(0x005482c8);
                check(true, kMsgCustomTooBig);
                // Mem_Copy(destination, source, 0x80): word aligned, whole dwords
                m_.copy(header + kHeaderCustomInfo + kCustomInfoStride * k, kCustomTable + slot * kCustomInfoBytes, kCustomInfoBytes);
            }
        }
    }
    set_half_at(m_, header + kHeaderLevel, global_word(m_, globals::kCurrentLevel));
    set_half_at(m_, header + kHeaderGameMode, global_word(m_, globals::kGameMode));
    set_half_at(m_, header + kHeaderZero, 0);
    set_byte_at(m_, header + kHeaderCompetition, global_byte(m_, kCompetitionByte));
    set_byte_at(m_, header + kHeaderCareer, global_byte(m_, kCareerByte));
    set_word_at(m_, header + kHeaderMoneyMaskLow, global_word(m_, kMoneyMaskLow));
    set_word_at(m_, header + kHeaderMoneyMaskHigh, global_word(m_, kMoneyMaskHigh));
    log_line(0x00548290);
}

// 0x004cba30: a 0x80000-byte buffer per skater (the second only with two players); the header goes 0x200 into it, the frames from 0x4800
void VideoKit::allocate_stream_buffers() {
    s_.note("alloc", {kAllocBytes, 1, 1, 0});
    const Word first = static_cast<Word>(s_.value("alloc"));
    set_global_word(m_, globals::kState0 + state::kBoundary, first);
    set_global_word(m_, globals::kState0 + state::kBuffer, first);
    check(first != 0, kMsgNoStream1);
    set_global_word(m_, globals::kState0 + state::kStart, global_word(m_, globals::kState0 + state::kBuffer) + kHeaderEnd);
    set_global_word(m_, globals::kState0 + state::kBoundary, global_word(m_, globals::kState0 + state::kBoundary) + kFramesAt);
    check(global_word(m_, globals::kState0 + state::kBuffer) + kFramesAt == global_word(m_, globals::kState0 + state::kBoundary), kMsgBadValue);
    if (global_word(m_, globals::kNumberOfPlayers) != 2) {
        set_global_word(m_, globals::kState1 + state::kBoundary, 0);
        set_global_word(m_, globals::kState1 + state::kBuffer, 0);
        set_global_word(m_, globals::kState1 + state::kStart, 0);
        return;
    }
    s_.note("alloc", {kAllocBytes, 1, 1, 0});
    const Word second = static_cast<Word>(s_.value("alloc"));
    set_global_word(m_, globals::kState1 + state::kBoundary, second);
    set_global_word(m_, globals::kState1 + state::kBuffer, second);
    check(second != 0, kMsgNoStream2);
    set_global_word(m_, globals::kState1 + state::kBoundary, global_word(m_, globals::kState1 + state::kBoundary) + kFramesAt);
    set_global_word(m_, globals::kState1 + state::kStart, global_word(m_, globals::kState1 + state::kBuffer) + kHeaderEnd);
}

// 0x004cbce0: `mode` 1 starts a recording (the buffer is cleared and the header written), anything else a playback. `first` / `last` bound the frames to play (0x7fffffff: all).
void VideoKit::configure_stream_state(Word skater, Word mode, std::int32_t first, std::int32_t last) {
    constexpr Word kTimes = 0x0056e75c;       // five dwords that start a run at 0
    set_global_word(m_, 0x0056e764, global_word(m_, globals::kSimulationTime));
    set_global_word(m_, 0x0056e768, 0);
    set_global_word(m_, kTimes, 0);
    set_global_word(m_, 0x0056e760, 0);
    set_global_word(m_, 0x0056e76c, 0);
    check(skater != 0, kMsgNullSkater);
    check(mode != 0, kMsgIncorrectMode);
    const Word block = state_of(skater);
    set_global_word(m_, 0x0056e778, 0);
    reset_sound_queue();
    reset_splats();
    reset_trick_objects();
    set_word_at(m_, block, mode);
    if (mode == 1) {
        set_word_at(m_, block + state::kFrames, 0);
        set_word_at(m_, block + state::kBitsLeft, 0x20);
        for (Word k = 0; k < 5; ++k) {
            set_word_at(m_, block + state::kHighlights + 4 + 8 * k, 0);
        }
        const Word buffer = word_at(m_, block + state::kBuffer);
        set_word_at(m_, block + state::kHighlightCurrent, 0);
        check(buffer != 0, kMsgNullBlockStart);
        // the whole 0x80000-byte buffer is cleared (a port that holds only part of it clears what it holds)
        for (Word at = buffer; at < buffer + kAllocBytes; at += 4) {
            if (m_.contains(at, 4)) {
                set_word_at(m_, at, 0);
            }
        }
        write_header(word_at(m_, block + state::kStart), static_cast<std::int32_t>(word_at(m_, skater + kPlayerNumber)));
    } else {
        set_word_at(m_, block + state::kBitsLeft, 0);
    }
    set_word_at(m_, block + state::kCursor, word_at(m_, block + state::kBoundary));
    set_word_at(m_, block + 0x28, 0xffffffff);
    set_word_at(m_, block + 0x40, 0xffffffff);
    set_word_at(m_, block + 0x18, 0);
    set_word_at(m_, block + state::kFramesLeft, word_at(m_, block + state::kFrames));
    set_word_at(m_, block + 0x48, 0xfffffffe);
    set_word_at(m_, block + 0x3c, 0);
    set_word_at(m_, block + 0x44, 0);
    if (last != 0x7fffffff) {
        set_word_at(m_, block + state::kFramesLeft, u((last - first) + 1));
    }
    for (Word offset : {0xa0u, 0x9cu, 0x98u, 0xacu, 0xa8u, 0xa4u}) {
        set_word_at(m_, block + offset, 0);
    }
    for (Word offset : {0x8cu, 0x8au, 0x88u, 0x94u, 0x92u, 0x90u}) {
        set_half_at(m_, block + offset, 0);
    }
    set_word_at(m_, block + 0x7c, 0);
    set_word_at(m_, block + 0x80, 0);
    set_word_at(m_, block + 0x84, 0);
    set_word_at(m_, block + 0xb0, 0);
    set_word_at(m_, block + 0xb8, global_word(m_, globals::kSimulationTime));
    set_word_at(m_, block + 0xbc, global_word(m_, globals::kSimulationTime));
    set_word_at(m_, block + 0xc0, 0);
    set_half_at(m_, skater + 0xf6, 0);
    set_half_at(m_, skater + 0xf4, 0);
    set_word_at(m_, skater + 0x2c8c, 0);
    engine(kBoardOn, {skater});
}

// 0x004cdc20: the loaded file is the replay: its header sets the level, the game mode, the highlights and the frame count. 0: ready; 1: the file would not load (the end cause becomes 9)
Word VideoKit::start_playback() {
    check((engine_value(kReplayPlaying) & 0xff) != 0, kMsgNotReplayMode);
    if (global_short(m_, globals::kNumSkaters) > 1) {
        check(global_word(m_, globals::kStreamBuffer1) != 0, kMsgNoReplayBuffer);
    }
    engine(kBuildReturnName, {kNameScratch});
    if (engine_value(kLoadValidated, {global_word(m_, globals::kStreamBuffer0), global_word(m_, globals::kStreamBuffer1), kNameScratch}) == 0) {
        set_global_word(m_, kEndCause, 9);
        return 1;
    }
    const Word first = global_word(m_, globals::kStreamBuffer0);
    set_global_word(m_, globals::kState0 + state::kStart, first);
    set_global_half(m_, globals::kReplayLevel, half_at(m_, first + 0x32));
    set_global_half(m_, globals::kReplayGame, half_at(m_, first + 0x34));
    for (Word k = 0; k < 4; ++k) {
        set_word_at(m_, kLoadedHeaderCopy + 4 * k, word_at(m_, first + 0x44 + 4 * k));
    }
    set_global_word(m_, globals::kNumFrames, word_at(m_, first));
    for (Word k = 0; k < 10; ++k) {
        set_word_at(m_, globals::kHighlightRanges + 4 * k, word_at(m_, first + 4 + 4 * k));
    }
    if (global_short(m_, globals::kNumSkaters) == 2) {
        const Word second = global_word(m_, globals::kStreamBuffer1);
        set_global_word(m_, globals::kState1 + state::kFrames, word_at(m_, second));
        for (Word k = 0; k < 10; ++k) {
            set_word_at(m_, globals::kState1 + state::kHighlights + 4 * k, word_at(m_, second + 4 + 4 * k));
        }
    }
    set_global_word(m_, globals::kHighlightCursor, 0xffffffff);
    // the skater count is negated once a replay is loaded (the dword is negated, its low half stored back)
    set_global_half(m_, globals::kNumSkaters, 0u - global_word(m_, globals::kNumSkaters));
    return 0;
}

// 0x004cddc0
void VideoKit::register_animations() {
    engine(kResetExtraAnimations);
    const std::int32_t skaters = -global_short(m_, globals::kNumSkaters);
    check(skaters >= 1 && skaters <= 2, kMsgIllegalSkaters);
    for (std::int32_t skater = 0; skater < -static_cast<std::int32_t>(global_short(m_, globals::kNumSkaters)); ++skater) {
        std::int16_t animation = 0;
        do {
            if ((engine_value(kIsKnownAnimation, {static_cast<Word>(static_cast<std::int32_t>(animation))}) & 0xff) == 0) {
                engine(kRegisterAnimation, {static_cast<Word>(static_cast<std::int32_t>(animation))});
            }
            ++animation;
        } while (animation < 300);
    }
}

}  // namespace opentony::runtime::videokit
