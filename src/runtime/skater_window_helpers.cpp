#include "auto_views.hpp"
#include "skater_window_helpers.hpp"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace opentony::runtime::frontend {

using namespace opentony::runtime::autoviews;

namespace {

std::int32_t add(std::int32_t a, std::int32_t b) { return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) + static_cast<std::uint32_t>(b)); }
std::int32_t sub_(std::int32_t a, std::int32_t b) { return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) - static_cast<std::uint32_t>(b)); }
std::int32_t mul(std::int32_t a, std::int32_t b) { return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) * static_cast<std::uint32_t>(b)); }
std::int32_t shl(std::int32_t a, int n) { return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) << n); }
std::uint32_t u(std::int32_t v) { return static_cast<std::uint32_t>(v); }
// `cdq; sub eax, edx; sar eax, 1`: halve, rounding toward zero
std::int32_t half(std::int32_t v) { return (v - (v >> 31)) >> 1; }
// `(x + ((x >> 31) & mask)) >> n`: divide by 2^n rounding toward zero
std::int32_t div_pow2(std::int32_t x, int n) {
    const std::int32_t mask = (1 << n) - 1;
    return add(x, (x >> 31) & mask) >> n;
}

constexpr std::uint32_t kCharacterRows = 0x0053a514;        // + character * 0x170: the flag byte (bit 1 = custom)
constexpr std::uint32_t kRegionRecords = 0x0056d428;        // PsxRegionNamesBySlot: 0x44-byte records
constexpr std::uint32_t kRegionModels = 0x0056d43c;         // + region * 0x44: the model table pointer of the record
constexpr std::uint32_t kRegionLoaded = 0x0056d431;         // + region * 0x44: byte, non-zero when the region holds an animated skater
constexpr std::uint32_t kRegionFirstByte = 0x0056d428;
constexpr std::uint32_t kSpoolObject = 0x0056db4c;
constexpr std::uint32_t kLightTemplate = 0x0053dc1c;
constexpr std::uint32_t kAnimationTimeScale = 0x0056865c;
constexpr std::uint32_t kViewTable = 0x00560fd4;
constexpr std::uint32_t kColourBytesA = 0x0055faf8;
constexpr std::uint32_t kColourBytesB = 0x0055fafa;
constexpr std::uint32_t kColourBytesC = 0x0055fb0e;
constexpr std::uint32_t kColourBytesD = 0x0055fb36;
constexpr std::uint32_t kColourBytesE = 0x0055fb38;

}  // namespace

// ---- plain helpers --------------------------------------------------------------------------------------------

// 0x00418200: the light rig of the front end's skater view: the template at 0x0053dc1c (thirteen words), then a few halved, doubled and
// scaled fields.
void SkaterWindowHelpers::light_defaults(std::uint32_t light) {
    check(light != 0, kMsgNoLight);
    for (std::uint32_t i = 0; i < 13; ++i) {
        set_word_at(m_, light + 4 * i, word_at(m_, kLightTemplate + 4 * i));
    }
    set_half_at(m_, light + 0x16, u(short_at(m_, light + 0x16) / 2));
    set_half_at(m_, light + 0x0c, 0xf04f);
    set_half_at(m_, light + 0x1c, u(short_at(m_, light + 0x1c) / 2));
    set_half_at(m_, light + 0x0e, 0);
    set_half_at(m_, light + 0x22, u(short_at(m_, light + 0x22) / 2));
    set_half_at(m_, light + 0x1e, static_cast<std::uint32_t>(half_at(m_, light + 0x1e)) << 1);
    set_half_at(m_, light + 0x18, static_cast<std::uint32_t>(half_at(m_, light + 0x18)) << 1);
    set_half_at(m_, light + 0x10, 0xfce1);
    set_word_at(m_, light + 0x28, u(half(mul(int_at(m_, light + 0x28), 3))));
    set_half_at(m_, light + 0x24, static_cast<std::uint32_t>(half_at(m_, light + 0x24)) << 1);
    set_word_at(m_, light + 0x2c, u(half(mul(int_at(m_, light + 0x2c), 3))));
    set_word_at(m_, light + 0x30, u(half(mul(int_at(m_, light + 0x30), 3))));
}

// 0x004182e0: two colour indices of a hair or skin entry: when byte 0x0055faf8[b] names `a` they are the table's alternate pair, else the pair
// of `a` itself.
void SkaterWindowHelpers::colour_indices(std::uint32_t a, std::uint32_t b, std::uint32_t out_a, std::uint32_t out_b) {
    if (byte_at(m_, kColourBytesA + b) == (a & 0xff)) {
        set_word_at(m_, out_a, byte_at(m_, kColourBytesD + b));
        set_word_at(m_, out_b, byte_at(m_, kColourBytesE + b));
        return;
    }
    set_word_at(m_, out_a, byte_at(m_, kColourBytesB + a));
    set_word_at(m_, out_b, byte_at(m_, kColourBytesC + a));
}

