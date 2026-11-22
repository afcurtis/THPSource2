#pragma once

// Exact integer semantics of the retail three-component vector operators:
// the 32-bit in-place Vec3 family (0x004ca9f0-0x004cabaf) and the packed
// signed 16-bit Vec3s helpers (0x004cad60-0x004cb0df); see
// re/evidence/functions/vector-math.md and match/cpp/Math_Vector3*.cpp.
//
// Retail contract, confirmed from the byte-matching VC6 sources and their
// disassembly:
// - add, sub and imul keep the low 32 (Vec3) or 16 (Vec3s) bits;
// - `sar`/`shl` mask the shift count to five bits, for 16-bit operands too;
// - `idiv` truncates toward zero; a zero divisor, or INT32_MIN / -1, raises
//   the x86 divide fault in retail, so those inputs are outside the contract;
// - every in-place operand is re-read through its reference before each
//   component, so an operand that aliases (part of) the destination sees
//   earlier writes. The value-returning Vec3s operators instead read every
//   input component before writing the result.
//
// Signed overflow and oversized shifts are undefined in C++, so the helpers
// go through uint32_t. The header is deliberately C++98-compatible (stdint.h,
// no library headers) because it is also compiled into the 32-bit hook DLL
// against the VC6 SDK.

#include <stdint.h>

