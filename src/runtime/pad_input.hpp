#pragma once

#include "pc_platform.hpp"
#include "guest_view.hpp"
#include "seam_addresses_generated.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace opentony::runtime::pcplatform {

// Phase 4.13a: the pad layer (pad.cpp) and the PC keyboard / joystick binding code (pc\pcinput.cpp) on the game's globals.
//
// The control state is two 0x16c-byte player blocks from 0x0056aff8: twenty 16-byte action records first (+0 held, +1 pressed-latch, +4 frames held, +8 frames released, +0xc frames
// since the last press), then the rumble records (+0x154..), the table of four record pointers at +0x15c that Pad_Remap and Pad_Restore shuffle, and the analog bytes at 0x0056b140.
// The PC layer keeps the keyboard / joystick bindings as 14 pointers at 0x0054adcc (to key-code chords of up to three bytes), the joystick button numbers at 0x0054ae04, and builds the
// pending button mask at 0x006a3f1c; the DirectInput wrapper, the PSX pad library and the HAL's rumble are services.
namespace pad {

constexpr std::uint32_t kStates = 0x0056aff8;             // the first player's block; the second's is kStride further
constexpr std::uint32_t kStride = 0x16c;
constexpr std::uint32_t kPlayers = 2;
constexpr std::uint32_t kRecordCount = 20;
constexpr std::uint32_t kRecordSize = 0x10;
constexpr std::uint32_t kPointerTable = 0x15c;            // within a block: four pointers to the records Pad_Remap moves (+0x40, +0x50, +0x60, +0x70)
constexpr std::uint32_t kRumble = 0x0056b14c;             // +0x154 of block 0: motor levels (bytes) and durations (words)
constexpr std::uint32_t kAnalog = 0x0056b140;             // block 0's four analog bytes (read as the stick axes)
constexpr std::uint32_t kInitialised = 0x0056b2e8;
constexpr std::uint32_t kIdleFrames = 0x0056b2e4;         // cleared by every press and by an analog value outside the dead zone (the attract-mode timer)
constexpr std::uint32_t kPadType = 0x0056b144;            // the pad type bytes the PC layer stores for each player (0x73 's' analog, 0x53 'S', 0x41 'A')
constexpr std::uint32_t kPadTypes = 0x0054ace4;           // the PC layer's source: the types of the two pads, one per byte
constexpr std::uint32_t kPadRegisters = 0x0056afb9;       // +0x20 per pad: the type byte at +0, the four stick samples at +4..+7
constexpr std::uint32_t kPadRegisterStride = 0x20;
constexpr std::uint32_t kMotorLimit = 0x0056a89c;         // non-zero: no rumble at all
constexpr std::uint32_t kPlayerCount = 0x0056a88c;        // non-zero in the two-player games
constexpr std::uint32_t kGameMode = 0x00533f3c;           // 1: the head-to-head mode
constexpr std::uint32_t kModeSelector = 0x00533f38;       // 7: pad 0 drives both ports
constexpr std::uint32_t kDemoTimer = 0x00466b60;          // ResetMainDemoTimer

constexpr std::uint32_t kMsgNumpads = 0x00535f70;
constexpr std::uint32_t kMsgAlreadyInitialised = 0x00535f4c;
constexpr std::uint32_t kMsgBadIndex = 0x00534e9c;
constexpr std::uint32_t kMsgBadMotor = 0x00535fa0;

}  // namespace pad