// 0x00418330: walk a list of (tag, size, data) chunks ended by tag 0xffffffff; the data of the first "RGBs" chunk, or 0.
std::uint32_t SkaterWindowHelpers::find_rgbs_chunk(std::uint32_t chunks) {
    std::uint32_t at = chunks;
    std::uint32_t tag = word_at(m_, at);
    if (tag == 0xffffffff) {
        return 0;
    }
    for (;;) {
        const std::uint32_t size = word_at(m_, at + 4);
        at += 8;
        if (tag == 0x73424752) {
            return at;
        }
        tag = word_at(m_, at + size);
        at += size;
        if (tag == 0xffffffff) {
            return 0;
        }
    }
}

// 0x00418360: the material of a skater's model (the one with the first head part) that uses the most palette entries. Custom skaters answer 0.
std::uint32_t SkaterWindowHelpers::busiest_material(std::int32_t character, std::uint32_t entry) {
    std::uint32_t best_material = 0;
    if ((byte_at(m_, kCharacterRows + u(mul(character, 0x170))) & 2) != 0) {
        return best_material;
    }
    const std::uint32_t region = byte_at(m_, entry + 0x1f);
    const std::uint32_t record = kRegionRecords + region * 0x44;
    const std::uint32_t models = word_at(m_, kRegionModels + region * 0x44);
    const std::int32_t count = int_at(m_, models - 4);
    std::int32_t index = 0;
    do {
        if (byte_at(m_, entry + 0x150 + u(index)) == 0x10) {
            break;
        }
        ++index;
    } while (index < 0x13);
    check(index >= 0 && count > index, kMsgModelIndexRange);
    const std::uint32_t model = word_at(m_, models + 4 * u(index));
    // PSX_CollectModelMaterials(record, model, materials, &count): the service answers the count, then that many materials
    std::vector<std::uint32_t> materials;
    std::int32_t found = 0;
    if (s_.live()) {
        // live the function fills the array and the count it is given (stack locals of the retail caller; here pc_platform's scratch window)
        constexpr std::uint32_t kList = 0x7ff0c000;
        constexpr std::uint32_t kCount = 0x7ff0c400;
        set_global_word(m_, kCount, 0);
        engine(0x004b4ad0, {record, model, kList, kCount});
        found = global_int(m_, kCount);
        for (std::int32_t i = 0; i < found; ++i) {
            materials.push_back(word_at(m_, kList + 4 * u(i)));
        }
    } else {
        engine(0x004b4ad0, {record, model});
        found = s_.value(engine_name(0x004b4ad0));
        for (std::int32_t i = 0; i < found; ++i) {
            materials.push_back(static_cast<std::uint32_t>(s_.value(engine_name(0x004b4ad0))));
        }
    }
    std::int32_t best = 0;
    for (std::int32_t i = 0; i < found; ++i) {
        const std::int32_t score = static_cast<std::int32_t>(engine_value(0x004b4e00, {materials[u(i)]}));
        if (best < score) {
            best_material = materials[u(i)];
            best = score;
        }
    }
    return best_material;
}

// 0x00418480: reload the face bitmap of a skater: the texture set entry the two colour indices name gives "XX", the name "XXg" is loaded
// into the bitmap.
void SkaterWindowHelpers::load_face_bitmap(std::uint32_t bitmap, std::uint32_t a, std::uint32_t b) {
    check(bitmap != 0, kMsgNoChecksum);
    if (bitmap == 0) {
        return;
    }
    // the two indices go through locals in retail; the pair is the same as colour_indices writes
    std::uint32_t first = 0;
    std::uint32_t second = 0;
    if (byte_at(m_, kColourBytesA + b) == (a & 0xff)) {
        first = byte_at(m_, kColourBytesD + b);
        second = byte_at(m_, kColourBytesE + b);
    } else {
        first = byte_at(m_, kColourBytesB + a);
        second = byte_at(m_, kColourBytesC + a);
    }
    const std::uint32_t found = engine_value(0x004547f0, {second, first});
    // sprintf(name, "%s", *found): the name is kept in a six-byte local whose third byte is then overwritten with 'g'
    std::string name = m_.cstring(word_at(m_, found), 0x40);
    if (name.size() < 3) {
        name.resize(3, '\0');
    }
    name[2] = 'g';
    const std::size_t end = name.find('\0');
    if (end != std::string::npos) {
        name.resize(end);
    }
    engine(0x004b5300, {global_word(m_, kSpoolObject)});
    if (s_.live()) {
        // the name is a local of the retail function (here pc_platform's scratch window); the probe's stub reports it as the call's text
        constexpr std::uint32_t kNameLocal = 0x7ff0d000;
        std::vector<std::uint8_t> bytes(name.begin(), name.end());
        bytes.push_back(0);
        m_.write(kNameLocal, bytes);
        engine(0x004674d0, {bitmap, kNameLocal}, name);
    } else {
        engine(0x004674d0, {bitmap}, name);
    }
    engine(0x004b5350, {global_word(m_, kSpoolObject)});
}

// 0x00419420: is the replay level flagged (byte 0x00560080 set, or the level's flag with game mode 1)
bool SkaterWindowHelpers::in_replay_career() {
    if (global_byte(m_, 0x00560080) != 0) {
        return true;
    }
    const std::uint32_t level = global_word(m_, 0x00561c90);
    if (byte_at(m_, 0x0053c240 + level * 0x1ac) == 0) {
        return false;
    }
    return global_word(m_, 0x00533f38) == 1;
}

// ---- ColourText -------------------------------------------------------------------------------------------------

