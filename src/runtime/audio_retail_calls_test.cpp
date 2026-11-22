// Replays retail forced calls (Phase 4.13b) through the native audio and movie layer (the crowd, the sound effect front, the VAB banks, the redbook sequencer, the game FMV list and the movie
// interface):
//
//   opentony_audio_retail_calls_test FIXTURE.txt [FIXTURE.txt ...]
//
// A fixture (scripts/make_career_calls_fixture.py, from re/gdb/probes/frontend_calls.py with the groups of audio_specs.py) holds the retail memory regions the ported code touches and, for
// every case, the state, the arguments and what the retail function did; see retail_calls_replay.hpp for the format and the comparison.
#include "audio_game.hpp"
#include "retail_calls_replay.hpp"

#include <memory>

using namespace opentony::runtime;
using namespace opentony::runtime::replay;
using namespace opentony::runtime::pcplatform;

namespace {

struct Ctx {
    RetailMemory mem;
    RetailServices svc;
    AudioGame audio;
    explicit Ctx(RetailMemory memory) : mem(std::move(memory)), audio(mem, svc) {
        if (!mem.contains(kScratchBase, 0x10)) {
            mem.add_region(kScratchBase, kScratchBytes);         // (the stack locals of the retail functions)
        }
    }
    Ctx(const Ctx&) = delete;
    Ctx& operator=(const Ctx&) = delete;
};

using AudioAdapter = Adapter<Ctx>;

// A(expr): the function returns expr. V(expr): it returns nothing the callers use. `a` are the stack arguments, `self` is `this`.
#define A(expr) [](Ctx& c, const Args& a, std::uint32_t self) -> std::uint32_t { (void)a; (void)c; (void)self; return U(expr); }
#define V(expr) [](Ctx& c, const Args& a, std::uint32_t self) -> std::uint32_t { (void)a; (void)c; (void)self; expr; return 0; }
#define G c.audio

const std::map<std::uint32_t, AudioAdapter>& adapters() {
    static const std::map<std::uint32_t, AudioAdapter> table = {
        // ---- crowd.cpp ----
        {0x0042f2e0, {"Crowd_ClearSound", 0, false, V(G.crowd_clear_sound())}},
        {0x0042f2f0, {"Crowd_Nothing", 0, false, V()}},
        {0x0042f300, {"Crowd_Start", 0, false, V(G.crowd_start())}},
        {0x0042f370, {"Crowd_Stop", 0, false, V(G.crowd_stop())}},
        {0x0042f3e0, {"Crowd_MoveTowards", 0xffffffff, false, A(G.crowd_move_towards(a[0], S(a[1]), S(a[2])))}},
        {0x0042f430, {"Crowd_Update", 0, false, V(G.crowd_update())}},
        {0x0042f540, {"Crowd_ReactToScore", 0, false, V(G.crowd_react_to_score(S(a[0])))}},
        {0x0042f680, {"Crowd_ReactToBail", 0, false, V(G.crowd_react_to_bail(S(a[0])))}},
        {0x0042f700, {"Crowd_ReactToTrick", 0, false, V(G.crowd_react_to_trick(S(a[0])))}},
        {0x0042f800, {"Crowd_Boo", 0, false, V(G.crowd_boo(S(a[0])))}},
        // ---- redbook.cpp ----
        {0x004ab9c0, {"Redbook_ResetVolume", 0, false, V(G.redbook_reset_volume())}},
        {0x004ab9e0, {"Redbook_UpdateVolume", 0, false, V(G.redbook_update_volume())}},
        {0x004aba90, {"Redbook_Reset", 0, false, V(G.redbook_reset())}},
        {0x004abb00, {"Redbook_Play", 0xff, false, A(G.redbook_play(S(a[0]), S(a[1])))}},
        {0x004abc80, {"Redbook_Stop", 0, false, V(G.redbook_stop())}},
        {0x004abcb0, {"Redbook_Next", 0, false, V(G.redbook_next(S(a[0])))}},
        {0x004abe20, {"Redbook_SetPaused", 0, false, V(G.redbook_set_paused(a[0]))}},
        {0x004abe50, {"Redbook_Update", 0, false, V(G.redbook_update())}},
        {0x004abed0, {"Redbook_CanContinue", 0xffffffff, false, A(G.redbook_can_continue())}},
        {0x004abf10, {"Redbook_SavePosition", 0, false, V(G.redbook_save_position(a[0]))}},
        {0x004abf80, {"Redbook_RestorePosition", 0, false, V(G.redbook_restore_position(a[0]))}},
        {0x004ac010, {"Redbook_TrackNumber", 0xffffffff, false, A(G.redbook_track_number())}},
        // ---- sfx.cpp and pc\soundlib.cpp ----
        {0x004acff0, {"Audio_CloseFrontendVab", 0, false, V(G.close_frontend_vab())}},
        {0x004ad020, {"Audio_OpenFrontendVab", 0, false, V(G.open_frontend_vab(a[0]))}},
        {0x004ad180, {"Audio_OpenLevelVab", 0, false, V(G.open_level_vab(a[0]))}},
        {0x004ad5b0, {"Audio_CloseLevelVab", 0, false, V(G.close_level_vab())}},
        {0x004ad5e0, {"Audio_Nothing", 0, false, V()}},
        {0x004ad620, {"SFX_PlayX", 0xffffffff, false, A(G.sfx_play(S(a[0]), S(a[1]), S(a[2])))}},
        {0x004ad750, {"SFX_PlayLR", 0xffffffff, false, A(G.sfx_play_lr(S(a[0]), S(a[1]), S(a[2]), S(a[3])))}},
        {0x004ad7d0, {"Audio_PlayPositionalSound", 0xffffffff, false, A(G.play_positional_sound(S(a[0]), a[1], S(a[2])))}},
        {0x004ad870, {"SFX_GetVoiceFrequency", 0xffffffff, false, A(G.voice_frequency(S(a[0])))}},
        {0x004ad890, {"SFX_SetVoiceFrequency", 0, false, V(G.set_voice_frequency(S(a[0]), a[1]))}},
        {0x004ad8b0, {"SFX_ModifyPos", 0, false, V(G.modify_position(S(a[0]), a[1]))}},
        {0x004ad950, {"Audio_UpdatePositionalSound", 0, false, V(G.update_positional_sound(S(a[0]), S(a[1]), S(a[2])))}},
        {0x004ad9b0, {"SFX_ClearFlag", 0, false, V(G.clear_sound_flag())}},
        {0x004ad9c0, {"SFX_Pause", 0, false, V(G.sfx_pause())}},
        {0x004ad9d0, {"SFX_Resume", 0, false, V(G.sfx_resume())}},
        {0x004ad9f0, {"SFX_StopVoice", 0, false, V(G.stop_voice(S(a[0])))}},
        {0x004ada10, {"SFX_StopAllAndCloseVab", 0, false, V(G.stop_all_and_close())}},
        {0x004ada20, {"SFX_SetReverb", 0, false, V(G.set_reverb(a[0]))}},
        {0x004adb90, {"SFX_ReverbOff", 0, false, V(G.set_reverb(0))}},
        {0x004adba0, {"SFX_SetReverbDepth", 0xffffffff, false, A(G.set_reverb_depth(a[0], a[1]))}},
        {0x004adc30, {"SFX_Nothing1", 0, false, V()}},
        {0x004adc50, {"SFX_Nothing2", 0, false, V()}},
        {0x004adc60, {"SFX_Nothing3", 0, false, V()}},
        {0x004adc70, {"SFX_Nothing4", 0, false, V()}},
        {0x004f38d0, {"Audio_StopAll", 0, false, V(G.stop_all_sounds())}},
        // ---- gamefmv.cpp and pc\pcmovie.cpp ----
        {0x00454d30, {"GameFmv_ChangeTrack", 0, false, V(G.change_fmv_track(S(a[0])))}},
        {0x00454e50, {"GameFmv_Start", 0, false, V(G.start_fmv())}},
        {0x00454e70, {"GameFmv_Update", 0, false, V(G.movie_update())}},
        {0x004e7070, {"PCMOVIE_GetMovieName", 0xffffffff, false, A(G.movie_name(a[0]))}},
        {0x004e7090, {"PCMovie_PlayGameFMV", 0xffffffff, false, A(G.play_game_fmv())}},
        {0x004e70e0, {"PCMOVIE_StartFMV", 0, false, V(G.start_fmv())}},
        {0x004e7140, {"PCMOVIE_StopFMV", 0, false, V(G.stop_fmv())}},
        {0x004e71c0, {"PCMOVIE_Update", 0, false, V(G.movie_update())}},
        {0x004e6e70, {"PCMOVIE_Pause", 0, false, V(G.movie_pause(a[0]))}},
        {0x004e7260, {"PCMOVIE_StartMusic", 0xff, false, A(G.start_music(a[0]))}},
        {0x004e72d0, {"PCMOVIE_SetXAVolume", 0, false, V(G.set_xa_volume(S(a[0]), S(a[1])))}},
        {0x004e7310, {"PCMOVIE_XAPlay", 0, false, V(G.xa_play(S(a[0]), S(a[1])))}},
        {0x004e7370, {"PCMOVIE_XAStop", 0, false, V(G.xa_stop())}},
        {0x004e73e0, {"PCMOVIE_XADone", 0xffffffff, false, A(G.xa_done())}},
        {0x004e6f00, {"PCMOVIE_DrawWindow", 0, false, V(G.draw_movie_window(a[0]))}},
        {0x004e5ec0, {"PCMovie_PlayStartupMedia", 0xffffffff, false, A(G.play_startup_media(a[0]))}},
    };
    return table;
}

}  // namespace

int main(int argc, char** argv) {
    Replay<Ctx> replay(adapters(), [](RetailMemory memory) { return std::make_unique<Ctx>(std::move(memory)); });
    return replay.run_files(argc, argv, "audio and movie retail forced calls");
}