namespace opentony {
namespace runtime {

struct RetailVec3 {
    int32_t x;
    int32_t y;
    int32_t z;
};

inline int32_t retail_wrap_add(int32_t left, int32_t right) {
    return static_cast<int32_t>(static_cast<uint32_t>(left) + static_cast<uint32_t>(right));
}

inline int32_t retail_wrap_sub(int32_t left, int32_t right) {
    return static_cast<int32_t>(static_cast<uint32_t>(left) - static_cast<uint32_t>(right));
}

inline int32_t retail_wrap_mul(int32_t left, int32_t right) {
    return static_cast<int32_t>(static_cast<uint32_t>(left) * static_cast<uint32_t>(right));
}

// `sar r32, cl`: count masked to 0..31, sign bits shifted in.
inline int32_t retail_sar(int32_t value, int32_t count) {
    const unsigned shift = static_cast<unsigned>(count) & 31u;
    if (value >= 0) {
        return static_cast<int32_t>(static_cast<uint32_t>(value) >> shift);
    }
    return static_cast<int32_t>(~(~static_cast<uint32_t>(value) >> shift));
}

// 0x004ca9f0: this += rhs; returns this.
inline RetailVec3& retail_vec3_add(RetailVec3& self, const RetailVec3& rhs) {
    self.x = retail_wrap_add(self.x, rhs.x);
    self.y = retail_wrap_add(self.y, rhs.y);
    self.z = retail_wrap_add(self.z, rhs.z);
    return self;
}

// 0x004caa20: this -= rhs; returns this.
inline RetailVec3& retail_vec3_sub(RetailVec3& self, const RetailVec3& rhs) {
    self.x = retail_wrap_sub(self.x, rhs.x);
    self.y = retail_wrap_sub(self.y, rhs.y);
    self.z = retail_wrap_sub(self.z, rhs.z);
    return self;
}

// 0x004caa50: this *= scalar (scalar re-read per component); returns this.
inline RetailVec3& retail_vec3_mul_scalar(RetailVec3& self, const int32_t& scalar) {
    self.x = retail_wrap_mul(self.x, scalar);
    self.y = retail_wrap_mul(self.y, scalar);
    self.z = retail_wrap_mul(self.z, scalar);
    return self;
}

// 0x004caab0: this >>= count (arithmetic, count re-read and masked per
// component); returns this.
inline RetailVec3& retail_vec3_shift_right(RetailVec3& self, const int32_t& count) {
    self.x = retail_sar(self.x, count);
    self.y = retail_sar(self.y, count);
    self.z = retail_sar(self.z, count);
    return self;
}

// 0x004cab50: 1 when all three components are equal, else 0 (full EAX).
inline int32_t retail_vec3_equal(const RetailVec3& self, const RetailVec3& rhs) {
    return (self.x == rhs.x && self.y == rhs.y && self.z == rhs.z) ? 1 : 0;
}

// `shl r32, cl`: count masked to 0..31, low 32 bits kept.
inline int32_t retail_shl(int32_t value, int32_t count) {
    return static_cast<int32_t>(static_cast<uint32_t>(value) << (static_cast<unsigned>(count) & 31u));
}

// 0x004caa80: this /= divisor (signed, toward zero, divisor re-read per
// component); returns this. Requires a nonzero divisor and no INT32_MIN / -1
// pair: retail faults there.
inline RetailVec3& retail_vec3_div_scalar(RetailVec3& self, const int32_t& divisor) {
    self.x = self.x / divisor;
    self.y = self.y / divisor;
    self.z = self.z / divisor;
    return self;
}

// 0x004caae0: this <<= count (count re-read and masked per component).
inline RetailVec3& retail_vec3_shift_left(RetailVec3& self, const int32_t& count) {
    self.x = retail_shl(self.x, count);
    self.y = retail_shl(self.y, count);
    self.z = retail_shl(self.z, count);
    return self;
}

// 0x004cab10: each component -= component >> counts[i] (arithmetic, byte
// count masked to five bits); returns this. The count bytes are re-read after
// the earlier components were written.
inline RetailVec3& retail_vec3_decay_shift(RetailVec3& self, const uint8_t* counts) {
    self.x = retail_wrap_sub(self.x, retail_sar(self.x, counts[0]));
    self.y = retail_wrap_sub(self.y, retail_sar(self.y, counts[1]));
    self.z = retail_wrap_sub(self.z, retail_sar(self.z, counts[2]));
    return self;
}

// 0x004cab80: 1 when any component differs, else 0 (full EAX).
inline int32_t retail_vec3_not_equal(const RetailVec3& self, const RetailVec3& rhs) {
    return (self.x != rhs.x || self.y != rhs.y || self.z != rhs.z) ? 1 : 0;
}

// Packed signed 16-bit vector (angles, short matrix rows).
struct RetailVec3s {
    int16_t x;
    int16_t y;
    int16_t z;
};

inline int16_t retail_wrap16(int32_t value) {
    return static_cast<int16_t>(static_cast<uint16_t>(static_cast<uint32_t>(value) & 0xffffu));
}

// 0x004cad60 (__fastcall, ECX = vector): mask every word with 0x0fff.
inline void retail_vec3s_mask12(RetailVec3s& self) {
    self.x = static_cast<int16_t>(self.x & 0x0fff);
    self.y = static_cast<int16_t>(self.y & 0x0fff);
    self.z = static_cast<int16_t>(self.z & 0x0fff);
}

// 0x004cad80 (__fastcall): zero every word in the inclusive range [-1, 1].
inline void retail_vec3s_snap_near_zero(RetailVec3s& self) {
    if (self.x >= -1 && self.x <= 1) self.x = 0;
    if (self.y >= -1 && self.y <= 1) self.y = 0;
    if (self.z >= -1 && self.z <= 1) self.z = 0;
}

// 0x004cae10: this += rhs (16-bit wrap, rhs re-read per word); returns this.
inline RetailVec3s& retail_vec3s_add(RetailVec3s& self, const RetailVec3s& rhs) {
    self.x = retail_wrap16(self.x + rhs.x);
    self.y = retail_wrap16(self.y + rhs.y);
    self.z = retail_wrap16(self.z + rhs.z);
    return self;
}

// 0x004cae30: this -= rhs; returns this.
inline RetailVec3s& retail_vec3s_sub(RetailVec3s& self, const RetailVec3s& rhs) {
    self.x = retail_wrap16(self.x - rhs.x);
    self.y = retail_wrap16(self.y - rhs.y);
    self.z = retail_wrap16(self.z - rhs.z);
    return self;
}

// 0x004caef0: each word -= word >> counts[i] (`sar r16, cl`: count masked to
// five bits, so 16..31 leave only sign bits); returns this.
inline RetailVec3s& retail_vec3s_decay_shift(RetailVec3s& self, const uint8_t* counts) {
    self.x = retail_wrap16(self.x - retail_sar(self.x, counts[0]));
    self.y = retail_wrap16(self.y - retail_sar(self.y, counts[1]));
    self.z = retail_wrap16(self.z - retail_sar(self.z, counts[2]));
    return self;
}

// 0x004caf40 / 0x004caf70: word-wise equality / inequality (full EAX).
inline int32_t retail_vec3s_equal(const RetailVec3s& self, const RetailVec3s& rhs) {
    return (self.x == rhs.x && self.y == rhs.y && self.z == rhs.z) ? 1 : 0;
}

inline int32_t retail_vec3s_not_equal(const RetailVec3s& self, const RetailVec3s& rhs) {
    return (self.x != rhs.x || self.y != rhs.y || self.z != rhs.z) ? 1 : 0;
}

// 0x004cb040 / 0x004cb090: value-returning a + b / a - b. Every input word is
// read before the result is written, so the result may alias either input.
inline RetailVec3s retail_vec3s_add_out(const RetailVec3s& a, const RetailVec3s& b) {
    RetailVec3s out;
    out.x = retail_wrap16(a.x + b.x);
    out.y = retail_wrap16(a.y + b.y);
    out.z = retail_wrap16(a.z + b.z);
    return out;
}

inline RetailVec3s retail_vec3s_sub_out(const RetailVec3s& a, const RetailVec3s& b) {
    RetailVec3s out;
    out.x = retail_wrap16(a.x - b.x);
    out.y = retail_wrap16(a.y - b.y);
    out.z = retail_wrap16(a.z - b.z);
    return out;
}

} // namespace runtime
} // namespace opentony