std::uint32_t SkaterWindowHelpers::colour_text_construct(std::uint32_t self, std::uint32_t id, std::uint32_t text, std::uint32_t props,
                                                         std::uint32_t table, std::uint32_t count, std::uint32_t left, std::uint32_t right) {
    menu_.text_construct(self, id, text, props);
    skater_window_helpers_colour_at(m_, self).set_field_0(static_cast<Word>(kVtColourText));
    skater_window_helpers_colour_at(m_, self).set_field_54(0xff);
    skater_window_helpers_colour_at(m_, self).set_field_55(0);
    skater_window_helpers_colour_at(m_, self).set_field_56(0xff);
    skater_window_helpers_colour_at(m_, self).set_field_60(static_cast<Word>(table));
    skater_window_helpers_colour_at(m_, self).set_field_5c(static_cast<Word>(count));
    skater_window_helpers_colour_at(m_, self).set_field_58(0);
    colour_text_set_custom(self, 0, 0x80, 0x80);
    skater_window_helpers_colour_at(m_, self).set_field_74(static_cast<Word>(right));
    skater_window_helpers_colour_at(m_, self).set_field_70(static_cast<Word>(left));
    skater_window_helpers_colour_at(m_, self).set_field_78(0x1e);
    return self;
}

std::uint32_t SkaterWindowHelpers::colour_text_delete(std::uint32_t self, std::uint32_t flag) {
    colour_text_destroy(self);
    if ((flag & 1) != 0) {
        operator_delete(self);
    }
    return self;
}

void SkaterWindowHelpers::colour_text_destroy(std::uint32_t self) { menu_.text_destroy(self); }

void SkaterWindowHelpers::colour_text_draw(std::uint32_t self) {
    const std::uint32_t height = global_word(m_, kScreenFillHeight);
    s_.note(kSvcFillRect, {0, 0, 0x200, height, 0});
    if (skater_window_helpers_colour_at(m_, self).field_c() != 0) {
        s_.note(kSvcFrameBox, {u(add(skater_window_helpers_colour_at(m_, self).field_1c(), 0x78)), skater_window_helpers_colour_at(m_, self).field_20(), 0x50, 8, skater_window_helpers_colour_at(m_, self).field_54(), skater_window_helpers_colour_at(m_, self).field_55(),
                               skater_window_helpers_colour_at(m_, self).field_56()});
    }
    menu_.text_draw(self);
    if (skater_window_helpers_colour_at(m_, self).field_e() != 0 && static_cast<std::int32_t>(skater_window_helpers_colour_at(m_, self).field_78()) > 0x0f) {
        const std::int32_t y = add(half(skater_window_helpers_colour_at(m_, self).field_18()), static_cast<std::int32_t>(skater_window_helpers_colour_at(m_, self).field_20()));
        if (skater_window_helpers_colour_at(m_, self).field_70() != 0) {
            s_.note(kSpriteDrawService, {skater_window_helpers_colour_at(m_, self).field_70(), u(add(skater_window_helpers_colour_at(m_, self).field_1c(), 0x72)), u(y), 0});
        }
        if (skater_window_helpers_colour_at(m_, self).field_74() != 0) {
            s_.note(kSpriteDrawService, {skater_window_helpers_colour_at(m_, self).field_74(), u(add(skater_window_helpers_colour_at(m_, self).field_1c(), 0xce)), u(y), 0});
        }
    }
    const std::int32_t timer = add(static_cast<std::int32_t>(skater_window_helpers_colour_at(m_, self).field_78()), -1);
    skater_window_helpers_colour_at(m_, self).set_field_78(static_cast<Word>(u(timer)));
    if (timer <= 0) {
        skater_window_helpers_colour_at(m_, self).set_field_78(0x1e);
    }
    s_.note(kSvcFillRect, {0, 0, 0x200, global_word(m_, kScreenFillHeight), 0});
}

void SkaterWindowHelpers::colour_text_set_custom(std::uint32_t self, std::uint32_t hue, std::uint32_t sat, std::uint32_t val) {
    skater_window_helpers_colour_at(m_, self).set_field_64(static_cast<Word>(hue));
    skater_window_helpers_colour_at(m_, self).set_field_68(static_cast<Word>(sat));
    skater_window_helpers_colour_at(m_, self).set_field_6c(static_cast<Word>(val));
    colour_text_swatch(self, skater_window_helpers_colour_at(m_, self).field_58(), self + 0x54, self + 0x55, self + 0x56);
}

