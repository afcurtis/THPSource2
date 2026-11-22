#pragma once

#include "collision_kit.hpp"
#include "guest_view.hpp"
#include "core_kit.hpp"
#include "math_kit.hpp"
#include "pc_platform.hpp"

#include <array>
#include <cstdint>
#include <string>

namespace opentony::runtime::videokit {

// Phase 4.15b: video.cpp, the replay recorder. The game records a run as a bit-packed stream, one record per frame and skater (position and orientation as deltas, the animation, a few events), plus a
// header (the frame count, the five best-scoring highlights, which skaters and levels) and plays it back by feeding the records to the skater. Like the rest of Phase 4 the functions work on the retail
// memory at the retail addresses; everything outside them (the allocator, the object lists, the sound, the collision queries, the screens) is a service reported by address (`engine_<address>`).

namespace globals {

constexpr std::uint32_t kPlayerOne = 0x0056a858;              // Player[0]; Player[1] follows
constexpr std::uint32_t kNumberOfPlayers = 0x00533f3c;        // GNumberOfPlayers
constexpr std::uint32_t kSimulationTime = 0x0056e320;         // SimulationTime
constexpr std::uint32_t kStatus = 0x00561bfc;                 // GStatus
constexpr std::uint32_t kCurrentLevel = 0x0056a898;
constexpr std::uint32_t kGameMode = 0x00533f38;               // LevelEventGameModeSelector
constexpr std::uint32_t kState0 = 0x0056e5d8;                 // ReplayPlaybackState0 (0x188 bytes); ReplayPlaybackState1 is 0x188 bytes below
constexpr std::uint32_t kState1 = 0x0056e450;
constexpr std::uint32_t kNumFrames = 0x0056e5f8;              // ReplayNumFrames
constexpr std::uint32_t kHighlightRanges = 0x0056e62c;        // ReplayHighlightRanges: five records of {start short, end short, score dword}
constexpr std::uint32_t kHighlightCursor = 0x0056e774;        // ReplayHighlightCursor
constexpr std::uint32_t kNumSkaters = 0x0056b8d4;             // ReplayNumSkaters (short; negated once a replay is loaded)
constexpr std::uint32_t kReplaySkater = 0x0056b8d6;
constexpr std::uint32_t kReplayLevel = 0x0056b8da;            // short
constexpr std::uint32_t kReplayGame = 0x0056b8dc;             // short
constexpr std::uint32_t kRestartActive = 0x00561990;          // ReplayVideoRestartActive (byte)
constexpr std::uint32_t kStreamBuffer0 = 0x0056e5dc;          // ReplayStreamBuffer0 (state 0 + 4)
constexpr std::uint32_t kStreamBuffer1 = 0x0056e454;          // ReplayStreamBuffer1 (state 1 + 4)

}  // namespace globals

// the fields of a playback state (ReplayPlaybackState0 / 1)
namespace state {

constexpr std::uint32_t kMode = 0x00;                         // dword: 0 idle, 1 recording, 2 playing
constexpr std::uint32_t kBuffer = 0x04;                       // dword: the 0x80000-byte stream buffer
constexpr std::uint32_t kStart = 0x08;                        // dword: where the header goes (the buffer + 0x200 while recording, the buffer while playing)
constexpr std::uint32_t kBoundary = 0x0c;                     // dword: where the frames start (the buffer + 0x4800)
constexpr std::uint32_t kCursor = 0x14;                       // dword: the next word of the stream to read or write
constexpr std::uint32_t kWord = 0x18;                         // dword: the word being filled or consumed
constexpr std::uint32_t kBitsLeft = 0x1c;                     // dword: bits of that word still free (writing) or unread (reading)
constexpr std::uint32_t kFrames = 0x20;                       // dword: frames recorded
constexpr std::uint32_t kFramesLeft = 0x24;                   // dword: frames still to play
constexpr std::uint32_t kHighlightCurrent = 0x50;             // dword: the highlight being played (0 none)
constexpr std::uint32_t kHighlights = 0x54;                   // five records of 8 bytes: {start short, end short, score dword}
constexpr std::uint32_t kBytes = 0x188;

}  // namespace state

// where the port builds the vectors the retail code keeps in stack locals
constexpr std::uint32_t kScratch = pcplatform::kScratchBase + 0xc00;

// 0x00401000-style assertion messages of video.cpp are reported by their address
namespace message {

constexpr std::uint32_t kBitsZeroTo32 = 0x00548478;           // "Bits must be 0..32"
constexpr std::uint32_t kBitsOneTo32 = 0x0054848c;            // "Bits must be 1..32"
constexpr std::uint32_t kBodyPart = 0x00548238;               // "BodyPart > 2 bits"
constexpr std::uint32_t kTrickObIndex = 0x00548278;           // "TrickObIndex > 8 bits"
constexpr std::uint32_t kWrongSkater = 0x00548268;            // "wrong skater no"

}  // namespace message

class VideoKit final : public collision::CollisionKit {
public:
    VideoKit(RetailMemory& memory, RetailServices& services) : CollisionKit(memory, services), core_(memory, services), math_(memory, services) { core_.use_native_square_root(); }

