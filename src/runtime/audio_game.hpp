#pragma once

#include "pc_platform.hpp"
#include "guest_view.hpp"

#include <cstdint>
#include <string>

namespace opentony::runtime::pcplatform {

// Phase 4.13b: the game's side of the audio and the movie interface, on the game's globals: the crowd (crowd.cpp), the redbook / XA music sequencer (redbook.cpp), the sound effect front
// (sfx.cpp: SFX_PlayX / SFX_PlayLR, the positional sounds, the VAB opening and closing, the reverb), Audio_StopAll (pc\soundlib.cpp), the game FMV list (gamefmv.cpp) and the movie interface
// the game calls (pc\pcmovie.cpp: start, stop, update, pause, music, volume, the startup media loop and the movie window's primitive). The HAL's voices (Audio_PlaySound and its
// siblings), the sound bank load, the SPU reverb calls, Bink and the Win32 calls are services (`engine_<address>`, `win32_<name>`).
namespace audio {

// the settings and the bank table
constexpr std::uint32_t kSfxVolume = 0x0052be44;           // the sound effect volume scale (0x4000 = full): every positional and plain play multiplies by it (>> 14)
constexpr std::uint32_t kMusicVolume = 0x0052be48;         // the music volume (0..0xff); non-zero also means the music is the CD / XA tracks
constexpr std::uint32_t kBankTable = 0x029d7d20;           // 128 pointers to the bank records (+4 the pitch scale as a float, +0x2c a byte: non-zero when the sound is loaded)
constexpr std::uint32_t kPitchScale = 0x00519728;          // the float the pitch scale is multiplied by
constexpr std::uint32_t kPlayPitch = 0x0056bed8;           // the pitch Audio_PlaySound reads (written by every play)
constexpr std::uint32_t kVoiceSlots = 0x029d6920;          // 128 voice slots of 0x28 bytes: the first dword is the buffer (non-zero: in use)
constexpr std::uint32_t kVoiceSlotsEnd = 0x029d7d20;
constexpr std::uint32_t kVoiceSlotSize = 0x28;
constexpr std::uint32_t kAudioOpen = 0x029d8360;           // non-zero when the audio device is up
constexpr std::uint32_t kCurrentLevel = 0x0056a898;
constexpr std::uint32_t kLevelRecords = 0x0053c240;        // 0x1ac bytes per level: +0 a byte (the crowd roars at high scores), +0x3c a dword (the level has a crowd)
constexpr std::uint32_t kLevelRecordSize = 0x1ac;
constexpr std::uint32_t kModeA = 0x00533f38;               // the game type words the crowd and the sound front read (1 and 2 are the values they test)
constexpr std::uint32_t kModeB = 0x00533f3c;
constexpr std::uint32_t kSplitPlayer = 0x0056a958;         // the second player's object in a split screen game (+0x2cc0 its index)
constexpr std::uint32_t kPlayerFlags = 0x0053a514;         // 0x170 bytes per player: bit 0 set when the player's sounds are the other set
constexpr std::uint32_t kPlayerFlagsSize = 0x170;
constexpr std::uint32_t kReverbState = 0x0056c058;

// the crowd
constexpr std::uint32_t kCrowdVoice = 0x00560fa8;          // the crowd's looping voice (-1: none)
constexpr std::uint32_t kCrowdLeft = 0x00560fac;           // its current and target levels
constexpr std::uint32_t kCrowdRight = 0x00560fb0;
constexpr std::uint32_t kCrowdTargetLeft = 0x00560fb4;
constexpr std::uint32_t kCrowdTargetRight = 0x00560fb8;
constexpr std::uint32_t kCrowdCheers = 0x00560fc0;         // how many cheers are waiting to turn to boos

// the VAB names
constexpr std::uint32_t kFrontendVab = 0x00537928;         // the open front end bank (-1: none), the level bank
constexpr std::uint32_t kLevelVab = 0x0053792c;

// the redbook sequencer
constexpr std::uint32_t kXaVolume = 0x0053778c;            // the music fade (0xff full)
constexpr std::uint32_t kXaFading = 0x0056b898;
constexpr std::uint32_t kTrackLengths = 0x0053770c;        // 32 dwords: the sector length of each (group * 8 + track), -1: none
constexpr std::uint32_t kGroupOffsets = 0x005376fc;        // 4 dwords: the sector offset of each group
constexpr std::uint32_t kXaBase = 0x0056b874;
constexpr std::uint32_t kXaStart = 0x0056b868;
constexpr std::uint32_t kXaEnd = 0x0056b864;
constexpr std::uint32_t kXaDelay = 0x0056b878;             // frames to wait before the next track
constexpr std::uint32_t kXaPaused = 0x0056b880;
constexpr std::uint32_t kXaStopped0 = 0x0056b87c;
constexpr std::uint32_t kXaStopped1 = 0x0056b87d;
constexpr std::uint32_t kXaEnabled = 0x0056b884;
constexpr std::uint32_t kXaGroup = 0x0056b888;
constexpr std::uint32_t kXaTrack = 0x0056b88c;
constexpr std::uint32_t kXaWaiting = 0x0056b890;
constexpr std::uint32_t kXaPlaying = 0x0056b894;
constexpr std::uint32_t kMuteA = 0x005615d0;
constexpr std::uint32_t kMuteB = 0x005615dc;
constexpr std::uint32_t kMuteC = 0x00561dc8;
constexpr std::uint32_t kMuteTable = 0x0056d9d5;           // 17 bytes per entry, indexed by 0x0056db30
constexpr std::uint32_t kMuteIndex = 0x0056db30;
constexpr std::uint32_t kPauseFlag = 0x0056a880;           // non-zero while the game is paused
constexpr std::uint32_t kSavedPosition = 0x005616f0;       // the saved music position (group, track, start)

// the game FMV list (36-byte rows from the pointer at 0x0052d4a8) and the movie interface
constexpr std::uint32_t kMovieTable = 0x0052d4a8;
constexpr std::uint32_t kMovieRows = 0x0052cfa0;           // the rows themselves (type word at +2)
constexpr std::uint32_t kMovieCurrent = 0x0052d4dc;
constexpr std::uint32_t kMovieCount = 0x00561ddc;
constexpr std::uint32_t kMovieIndex = 0x00561d36;
constexpr std::uint32_t kMovieActive = 0x00561dc8;
constexpr std::uint32_t kMoviePlaying = 0x006a6ce0;        // a movie or the music thread is running
constexpr std::uint32_t kMovieStopping = 0x006a6ce2;       // the music thread was asked to stop (pause)
constexpr std::uint32_t kMovieHandle = 0x006a6ce4;         // the Bink handle
constexpr std::uint32_t kMovieThreadFlag = 0x0054b0a8;     // the music thread must end
constexpr std::uint32_t kMovieWindow = 0x006a6d08;

}  // namespace audio

class AudioGame final : public PcKit {
public:
    AudioGame(RetailMemory& memory, RetailServices& services) : PcKit(memory, services) {}

