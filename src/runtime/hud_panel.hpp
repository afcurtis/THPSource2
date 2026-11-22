#pragma once

#include "retail_memory.hpp"
#include "guest_view.hpp"
#include "retail_services.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace opentony::runtime::hud {

// Phase 4.10b: the in-game HUD (panel.cpp, 0x0048ad50..0x0048e9c0) and the trick score stack, from the disassembly.
//
// Every skater object embeds a CPanelData at skater + 0x168 (`panel`): its first word points back at the skater, and
// it owns the score, the trick stack and the text objects the HUD draws with. All offsets below are from `panel`
// unless a name says `kSk`.
//
// The trick stack (the tricks of the combo being built) is up to 200 entries of 0x28 bytes at panel + 0x7b0, with the
// entry count at panel + 0x26f0:
//   +0x00 trick name (a string pointer)         +0x04 points (the point timer adds to the newest entry)
//   +0x08 a countdown (0x12 when pushed)        +0x0c the landing quality when the entry was closed (0 until then)
//   +0x10 kind (1 = a trick that ends a chain, 2 = a spin / gap entry)   +0x14 the caller's flag (0 = not repeatable)
//   +0x18 how many times the trick has been done (the trick history, see below)   +0x1c 1 when done switch
//   +0x20 the caller's extra word               +0x24 the message node showing the entry (0 until shown)
// The "trick history" (skater + 0x3214, a count then 8-byte hash/count pairs) belongs to another unit and is
// reached as a service.
//
// A function that reaches anything outside this file (the career, replay recording, sound, the message layer, the
// font and its text effects, the engine's sprites) reports the reach as a named call (RetailServices) and takes its
// answer from the scripted values, exactly as the 4.8 to 4.10a ports do. A virtual method call is reported by its
// class (effect_*, font_*, sprite_*) with the object first.

// ---- panel offsets ----
constexpr std::uint32_t kOwner = 0x000;
constexpr std::uint32_t kScore = 0x004;           // the total the HUD shows (skater + 0x16c)
constexpr std::uint32_t kTrickCount = 0x00a;      // u16: tricks landed so far
constexpr std::uint32_t kBailCount = 0x00c;       // u16: tricks bailed
constexpr std::uint32_t kBestStackCount = 0x00e;  // u16: the number of tricks in the best stack
constexpr std::uint32_t kBestScore = 0x010;       // the best single combo
constexpr std::uint32_t kBestNames = 0x014;       // three 0x64-byte strings: the best combo's trick names
constexpr std::uint32_t kBestNameStride = 0x64;
constexpr std::uint32_t kComboTotal = 0x140;      // points of the combo in progress, summed over landed stacks
constexpr std::uint32_t kLandedValue = 0x144;     // the value the last landing committed
constexpr std::uint32_t kLandedAux = 0x148;
constexpr std::uint32_t kStackShown = 0x14c;      // the stack's text is showing (set by the refresh)
constexpr std::uint32_t kZoomPending = 0x15c;     // a zoom effect was started and the falling effect is to follow
constexpr std::uint32_t kStackBase = 0x7b0;
constexpr std::uint32_t kEntrySize = 0x28;
constexpr std::uint32_t kStackCount = 0x26f0;
constexpr std::uint32_t kStackSlots = 200;
constexpr std::uint32_t kTextNames = 0x26f4;      // three 0x64-byte strings: the stack's names as drawn
constexpr std::uint32_t kStackTimer = 0x2844;
constexpr std::uint32_t kStackClock = 0x2824;

constexpr std::uint32_t kEntName = 0x00;
constexpr std::uint32_t kEntPoints = 0x04;
constexpr std::uint32_t kEntCountdown = 0x08;
constexpr std::uint32_t kEntValue = 0x0c;
constexpr std::uint32_t kEntKind = 0x10;
constexpr std::uint32_t kEntFlag = 0x14;
constexpr std::uint32_t kEntHistory = 0x18;
constexpr std::uint32_t kEntSwitch = 0x1c;
constexpr std::uint32_t kEntExtra = 0x20;
constexpr std::uint32_t kEntNode = 0x24;