namespace pcinput {

constexpr std::uint32_t kActionMask = 0x006a3f1c;         // two words: the pending masks of port 0 and port 1
constexpr std::uint32_t kKeyboard = 0x006a43e0;           // the DirectInput keyboard wrapper; +4 the 256 key bytes (bit 7 = down)
constexpr std::uint32_t kKeyState = 0x006a43e4;
constexpr std::uint32_t kJoystick = 0x006a42e8;           // the joystick wrapper: +4 / +8 the axes, +0x6c the button flags
constexpr std::uint32_t kJoystickAcquired = 0x006a43d4;
constexpr std::uint32_t kButtonFlags = 0x006a4354;
constexpr std::uint32_t kJoystickEnabled = 0x0054ace0;
constexpr std::uint32_t kSlots = 0x0054adcc;              // the 14 binding pointers
constexpr std::uint32_t kSlotCount = 14;
// the joystick button number of each action (-1: none), named as the JOYPAD entries of the options file are; the action bit each one sets in the mask is in build_action_mask
constexpr std::uint32_t kJoyJump = 0x0054ae04;
constexpr std::uint32_t kJoyGrind = 0x0054ae08;
constexpr std::uint32_t kJoyGrab = 0x0054ae0c;
constexpr std::uint32_t kJoyKick = 0x006a6984;
constexpr std::uint32_t kJoySpinLeft = 0x0054ae10;
constexpr std::uint32_t kJoySpinRight = 0x0054ae14;
constexpr std::uint32_t kJoyNollie = 0x0054ae18;
constexpr std::uint32_t kJoySwitch = 0x0054ae1c;
constexpr std::uint32_t kJoyPause = 0x0054ae20;
constexpr std::uint32_t kJoyCamera = 0x0054ae24;
constexpr std::uint32_t kDefaultSlots = 0x0054ace8;       // the 14 default chord dwords (copied by ResetBindings)
constexpr std::uint32_t kCurrentSlots = 0x0054ad94;       // mode 0's chords (the other modes' sit at 0x0054ad20 and 0x0054ad5c)
constexpr std::uint32_t kControlMode = 0x006a6964;
constexpr std::uint32_t kPendingFrames = 0x006a6970;
constexpr std::uint32_t kPendingWord = 0x006a696c;
constexpr std::uint32_t kPauseLatch = 0x006a6988;
constexpr std::uint32_t kTouchedFlag = 0x00561c04;
constexpr std::uint32_t kPortSelect = 0x006a3d28;
constexpr std::uint32_t kPlayerPort = 0x0056a888;
constexpr std::uint32_t kMouseEnabled = 0x006a1400;
constexpr std::uint32_t kMouseButton = 0x006a44f8;
constexpr std::uint32_t kMousePosition = 0x006a44f0;
constexpr std::uint32_t kKeyIsDown = seam::hal::SDXKeyboard_KeyIsDown;          // the wrapper's chord test (thiscall on kKeyboard)
constexpr std::uint32_t kMouseTracker = 0x004dbee0;

}  // namespace pcinput

class PadInput final : public PcKit {
public:
    PadInput(RetailMemory& memory, RetailServices& services) : PcKit(memory, services) {}

    // ---- pad.cpp ----
    void init_at_start();                                                                          // 0x004894a0
    void clear_all_one(std::uint32_t player);                                                       // 0x004895c0
    void clear_all();                                                                              // 0x00489610
    void clear(std::uint32_t block);                                                                // 0x00489630
    void clear_triggers(std::uint32_t block);                                                       // 0x00489670
    std::uint32_t nothing() { return 0; }                                                          // 0x00489690
    void actuator_on(std::uint32_t port, std::uint32_t strength, std::uint32_t motor, std::uint32_t duration);   // 0x004896a0
    void actuator_off(std::uint32_t port, std::uint32_t motor);                                     // 0x00489850
    void advance_action_state(std::uint32_t record, std::uint32_t pressed);                         // 0x00489930
    std::int32_t analog(std::int32_t value, std::int32_t low, std::int32_t high, std::int32_t middle);   // 0x00489990
    std::uint32_t update_action_states();                                                           // 0x00489a10
    void restore();                                                                                // 0x00489cd0
    void remap();                                                                                  // 0x00489e70

    // ---- pc\pcinput.cpp: the small ones ----
    void reset_bindings();                                                                         // 0x004e3e80
    void set_control_mode(std::uint32_t mode);                                                      // 0x004e3f80
    void set_pending_buttons(std::uint32_t buttons);                                                // 0x004e4190
    std::uint32_t key_down(std::uint32_t code);                                                     // 0x004e41b0
    std::int32_t first_key_down(std::int32_t start);                                                // 0x004e41d0
    std::int32_t key_char(std::uint32_t code);                                                      // 0x004e4230
    std::uint32_t key_name(std::uint32_t code);                                                     // 0x004e4250
    std::int32_t first_joystick_button();                                                           // 0x004e4260
    void clear_pad_triggers();                                                                     // 0x004e4290
    void enable_joystick_buttons(std::uint32_t enabled);                                            // 0x004e42b0
    void build_action_mask();                                                                      // 0x004e42c0
    std::uint32_t get_action_mask();                                                                // 0x004e4650
    void mouse_button_message(std::uint32_t message);                                               // 0x004e4bf0
    void load_bindings();                                                                          // 0x004e4d10
    void save_bindings();                                                                          // 0x004e5460

private:
    std::string key_name_text(std::uint32_t code) { return m_.cstring(guest::read_scalar<std::uint32_t>(m_, 0x0052eff0 + 4 * (code & 0xff)), 0x40); }
    // the text a chord (up to three key codes) is saved as: "Left", "Left,Right", "Left,Right,Up"
    bool chord_text_into(std::uint32_t chord, std::string& text);
    struct Record {
        std::uint32_t w[4];
    };
    Record read_record(std::uint32_t address) const;
    void write_record(std::uint32_t address, const Record& record);
    void swap_records(std::uint32_t a, std::uint32_t b);
    bool chord_down(std::uint32_t slot);
};

}  // namespace opentony::runtime::pcplatform
