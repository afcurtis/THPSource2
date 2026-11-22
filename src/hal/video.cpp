#include "video.h"

// Portable helpers of the video service.

extern "C" void OT_HAL_CALL ot_hal_blend_preset(OtHalDrawState *state, uint32_t blend) {
    state->blend_op = OT_HAL_BLEND_OP_ADD;
    state->flags |= OT_HAL_DRAW_BLEND;
    switch (blend) {
    case OT_HAL_BLEND_HALF:
    case OT_HAL_BLEND_ALPHA:
        state->src_factor = OT_HAL_FACTOR_SRC_ALPHA;
        state->dst_factor = OT_HAL_FACTOR_INV_SRC_ALPHA;
        break;
    case OT_HAL_BLEND_ADD:
        state->src_factor = OT_HAL_FACTOR_ONE;
        state->dst_factor = OT_HAL_FACTOR_ONE;
        break;
    case OT_HAL_BLEND_SUBTRACT:
        state->src_factor = OT_HAL_FACTOR_ONE;
        state->dst_factor = OT_HAL_FACTOR_ONE;
        state->blend_op = OT_HAL_BLEND_OP_REV_SUBTRACT;
        break;
    case OT_HAL_BLEND_ADD_QUARTER:
        state->src_factor = OT_HAL_FACTOR_SRC_ALPHA;
        state->dst_factor = OT_HAL_FACTOR_ONE;
        break;
    default: // OT_HAL_BLEND_OPAQUE
        state->src_factor = OT_HAL_FACTOR_ONE;
        state->dst_factor = OT_HAL_FACTOR_ZERO;
        state->flags &= ~static_cast<uint32_t>(OT_HAL_DRAW_BLEND);
        break;
    }
}
