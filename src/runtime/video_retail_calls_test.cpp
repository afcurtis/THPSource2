// Replays retail forced calls (Phase 4.15b) through the native replay recorder (video_*.cpp: the bit stream, the queues and the highlights of video.cpp):
//
//   opentony_video_retail_calls_test FIXTURE.txt [FIXTURE.txt ...]
//
// A fixture (scripts/make_career_calls_fixture.py, from re/gdb/probes/frontend_calls.py with the groups of video_specs.py) holds the retail memory regions the ported code touches and, for every
// case, the state, the arguments and what the retail function did; see retail_calls_replay.hpp for the format and the comparison.
#include "retail_calls_replay.hpp"
#include "video_kit.hpp"

#include <memory>

using namespace opentony::runtime;
using namespace opentony::runtime::replay;
using namespace opentony::runtime::videokit;
using opentony::runtime::pcplatform::kScratchBase;
using opentony::runtime::pcplatform::kScratchBytes;

namespace {

struct Ctx {
    RetailMemory mem;
    RetailServices svc;
    VideoKit video;
    explicit Ctx(RetailMemory memory) : mem(std::move(memory)), video(mem, svc) {
        if (!mem.contains(kScratchBase, 0x10)) {
            mem.add_region(kScratchBase, kScratchBytes);         // (the stack locals of the retail functions)
        }
    }
    Ctx(const Ctx&) = delete;
    Ctx& operator=(const Ctx&) = delete;
};

using VideoAdapter = Adapter<Ctx>;

// A(expr): the function returns expr. V(expr): it returns nothing the callers use. `a` are the stack arguments, `self` is `this` (ecx).
#define A(expr) [](Ctx& c, const Args& a, std::uint32_t self) -> std::uint32_t { (void)a; (void)c; (void)self; return U(expr); }
#define V(expr) [](Ctx& c, const Args& a, std::uint32_t self) -> std::uint32_t { (void)a; (void)c; (void)self; expr; return 0; }
#define G c.video

const std::map<std::uint32_t, VideoAdapter>& adapters() {
    static const std::map<std::uint32_t, VideoAdapter> table = {
        {0x004cb130, {"Replay_ScaleShorts", 0, false, V(G.scale_shorts(a[0], a[1], a[2]))}},
        {0x004cb1d0, {"Replay_ShiftShorts", 0, false, V(G.shift_shorts(a[0], a[1], a[2]))}},
        {0x004cb320, {"Replay_ResetSoundQueue", 0, false, V(G.reset_sound_queue())}},
        {0x004cb330, {"Replay_PushSound", 0xffffffff, false, A(G.push_sound(a[0], a[1]))}},
        {0x004cb360, {"Replay_SoundPending", 0xff, false, A(G.sound_pending())}},
        {0x004cb380, {"Replay_PopSound", 0xffffffff, false, A(G.pop_sound(a[0], a[1]))}},
        {0x004cb3f0, {"Replay_RecordSoundStart", 0, false, V(G.record_sound_start(a[0]))}},
        {0x004cb420, {"Replay_ResetSplats", 0, false, V(G.reset_splats())}},
        {0x004cb430, {"Replay_RecordSplat", 0, false, V(G.record_splat(a[0], a[1]))}},
        {0x004cb4b0, {"Replay_ResetTrickObjects", 0, false, V(G.reset_trick_objects())}},
        {0x004cb4c0, {"Replay_RecordTrickObject", 0, false, V(G.record_trick_object(a[0], a[1]))}},
        {0x004cb580, {"Replay_RecordHighlightStart", 0, false, V(G.highlight_start(a[0]))}},
        {0x004cb5c0, {"Replay_RecordHighlightEnd", 0, false, V(G.highlight_end(a[0], S(a[1])))}},
        {0x004cb6e0, {"Replay_PrepareHighlights", 0, false, V(G.prepare_highlights())}},
        {0x004ccc40, {"Replay_BitsNeeded", 0xffffffff, false, A(G.bits_needed(S(a[0])))}},
        {0x004ccc70, {"Replay_WriteBits", 0, false, V(G.write_bits(a[0], a[1], S(a[2])))}},
        {0x004cd5a0, {"Replay_ReadBits", 0xffffffff, false, A(G.read_bits(a[0], S(a[1])))}},
        {0x004cd690, {"Replay_ReadSignedBits", 0xffffffff, false, A(G.read_signed_bits(a[0], S(a[1])))}},
        {0x004cd710, {"Replay_ReadFlag", 0xffffffff, false, A(G.read_flag(a[0]))}},
        {0x004cbf70, {"Replay_FlushInputBlock", 0, false, V(G.flush_input_block(a[0]))}},
        {0x004cbf30, {"Replay_ResetSkaterInput", 0, false, V(G.reset_skater_input(a[0]))}},
        {0x004cbc90, {"Replay_ClearPlaybackState", 0, false, V(G.clear_playback_state())}},
        {0x004cbca0, {"Replay_FreeStreamBuffers", 0, false, V(G.free_stream_buffers())}},
        {0x004cde70, {"Video_Playing", 0xff, false, A(G.is_playing())}},
        {0x004cadc0, {"Math_Vector3sLength", 0xffffffff, false, A(G.shorts_length(self))}},
        {0x004cae50, {"Math_Vector3sMulScalar", 0xffffffff, false, A(G.shorts_mul_assign(self, a[0]))}},
        {0x004cae80, {"Math_Vector3sDivScalar", 0xffffffff, false, A(G.shorts_div_assign(self, a[0]))}},
        {0x004caeb0, {"Math_Vector3sShiftRight", 0xffffffff, false, A((G.shorts_shift_right(self, a[0]), self))}},
        {0x004caed0, {"Math_Vector3sShiftLeft", 0xffffffff, false, A((G.shorts_shift_left(self, a[0]), self))}},
        {0x004cafa0, {"Math_Vector3sAngleDifferenceOut", 0xffffffff, false, A(G.shorts_angle_difference(self, a[0], a[1]))}},
        {0x004cb0e0, {"Math_Vector3sMulScalarOut", 0xffffffff, false, A(G.shorts_mul_out(a[0], a[1], a[2]))}},
        {0x004cb180, {"Math_Vector3sDivScalarOut", 0xffffffff, false, A(G.shorts_div_out(a[0], a[1], a[2]))}},
        {0x004cb220, {"Math_Vector3sShiftLeftOut", 0xffffffff, false, A(G.shorts_shift_left_out(a[0], a[1], a[2]))}},
        {0x004cb270, {"Math_Vector3sNegateOut", 0xffffffff, false, A(G.shorts_negate_out(self, a[0]))}},
        {0x004cb2c0, {"Replay_ClearBodyState", 0, false, V(G.clear_body_state())}},
        {0x004cb2b0, {"Replay_ClearBodyStateThunk", 0, false, V(G.clear_body_state())}},
        {0x004cb3c0, {"Replay_RecordSoundStop", 0, false, V(G.record_sound_stop(a[0]))}},
        {0x004cbfa0, {"Replay_StopStream", 0, false, V(G.stop_stream(a[0]))}},
        {0x004cc020, {"Replay_RewindStream", 0, false, V(G.rewind_stream(a[0]))}},
        {0x004cb810, {"Replay_WriteHeader", 0, false, V(G.write_header(a[0], S(a[1])))}},
        {0x004cba30, {"Replay_AllocateStreamBuffers", 0, false, V(G.allocate_stream_buffers())}},
        {0x004cbce0, {"Replay_ConfigureStreamState", 0, false, V(G.configure_stream_state(a[0], a[1], S(a[2]), S(a[3])))}},
        {0x004cdc20, {"Replay_StartPlayback", 0xffffffff, false, A(G.start_playback())}},
        {0x004cddc0, {"Replay_RegisterAnimations", 0, false, V(G.register_animations())}},
        {0x004cc0a0, {"Replay_MatrixToAngles", 0, false, V(G.matrix_to_angles(a[0], a[1]))}},
        {0x004cc190, {"Replay_WriteFrame", 0, false, V(G.write_frame(a[0]))}},
        {0x004cc0e0, {"Replay_WriteFrameChecked", 0, false, V(G.write_frame_checked(a[0]))}},
        {0x004ccd70, {"Replay_ApplySkaterFrame", 0, false, V(G.apply_frame(a[0], S(a[1])))}},
        {0x004cbbb0, {"Replay_AdvanceFrames", 0, false, V(G.advance_frames(S(a[0])))}},
        {0x004cbc40, {"Replay_AdvanceHighlight", 0, false, V(G.advance_highlight())}},
        {0x004cd750, {"Video_Playback", 0, false, V(G.playback(a[0]))}},
        {0x00411fc0, {"Camera_SelectNearestTriggerPoint", 0, false, V(G.camera_trigger(a[0]))}},
        {0x004cdab0, {"Video_HandleStepping", 0, false, V(G.handle_stepping())}},
        {0x004cda90, {"Video_Paused", 0xff, false, A(G.is_paused())}},
        {0x004cde80, {"Replay_StoreCarPosition", 0, false, V(G.store_car_position(a[0], S(a[1]), a[2], a[3], a[4], a[5], a[6]))}},
        {0x004cdef0, {"Replay_LoadCarPosition", 0, false, V(G.load_car_position(a[0], S(a[1]), a[2], a[3]))}},
    };
    return table;
}

}  // namespace

int main(int argc, char** argv) {
    Replay<Ctx> replay(adapters(), [](RetailMemory memory) { return std::make_unique<Ctx>(std::move(memory)); });
    return replay.run_files(argc, argv, "replay recorder retail forced calls");
}
