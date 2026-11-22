// Native replacements for the retail Vec3 operators at 0x004ca9f0..0x004cabaf
// and the packed 16-bit Vec3s helpers at 0x004cad60..0x004cb0df.
//
// The retail functions are VC6 __thiscall members (this in ECX, one stack
// argument, `ret 4`). A __fastcall function whose second parameter takes the
// unused EDX slot has the identical machine contract and is accepted by MSVC,
// clang-cl and MinGW alike, unlike __thiscall on a free function.
//
// All arithmetic lives in src/runtime/retail_vector3.hpp, which the Linux unit
// test covers; these adapters only translate the ABI.

#include "hook_types.h"
#include "retail_vector3.hpp"

namespace {

using opentony::runtime::RetailVec3;
using opentony::runtime::RetailVec3s;

RetailVec3 *__fastcall ot_Math_Vector3Add(RetailVec3 *self, void *, const RetailVec3 *rhs) {
    return &opentony::runtime::retail_vec3_add(*self, *rhs);
}

RetailVec3 *__fastcall ot_Math_Vector3Sub(RetailVec3 *self, void *, const RetailVec3 *rhs) {
    return &opentony::runtime::retail_vec3_sub(*self, *rhs);
}

RetailVec3 *__fastcall ot_Math_Vector3MulScalar(RetailVec3 *self, void *, const int32_t *scalar) {
    return &opentony::runtime::retail_vec3_mul_scalar(*self, *scalar);
}

RetailVec3 *__fastcall ot_Math_Vector3ShiftRight(RetailVec3 *self, void *, const int32_t *count) {
    return &opentony::runtime::retail_vec3_shift_right(*self, *count);
}

int32_t __fastcall ot_Math_Vector3Equal(const RetailVec3 *self, void *, const RetailVec3 *rhs) {
    return opentony::runtime::retail_vec3_equal(*self, *rhs);
}

RetailVec3 *__fastcall ot_Math_Vector3DivScalar(RetailVec3 *self, void *, const int32_t *divisor) {
    return &opentony::runtime::retail_vec3_div_scalar(*self, *divisor);
}

RetailVec3 *__fastcall ot_Math_Vector3ShiftLeft(RetailVec3 *self, void *, const int32_t *count) {
    return &opentony::runtime::retail_vec3_shift_left(*self, *count);
}

RetailVec3 *__fastcall ot_Math_Vector3DecayShift(RetailVec3 *self, void *, const uint8_t *counts) {
    return &opentony::runtime::retail_vec3_decay_shift(*self, counts);
}

int32_t __fastcall ot_Math_Vector3NotEqual(const RetailVec3 *self, void *, const RetailVec3 *rhs) {
    return opentony::runtime::retail_vec3_not_equal(*self, *rhs);
}

// Packed 16-bit helpers. Mask12 and SnapNearZero are genuine __fastcall
// functions taking only ECX (`ret`); the rest are thiscall members like the
// Vec3 family.
void __fastcall ot_Math_Vector3sMask12(RetailVec3s *self) {
    opentony::runtime::retail_vec3s_mask12(*self);
}

void __fastcall ot_Math_Vector3sSnapNearZero(RetailVec3s *self) {
    opentony::runtime::retail_vec3s_snap_near_zero(*self);
}

RetailVec3s *__fastcall ot_Math_Vector3sAdd(RetailVec3s *self, void *, const RetailVec3s *rhs) {
    return &opentony::runtime::retail_vec3s_add(*self, *rhs);
}

RetailVec3s *__fastcall ot_Math_Vector3sSub(RetailVec3s *self, void *, const RetailVec3s *rhs) {
    return &opentony::runtime::retail_vec3s_sub(*self, *rhs);
}

RetailVec3s *__fastcall ot_Math_Vector3sDecayShift(RetailVec3s *self, void *, const uint8_t *counts) {
    return &opentony::runtime::retail_vec3s_decay_shift(*self, counts);
}

int32_t __fastcall ot_Math_Vector3sEqual(const RetailVec3s *self, void *, const RetailVec3s *rhs) {
    return opentony::runtime::retail_vec3s_equal(*self, *rhs);
}

int32_t __fastcall ot_Math_Vector3sNotEqual(const RetailVec3s *self, void *, const RetailVec3s *rhs) {
    return opentony::runtime::retail_vec3s_not_equal(*self, *rhs);
}

// Value-returning free functions: VC6 returns the 6-byte struct through a
// hidden result pointer passed first (cdecl, caller cleans) and returns that
// pointer in EAX.
RetailVec3s *__cdecl ot_Math_Vector3sAddOut(RetailVec3s *out, const RetailVec3s *a, const RetailVec3s *b) {
    *out = opentony::runtime::retail_vec3s_add_out(*a, *b);
    return out;
}

RetailVec3s *__cdecl ot_Math_Vector3sSubOut(RetailVec3s *out, const RetailVec3s *a, const RetailVec3s *b) {
    *out = opentony::runtime::retail_vec3s_sub_out(*a, *b);
    return out;
}

// Deliberately wrong variants, selected only with `fault:<name>` in
// OPENTONY_HOOKS. They prove that the replay/self-test comparisons can see a
// one-unit error in a hooked function (a negative control, never a default).
RetailVec3 *__fastcall fault_Math_Vector3Add(RetailVec3 *self, void *, const RetailVec3 *rhs) {
    opentony::runtime::retail_vec3_add(*self, *rhs);
    self->x = opentony::runtime::retail_wrap_add(self->x, 1);
    return self;
}

int32_t __fastcall fault_Math_Vector3Equal(const RetailVec3 *self, void *, const RetailVec3 *rhs) {
    return opentony::runtime::retail_vec3_equal(*self, *rhs) ^ 1;
}

// One negative control per new self-test shape: bytes-referenced counts,
// fastcall self-only, 16-bit thiscall and value-returning cdecl.
RetailVec3 *__fastcall fault_Math_Vector3DecayShift(RetailVec3 *self, void *, const uint8_t *counts) {
    opentony::runtime::retail_vec3_decay_shift(*self, counts);
    self->z = opentony::runtime::retail_wrap_add(self->z, 1);
    return self;
}

void __fastcall fault_Math_Vector3sSnapNearZero(RetailVec3s *self) {
    // Exclusive instead of inclusive range: misses exactly -1 and 1.
    if (self->x == 0) self->x = 0;
    if (self->y == 0) self->y = 0;
    if (self->z == 0) self->z = 0;
}

RetailVec3s *__fastcall fault_Math_Vector3sAdd(RetailVec3s *self, void *, const RetailVec3s *rhs) {
    opentony::runtime::retail_vec3s_add(*self, *rhs);
    self->y = opentony::runtime::retail_wrap16(self->y + 1);
    return self;
}

RetailVec3s *__cdecl fault_Math_Vector3sAddOut(RetailVec3s *out, const RetailVec3s *a, const RetailVec3s *b) {
    // Writes x before reading the other inputs: wrong only when out aliases.
    out->x = opentony::runtime::retail_wrap16(a->x + b->x);
    out->y = opentony::runtime::retail_wrap16(a->y + b->y);
    out->z = opentony::runtime::retail_wrap16(a->z + b->z);
    return out;
}

} // namespace

