#include "math_views.hpp"

// Phase 4.14a: the GTE operations of the PC build (0x004e2070..0x004e3d1f). Every one is plain 32-bit integer code on the register file in 0x006a3e00..0x006a3f20: the products are `imul` (they wrap),
// the shifts are `sar`, and the clamps go through the two helpers at the top. The ops that are not named after a library call (`gte_lcv0_lm`, the `llv*bk` family, `gte_avsz4`) have no retail caller; they
// are ported from the disassembly and replayed against forced calls like the rest.
//
// 11c: the register file and the matrices and vectors the operations name are read through the typed views of re/types/math.yml (`RuntimeGteRegisterFile`, `RuntimeQ12Matrix`,
// `RuntimeShortVector3`, `RuntimeIntVector3`); the arithmetic is unchanged.

namespace opentony::runtime::mathkit {

using namespace gte;
using namespace views;

// 0x004e2070: IR1..IR3 = MAC1..MAC3 clamped to [-32768, 32767] (the bounds are `pow(2, 15)` and `pow(2, 15) - 1` through `_ftol`)
void MathKit::clamp_ir_signed() {
    const RuntimeGteRegisterFileView g(m_, kRegisters);
    const RuntimeIntVector3View mac = g.mac();
    const RuntimeShortVector3View ir = g.ir();
    for (std::uint32_t k = 0; k < 3; ++k) {
        std::int32_t v = static_cast<std::int32_t>(component(mac, k));
        if (v < -32768) {
            v = -32768;
        } else if (v > 32767) {
            v = 32767;
        }
        set_component(ir, k, v);
    }
}

// 0x004e2130: the same for [0, 32767]
void MathKit::clamp_ir_unsigned() {
    const RuntimeGteRegisterFileView g(m_, kRegisters);
    const RuntimeIntVector3View mac = g.mac();
    const RuntimeShortVector3View ir = g.ir();
    for (std::uint32_t k = 0; k < 3; ++k) {
        std::int32_t v = static_cast<std::int32_t>(component(mac, k));
        if (v < 0) {
            v = 0;
        } else if (v > 32767) {
            v = 32767;
        }
        set_component(ir, k, v);
    }
}

// 0x004e21e0: (IR >> 4) clamped to a byte, written into the three RGB slots of the colour FIFO (red at +0 / +4 / +8, green at +1 / +5 / +9, blue at +2 / +6 / +10 of 0x6a3e70)
void MathKit::colour_from_ir() {
    const RuntimeGteRegisterFileView g(m_, kRegisters);
    const RuntimeShortVector3View ir = g.ir();
    std::uint32_t colour[3];
    for (std::uint32_t k = 0; k < 3; ++k) {
        std::int32_t v = component(ir, k) >> 4;
        if (v < 0) {
            v = 0;
        } else if (v > 0xff) {
            v = 0xff;
        }
        colour[k] = static_cast<std::uint32_t>(v);
    }
    for (std::uint32_t slot = 0; slot < 3; ++slot) {
        for (std::uint32_t k = 0; k < 3; ++k) {
            g.set_rgb_fifo(4 * slot + k, static_cast<std::uint8_t>(colour[k]));
        }
    }
}

// 0x004e22b0: LZCR = the number of leading bits equal to the sign bit (retail loops for ever on 0; the port leaves LZCR cleared)
void MathKit::leading_zero_count(std::uint32_t value) {
    const RuntimeGteRegisterFileView g(m_, kRegisters);
    g.set_lzcr(0);
    if (value == 0) {
        return;
    }
    std::uint32_t count = 0;
    if (static_cast<std::int32_t>(value) < 0) {
        do {
            value <<= 1;
            ++count;
        } while ((value & 0x80000000u) != 0);
    } else {
        do {
            value <<= 1;
            ++count;
        } while ((value & 0x80000000u) == 0);
    }
    g.set_lzcr(count);
}

// 0x004e22f0: OTZ = the average of SZ0..SZ3 as the retail x87 sequence computes it: (sum * 1024 / 4096) truncated, then divided by 4 again (MAC0 gets the quotient, OTZ its clamp to 16 bits)
void MathKit::average_z4() {
    const RuntimeGteRegisterFileView g(m_, kRegisters);
    long double sum = 0;
    for (std::uint32_t k = 0; k < 4; ++k) {
        sum += static_cast<long double>(g.sz(k));
    }
    std::int32_t quarter = x87::ftol(sum * 1024.0L * x87::kInvQ12);
    quarter += (quarter >> 31) & 3;
    const std::int32_t mac0 = quarter >> 2;
    g.set_mac0_copy(static_cast<std::uint32_t>(mac0));
    const std::uint32_t top = static_cast<std::uint32_t>(x87::ftol(65536.0L - 1.0L)) & 0xffff;
    const std::uint32_t low = static_cast<std::uint32_t>(mac0) & 0xffff;
    g.set_otz(static_cast<std::uint16_t>(low > top ? top : low));
}

// 0x004e23a0: MAC = (IR * IR0) >> 12, IR = clamp(MAC), then the colour
void MathKit::interpolate_gpf12() {
    const RuntimeGteRegisterFileView g(m_, kRegisters);
    const RuntimeShortVector3View ir = g.ir();
    const std::int32_t factor = g.ir0();
    std::uint32_t mac[3];
    for (std::uint32_t k = 0; k < 3; ++k) {
        mac[k] = static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::uint32_t>(component(ir, k)) * static_cast<std::uint32_t>(factor)) >> 12);
    }
    set_mac(g, mac[0], mac[1], mac[2]);
    clamp_ir_signed();
    colour_from_ir();
}