// 0x00418720: the swatch colour of preset `index`: its hue, saturation and value (preset 0 is the custom colour, saturation and value 0x80),
// scaled by the custom factors, converted to RGB through the colour code of 4.9.
void SkaterWindowHelpers::colour_text_swatch(std::uint32_t self, std::uint32_t index, std::uint32_t out_r, std::uint32_t out_g,
                                             std::uint32_t out_b) {
    const std::uint32_t row = skater_window_helpers_colour_at(m_, self).field_60() + index * 12;
    std::int32_t sat = int_at(m_, row + 4);
    std::int32_t hue = int_at(m_, row);
    std::int32_t val = int_at(m_, row + 8);
    if (index == 0) {
        hue = static_cast<std::int32_t>(skater_window_helpers_colour_at(m_, self).field_64());
        sat = 0x80;
        val = 0x80;
    }
    std::int32_t scaled_val = shl(val, 20);
    scaled_val = div_pow2(scaled_val, 7) >> 10;
    scaled_val = mul(scaled_val, shl(static_cast<std::int32_t>(skater_window_helpers_colour_at(m_, self).field_6c()), 20) >> 10);
    scaled_val = mul(div_pow2(scaled_val, 9), 5);
    std::int32_t scaled_sat = shl(sat, 20);
    scaled_sat = div_pow2(scaled_sat, 7) >> 10;
    scaled_sat = mul(scaled_sat, shl(static_cast<std::int32_t>(skater_window_helpers_colour_at(m_, self).field_68()), 20) >> 10);
    colour_.hsv_to_rgb(shl(hue, 20), div_pow2(scaled_sat, 7), div_pow2(scaled_val, 2), out_r, out_g, out_b);
}

void SkaterWindowHelpers::colour_text_colour(std::uint32_t self, std::uint32_t out_h, std::uint32_t out_s, std::uint32_t out_v) {
    check(out_h != 0, kMsgNoPointerHue);
    check(out_s != 0, kMsgNoPointerSat);
    check(out_v != 0, kMsgNoPointerVal);
    const std::uint32_t index = skater_window_helpers_colour_at(m_, self).field_58();
    if (index == 0) {
        set_word_at(m_, out_h, 0xffffffff);
        set_word_at(m_, out_s, 0xffffffff);
        set_word_at(m_, out_v, 0xffffffff);
        return;
    }
    const std::uint32_t row = skater_window_helpers_colour_at(m_, self).field_60() + index * 12;
    set_word_at(m_, out_h, word_at(m_, row));
    set_word_at(m_, out_s, word_at(m_, row + 4));
    set_word_at(m_, out_v, word_at(m_, row + 8));
}

void SkaterWindowHelpers::colour_text_set_index(std::uint32_t self, std::int32_t index) {
    check(index >= 0 && index < static_cast<std::int32_t>(skater_window_helpers_colour_at(m_, self).field_5c()), kMsgValueRange);
    skater_window_helpers_colour_at(m_, self).set_field_58(static_cast<Word>(u(index)));
}

std::uint32_t SkaterWindowHelpers::colour_text_index(std::uint32_t self) const { return word_at(m_, self + 0x58); }

void SkaterWindowHelpers::colour_text_step(std::uint32_t self, std::uint32_t direction) {
    if (direction == 0) {
        skater_window_helpers_colour_at(m_, self).set_field_58(static_cast<Word>(u(add(static_cast<std::int32_t>(skater_window_helpers_colour_at(m_, self).field_58()), -1))));
    } else if (direction == 1) {
        skater_window_helpers_colour_at(m_, self).set_field_58(static_cast<Word>(u(add(static_cast<std::int32_t>(skater_window_helpers_colour_at(m_, self).field_58()), 1))));
    }
    if (static_cast<std::int32_t>(skater_window_helpers_colour_at(m_, self).field_58()) < 0) {
        skater_window_helpers_colour_at(m_, self).set_field_58(static_cast<Word>(u(add(static_cast<std::int32_t>(skater_window_helpers_colour_at(m_, self).field_5c()), -1))));
    }
    if (static_cast<std::int32_t>(skater_window_helpers_colour_at(m_, self).field_58()) >= static_cast<std::int32_t>(skater_window_helpers_colour_at(m_, self).field_5c())) {
        skater_window_helpers_colour_at(m_, self).set_field_58(0);
    }
}

// ---- StepperRow -------------------------------------------------------------------------------------------------

std::uint32_t SkaterWindowHelpers::stepper_construct(std::uint32_t self, std::uint32_t id, std::uint32_t props, std::uint32_t words,
                                                     std::int32_t count, std::uint32_t lit) {
    menu_.control_b_construct(self, id);
    skater_window_helpers_stepper_at(m_, self).set_field_0(static_cast<Word>(kVtStepperRow));
    skater_window_helpers_stepper_at(m_, self).set_field_f4(static_cast<Word>(u(count)));
    for (std::int32_t i = 0; i < count; ++i) {
        const std::uint32_t dest = self + 0x31 + 0x20 * u(i);
        std::string word = m_.cstring(word_at(m_, words + 4 * u(i)), 0x1000);
        std::vector<std::uint8_t> bytes(word.begin(), word.end());
        bytes.push_back(0);
        m_.write(dest, bytes);
        if (i < add(static_cast<std::int32_t>(skater_window_helpers_stepper_at(m_, self).field_f4()), -1)) {
            set_byte_at(m_, dest + word.size(), 0x20);
            set_byte_at(m_, dest + word.size() + 1, 0);
        }
    }
    skater_window_helpers_stepper_at(m_, self).set_field_c(1);
    skater_window_helpers_stepper_at(m_, self).set_field_d(0);
    skater_window_helpers_stepper_at(m_, self).set_field_30(0);
    skater_window_helpers_stepper_at(m_, self).set_field_10(static_cast<Word>(lit));
    skater_window_helpers_stepper_at(m_, self).set_field_fc(static_cast<Word>(props));
    set_word_at(m_, props + 0x34, word_at(m_, props + 0x150));
    skater_window_helpers_stepper_at(m_, self).set_field_14(0);
    skater_window_helpers_stepper_at(m_, self).set_field_18(0);
    for (std::int32_t i = 0; i < static_cast<std::int32_t>(skater_window_helpers_stepper_at(m_, self).field_f4()); ++i) {
        const std::uint32_t word = self + 0x31 + 0x20 * u(i);
        skater_window_helpers_stepper_at(m_, self).set_field_14(static_cast<Word>(u(add(static_cast<std::int32_t>(skater_window_helpers_stepper_at(m_, self).field_14()), f_.text_width(props + 4, word)))));
        const std::int32_t extent = f_.text_extent(props + 4, word);
        if (static_cast<std::int32_t>(skater_window_helpers_stepper_at(m_, self).field_18()) < extent) {
            skater_window_helpers_stepper_at(m_, self).set_field_18(static_cast<Word>(u(extent)));
        }
    }
    return self;
}