// ---- skater offsets the panel code reads ----
constexpr std::uint32_t kPanelInSkater = 0x168;
constexpr std::uint32_t kSkScore = 0x16c;
constexpr std::uint32_t kSkPlayerNum = 0x170;
constexpr std::uint32_t kSkLandedTotal = 0x2a8;
constexpr std::uint32_t kSkLandedLast = 0x2ac;
constexpr std::uint32_t kSkStackCountMirror = 0x2858;   // panel + kStackCount
constexpr std::uint32_t kSkStackBase = 0x8f4;           // panel + kStackBase + kEntSize + kEntPoints (the newest entry's points: + count * 0x28)
constexpr std::uint32_t kSkPointTimer = 0x2f54;
constexpr std::uint32_t kSkPointLimit = 0x2f58;
constexpr std::uint32_t kSkGapRecord = 0x3024;
constexpr std::uint32_t kSkGoalGapRecord = 0x3028;
constexpr std::uint32_t kSkLandQuality = 0x306c;
constexpr std::uint32_t kSkPerfect = 0x3078;
constexpr std::uint32_t kSkSloppy = 0x307c;
constexpr std::uint32_t kSkState = 0x30b8;
constexpr std::uint32_t kSkPrevState = 0x30c0;

// ---- globals ----
constexpr std::uint32_t kGameMode = 0x00533f38;          // LevelEventGameModeSelector
constexpr std::uint32_t kNumPlayers = 0x00533f3c;        // GNumberOfPlayers
constexpr std::uint32_t kSimulationTime = 0x0056e320;
constexpr std::uint32_t kAnimationClock = 0x005685f4;
constexpr std::uint32_t kCurrentLevel = 0x0056a898;
constexpr std::uint32_t kCareerFlag = 0x0056a470;        // DAT_0056a470: career records are being kept
constexpr std::uint32_t kPointDelta = 0x0056a93c;        // the point timer's step per frame (Q12)
constexpr std::uint32_t kLevelGoalTable = 0x0053c2b0;    // per level 0x1ac bytes: ten goals of 0x18 bytes (kind at +0)
constexpr std::uint32_t kLevelGoalStride = 0x1ac;
constexpr std::uint32_t kScoreMultTable = 0x00536168;    // %: the repeat multipliers (5 entries)
constexpr std::uint32_t kChainMultTable = 0x0053617c;    // chain length multipliers (6 entries)
constexpr std::uint32_t kChainMultBase = 0x00536190;
constexpr std::uint32_t kStackMultTable = 0x00536194;    // stack length multipliers (15 entries)
constexpr std::uint32_t kStackMultBase = 0x005361cc;
constexpr std::uint32_t kTenfoldFlag = 0x0056b760;       // DAT_0056b760: tricks score ten times
constexpr std::uint32_t kViewportMode = 0x0056a928;      // ViewportMode: 0 full screen, 1 split
constexpr std::uint32_t kPerfectText = 0x0052e32c;       // pointers to the "Perfect" and "Sloppy" strings
constexpr std::uint32_t kSloppyText = 0x0052e330;