OT_REGISTER_NATIVE(Math_Vector3Add, ot_Math_Vector3Add)
OT_REGISTER_NATIVE(Math_Vector3Sub, ot_Math_Vector3Sub)
OT_REGISTER_NATIVE(Math_Vector3MulScalar, ot_Math_Vector3MulScalar)
OT_REGISTER_NATIVE(Math_Vector3ShiftRight, ot_Math_Vector3ShiftRight)
OT_REGISTER_NATIVE(Math_Vector3Equal, ot_Math_Vector3Equal)
OT_REGISTER_NATIVE(Math_Vector3DivScalar, ot_Math_Vector3DivScalar)
OT_REGISTER_NATIVE(Math_Vector3ShiftLeft, ot_Math_Vector3ShiftLeft)
OT_REGISTER_NATIVE(Math_Vector3DecayShift, ot_Math_Vector3DecayShift)
OT_REGISTER_NATIVE(Math_Vector3NotEqual, ot_Math_Vector3NotEqual)
OT_REGISTER_NATIVE(Math_Vector3sMask12, ot_Math_Vector3sMask12)
OT_REGISTER_NATIVE(Math_Vector3sSnapNearZero, ot_Math_Vector3sSnapNearZero)
OT_REGISTER_NATIVE(Math_Vector3sAdd, ot_Math_Vector3sAdd)
OT_REGISTER_NATIVE(Math_Vector3sSub, ot_Math_Vector3sSub)
OT_REGISTER_NATIVE(Math_Vector3sDecayShift, ot_Math_Vector3sDecayShift)
OT_REGISTER_NATIVE(Math_Vector3sEqual, ot_Math_Vector3sEqual)
OT_REGISTER_NATIVE(Math_Vector3sNotEqual, ot_Math_Vector3sNotEqual)
OT_REGISTER_NATIVE(Math_Vector3sAddOut, ot_Math_Vector3sAddOut)
OT_REGISTER_NATIVE(Math_Vector3sSubOut, ot_Math_Vector3sSubOut)
OT_REGISTER_FAULT(Math_Vector3Add, fault_Math_Vector3Add)
OT_REGISTER_FAULT(Math_Vector3Equal, fault_Math_Vector3Equal)
OT_REGISTER_FAULT(Math_Vector3DecayShift, fault_Math_Vector3DecayShift)
OT_REGISTER_FAULT(Math_Vector3sSnapNearZero, fault_Math_Vector3sSnapNearZero)
OT_REGISTER_FAULT(Math_Vector3sAdd, fault_Math_Vector3sAdd)
OT_REGISTER_FAULT(Math_Vector3sAddOut, fault_Math_Vector3sAddOut)