std::uint32_t SkaterWindowHelpers::stepper_delete(std::uint32_t self, std::uint32_t flag) {
    stepper_destroy(self);
    if ((flag & 1) != 0) {
        operator_delete(self);
    }
    return self;
}

void SkaterWindowHelpers::stepper_destroy(std::uint32_t self) {
    skater_window_helpers_stepper_at(m_, self).set_field_0(static_cast<Word>(kVtStepperRow));
    menu_.control_destroy(self);
}

void SkaterWindowHelpers::control_destroy_thunk(std::uint32_t self) { menu_.control_destroy(self); }

void SkaterWindowHelpers::stepper_position(std::uint32_t self, std::int32_t x, std::int32_t y) {
    skater_window_helpers_stepper_at(m_, self).set_field_1c(static_cast<Word>(u(x)));
    skater_window_helpers_stepper_at(m_, self).set_field_20(static_cast<Word>(u(y)));
}

void SkaterWindowHelpers::stepper_draw(std::uint32_t self) {
    const std::uint32_t just = skater_window_helpers_stepper_at(m_, self).field_2c();
    check(just == 0 || just == 1, kMenuMsgJustification);
    const std::uint32_t props = skater_window_helpers_stepper_at(m_, self).field_fc();
    set_word_at(m_, props + 0x08, 0);
    set_word_at(m_, props + 0x0c, 1);
    if (skater_window_helpers_stepper_at(m_, self).field_e() == 0) {
        set_word_at(m_, props + 0x34, word_at(m_, props + 0x154));
    } else {
        set_word_at(m_, props + 0x34, word_at(m_, props + 0x150));
    }
    std::int32_t x = 0;
    if (static_cast<std::int32_t>(skater_window_helpers_stepper_at(m_, self).field_2c()) == 1 && skater_window_helpers_stepper_at(m_, self).field_30() == 0) {
        const std::int32_t product = mul(int_at(m_, props + 0x154), static_cast<std::int32_t>(skater_window_helpers_stepper_at(m_, self).field_14()));
        const std::int32_t scaled = int_at(m_, props + 0x150) == 0 ? 0 : product / int_at(m_, props + 0x150);
        x = add(sub_(static_cast<std::int32_t>(skater_window_helpers_stepper_at(m_, self).field_14()) >> 1, scaled >> 1), static_cast<std::int32_t>(skater_window_helpers_stepper_at(m_, self).field_1c()));
    } else {
        x = static_cast<std::int32_t>(skater_window_helpers_stepper_at(m_, self).field_1c());
    }
    for (std::int32_t i = 0; i < static_cast<std::int32_t>(skater_window_helpers_stepper_at(m_, self).field_f4()); ++i) {
        const std::uint32_t word = self + 0x31 + 0x20 * u(i);
        auto colour = [&](std::uint32_t from) {
            set_word_at(m_, props + 0x18, word_at(m_, props + from));
            set_word_at(m_, props + 0x1c, word_at(m_, props + from + 4));
            set_word_at(m_, props + 0x20, word_at(m_, props + from + 8));
        };
        if ((i == 0 || i == static_cast<std::int32_t>(skater_window_helpers_stepper_at(m_, self).field_10())) && skater_window_helpers_stepper_at(m_, self).field_c() != 0) {
            colour(skater_window_helpers_stepper_at(m_, self).field_e() != 0 ? 0x158 : 0x164);
        } else {
            colour(0x170);
        }
        const std::int32_t y = add(static_cast<std::int32_t>(skater_window_helpers_stepper_at(m_, self).field_18()) >> 1, static_cast<std::int32_t>(skater_window_helpers_stepper_at(m_, self).field_20()));
        s_.note(kFontDrawService, {props + 4, u(x), u(y), word, 0}, m_.cstring(word, 0x40));
        x = add(x, f_.text_width(props + 4, word));
    }
}