// 0x004e23f0: MAC += IR * IR0 (no shift)
void MathKit::interpolate_gpl0() {
    const RuntimeGteRegisterFileView g(m_, kRegisters);
    const RuntimeShortVector3View ir = g.ir();
    const RuntimeIntVector3View old = g.mac();
    const std::int32_t factor = g.ir0();
    std::uint32_t mac[3];
    for (std::uint32_t k = 0; k < 3; ++k) {
        mac[k] = component(old, k) + static_cast<std::uint32_t>(component(ir, k)) * static_cast<std::uint32_t>(factor);
    }
    set_mac(g, mac[0], mac[1], mac[2]);
    clamp_ir_signed();
    colour_from_ir();
}

// 0x004e2450: MAC += (IR * IR0) >> 12
void MathKit::interpolate_gpl12() {
    const RuntimeGteRegisterFileView g(m_, kRegisters);
    const RuntimeShortVector3View ir = g.ir();
    const RuntimeIntVector3View old = g.mac();
    const std::int32_t factor = g.ir0();
    std::uint32_t mac[3];
    for (std::uint32_t k = 0; k < 3; ++k) {
        const std::int32_t product = static_cast<std::int32_t>(static_cast<std::uint32_t>(component(ir, k)) * static_cast<std::uint32_t>(factor)) >> 12;
        mac[k] = component(old, k) + static_cast<std::uint32_t>(product);
    }
    set_mac(g, mac[0], mac[1], mac[2]);
    clamp_ir_signed();
    colour_from_ir();
}

void MathKit::matrix_op(std::uint32_t matrix_address, std::uint32_t vector_address, std::uint32_t addend_address, bool shift, bool unsigned_clamp) {
    const RuntimeGteRegisterFileView g(m_, kRegisters);
    const RuntimeQ12MatrixView matrix(m_, matrix_address);
    const RuntimeShortVector3View vector(m_, vector_address);
    const RuntimeIntVector3View addend(m_, addend_address);
    const std::uint32_t v[3] = {static_cast<std::uint32_t>(component(vector, 0)), static_cast<std::uint32_t>(component(vector, 1)), static_cast<std::uint32_t>(component(vector, 2))};
    std::uint32_t mac[3];
    for (std::uint32_t r = 0; r < 3; ++r) {
        std::uint32_t sum = 0;
        for (std::uint32_t c = 0; c < 3; ++c) {
            sum += static_cast<std::uint32_t>(element(matrix, r, c)) * v[c];
        }
        if (shift) {
            sum = static_cast<std::uint32_t>(static_cast<std::int32_t>(sum) >> 12);
        }
        if (addend_address != 0) {
            sum += component(addend, r);
        }
        mac[r] = sum;
    }
    set_mac(g, mac[0], mac[1], mac[2]);
    if (unsigned_clamp) {
        clamp_ir_unsigned();
    } else {
        clamp_ir_signed();
    }
}