    // ---- crowd.cpp ----
    void crowd_clear_sound() { guest::write_scalar<std::uint32_t>(m_, audio::kCrowdVoice, 0xffffffffu); }                  // 0x0042f2e0
    void crowd_start();                                                                       // 0x0042f300
    void crowd_stop();                                                                        // 0x0042f370
    bool crowd_move_towards(std::uint32_t value, std::int32_t target, std::int32_t step);    // 0x0042f3e0
    void crowd_update();                                                                      // 0x0042f430
    void crowd_react_to_score(std::int32_t score);                                            // 0x0042f540
    void crowd_react_to_bail(std::int32_t score);                                             // 0x0042f680
    void crowd_react_to_trick(std::int32_t score);                                            // 0x0042f700
    void crowd_boo(std::int32_t strength);                                                    // 0x0042f800

    // ---- redbook.cpp ----
    void redbook_reset_volume();                                                              // 0x004ab9c0
    void redbook_update_volume();                                                             // 0x004ab9e0
    void redbook_reset();                                                                     // 0x004aba90
    bool redbook_play(std::int32_t group, std::int32_t track);                                // 0x004abb00
    void redbook_stop();                                                                      // 0x004abc80
    void redbook_next(std::int32_t step);                                                     // 0x004abcb0
    void redbook_set_paused(std::uint32_t paused);                                            // 0x004abe20
    void redbook_update();                                                                    // 0x004abe50
    bool redbook_can_continue();                                                              // 0x004abed0
    void redbook_save_position(std::uint32_t out);                                            // 0x004abf10
    void redbook_restore_position(std::uint32_t saved);                                       // 0x004abf80
    std::int32_t redbook_track_number();                                                      // 0x004ac010