// ---- more globals the drawing code reads ----
constexpr std::uint32_t kScreenHeight = 0x00527d54;
constexpr std::uint32_t kScreenWidthScale = 0x029da394;  // ScreenWidth
constexpr std::uint32_t kScreenHeightScale = 0x029da398; // ScreenHeight
constexpr std::uint32_t kArenaCursor = 0x00560fd0;       // RenderPolygonArenaCursor
constexpr std::uint32_t kArenaEnd = 0x00568628;
constexpr std::uint32_t kRenderState = 0x00560fd4;       // pointer to the render state; +0x84 the bucket heads
constexpr std::uint32_t kReplayState = 0x0056e5d8;       // ReplayPlaybackState0 (2: a replay is playing)
constexpr std::uint32_t kAnimationScale = 0x0056865c;
constexpr std::uint32_t kPlayerOne = 0x0056a960;         // the player-one skater
constexpr std::uint32_t kHudEnabled = 0x00533f9c;
constexpr std::uint32_t kPausedHud = 0x00561c04;
constexpr std::uint32_t kGameStatus = 0x00561bfc;        // GStatus
constexpr std::uint32_t kVersusState = 0x0056db64;       // LevelEventVersusStateSelector
constexpr std::uint32_t kVersusIt = 0x0056db5c;          // the skater who is "it"
constexpr std::uint32_t kNetActive = 0x006a3b21;         // byte: a network game is running
constexpr std::uint32_t kNetLocalPlayer = 0x006a3d2c;
constexpr std::uint32_t kCountdownMode = 0x029da3a4;     // byte: draw the holding timer as a count up
constexpr std::uint32_t kSpriteDisplay = 0x0056b73c;     // the engine's sprites the score and the icons draw with
constexpr std::uint32_t kSpriteScoreBack = 0x0056b710;
constexpr std::uint32_t kSpriteSwitch = 0x0056b744;
constexpr std::uint32_t kSpriteIcon8 = 0x0056b748;
constexpr std::uint32_t kSpriteIcon9 = 0x0056b74c;
constexpr std::uint32_t kSpriteIcon10 = 0x0056b750;
constexpr std::uint32_t kHoldingFlash = 0x0056b740;
constexpr std::uint32_t kTapeTimerB = 0x0056b758;
constexpr std::uint32_t kTapeTimerA = 0x0056b75c;
constexpr std::uint32_t kTapeSlideA = 0x005361d8;
constexpr std::uint32_t kTapeSlideB = 0x005361d4;
constexpr std::uint32_t kReplayHintLimit = 0x005361d0;
constexpr std::uint32_t kReplayFrames = 0x0056e5f8;
constexpr std::uint32_t kReplayFrame = 0x0056e5fc;
constexpr std::uint32_t kReplayStep = 0x0056e628;
constexpr std::uint32_t kReplayVideoRestart = 0x00561990;
constexpr std::uint32_t kLevelMessages = 0x0053c3a0;     // per level 0x1ac bytes: a pointer to that level's message list
constexpr std::uint32_t kLevelBytes = 0x0053c240;        // per level 0x1ac bytes: the first byte is "goals done"
constexpr std::uint32_t kReplayHintLine1 = 0x0052e5e8;   // pointers to the replay hint texts
constexpr std::uint32_t kReplayHintLine2 = 0x0052e5e4;
constexpr std::uint32_t kStealsBlue = 0x0052e5b4;
constexpr std::uint32_t kStealsRed = 0x0052e5b8;
constexpr std::uint32_t kSkateLetters = 0x005194e4;      // the letters S K A T E (one byte string each, 4 bytes apart)
constexpr std::uint32_t kHorseWords = 0x00568a60;
constexpr std::uint32_t kCollectBuffer = 0x0056b714;     // the "%d OF %d %s" message text