void MathKit::lcv0() { matrix_op(kColour, kV0, 0, true, false); }                      // 0x004e24b0
void MathKit::lcv0_lm() { matrix_op(kColour, kV0, 0, true, true); }                    // 0x004e2550
void MathKit::lcv1() { matrix_op(kColour, kV1, 0, true, false); }                      // 0x004e25f0
void MathKit::lcv1_lm() { matrix_op(kColour, kV1, 0, true, true); }                    // 0x004e2690
void MathKit::lcv2() { matrix_op(kColour, kV2, 0, true, false); }                      // 0x004e2730
void MathKit::lcir() { matrix_op(kColour, kIr, 0, true, false); }                      // 0x004e27d0
void MathKit::lcirtr() { matrix_op(kColour, kIr, kTranslation, true, false); }         // 0x004e2870
void MathKit::llir_sf0() { matrix_op(kLight, kIr, 0, false, false); }                  // 0x004e2930
void MathKit::llirbk() { matrix_op(kLight, kIr, kBackground, true, false); }           // 0x004e29d0
void MathKit::llv0() { matrix_op(kLight, kV0, 0, true, false); }                       // 0x004e2a90
void MathKit::llv0bk() { matrix_op(kLight, kV0, kBackground, true, false); }           // 0x004e2b30
void MathKit::llv0bk_lm() { matrix_op(kLight, kV0, kBackground, true, true); }         // 0x004e2bf0
void MathKit::llv1bk() { matrix_op(kLight, kV1, kBackground, true, false); }           // 0x004e2cb0
void MathKit::llv1bk_lm() { matrix_op(kLight, kV1, kBackground, true, true); }         // 0x004e2d70
void MathKit::llv2bk() { matrix_op(kLight, kV2, kBackground, true, false); }           // 0x004e2e30
void MathKit::rt() { matrix_op(kRotation, kV0, kTranslation, true, false); }           // 0x004e3070
void MathKit::rtir() { matrix_op(kRotation, kIr, 0, true, false); }                    // 0x004e3130
void MathKit::rtirtr() { matrix_op(kRotation, kIr, kTranslation, true, false); }       // 0x004e31d0
void MathKit::rtir_sf0() { matrix_op(kRotation, kIr, 0, false, false); }               // 0x004e3290
void MathKit::rtv0() { matrix_op(kRotation, kV0, 0, true, false); }                    // 0x004e39a0
void MathKit::rtv0tr() { matrix_op(kRotation, kV0, kTranslation, true, false); }       // 0x004e3a40
void MathKit::rtv0_sf0() { matrix_op(kRotation, kV0, 0, false, false); }               // 0x004e3b00
void MathKit::rtv1() { matrix_op(kRotation, kV1, 0, true, false); }                    // 0x004e3ba0
void MathKit::rtv2() { matrix_op(kRotation, kV2, 0, true, false); }                    // 0x004e3c40

// 0x004e2ef0: the dispatcher of the PS1 `mvmva` macro. Only RT x V0 (first argument 0, third 0, no shift), RT x IR (first 0, third 3, no shift) and RT x V0 shifted (first 1) are wired; the others return
void MathKit::mvmva(std::uint32_t mode, std::uint32_t source) {
    if (mode == 0) {
        if (source == 0) {
            rtv0_sf0();
        } else if (source == 3) {
            rtir_sf0();
        }
    } else if (mode == 1) {
        rtv0();
    }
}

// 0x004e2f20: MAC0 = the signed area of the screen triangle, stored twice
void MathKit::nclip() {
    const RuntimeGteRegisterFileView g(m_, kRegisters);
    const std::uint32_t x0 = widen(g.screen_xy(0)), y0 = widen(g.screen_xy(1));
    const std::uint32_t x1 = widen(g.screen_xy(2)), y1 = widen(g.screen_xy(3));
    const std::uint32_t x2 = widen(g.screen_xy(4)), y2 = widen(g.screen_xy(5));
    const std::uint32_t area = x0 * y2 - x0 * y1 - y2 * x1 - x2 * y0 + y1 * x2 + x1 * y0;
    g.set_mac0_copy(area);
    g.set_mac0(area);
}

// 0x004e2f80 and 0x004e2ff0: the outer product of the rotation matrix's diagonal with IR (the PS1 `op`); the second shifts by 12
void MathKit::cross_sf0() {
    const RuntimeGteRegisterFileView g(m_, kRegisters);
    const RuntimeQ12MatrixView rotation = g.rotation();
    const RuntimeShortVector3View ir = g.ir();
    const std::uint32_t d1 = static_cast<std::uint32_t>(element(rotation, 0, 0)), d2 = static_cast<std::uint32_t>(element(rotation, 1, 1)), d3 = static_cast<std::uint32_t>(element(rotation, 2, 2));
    const std::uint32_t i1 = static_cast<std::uint32_t>(component(ir, 0)), i2 = static_cast<std::uint32_t>(component(ir, 1)), i3 = static_cast<std::uint32_t>(component(ir, 2));
    set_mac(g, d2 * i3 - d3 * i2, i1 * d3 - d1 * i3, d1 * i2 - i1 * d2);
    clamp_ir_signed();
}