void SkaterWindowHelpers::stepper_step(std::uint32_t self, std::uint32_t direction) {
    if (direction == 0) {
        const std::int32_t value = add(static_cast<std::int32_t>(skater_window_helpers_stepper_at(m_, self).field_10()), -1);
        skater_window_helpers_stepper_at(m_, self).set_field_10(static_cast<Word>(u(value)));
        if (value < 1) {
            skater_window_helpers_stepper_at(m_, self).set_field_10(static_cast<Word>(u(add(static_cast<std::int32_t>(skater_window_helpers_stepper_at(m_, self).field_f4()), -1))));
        }
    } else if (direction == 1) {
        const std::int32_t value = add(static_cast<std::int32_t>(skater_window_helpers_stepper_at(m_, self).field_10()), 1);
        skater_window_helpers_stepper_at(m_, self).set_field_10(static_cast<Word>(u(value)));
        if (value >= static_cast<std::int32_t>(skater_window_helpers_stepper_at(m_, self).field_f4())) {
            skater_window_helpers_stepper_at(m_, self).set_field_10(1);
        }
    }
}

// ---- NumberRow --------------------------------------------------------------------------------------------------

// A number as the "%d" format makes it, then the row's second column is set through the row's virtual method 9.
void SkaterWindowHelpers::sprintf_number(std::uint32_t self, std::int32_t value) {
    const std::string text = std::to_string(value);
    const std::uint32_t vt = word_at(m_, self);
    if (vt == kMenuVtRow || vt == kVtNumberRow) {
        menu_.row_set_column_from(self, text, 1, word_at(m_, self + 0xd8), word_at(m_, self + 0xec));
    } else {
        s_.note("item_set_column", {self, 1, word_at(m_, self + 0xd8), word_at(m_, self + 0xec)}, text);
    }
}

std::uint32_t SkaterWindowHelpers::number_construct(std::uint32_t self, std::uint32_t id, std::uint32_t text, std::int32_t value,
                                                    std::int32_t minimum, std::int32_t maximum, std::uint32_t offset, std::uint32_t props,
                                                    std::uint32_t props2, std::uint32_t left, std::uint32_t right) {
    menu_.row_construct(self, id, text, props, 2, left, right);
    skater_window_helpers_number_at(m_, self).set_field_108(static_cast<Word>(u(maximum)));
    skater_window_helpers_number_at(m_, self).set_field_0(static_cast<Word>(kVtNumberRow));
    skater_window_helpers_number_at(m_, self).set_field_104(static_cast<Word>(u(minimum)));
    skater_window_helpers_number_at(m_, self).set_field_100(static_cast<Word>(u(value)));
    const std::string number = std::to_string(value);
    skater_window_helpers_number_at(m_, self).set_field_d8(static_cast<Word>(offset));
    skater_window_helpers_number_at(m_, self).set_field_ec(static_cast<Word>(props2));
    menu_.row_set_column_from(self, number, 1, offset, props2);
    return self;
}

std::uint32_t SkaterWindowHelpers::number_delete(std::uint32_t self, std::uint32_t flag) {
    number_destroy(self);
    if ((flag & 1) != 0) {
        operator_delete(self);
    }
    return self;
}

void SkaterWindowHelpers::number_destroy(std::uint32_t self) {
    skater_window_helpers_number_at(m_, self).set_field_0(static_cast<Word>(kVtNumberRow));
    menu_.row_destroy(self);
}

std::uint32_t SkaterWindowHelpers::number_value(std::uint32_t self) const { return word_at(m_, self + 0x100); }

void SkaterWindowHelpers::number_set_value(std::uint32_t self, std::int32_t value) {
    check(value >= static_cast<std::int32_t>(skater_window_helpers_number_at(m_, self).field_104()) && value <= static_cast<std::int32_t>(skater_window_helpers_number_at(m_, self).field_108()), kEmptyText);
    skater_window_helpers_number_at(m_, self).set_field_100(static_cast<Word>(u(value)));
    sprintf_number(self, value);
}

void SkaterWindowHelpers::number_step(std::uint32_t self, std::uint32_t direction) {
    std::int32_t value = static_cast<std::int32_t>(skater_window_helpers_number_at(m_, self).field_100());
    if (direction == 0) {
        value = add(value, -1);
        skater_window_helpers_number_at(m_, self).set_field_100(static_cast<Word>(u(value)));
    } else if (direction == 1) {
        value = add(value, 1);
        skater_window_helpers_number_at(m_, self).set_field_100(static_cast<Word>(u(value)));
    }
    if (static_cast<std::int32_t>(skater_window_helpers_number_at(m_, self).field_100()) < static_cast<std::int32_t>(skater_window_helpers_number_at(m_, self).field_104())) {
        skater_window_helpers_number_at(m_, self).set_field_100(static_cast<Word>(skater_window_helpers_number_at(m_, self).field_104()));
    }
    if (static_cast<std::int32_t>(skater_window_helpers_number_at(m_, self).field_100()) > static_cast<std::int32_t>(skater_window_helpers_number_at(m_, self).field_108())) {
        skater_window_helpers_number_at(m_, self).set_field_100(static_cast<Word>(skater_window_helpers_number_at(m_, self).field_108()));
    }
    sprintf_number(self, static_cast<std::int32_t>(skater_window_helpers_number_at(m_, self).field_100()));
}

// ---- ModelView --------------------------------------------------------------------------------------------------