    // ---- vectors of shorts (0x004cb130, 0x004cb1d0) ----
    void scale_shorts(std::uint32_t out, std::uint32_t scalar, std::uint32_t vector);          // 0x004cb130 (cdecl): out = *scalar * vector, each component truncated to a short
    void shift_shorts(std::uint32_t out, std::uint32_t vector, std::uint32_t count);           // 0x004cb1d0 (cdecl): out = vector >> *count

    // ---- the rest of the short-vector class (vector.cpp, video_shorts.cpp) ----
    std::uint32_t shorts_length(std::uint32_t self);                                           // 0x004cadc0 Math_Vector3sLength (thiscall)
    std::uint32_t shorts_mul_assign(std::uint32_t self, std::uint32_t scalar);                 // 0x004cae50 (thiscall, one argument)
    std::uint32_t shorts_div_assign(std::uint32_t self, std::uint32_t divisor);                // 0x004cae80 (thiscall, one argument)
    void shorts_shift_right(std::uint32_t self, std::uint32_t count);                          // 0x004caeb0 (thiscall, one argument)
    void shorts_shift_left(std::uint32_t self, std::uint32_t count);                           // 0x004caed0 (thiscall, one argument)
    std::uint32_t shorts_angle_difference(std::uint32_t self, std::uint32_t out, std::uint32_t other);   // 0x004cafa0 (thiscall, two arguments)
    std::uint32_t shorts_mul_out(std::uint32_t out, std::uint32_t vector, std::uint32_t scalar);         // 0x004cb0e0 (cdecl)
    std::uint32_t shorts_div_out(std::uint32_t out, std::uint32_t vector, std::uint32_t divisor);        // 0x004cb180 (cdecl)
    std::uint32_t shorts_shift_left_out(std::uint32_t out, std::uint32_t vector, std::uint32_t count);   // 0x004cb220 (cdecl)
    std::uint32_t shorts_negate_out(std::uint32_t self, std::uint32_t out);                              // 0x004cb270 (thiscall, one argument)
    void clear_body_state();                                                                   // 0x004cb2c0 (and its jump thunk 0x004cb2b0)

    // ---- the queue of sound commands the recorder takes along (0x004cb320..0x004cb3f0) ----
    void reset_sound_queue();                                                                  // 0x004cb320
    std::uint32_t push_sound(std::uint32_t kind, std::uint32_t sound);                         // 0x004cb330 (cdecl): the wrap count
    std::uint32_t sound_pending();                                                             // 0x004cb360
    std::uint32_t pop_sound(std::uint32_t kind_out, std::uint32_t sound_out);                  // 0x004cb380 (cdecl): the wrap count
    void record_sound_start(std::uint32_t sound);                                              // 0x004cb3f0 (cdecl): queued only while recording player one's run
    void record_sound_stop(std::uint32_t sound);                                               // 0x004cb3c0 (cdecl): the same with the stop kind
    void stop_stream(std::uint32_t skater);                                                    // 0x004cbfa0 (cdecl)
    void rewind_stream(std::uint32_t skater);                                                  // 0x004cc020 (cdecl)
    void reset_splats();                                                                       // 0x004cb420
    void record_splat(std::uint32_t skater, std::uint32_t part);                               // 0x004cb430 (cdecl)
    void reset_trick_objects();                                                                // 0x004cb4b0
    void record_trick_object(std::uint32_t player, std::uint32_t index);                       // 0x004cb4c0 (cdecl)

    // ---- the highlights (0x004cb580..0x004cb6e0) ----
    void highlight_start(std::uint32_t skater);                                                // 0x004cb580 Replay_RecordHighlightStart (cdecl)
    void highlight_end(std::uint32_t skater, std::int32_t score);                              // 0x004cb5c0 Replay_RecordHighlightEnd (cdecl)
    void prepare_highlights();                                                                 // 0x004cb6e0 Replay_PrepareHighlights

