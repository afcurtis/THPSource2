#include "camera_views.hpp"

// Phase 4.15b: the camera tuning initialisers (camera.cpp, 0x0040ed70..0x0040f0b0). Each is a one-instruction `jmp` thunk to a body of three stores that put the default of one camera tuning value (a vector of
// three dwords, a trio of shorts or bytes) into the globals at 0x0055f750..0x0055fa14, where the shake and offset code of the camera modes read it. (Generated from the disassembly: build/p415b/mk_tuning.py.)

namespace opentony::runtime::camerakit {

using namespace views;

void CameraKit::tune_0040ed80() {
    set_global_word(m_, 0x55f7ac, 0xffe70000);
    set_global_word(m_, 0x55f7a8, 0);
    set_global_word(m_, 0x55f7b0, 0);
}

void CameraKit::tune_0040edb0() {
    set_global_word(m_, 0x55f9c4, 0xfff9c000);
    set_global_word(m_, 0x55f9c0, 0);
    set_global_word(m_, 0x55f9c8, 0);
}

void CameraKit::tune_0040ede0() {
    set_global_word(m_, 0x55f798, 0x0);
    set_global_word(m_, 0x55f79c, 0xfff60000);
    set_global_word(m_, 0x55f7a0, 0xffc4a000);
}

void CameraKit::tune_0040ee10() {
    set_global_word(m_, 0x55f9e4, 0xfff9c000);
    set_global_word(m_, 0x55f9e0, 0);
    set_global_word(m_, 0x55f9e8, 0);
}

void CameraKit::tune_0040ee40() {
    set_global_word(m_, 0x55f778, 0x0);
    set_global_word(m_, 0x55f77c, 0xfff6a000);
    set_global_word(m_, 0x55f780, 0x28a000);
}

void CameraKit::tune_0040ee70() {
    set_global_word(m_, 0x55fa0c, 0xfff9c000);
    set_global_word(m_, 0x55fa08, 0);
    set_global_word(m_, 0x55fa10, 0);
}

void CameraKit::tune_0040eea0() {
    set_global_word(m_, 0x55f768, 0x0);
    set_global_word(m_, 0x55f76c, 0xffbb4000);
    set_global_word(m_, 0x55f770, 0x514000);
}

void CameraKit::tune_0040eed0() {
    set_global_word(m_, 0x55f998, 0x0);
    set_global_word(m_, 0x55f99c, 0xfff9c000);
    set_global_word(m_, 0x55f9a0, 0xffce0000);
}

void CameraKit::tune_0040ef00() {
    set_global_word(m_, 0x55f9b0, 0x0);
    set_global_word(m_, 0x55f9b4, 0xff736000);
    set_global_word(m_, 0x55f9b8, 0x190000);
}

void CameraKit::tune_0040ef30() {
    set_global_word(m_, 0x55f980, 0x0);
    set_global_word(m_, 0x55f984, 0xfff9c000);
    set_global_word(m_, 0x55f988, 0xffed4000);
}

void CameraKit::tune_0040ef60() {
    set_global_word(m_, 0x55f758, 0x0);
    set_global_word(m_, 0x55f75c, 0x64000);
    set_global_word(m_, 0x55f760, 0xf92a0000);
}

void CameraKit::tune_0040ef90() {
    set_global_word(m_, 0x55f9fc, 0xfff9c000);
    set_global_word(m_, 0x55f9f8, 0);
    set_global_word(m_, 0x55fa00, 0);
}

void CameraKit::tune_0040efc0() {
    set_global_half(m_, 0x55f792, 0x19);
    set_global_half(m_, 0x55f794, 0x0);
    set_global_half(m_, 0x55f796, 0x32);
}

void CameraKit::tune_0040eff0() {
    set_global_half(m_, 0x55f78c, 0x1e);
    set_global_half(m_, 0x55f788, 0);
    set_global_half(m_, 0x55f78a, 0);
}

void CameraKit::tune_0040f020() {
    set_global_half(m_, 0x55f994, 0x19);
    set_global_half(m_, 0x55f990, 0);
    set_global_half(m_, 0x55f992, 0);
}

void CameraKit::tune_0040f050() {
    set_global_byte(m_, 0x55f9bd, 0x0);
    set_global_byte(m_, 0x55f9bc, 0x3);
    set_global_byte(m_, 0x55f9be, 0x3);
}

void CameraKit::tune_0040f080() {
    set_global_half(m_, 0x55f752, 0x0);
    set_global_half(m_, 0x55f750, 0x258);
    set_global_half(m_, 0x55f754, 0x258);
}

void CameraKit::tune_0040f0b0() {
    set_global_word(m_, 0x55f9d4, 0xfff92000);
    set_global_word(m_, 0x55f9d0, 0);
    set_global_word(m_, 0x55f9d8, 0);
}

}  // namespace opentony::runtime::camerakit