// ---- services (named calls) ----
constexpr const char* kSvcPrintfLog = "printf_log";                 // (format)
constexpr const char* kSvcAwardTrickGap = "career_award_trick_gap"; // Career_AwardTrickGap(gap record)
constexpr const char* kSvcAwardGoalGap = "career_award_goal_gap";   // Career_AwardGoalGap(goal gap record)
constexpr const char* kSvcHighlightStart = "replay_highlight_start";// Replay_RecordHighlightStart(skater)
constexpr const char* kSvcHighlightEnd = "replay_highlight_end";    // Replay_RecordHighlightEnd(skater, value)
constexpr const char* kSvcFindNoCase = "string_find_nocase";        // String_FindNoCase(haystack, needle) -> value (scripted); text is the haystack
constexpr const char* kSvcHistoryReset = "trick_history_reset";     // (skater)
constexpr const char* kSvcHistoryAdd = "trick_history_add";         // (skater, name, key) -> count (scripted)
constexpr const char* kSvcHistoryRemove = "trick_history_remove";   // (skater, name, key)
constexpr const char* kSvcSfx = "sfx_play";                         // SFX_PlayX(id, volume, 0)
constexpr const char* kSvcProfileValue = "skater_profile_value";    // Skater_ProfileValue(skater, 8) -> value (scripted)
constexpr const char* kSvcBail = "skater_bail";                     // CBruce__Bail(player, 0)
constexpr const char* kSvcEffectFinished = "effect_finished";       // zoom effect virtual +0x1c (object) -> value (scripted)
constexpr const char* kSvcZoomStart = "zoom_start";                 // ZoomTextEffect_Start(object)
constexpr const char* kSvcResetTrickBuffers = "trick_reset_buffers";// CBruce__ResetTrickBuffers(skater)
constexpr const char* kSvcTextWidth = "font_text_width";            // Font_TextWidth(font, text) -> pixels (scripted); text is the string
constexpr const char* kSvcTextExtent = "font_text_extent";          // Font_TextExtent(font, text) -> pixels (scripted); text is the string
constexpr const char* kSvcCheat = "career_get_cheat";               // Career_GetCheat(index) -> value (scripted)
constexpr const char* kSvcCheckScore = "career_check_score";        // Career_CheckScore()
constexpr const char* kSvcGiveGoal = "career_give_goal";            // Career_GiveGoal(index)
constexpr const char* kSvcGiveGoalType = "career_give_goal_type";   // Career_GiveGoalType(type)
constexpr const char* kSvcGoalIndex = "career_goal_index";          // Career_GoalIndex(type) -> index (scripted)
constexpr const char* kSvcHorseWords = "horse_display_words";       // Horse_DisplayWords(object)
constexpr const char* kSvcMessRgb = "mess_set_rgb";                 // Mess_SetRGB(r, g, b, 0)
constexpr const char* kSvcMessJustify = "mess_set_justify";         // Mess_SetTextJustify(mode)
constexpr const char* kSvcMessScale = "mess_set_scale";             // Mess_SetScale(scale)
constexpr const char* kSvcMessDraw = "mess_draw_panel_text";        // Mess_DrawPanelText(x, y, text, 0); text is the string
constexpr const char* kSvcMessDeleteByText = "mess_delete_by_text"; // Mess_DeleteByText(text); text is the string
constexpr const char* kSvcMessAdd = "mess_add_message";             // Mess_AddMessage(text, program, stacked, x, y); text is the string
constexpr const char* kSvcFontRgb = "font_set_rgb";                 // Font_SetRgb(font, r, g, b)
constexpr const char* kSvcFontDraw = "font_draw";                   // the font's draw method (font, x, y); the string is the text
constexpr const char* kSvcEffectBind = "effect_bind";               // effect virtual +4 (effect, font)
constexpr const char* kSvcEffectUnbind = "effect_unbind";           // effect virtual +8 (effect, font)
constexpr const char* kSvcEffectInit = "effect_init";               // effect virtual +0xc (effect)
constexpr const char* kSvcEffectUpdate = "effect_update";           // effect virtual +0x10 (effect)
constexpr const char* kSvcClipClear = "falling_clear_clip";         // FallingTextEffect_ClearClip(effect)
constexpr const char* kSvcClipClearFor = "falling_clear_clip_for";  // FallingTextEffect_ClearClipFor(viewport)
constexpr const char* kSvcBloodUpdate = "falling_blood_update";     // FallingTextEffect_BloodUpdate(effect)
constexpr const char* kSvcSetBlood = "falling_set_blood";           // FallingTextEffect_SetBlood(effect, value)
constexpr const char* kSvcSetScale = "falling_set_scale";           // FallingTextEffect_SetScale(effect, scale)
constexpr const char* kSvcSpriteDraw = "sprite_draw";               // a sprite's draw method (sprite, x, y, argument)
constexpr const char* kSvcSpriteDelete = "virtual_delete";          // a sprite's deleting destructor (sprite, 1)
constexpr const char* kSvcTrickTips = "trick_tips_display";         // TrickTips_Display()
constexpr const char* kSvcVersusUpdate = "versus_update";           // the versus-mode update (0x004b8b40)
constexpr const char* kSvcRnd = "rnd";                              // Rnd(limit) -> value (scripted)
constexpr const char* kSvcFontConstruct = "font_construct";         // the font constructor (font)
constexpr const char* kSvcFallingConstruct = "falling_construct";   // FallingTextEffect_Construct(effect)
constexpr const char* kSvcZoomConstruct = "zoom_construct";         // ZoomTextEffect_Construct(effect)
constexpr const char* kSvcFontFind = "font_find";                   // Font_Find(name) -> Font object (scripted); name is the text
constexpr const char* kSvcFontMapping = "font_set_mapping";         // Font_SetMapping(font, mapping)
constexpr const char* kSvcFontLoad = "font_load";                   // Font_Load(name); name is the text
constexpr const char* kSvcFontUnload = "font_unload";               // Font_Unload(name); name is the text
constexpr const char* kSvcMessLoadFont = "mess_load_font";          // Mess_LoadFont(name, 2, 2, -1) -> font id (scripted); name is the text
constexpr const char* kSvcMessUnloadFont = "mess_unload_font";      // Mess_UnloadFont()
constexpr const char* kSvcMessSetFont = "mess_set_current_font";    // Mess_SetCurrentFont(font)
constexpr const char* kSvcOperatorNew = "operator_new";             // operator new(size) -> memory (scripted)
constexpr const char* kSvcSpriteConstruct = "sprite_construct";     // the sprite constructor (memory, name, 0x40, 0x40, 0) -> sprite (scripted)
constexpr const char* kSvcSpriteInit = "sprite_init";               // (sprite)
constexpr const char* kSvcCreateBloodSprite = "falling_create_sprite";   // FallingTextEffect_CreateSprite()
constexpr const char* kSvcDestroyBloodSprite = "falling_destroy_sprite"; // FallingTextEffect_DestroySprite()
constexpr const char* kSvcPrepare = "panel_prepare";                // 0x00466a00 (param)
constexpr const char* kSvcApplyGap = "gap_apply_definition_state";  // Gap_ApplyDefinitionState(skater, name, value, 0, -1); name is the text
constexpr const char* kSvcActivateTrickObject = "trick_object_activate"; // TrickObject_ActivateForObject(object, skater, landed total)
constexpr const char* kSvcLandedTotal = "career_score_landed";      // 0x0042f540(landed total)
constexpr std::uint32_t kNameOllie = 0x00536ad0;                    // "OLLIE"
constexpr std::uint32_t kNameTimeFont = 0x00536254;                 // "time.fnt"
constexpr std::uint32_t kNameScoreFont = 0x00536248;                // "score.fnt"
constexpr std::uint32_t kNameTricksFont = 0x005254e0;               // "s2tricks.fnt"
constexpr std::uint32_t kMsgSpriteNotEmpty = 0x00536214;            // "Sprite should be empty"
constexpr std::uint32_t kMsgSpriteMissing = 0x00536260;             // "Sprite does not exist"