std::uint32_t SkaterWindowHelpers::model_view_construct(std::uint32_t self, std::uint32_t name, std::uint32_t view_x, std::uint32_t view_y,
                                                        std::uint32_t tag, std::int32_t x, std::int32_t y, std::int32_t z) {
    w_.element_construct(self, 0, 0, -1, -1);
    skater_window_helpers_model_at(m_, self).set_field_0(static_cast<Word>(kVtModelView));
    const std::uint32_t region = engine_value(0x004b37a0, {name, 0, 0, 0});
    skater_window_helpers_model_at(m_, self).set_field_34(static_cast<Word>(region));
    engine(0x004b3df0);
    skater_window_helpers_model_at(m_, self).set_field_30(0);
    skater_window_helpers_model_at(m_, self).set_field_2c(0);
    if (byte_at(m_, kRegionLoaded + region * 0x44) == 0) {
        std::uint32_t record = engine_value(0x0047fd30, {0x4c});
        if (record != 0) {
            record = engine_value(0x0047fdb0, {record});
        }
        skater_window_helpers_model_at(m_, self).set_field_2c(static_cast<Word>(record));
        set_word_at(m_, record + 0x40, tag);
        engine(0x0047fe30, {record, name}, m_.cstring(name, 0x40));
        set_word_at(m_, skater_window_helpers_model_at(m_, self).field_2c() + 8, u(shl(x, 12)));
        set_word_at(m_, skater_window_helpers_model_at(m_, self).field_2c() + 0x0c, u(shl(y, 12)));
        set_word_at(m_, skater_window_helpers_model_at(m_, self).field_2c() + 0x10, u(shl(z, 12)));
    } else {
        std::uint32_t record = engine_value(0x0047fd30, {0x168});
        if (record != 0) {
            record = engine_value(0x00480610, {record});
        }
        skater_window_helpers_model_at(m_, self).set_field_30(static_cast<Word>(record));
        set_word_at(m_, record + 0x40, tag);
        engine(0x0047fe30, {record, name}, m_.cstring(name, 0x40));
        set_global_word(m_, kAnimationTimeScale, 0x100);
        set_word_at(m_, skater_window_helpers_model_at(m_, self).field_30() + 0x108, 0x10000);
        engine(0x00480890, {skater_window_helpers_model_at(m_, self).field_30(), 0, 1});
        set_word_at(m_, skater_window_helpers_model_at(m_, self).field_30() + 8, u(shl(x, 12)));
        set_word_at(m_, skater_window_helpers_model_at(m_, self).field_30() + 0x0c, u(shl(y, 12)));
        set_word_at(m_, skater_window_helpers_model_at(m_, self).field_30() + 0x10, u(shl(z, 12)));
    }
    skater_window_helpers_model_at(m_, self).set_field_3c(static_cast<Word>(view_y));
    skater_window_helpers_model_at(m_, self).set_field_38(static_cast<Word>(view_x));
    set_word_at(m_, self + 0x28, 0x3c);
    return self;
}

std::uint32_t SkaterWindowHelpers::model_view_delete(std::uint32_t self, std::uint32_t flag) {
    model_view_destroy(self);
    if ((flag & 1) != 0) {
        operator_delete(self);
    }
    return self;
}

void SkaterWindowHelpers::model_view_destroy(std::uint32_t self) {
    skater_window_helpers_model_at(m_, self).set_field_0(static_cast<Word>(kVtModelView));
    const std::uint32_t region = skater_window_helpers_model_at(m_, self).field_34();
    engine(0x004b32f0, {region, 1, 0});
    set_byte_at(m_, kRegionFirstByte + region * 0x44, 0);
    if (skater_window_helpers_model_at(m_, self).field_30() != 0) {
        s_.note(kVirtualDelete, {skater_window_helpers_model_at(m_, self).field_30(), 1});
    }
    if (skater_window_helpers_model_at(m_, self).field_2c() != 0) {
        s_.note(kVirtualDelete, {skater_window_helpers_model_at(m_, self).field_2c(), 1});
    }
    w_.element_destroy(self);
}

void SkaterWindowHelpers::model_view_update(std::uint32_t self) {
    if (skater_window_helpers_model_at(m_, self).field_30() != 0) {
        const std::uint32_t at = skater_window_helpers_model_at(m_, self).field_30() + 0x16;
        set_half_at(m_, at, static_cast<std::uint32_t>(half_at(m_, at)) + 0x1e);
        return;
    }
    const std::uint32_t object = skater_window_helpers_model_at(m_, self).field_2c();
    set_half_at(m_, object + 0x16, static_cast<std::uint32_t>(half_at(m_, object + 0x16)) + half_at(m_, self + 0x28));
    set_half_at(m_, object + 0x16, half_at(m_, object + 0x16) & 0xfff);
}

void SkaterWindowHelpers::model_view_draw(std::uint32_t self) {
    if (skater_window_helpers_model_at(m_, self).field_18() < 1 && skater_window_helpers_model_at(m_, self).field_14() == 0) {
        engine(0x0045e8e0, {skater_window_helpers_model_at(m_, self).field_38(), skater_window_helpers_model_at(m_, self).field_3c(), word_at(m_, global_word(m_, kViewTable) + 0x88)});
        if (skater_window_helpers_model_at(m_, self).field_30() != 0) {
            engine(0x00461bf0, {skater_window_helpers_model_at(m_, self).field_30()});
            engine(0x0045f530, {skater_window_helpers_model_at(m_, self).field_30()});
            engine(0x0045f3f0);
            return;
        }
        engine(0x0045f530, {skater_window_helpers_model_at(m_, self).field_2c()});
        engine(0x0045f3f0);
    }
}