void MathKit::cross_sf12() {
    const RuntimeGteRegisterFileView g(m_, kRegisters);
    const RuntimeQ12MatrixView rotation = g.rotation();
    const RuntimeShortVector3View ir = g.ir();
    const std::uint32_t d1 = static_cast<std::uint32_t>(element(rotation, 0, 0)), d2 = static_cast<std::uint32_t>(element(rotation, 1, 1)), d3 = static_cast<std::uint32_t>(element(rotation, 2, 2));
    const std::uint32_t i1 = static_cast<std::uint32_t>(component(ir, 0)), i2 = static_cast<std::uint32_t>(component(ir, 1)), i3 = static_cast<std::uint32_t>(component(ir, 2));
    const auto shifted = [](std::uint32_t v) { return static_cast<std::uint32_t>(static_cast<std::int32_t>(v) >> 12); };
    set_mac(g, shifted(d2 * i3 - d3 * i2), shifted(i1 * d3 - d1 * i3), shifted(d1 * i2 - i1 * d2));
    clamp_ir_signed();
}

// 0x004e3ce0: MAC = IR * IR, then the unsigned clamp
void MathKit::sqr0() {
    const RuntimeGteRegisterFileView g(m_, kRegisters);
    const RuntimeShortVector3View ir = g.ir();
    std::uint32_t mac[3];
    for (std::uint32_t k = 0; k < 3; ++k) {
        const std::uint32_t v = static_cast<std::uint32_t>(component(ir, k));
        mac[k] = v * v;
    }
    set_mac(g, mac[0], mac[1], mac[2]);
    clamp_ir_unsigned();
}

// The perspective transform of one vertex (the body of 0x004e3330, and three times that of 0x004e34e0): MAC = RT * V + TR, IR = clamp, SZ = MAC3 clamped to [0, 65535]; when SZ is not zero the screen
// position is (IR * (H << 12) / SZ >> 12) + OFX / OFY, each limited to [low, 1023] as a short. `sz_index` is the SZ register the depth lands in, `sx_index` the first of the two screen shorts.
void MathKit::project_vertex(std::uint32_t vector, std::size_t sz_index, std::size_t sx_index, std::int32_t low) {
    const RuntimeGteRegisterFileView g(m_, kRegisters);
    matrix_op(kRotation, vector, kTranslation, true, false);
    std::int32_t z = g.mac().z();
    if (z < 0) {
        z = 0;
    } else {
        const std::int32_t top = x87::ftol(65536.0L - 1.0L) & 0xffff;
        if (z > top) {
            z = top;
        }
    }
    g.set_sz(sz_index, static_cast<std::uint16_t>(z));
    if (static_cast<std::int16_t>(z) == 0) {
        return;
    }
    const std::int32_t quotient = static_cast<std::int32_t>(g.h() << 12) / z;
    const RuntimeShortVector3View ir = g.ir();
    const auto place = [&](std::int32_t ir_value, std::uint32_t offset, std::size_t slot) {
        const std::uint32_t scaled = static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::uint32_t>(ir_value) * static_cast<std::uint32_t>(quotient)) >> 12) + offset;
        const std::int16_t v = static_cast<std::int16_t>(scaled);
        const std::int32_t placed = v < low ? low : (v > 0x3ff ? 0x3ff : v);
        g.set_screen_xy(slot, static_cast<std::int16_t>(placed));
    };
    place(component(ir, 0), g.ofx(), sx_index);
    place(component(ir, 1), g.ofy(), sx_index + 1);
}

// 0x004e3330: one vertex (V0) into SXY1 and SZ0, the other screen slots cleared
void MathKit::rtps() {
    const RuntimeGteRegisterFileView g(m_, kRegisters);
    for (std::size_t k = 0; k < 6; ++k) {
        g.set_screen_xy(k, 0);
    }
    project_vertex(kV0, 0, 2, -1024);
}

// 0x004e34e0: three vertices (V0 to SXY0 / SZ3, V1 to SXY2 / SZ2, V2 to SXY1 / SZ1), the other slots cleared
void MathKit::rtpt() {
    const RuntimeGteRegisterFileView g(m_, kRegisters);
    for (std::size_t k = 0; k < 4; ++k) {
        g.set_sz(k, 0);
    }
    for (std::size_t k = 0; k < 6; ++k) {
        g.set_screen_xy(k, 0);
    }
    project_vertex(kV0, 3, 0, -1023);
    project_vertex(kV1, 2, 4, -1023);
    project_vertex(kV2, 1, 2, -1023);
}

}  // namespace opentony::runtime::mathkit