// ---- assertion messages (format string addresses) ----
constexpr std::uint32_t kMsgTrickNotFound = 0x00536350;      // "Trick nout found in TotalTrickStore, in History"
constexpr std::uint32_t kMsgTweakNothing = 0x00536418;       // "Tweaking trick, but nothing on stack"
constexpr std::uint32_t kMsgOutOfSlots = 0x00536528;         // "out of trick queue slots"
constexpr std::uint32_t kMsgZeroPoints = 0x00536510;         // "Trick with zero points"
constexpr std::uint32_t kMsgGapNoMatch = 0x00536564;         // "Trick gap found %s, but no matching goal"
constexpr std::uint32_t kMsgNullGapName = 0x00536544;        // "NULL Trick name for trick gap"
constexpr std::uint32_t kFmtLanded = 0x00536590;             // "Landed, mpGapTrick = %s\n"
constexpr std::uint32_t kMsgRefreshEmpty = 0x005364b0;       // "Refreshing messages with nothing on stack"
constexpr std::uint32_t kMsgTempBuffer = 0x00536498;         // "TEMP BUFFER TOO SMALL"
constexpr std::uint32_t kMsgTooLong = 0x0053647c;            // "Too long"

class Panel final {
public:
    Panel(RetailMemory& memory, RetailServices& services) : m_(memory), s_(services) {}

    // ---- the score stack ----
    // 0x0048caf0 (this = panel): the sum of every entry's closing value.
    [[nodiscard]] std::int32_t sum_entry_values(std::uint32_t panel);
    // 0x0048cb60 (this = panel): the closing values from the chain start at or before `index` up to the next chain end.
    [[nodiscard]] std::int32_t chain_value(std::uint32_t panel, std::int32_t index);
    // 0x0048cb10 (this = panel): the chain value when `index` starts its chain, else 0; 0 for a spin entry.
    [[nodiscard]] std::int32_t chain_value_if_first(std::uint32_t panel, std::int32_t index);
    // 0x0048cbc0 (this = panel): the points of the stack after the repeat and chain multipliers, with the landing bonus.
    [[nodiscard]] std::int32_t compute_stack_sum(std::uint32_t panel);
    // 0x0048cd40 (this = panel): the stack's sum times the stack length multiplier, halved.
    [[nodiscard]] std::int32_t compute_landed_value(std::uint32_t panel);
    // 0x0048d960 (this = entry): fill a fresh entry.
    void init_entry(std::uint32_t entry, std::uint32_t node, std::uint32_t name, std::uint32_t points);
    // 0x0048e3b0 (this = panel): push a trick on the stack.
    void push_entry(std::uint32_t panel, std::uint32_t name, std::int32_t points, std::int32_t kind, std::uint32_t flag, std::uint32_t extra);
    // 0x0048d710 (skater): add `delta` to the newest entry's points and refresh the text.
    void adjust_current_entry(std::uint32_t skater, std::int32_t delta);
    // 0x0048ddc0 (this = panel): rebuild the text of the stack.
    void refresh_text(std::uint32_t panel);
    // 0x0048e320 (this = panel): show the stack's text and start its effect.
    void show_stack_text(std::uint32_t panel);
    // 0x0048e620 (this = panel): clear the countdown of every entry whose message has run its course.
    void expire_entries(std::uint32_t panel);
    // 0x0048e680 (this = panel): the skater landed: commit the stack to the totals. 0x0048d7a0 (skater) is the wrapper.
    void commit_landed_stack(std::uint32_t panel);
    // 0x0048e8a0 (this = panel): the skater bailed: take the stack back. 0x0048d7b0 (skater) is the wrapper.
    void rollback_stack(std::uint32_t panel);
    void commit_for_skater(std::uint32_t skater) { commit_landed_stack(skater + kPanelInSkater); }
    void bail_for_skater(std::uint32_t skater) { rollback_stack(skater + kPanelInSkater); }