// ---- InitialsWindow ---------------------------------------------------------------------------------------------

std::uint32_t SkaterWindowHelpers::initials_construct(std::uint32_t self, std::uint32_t font, std::int32_t x, std::int32_t y,
                                                      std::int32_t start_x, std::int32_t start_y) {
    w_.window_construct(self, x, y, start_x, start_y);
    skater_window_helpers_initials_at(m_, self).set_field_0(static_cast<Word>(kVtInitialsWindow));
    check(font != 0, kMsgNoFontName);
    std::int32_t at = -0x1e;
    std::uint32_t slot = self + 0xac;
    do {
        std::uint32_t element = operator_new(0x30);
        if (element != 0) {
            element = w_.text_construct(element, 0x00525768, font, at, 0, -1, -1);
        }
        set_word_at(m_, slot, element);
        w_.window_add(self, element);
        at += 0x1e;
        slot += 4;
    } while (at < 0x3c);
    return self;
}

std::uint32_t SkaterWindowHelpers::initials_delete(std::uint32_t self, std::uint32_t flag) {
    initials_destroy(self);
    if ((flag & 1) != 0) {
        operator_delete(self);
    }
    return self;
}

void SkaterWindowHelpers::initials_destroy(std::uint32_t self) {
    skater_window_helpers_initials_at(m_, self).set_field_0(static_cast<Word>(kVtInitialsWindow));
    w_.window_destroy(self);
}

void SkaterWindowHelpers::initials_set_letters(std::uint32_t self, std::uint32_t a, std::uint32_t b, std::uint32_t c) {
    // each letter is a one-character string built in a local buffer (the third argument's slot too), passed to TextScreenElement_SetString
    const std::uint32_t letters[3] = {a, b, c};
    for (std::uint32_t i = 0; i < 3; ++i) {
        const char ch = static_cast<char>(letters[i] & 0xff);
        w_.text_set_string_from(word_at(m_, self + 0xac + 4 * i), ch == 0 ? std::string() : std::string(1, ch));
    }
}

// ---- virtual dispatch --------------------------------------------------------------------------------------------

// The slot numbers are the method numbers of the toolkit (see menu_dispatch.cpp): 0 delete, 1 position, 2 draw, 3 highlight, 4 value,
// 5 step, 6 nothing, 7 engine, 8 set text, 9 set column. A slot a class does not implement natively falls through (the caller reports it).
bool SkaterWindowHelpers::dispatch(std::uint32_t object, std::uint32_t slot, const std::vector<std::uint32_t>& args) {
    const std::uint32_t vt = word_at(m_, object);
    const std::uint32_t a0 = args.empty() ? 0 : args[0];
    const std::uint32_t a1 = args.size() < 2 ? 0 : args[1];
    auto s32 = [](std::uint32_t v) { return static_cast<std::int32_t>(v); };
    if (vt == kVtColourText) {
        switch (slot) {
        case 0: colour_text_delete(object, a0); return true;
        case 1: menu_.item_set_position(object, s32(a0), s32(a1)); return true;
        case 2: colour_text_draw(object); return true;
        case 3: menu_.control_set_highlighted(object, a0); return true;
        case 4: menu_.control_set_value(object, a0); return true;
        case 5: colour_text_step(object, a0); return true;
        case 6: return true;
        case 8: menu_.text_set_text(object, a0); return true;
        default: return false;
        }
    }
    if (vt == kVtStepperRow) {
        switch (slot) {
        case 0: stepper_delete(object, a0); return true;
        case 1: stepper_position(object, s32(a0), s32(a1)); return true;
        case 2: stepper_draw(object); return true;
        case 3: menu_.control_set_highlighted(object, a0); return true;
        case 4: menu_.control_set_value(object, a0); return true;
        case 5: stepper_step(object, a0); return true;
        case 6: return true;
        default: return false;
        }
    }
    if (vt == kVtNumberRow) {
        switch (slot) {
        case 0: number_delete(object, a0); return true;
        case 1: menu_.item_set_position(object, s32(a0), s32(a1)); return true;
        case 2: menu_.row_draw(object); return true;
        case 3: menu_.control_set_highlighted(object, a0); return true;
        case 4: menu_.control_set_value(object, a0); return true;
        case 5: number_step(object, a0); return true;
        case 6: return true;
        case 8: menu_.text_set_text(object, a0); return true;
        case 9: menu_.row_set_column(object, a0, s32(a1), args.size() > 2 ? args[2] : 0, args.size() > 3 ? args[3] : 0); return true;
        default: return false;
        }
    }
    if (vt == kVtModelView) {
        switch (slot) {
        case 0: model_view_delete(object, a0); return true;
        case 1: model_view_update(object); return true;
        case 2: model_view_draw(object); return true;
        case 5: w_.element_snap_x(object); return true;
        case 6: w_.element_snap_y(object); return true;
        default: return false;
        }
    }
    if (vt == kVtInitialsWindow) {
        switch (slot) {
        case 0: initials_delete(object, a0); return true;
        case 1: w_.window_update(object); return true;
        case 2: w_.window_draw(object, s32(a0), s32(a1)); return true;
        default: return false;
        }
    }
    return false;
}

}  // namespace opentony::runtime::frontend