    // ---- the bit stream (0x004ccc40..0x004cd710, 0x004cbf30, 0x004cbf70) ----
    std::uint32_t bits_needed(std::int32_t value);                                             // 0x004ccc40 (cdecl): the width of the smallest field that holds the signed value
    void write_bits(std::uint32_t state, std::uint32_t value, std::int32_t bits);              // 0x004ccc70 Replay_WriteBits (cdecl)
    std::uint32_t read_bits(std::uint32_t state, std::int32_t bits);                           // 0x004cd5a0 Replay_ReadBits (cdecl)
    std::uint32_t read_signed_bits(std::uint32_t state, std::int32_t bits);                    // 0x004cd690 Replay_ReadSignedBits (cdecl)
    std::uint32_t read_flag(std::uint32_t state);                                              // 0x004cd710 Replay_ReadFlag (cdecl)
    void flush_input_block(std::uint32_t state);                                               // 0x004cbf70 Replay_FlushInputBlock (cdecl)
    void reset_skater_input(std::uint32_t skater);                                             // 0x004cbf30 Replay_ResetSkaterInput (cdecl)
    void clear_playback_state();                                                               // 0x004cbc90 Replay_ClearPlaybackState
    void free_stream_buffers();                                                                // 0x004cbca0

    // ---- the session: header, buffers, start of a recording or a playback ----
    void write_header(std::uint32_t header, std::int32_t player);                              // 0x004cb810 Replay_WriteHeader (cdecl)
    void allocate_stream_buffers();                                                            // 0x004cba30 Replay_AllocateStreamBuffers
    void configure_stream_state(std::uint32_t skater, std::uint32_t mode, std::int32_t first, std::int32_t last);   // 0x004cbce0 Replay_ConfigureStreamState (cdecl)
    std::uint32_t start_playback();                                                            // 0x004cdc20 Replay_StartPlayback: 0 on success, 1 when the file did not load
    void register_animations();                                                                // 0x004cddc0: every animation of the trick scripts, once per skater of the replay

    // ---- one frame ----
    void matrix_to_angles(std::uint32_t matrix, std::uint32_t angles);                         // 0x004cc0a0 (cdecl)
    void write_frame(std::uint32_t skater);                                                    // 0x004cc190 Replay_WriteFrame (cdecl)
    void write_frame_checked(std::uint32_t skater);                                            // 0x004cc0e0 (cdecl): the asserts, then Replay_WriteFrame
    void apply_frame(std::uint32_t skater, std::int32_t mode);                                 // 0x004ccd70 Replay_ApplySkaterFrame (cdecl): mode 1 plays the frame for the screen, -1 skips ahead
    void advance_frames(std::int32_t count);                                                   // 0x004cbbb0 Replay_AdvanceFrames (cdecl)
    void advance_highlight();                                                                  // 0x004cbc40 Replay_AdvanceHighlight
    void playback(std::uint32_t skater);                                                       // 0x004cd750 Video_Playback (cdecl)
    void camera_trigger(std::uint32_t skater);                                                 // 0x00411fc0 Camera_SelectNearestTriggerPoint (cdecl): the first call of Video_Playback
    void handle_stepping();                                                                    // 0x004cdab0 Video_HandleStepping
    std::uint32_t is_paused();                                                                 // 0x004cda90: playing and paused (frame by frame)

    // ---- small accessors ----
    std::uint32_t is_playing();                                                                // 0x004cde70 Video_Playing
    void store_car_position(std::uint32_t unused, std::int32_t slot, std::uint32_t x, std::uint32_t y, std::uint32_t z, std::uint32_t angles, std::uint32_t last);   // 0x004cde80 (cdecl)
    void load_car_position(std::uint32_t unused, std::int32_t slot, std::uint32_t position, std::uint32_t angles);   // 0x004cdef0 (cdecl)

private:
    using Word = std::uint32_t;
    // the state block of a skater: player one's, else the second one's
    Word state_of(Word skater) { return skater == guest::read_scalar<std::uint32_t>(m_, globals::kPlayerOne) ? globals::kState0 : globals::kState1; }

    void take_filled(Word destination, Word bytes, const std::string& service);
    static Word u(std::int32_t v) { return static_cast<Word>(v); }
    static std::int32_t s(Word v) { return static_cast<std::int32_t>(v); }
    // x86 shifts take the count modulo 32
    static Word shl(Word v, Word count) { return v << (count & 31u); }
    static Word shr(Word v, Word count) { return v >> (count & 31u); }
    static std::int32_t sar(std::int32_t v, Word count) { return v >> (count & 31u); }

    core::CoreKit core_;
    mathkit::MathKit math_;
};

}  // namespace opentony::runtime::videokit
