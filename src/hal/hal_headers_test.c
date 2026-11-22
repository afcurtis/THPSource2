/* Compiles every HAL header as strict C99 on its own: the headers must not
 * depend on SDL, Windows or Direct3D, and their layouts must not depend on the
 * pointer size where a layout is part of a contract (docs/HAL.md). */

#include "audio.h"
#include "clock.h"
#include "fs.h"
#include "hal.h"
#include "input.h"
#include "movie.h"
#include "video.h"
#include "window.h"

#include <stddef.h>
#include <stdio.h>

#define OT_CHECK(name, condition) typedef char ot_check_##name[(condition) ? 1 : -1]

/* The retail D3DTLVERTEX layout: the adapter copies retail vertices as is. */
OT_CHECK(vertex_size, sizeof(OtHalVertex) == 32);
OT_CHECK(vertex_color, offsetof(OtHalVertex, color) == 16);
OT_CHECK(vertex_uv, offsetof(OtHalVertex, u) == 24);
/* Plain-data records that recordings and oracles may store. */
OT_CHECK(rect_size, sizeof(OtHalRect) == 16);
OT_CHECK(gamepad_size, sizeof(OtHalGamepadState) == 24);
OT_CHECK(input_size, sizeof(OtHalInputState) == 8 + OT_HAL_KEY_COUNT + 24 + OT_HAL_MAX_GAMEPADS * 24);
OT_CHECK(window_event_size, sizeof(OtHalWindowEvent) == 12);
OT_CHECK(draw_state_size, sizeof(OtHalDrawState) == 16);
OT_CHECK(handle_size, sizeof(OtHalHandle) == 4);
/* The action bank is the PlayStation pad word. */
OT_CHECK(pad_word, (OT_PAD_L2 | OT_PAD_R2 | OT_PAD_L1 | OT_PAD_R1 | OT_PAD_TRIANGLE | OT_PAD_CIRCLE | OT_PAD_CROSS |
                       OT_PAD_SQUARE | OT_PAD_SELECT | OT_PAD_L3 | OT_PAD_R3 | OT_PAD_START | OT_PAD_UP |
                       OT_PAD_RIGHT | OT_PAD_DOWN | OT_PAD_LEFT) == 0xffff);
OT_CHECK(gamepad_buttons, OT_GAMEPAD_DPAD_RIGHT == 1u << (OT_GAMEPAD_BUTTON_COUNT - 1));

int main(void) {
    OtHalClockBackend clock = {0};
    OtHalWindowBackend window = {0};
    OtHalInputBackend input = {0};
    OtHalVideoBackend video = {0};
    OtHalAudioBackend audio = {0};
    OtHalMovieBackend movie = {0};
    OtHalFsBackend fs = {0};

    (void)clock;
    (void)window;
    (void)input;
    (void)video;
    (void)audio;
    (void)movie;
    (void)fs;
    puts("hal headers: ok");
    return 0;
}