    // ---- the display (panel.cpp) ----
    // 0x0048b430 / 0x0048b540: place the four corners of a quad from a frame record (x, y, polygon, frame); the first
    // scales the frame's x and width by 512/320. 0x0048b2c0: allocate a textured quad from the arena for a frame.
    void place_frame_quad_wide(std::int32_t x, std::int32_t y, std::uint32_t poly, std::uint32_t frame);
    void place_frame_quad(std::int32_t x, std::int32_t y, std::uint32_t poly, std::uint32_t frame);
    [[nodiscard]] std::uint32_t alloc_frame_primitive(std::uint32_t frame);
    // 0x0048b620 / 0x0048b760: a flat or gouraud shaded quad (x, y, w, h, colours, bucket) from the polygon arena.
    [[nodiscard]] std::uint32_t draw_flat_poly(std::int32_t x, std::int32_t y, std::int32_t w, std::int32_t h, std::uint32_t r,
                                               std::uint32_t g, std::uint32_t b, std::int32_t bucket);
    [[nodiscard]] std::uint32_t draw_gouraud_poly(std::int32_t x, std::int32_t y, std::int32_t w, std::int32_t h, const std::uint32_t colours[6],
                                                  std::int32_t bucket);
    // 0x0048b8e0: the S-K-A-T-E letters and the tape counter sliding in; 0x0048ba70: the colours of graffiti mode.
    void display_tapes();
    void graffiti_color(std::uint32_t skater);
    // 0x0048bae0 (skater, x, y): the special meter and the score. 0x0048bef0: the switch icon. 0x0048c0b0: the balance icon.
    void display_score(std::uint32_t skater, std::uint32_t x, std::uint32_t y);
    void draw_switch_icon(std::uint32_t skater, std::uint32_t x, std::uint32_t y);
    void draw_balance_icon(std::uint32_t skater, std::uint32_t x, std::uint32_t y);
    // 0x0048c1a0 (skater, x, y): the trick points and multiplier.
    void display_trick_points(std::uint32_t skater, std::uint32_t x, std::uint32_t y);
    // 0x0048c520 (skater, x, y): the timer.
    void display_holding(std::uint32_t skater, std::uint32_t x, std::uint32_t y);
    // 0x0048c760: the offsets of the text of viewport `index` (the two out values are 16-bit).
    void viewport_offset(std::uint32_t index, std::uint32_t x_out, std::uint32_t y_out);
    // 0x0048c8d0 (skater, x, y): the level message. 0x0048ca00: the replay instructions.
    void display_level_message(std::uint32_t skater, std::uint32_t x, std::uint32_t y);
    void replay_instructions();
    // 0x0048cd80 (this = panel; skater, x, y): the stack's text with its effect.
    void display_trick_names(std::uint32_t panel, std::uint32_t skater, std::uint32_t x, std::uint32_t y);
    // 0x0048d090: draw the whole HUD of every skater. 0x0048d040: an assertion that always fails (an obsolete mode).
    void display();
    void obsolete_viewport_mode();
    // 0x0048d660: tell a skater which player it is. 0x0048d6d0: start a trick: clear the gap records.
    void set_player_number(std::uint32_t player, std::uint32_t skater);
    void reset_trick_state(std::uint32_t skater);
    // 0x0048d7c0 (letter): one of S K A T E was collected; 0x0048d830 (n): a tape was; 0x0048d8a0: a collectible was.
    [[nodiscard]] std::int32_t skate_letter_collected(std::int32_t letter);
    void tape_collected(std::int32_t index);
    void collectible_counted();
    // 0x0048e950 (this = panel): the "steals" message; 0x0048e9c0 (this = panel): pick a random message and restart the effect.
    void show_steals_message(std::uint32_t panel);
    void message_out_of_bounds(std::uint32_t panel);

