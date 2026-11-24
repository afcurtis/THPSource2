// What the program hosting the runtime asks of it, kept apart from the ported game code: every value is the game's own behaviour until a host sets it (the
// standalone does from the player's options, standalone_options.cpp). Gates and scenarios never set one.
#pragma once

#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

namespace opentony::runtime {

struct HostOptions {
    // Fixes of the retail game's own mistakes, the kind a player never chooses (OT_RETAIL_FIXES: the launcher always sets it, as it does OT_VEHICLE_SOUND_FIX; unset, as in
    // every gate and scenario, the game is retail's). Each use names the mistake it corrects.
    bool retail_fixes = false;
    bool unlock_movies = false;   // the movie list (OptionsScreen::movie_unlocked) offers every movie that exists, not only those the career has earned
    bool disable_controls = false;   // Options > Player and Display controls (the pad and key mapping screens) are greyed and answer with the error sound: the player's launcher has its own
    int default_camera = 0;       // the in-level camera the game starts each level on: 0 its first position, 1 the second (the one the pad's view toggle switches to)
    // What the host asks of the level in play, answered by the front end's update (FrontScreens::update) at its next frame; the game's own behaviour with none:
    //   1 pause: puts up the pause menu as the Start button does (the window lost its focus)
    //   2 retry: pause, then take the pause menu's Retry choice on its first frame (the retry hotkey), so the pause set-up and tear-down are retail's own
    //   3 retry, menu up (set by the update itself)
    int request = 0;
    // The launcher's quick start (OT_QUICK_START): the player is kept out of anything that writes the save file or leads back to the shell. The end-of-run menu's Level
    // Select, Update save file, Save Career and Spend Some Cash lines and the replay menus' Save Replay are greyed (CMenu's own "cannot be chosen", the cursor passes over them), the
    // career's "Save Career?" questions after an unlock message are answered No without being asked, and the pause menu has a Quit line: Quit there or on the end-of-run
    // menu, once confirmed, sets quit_to_launcher, and the host ends the program at its next poll (the launcher comes back, as with the Quit hotkey).
    bool quick_play = false;
    bool quit_to_launcher = false;
    // The goals screen (the career's level checklist, GoalScreens::goals_run) is up and waiting for the player. A Goals Speed Run waits there for Play instead of passing
    // the screen by itself (standalone_options.cpp); its clock starts once the screen is gone. In a quick start the screen's Back goes to the launcher, not to the shell.
    bool goals_screen_up = false;
    // The quick start's Goals Speed Run (OT_QUICK_MODE=speedrun): a career run whose clock counts up (the skater's +0x2cc8 holds the time so far, the count-up byte
    // 0x029da3a4 set: retail's own mode of the clock, standalone_options.cpp writes both every poll). Panel::display_holding draws it without the last-ten-seconds flash
    // and beep, which retail's count-up keeps from the countdown and which would sound in the run's first ten seconds.
    bool speed_run = false;
    // The quick start's Competition Session (OT_QUICK_MODE=competition), on any level (the host marks the level a contest for the session):
    //   judge_level   the level whose judging Competition::judge_score uses (its three score thresholds and its deduction rule), -1 the level in play: 7, The Bullring.
    //                 Set, GoalScreens::loading_start gives a contest that is not one of the three the plain loading picture (retail leaves its lines unset there)
    //   hold_intro    the level has no point for the competition's opening shot (its record's position and turns are all 0, as on every level that is not a contest but
    //                 Chopper Drop): CameraKit::point_mode keeps the shot where the first frame put it, behind the skater, instead of swinging in from the world's origin
    int judge_level = -1;
    bool hold_intro = false;
    // The custom soundtrack plays (standalone_music.cpp): the pause menu's Sound > TRACK line (FrontScreens::display, status 0x2f) shows "--" in place of the game's track
    // number, which names a disc track the playlist has replaced. The line still skips songs
    bool custom_soundtrack = false;
    bool hide_hud = false;        // the in-level HUD (Panel::display) is not drawn: its code still runs, only its draws are dropped (screenshots and video)
    bool grind_meter = false;     // a horizontal balance bar is drawn while a skater grinds (CoreKit::display): the PlayStation game's, which the PC game's panel code has but never calls
    bool draws_muted = false;    // DrawKit::walk sets it while it takes a hidden packet: the packet is walked as usual (its state carries on) but DrawKit::emit draws nothing
    // The HUD's polygons are packets written to the polygon arena and drawn later, when the frame's ordering table is walked: the arena range Panel::display wrote with hide_hud
    // on, [begin, end), which DrawKit::walk does not draw. Cleared when the frame is presented. (Read only inside the runtime's own module: the video layer is another DLL with its own copy.)
    std::vector<std::pair<std::uint32_t, std::uint32_t>> hidden_packets;
    // The in-level HUD's layout (the launcher's "HUD anchor", "HUD size" and "HUD margin"). With none of the three asked for the HUD is drawn as it always was (4:3 in the
    // middle of a wide picture). Otherwise Panel::display records each of its elements (the score block, the switch icon, the clock, ...) as a group of arena packets, and
    // DrawKit::fit_flat places a group about one point: a left element hugs the picture's left edge, a right one its right edge, the rest stay where they are, and
    // `hud_scale` grows or shrinks the element about that point. Cleared when the frame is presented; read only inside the runtime's own module, like hidden_packets.
    bool hud_anchor = false;      // elements near an edge of the retail layout go to that edge of the (wide) picture
    float hud_scale = 1.0f;       // 1 is the game's size
    float hud_margin = 0.0f;      // the part of the picture's width and height kept clear at the edge an element is anchored to (a TV's overscan)
    [[nodiscard]] bool hud_layout() const { return hud_anchor || hud_scale != 1.0f || hud_margin > 0.0f; }
    struct HudGroup {
        std::uint32_t begin = 0;    // the arena range [begin, end) of the element's packets
        std::uint32_t end = 0;
        float fx = 0.5f;            // where Panel::display put the element, as a part of the picture's width and height (its x of 512 and y of 240 PlayStation units)
        float fy = 0.5f;
        bool placed = false;        // the point below is known (worked out by the first of its primitives that is drawn)
        float from_x = 0.0f;        // the element's point in the game's own layout, and where it goes: x' = to_x + (x - from_x) * scale, the same for y
        float to_x = 0.0f;
        float from_y = 0.0f;
        float to_y = 0.0f;
    };
    std::vector<HudGroup> hud_groups;
    // The host's own banner messages that are laid out with the HUD (the song's name, standalone_music.cpp): Mess::display records a message whose text is at `text` as one
    // more element of hud_groups about (x, y) in the 512 x 240 units of the layout, so on a wide picture it follows the anchor, size and margin like the score does. The
    // game's own messages are not touched. Set once by the host; read only inside the runtime's own module, like hud_groups.
    struct HudMessage {
        std::uint32_t text = 0;
        std::int32_t x = 0;
        std::int32_t y = 0;
    };
    std::vector<HudMessage> hud_messages;
    int detail_distance = 100;    // percent of the distance at which an object takes its rougher model or is dropped (RenderKit::render_objects); 100 is the game's
    bool sky_behind_yon = false;  // the background (the sky) is drawn at the far plane when that is beyond the game's fixed depth (background_depth); set with the player's fog distance
    float item_reach_scale = 0.0f;   // the player's fog distance as a multiple of the level's (0: the game's): the reach of a power-up (cash, letters, goal items; item_reach)
    // A level's trigger script hides whole areas of its geometry with boxes (CoreKit::update_visibility_bounds, TRG command 0x8d) and shows them again as the skater rides
    // through a trigger: with the game's fog they lie at or past the fog, with it pushed out or off a block of the city appears in one frame (NYC, 8 seconds from the start).
    // With draw_hidden_areas (set with the player's fog distance) the environment objects the boxes hid are drawn anyway (M3dKit::cull_objects_to_view, while the polygon
    // arena has room); `area_hidden` is each one's flag word as the boxes wrote it, so an object whose word another writer has changed since (a goal's crates and items share
    // the bits) keeps the game's rule, and so does an object a box drawn tight round it hid (area_sized_box: Hangar's chopper debris, hidden until the chopper comes through
    // the roof). Cleared at each level clean-up (CoreKit::init_cleanup).
    bool draw_hidden_areas = false;
    std::unordered_map<std::uint32_t, std::uint16_t> area_hidden;
    // The polygon arena (RenderKit::db_init): each of its two buffers is poly_buffer_scale times the game's size, so the far scenery the options above add does not reach
    // the three-quarters guards of render_objects and cull_objects_to_view (School II with fog off filled 94% of retail's). At 1, retail: the area comes from the game's
    // heap. Above 1 it comes from the guest heap (the C runtime's operator new), outside the game's heap, kPolyBufferPad bytes longer. The standalone sets it once at its
    // boot (poly_buffer_scale_for). `poly_buffer_area_scale` is the scale of the area db_init holds: 0 while that is the game's own, or there is none.
    int poly_buffer_scale = 1;
    int poly_buffer_area_scale = 0;
    // With an enlarged area held, a polygon that finds the arena full is dropped (RenderKit::model_allocate_polygon) instead of stopping the program, as retail's overflow would. Faces
    // dropped since the level began, and whether retail's message was logged for this level (once). Cleared by CoreKit::init_cleanup. Never touched at poly_buffer_area_scale <= 1.
    std::uint32_t poly_overflows = 0;
    bool poly_overflow_logged = false;
};

inline HostOptions& host_options() {
    static HostOptions options;
    return options;
}

// A level-of-detail distance as the game works it out, stretched by HostOptions::detail_distance. At 100 it is the same value (no arithmetic); the product is made after the
// game's own 16-bit narrowing, so a large percentage cannot wrap, and is held to what a 32-bit compare can take.
inline std::int32_t detail_reach(std::int32_t limit) {
    const std::int64_t percent = host_options().detail_distance;
    if (percent == 100) {
        return limit;
    }
    const std::int64_t wide = static_cast<std::int64_t>(limit) * percent / 100;
    return static_cast<std::int32_t>(wide > INT32_MAX ? INT32_MAX : (wide < INT32_MIN ? INT32_MIN : wide));
}

// One limit of the box around the camera outside which an object is not drawn (M3dKit::cull_objects_to_view: -32767 and 32767 world units on each axis, the reach of the GTE's 16-bit
// vector), stretched by HostOptions::detail_distance when `room` (the polygon arena has room). At 100, or without room, the game's value. A distant block of School II sits 32800 to
// 34600 units from the start and came in object by object as the skater rode up to the first planter. RenderKit::render_objects rotates the position of an object past 16 bits
// in 64-bit arithmetic (the GTE would wrap it).
// The distance from the skater within which a power-up (cash, the letters, a goal's items) is shown (WorldKit::powerup_update). Retail's is the level's fog far distance unscaled, so
// with the far plane pushed out (yon = that distance times the fog scale) the item pops in well inside the drawn scenery (School II's cash). With the player's fog distance it is
// stretched by the same scale; at 0 (unset) or 1 and below, the game's value.
inline std::uint32_t item_reach(std::uint32_t limit) {
    const float scale = host_options().item_reach_scale;
    if (!(scale > 1.0f)) {
        return limit;
    }
    const double reach = static_cast<double>(limit) * scale;
    return reach >= 4294967295.0 ? 0xffffffffu : static_cast<std::uint32_t>(reach);
}

inline float cull_reach(float limit, bool room) {
    const int percent = host_options().detail_distance;
    if (percent == 100 || !room) {
        return limit;
    }
    return static_cast<float>(static_cast<double>(limit) * percent / 100.0);
}

// The depth the background's polygons (the sky: RenderKit::model_classify_depth, depth mode 4) are moved back by before the reciprocal is taken. Retail's is a fixed 16380, and the sky
// writes depth, so a world surface farther than that loses the depth test to the sky and is cut off with a hard edge that moves as the camera does. At the game's fog distances the
// far plane (yon) is nearer than 16380 and fog hides the cut; with fog pushed out or off it shows (School II's far buildings from the first planter). With sky_behind_yon the
// sky is put at the far plane when that is the farther: the same value whenever yon <= 16380, so the game's own fog distances are untouched. The draw order is the same either
// way (the bucket is held at its last, 0xfff, from 16380 on).
inline float background_depth(float forced, float yon) {
    return host_options().sky_behind_yon && yon > forced ? yon : forced;
}

// The polygon arena's scale (HostOptions::poly_buffer_scale) from the standalone's options. `requested` is OT_POLY_BUFFER: a whole number, held to 1 (retail) ..
// kPolyBufferScaleMax. Unset, empty or not a number: kPolyBufferScaleAuto when an option draws beyond retail (`beyond_retail`: a detail distance not 100, or a fog
// distance of the player's), else 1.
inline constexpr int kPolyBufferScaleMax = 16;
inline constexpr int kPolyBufferScaleAuto = 4;           // School II's 94% of retail's arena becomes about 24%; the guards then trip at three times retail's whole arena
inline constexpr std::uint32_t kPolyBufferPad = 0x100;   // a loose check (cursor < end, then a 0xc0-byte slot) can write past the second buffer's end, as retail's can
inline int poly_buffer_scale_for(const char* requested, bool beyond_retail) {
    const int automatic = beyond_retail ? kPolyBufferScaleAuto : 1;
    if (requested == nullptr || *requested == '\0') {
        return automatic;
    }
    int value = 0;
    for (const char* c = requested; *c != '\0'; ++c) {
        if (*c < '0' || *c > '9') {
            return automatic;
        }
        value = value > kPolyBufferScaleMax ? value : value * 10 + (*c - '0');
    }
    return value < 1 ? 1 : (value > kPolyBufferScaleMax ? kPolyBufferScaleMax : value);
}

// The bits of an object's flag word (+4) that hide it: 0x01 for the first skater's view, 0x40 for the second's (both in a one-player game).
inline constexpr std::uint16_t kAreaHiddenBits = 0x41;

// The same command (TRG 0x8d) also hides single objects until an event shows them: Hangar's chopper debris and roof pieces (boxes 166 and 372 units across) wait at the
// start for the chopper to come through the roof, NYC's, Philadelphia's and the 34-unit cubes of Marseille, Venice and others hold one object each. Those boxes are drawn
// tight round the object; an area's are the size of a room or a block. A box whose narrower side across the ground (x or z) is under kAreaBoxMinSide hides an object, and the
// game's rule stays for it. Every level's boxes (2026-11): objects at most 373 across, areas at least 830 (Hangar's), nearly all over 1500.
inline constexpr std::int32_t kAreaBoxMinSide = 512;
inline bool area_sized_box(std::int32_t min_x, std::int32_t min_z, std::int32_t max_x, std::int32_t max_z) {
    const std::int64_t x = static_cast<std::int64_t>(max_x) - min_x;
    const std::int64_t z = static_cast<std::int64_t>(max_z) - min_z;
    return x >= kAreaBoxMinSide && z >= kAreaBoxMinSide;
}

// CoreKit::update_visibility_bounds has written `flags` to the environment object at `object`: remember it while it hides the object (HostOptions::draw_hidden_areas only),
// when the box that did is an area's (`area`: area_sized_box); a box round a single object makes it forgotten, so the game's rule decides.
inline void note_area_visibility(std::uint32_t object, std::uint16_t flags, bool area = true) {
    HostOptions& o = host_options();
    if (!o.draw_hidden_areas) {
        return;
    }
    if (area && (flags & kAreaHiddenBits) != 0) {
        o.area_hidden[object] = static_cast<std::uint16_t>(flags & 0x7fff);
    } else {
        o.area_hidden.erase(object);
    }
}

// The object at `object` is outside the view only because of `hit` (its flag word `flags` met the cull's mask in those bits), and the area boxes are what hid it, its word
// unchanged since: M3dKit::cull_objects_to_view goes on to test it against the view as any other.
inline bool hidden_only_by_area(std::uint32_t object, std::uint16_t flags, std::uint16_t hit) {
    const HostOptions& o = host_options();
    if (!o.draw_hidden_areas || (hit & ~kAreaHiddenBits) != 0) {
        return false;
    }
    const auto found = o.area_hidden.find(object);
    return found != o.area_hidden.end() && found->second == (flags & 0x7fff);
}

}  // namespace opentony::runtime