    // ---- sfx.cpp and pc\soundlib.cpp ----
    void close_frontend_vab();                                                                // 0x004acff0
    void open_frontend_vab(std::uint32_t name);                                               // 0x004ad020
    void open_level_vab(std::uint32_t name);                                                  // 0x004ad180
    void close_level_vab();                                                                   // 0x004ad5b0
    std::int32_t sfx_play(std::int32_t sound, std::int32_t volume, std::int32_t extra);       // 0x004ad620 (SFX_PlayX)
    std::int32_t sfx_play_lr(std::int32_t sound, std::int32_t left, std::int32_t right, std::int32_t extra);   // 0x004ad750
    std::int32_t play_positional_sound(std::int32_t sound, std::uint32_t position, std::int32_t extra);       // 0x004ad7d0
    std::uint32_t voice_frequency(std::int32_t voice);                                        // 0x004ad870
    void set_voice_frequency(std::int32_t voice, std::uint32_t frequency);                    // 0x004ad890
    void modify_position(std::int32_t voice, std::uint32_t position);                         // 0x004ad8b0
    void update_positional_sound(std::int32_t voice, std::int32_t left, std::int32_t right);  // 0x004ad950
    void clear_sound_flag() { guest::write_scalar<std::uint8_t>(m_, 0x0056c4e8, 0); }                                       // 0x004ad9b0
    void sfx_pause();                                                                         // 0x004ad9c0
    void sfx_resume();                                                                        // 0x004ad9d0
    void stop_voice(std::int32_t voice);                                                      // 0x004ad9f0
    void stop_all_and_close();                                                                // 0x004ada10
    void set_reverb(std::uint32_t type);                                                      // 0x004ada20
    std::uint32_t set_reverb_depth(std::uint32_t left, std::uint32_t right);                  // 0x004adba0
    void stop_all_sounds();                                                                   // 0x004f38d0 (Audio_StopAll)

    // ---- gamefmv.cpp and pc\pcmovie.cpp ----
    void change_fmv_track(std::int32_t delta);                                                // 0x00454d30
    std::uint32_t movie_name(std::uint32_t index) { return guest::read_scalar<std::uint32_t>(m_, guest::read_scalar<std::uint32_t>(m_, audio::kMovieTable) + 36 * index); }   // 0x004e7070
    std::uint32_t play_game_fmv();                                                            // 0x004e7090
    void start_fmv();                                                                         // 0x004e70e0
    void stop_fmv();                                                                          // 0x004e7140
    void movie_update();                                                                      // 0x004e71c0
    void movie_pause(std::uint32_t paused);                                                   // 0x004e6e70
    std::uint32_t start_music(std::uint32_t name);                                            // 0x004e7260
    void set_xa_volume(std::int32_t left, std::int32_t right);                                // 0x004e72d0
    void xa_play(std::int32_t group, std::int32_t track);                                     // 0x004e7310
    void xa_stop();                                                                           // 0x004e7370
    bool xa_done() { return guest::read_scalar<std::uint8_t>(m_, audio::kMoviePlaying) == 0; }                               // 0x004e73e0
    void draw_movie_window(std::uint32_t rect);                                               // 0x004e6f00
    std::uint32_t play_startup_media(std::uint32_t name);                                     // 0x004e5ec0

private:
    std::int32_t rnd(std::int32_t limit) {
        s_.note("rnd", {static_cast<std::uint32_t>(limit)});
        return s_.value("rnd");
    }
    std::uint32_t level_record() { return audio::kLevelRecords + audio::kLevelRecordSize * guest::read_scalar<std::uint32_t>(m_, audio::kCurrentLevel); }
    std::uint32_t bank_record(std::int32_t sound) { return guest::read_scalar<std::uint32_t>(m_, audio::kBankTable + 4u * static_cast<std::uint32_t>(sound)); }
    void store_pitch(std::uint32_t record);
    // a Win32 or Bink import: reported as win32_<name> and answered from the script
    std::uint32_t win32(const std::string& name, std::vector<std::uint32_t> args) {
        s_.note("win32_" + name, std::move(args));
        return static_cast<std::uint32_t>(s_.value("win32_" + name));
    }
    void stop_movie_or_music(bool close_when_idle);
    std::uint32_t start_music_text(const std::string& text, std::uint32_t name_argument);
};

}  // namespace opentony::runtime::pcplatform