    // ---- construction ----
    // 0x0048d990 (this = panel; skater): build a panel. 0x0048ad50 (param): load the HUD fonts and sprites. 0x0048b100: release them.
    void construct(std::uint32_t panel, std::uint32_t skater);
    void load_runtime_sprites(std::uint32_t param);
    void uninit();

    // ---- the skater side of the score (physics.cpp) ----
    // 0x0048f5f0 (this = skater): a trick starts: clear the landing flags and the trick buffers. 0x0048f8f0 (this = skater): the
    // landing: commit the stack, give the landing its OLLIE, run the trick objects and charge the special meter.
    void take_off(std::uint32_t skater);
    void land(std::uint32_t skater);
    // 0x0048f800 (this = skater; object): remember a trick object the skater touched. 0x0048f7c0 (this = skater): end a pending trick.
    void register_trick_object(std::uint32_t skater, std::uint32_t object);
    void land_pending(std::uint32_t skater);
    // 0x0048f720 (this = skater): the point timer: advance, and bail past the limit or add a share of the step to the stack.
    void advance_point_timer(std::uint32_t skater, std::int32_t share);
    // 0x0048f690 (this = skater; name, value, kind, extra): a gap or bonus scored mid-trick: push it on the stack as a trick named
    // `name` with the magnitude of `value` (the sign becomes the entry's flag). Nothing happens while no trick is open (+0x3064 == 0).
    void apply_gap(std::uint32_t skater, std::uint32_t name, std::int32_t value, std::int32_t kind, std::uint32_t extra);
    // 0x0048f8a0 (this = skater): the landing quality for the trick just ended.
    [[nodiscard]] std::int32_t landing_quality(std::uint32_t skater);

private:
    void check(bool condition, std::uint32_t message) { s_.check(condition, message); }
    [[nodiscard]] bool video_playing() const { return guest::read_scalar<std::int32_t>(m_, kReplayState) == 2; }
    [[nodiscard]] bool net_remote_player(std::uint32_t skater) const {
        return guest::read_scalar<std::uint8_t>(m_, kNetActive) != 0 && guest::read_scalar<std::int32_t>(m_, skater + 0x2cc4) == guest::read_scalar<std::int32_t>(m_, kNetLocalPlayer);
    }
    void vcall(const char* name, std::uint32_t object, std::vector<std::uint32_t> args = {}, std::string text = {}) {
        std::vector<std::uint32_t> all{object};
        all.insert(all.end(), args.begin(), args.end());
        s_.note(name, std::move(all), std::move(text));
    }
    void draw_text(std::uint32_t font, std::uint32_t x, std::uint32_t y, const std::string& text) { vcall(kSvcFontDraw, font, {x, y}, text); }
    // draw `text` through `effect` bound to `font` (the pattern of the trick points)
    void draw_with_effect(std::uint32_t effect, std::uint32_t font, std::uint32_t x, std::uint32_t y, const std::string& text,
                          std::uint32_t skater);
    // the offsets of viewport `index` as 16-bit words; false for an index no layout has
    [[nodiscard]] bool viewport_offsets(std::uint32_t index, std::uint32_t& x, std::uint32_t& y);
    [[nodiscard]] std::int32_t get(std::uint32_t address) const { return guest::read_scalar<std::int32_t>(m_, address); }
    void put(std::uint32_t address, std::int32_t value) { guest::write_scalar<std::uint32_t>(m_, address, static_cast<std::uint32_t>(static_cast<std::uint32_t>(value))); }
    [[nodiscard]] std::uint32_t entry(std::uint32_t panel, std::int32_t index) const {
        return panel + kStackBase + static_cast<std::uint32_t>(index) * kEntrySize;
    }
    RetailMemory& m_;
    RetailServices& s_;
};

}  // namespace opentony::runtime::hud
