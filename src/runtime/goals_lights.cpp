#include "auto_views.hpp"
#include "goals.hpp"

namespace opentony::runtime::frontend {

using namespace opentony::runtime::autoviews;

namespace {

std::uint32_t u(std::int32_t v) { return static_cast<std::uint32_t>(v); }

// the type 500 node's record (0x58 bytes, vtable 0x00518b70)
constexpr std::uint32_t kVtLightRecord = 0x00518b70;
constexpr std::uint32_t kRPosition = 0x10;      // three dwords
constexpr std::uint32_t kRReach = 0x50;
constexpr std::uint32_t kRLight = 0x54;         // the light object made while the camera is near
constexpr std::uint32_t kCameraPointer = 0x0056a960;
constexpr std::uint32_t kLightMade = 0x005244cc;

// the light editor (0x240 bytes, vtable 0x00518b78)
constexpr std::uint32_t kVtLightEditor = 0x00518b78;
constexpr std::uint32_t kEList = 0xac;
constexpr std::uint32_t kERows = 0xb0;          // 0x60 row slots, fourteen used
constexpr std::uint32_t kEProps = 0x230;
constexpr std::uint32_t kERecord = 0x234;       // the light record the values are stored in
constexpr std::uint32_t kLightValues = 0x0056201c;   // fourteen dwords: ambient, top, middle and bottom red / green / blue, two angles
constexpr std::uint32_t kLightEnd = 0x00562054;

std::string text_of(const RetailMemory& m, std::uint32_t pointer) { return m.cstring(pointer, 0x100); }

}  // namespace

// ---- the type 500 node's record --------------------------------------------------------------------------------------------------------------------

std::uint32_t GoalScreens::light_record_construct(std::uint32_t self, std::uint32_t position, std::uint32_t word, std::uint32_t reach, std::uint32_t a4, std::uint32_t a5,
                                                  std::uint32_t a6, std::uint32_t b7, std::uint32_t b8, std::uint32_t b9, std::uint32_t b10, std::uint32_t b11,
                                                  std::uint32_t b12) {
    engine(0x00404020, {self});
    goals_lights_light_at(m_, self).set_field_0(static_cast<Word>(kVtLightRecord));
    for (std::uint32_t k = 0; k < 3; ++k) {
        set_word_at(m_, self + kRPosition + 4 * k, word_at(m_, position + 4 * k));
    }
    goals_lights_light_at(m_, self).set_field_50(static_cast<Word>(reach));
    goals_lights_light_at(m_, self).set_field_3c(static_cast<Word>(a4));
    goals_lights_light_at(m_, self).set_field_45(static_cast<std::uint8_t>(b8));
    goals_lights_light_at(m_, self).set_field_40(static_cast<Word>(a5));
    goals_lights_light_at(m_, self).set_field_44(static_cast<std::uint8_t>(b7));
    goals_lights_light_at(m_, self).set_field_48(static_cast<std::uint8_t>(b11));
    goals_lights_light_at(m_, self).set_field_e(static_cast<std::uint16_t>(word));
    goals_lights_light_at(m_, self).set_field_46(static_cast<std::uint8_t>(b9));
    goals_lights_light_at(m_, self).set_field_47(static_cast<std::uint8_t>(b10));
    goals_lights_light_at(m_, self).set_field_49(static_cast<std::uint8_t>(b12));
    goals_lights_light_at(m_, self).set_field_4c(static_cast<Word>(a6));
    goals_lights_light_at(m_, self).set_field_3b(2);
    goals_lights_light_at(m_, self).set_field_4a(1);
    return self;
}

std::uint32_t GoalScreens::light_record_delete(std::uint32_t self, std::uint32_t flag) {
    light_record_destroy(self);
    if ((flag & 1) != 0) {
        engine(0x00403dc0, {self});
    }
    return self;
}

void GoalScreens::light_record_destroy(std::uint32_t self) {
    goals_lights_light_at(m_, self).set_field_0(static_cast<Word>(kVtLightRecord));
    const std::uint32_t light = goals_lights_light_at(m_, self).field_54();
    if (light != 0) {
        vcall_item(kVirtualDelete, light, 0, {1});
    }
    engine(0x00404060, {self});
}

// 0x0045c4a0: a light is made while the camera is within the record's reach (the distance is the larger of the two ground offsets plus half the smaller),
// its strength follows the distance, and it is deleted when the camera goes away or the record is switched off
void GoalScreens::light_record_update(std::uint32_t self) {
    auto drop = [&] {
        const std::uint32_t light = goals_lights_light_at(m_, self).field_54();
        if (light != 0) {
            vcall_item(kVirtualDelete, light, 0, {1});
            goals_lights_light_at(m_, self).set_field_54(0);
        }
    };
    if (goals_lights_light_at(m_, self).field_4a() == 0) {
        drop();
        return;
    }
    const std::uint32_t camera = global_word(m_, kCameraPointer);
    std::int32_t dx = (goals_lights_light_at(m_, self).field_10() - int_at(m_, camera + 8)) >> 12;
    if (dx < 0) {
        dx = -dx;
    }
    std::int32_t dz = (int_at(m_, self + kRPosition + 8) - int_at(m_, camera + 0x10)) >> 12;
    if (dz < 0) {
        dz = -dz;
    }
    const std::int32_t distance = dx < dz ? dz + dx / 2 : dx + dz / 2;
    if (distance >= static_cast<std::int32_t>(goals_lights_light_at(m_, self).field_50())) {
        drop();
        return;
    }
    if (goals_lights_light_at(m_, self).field_54() == 0) {
        set_global_word(m_, kLightMade, 0);
        const std::uint32_t memory = engine_value(0x00403d30, {0xa4});
        std::uint32_t light = 0;
        if (memory != 0) {
            light = engine_value(0x00405d60, {memory, self + kRPosition, goals_lights_light_at(m_, self).field_3c(), goals_lights_light_at(m_, self).field_40(), goals_lights_light_at(m_, self).field_4c(), goals_lights_light_at(m_, self).field_44(), goals_lights_light_at(m_, self).field_45(),
                                                goals_lights_light_at(m_, self).field_46(), goals_lights_light_at(m_, self).field_47(), goals_lights_light_at(m_, self).field_48(), goals_lights_light_at(m_, self).field_49()});
        }
        goals_lights_light_at(m_, self).set_field_54(static_cast<Word>(light));
        set_global_word(m_, kLightMade, 1);
        set_byte_at(m_, light + 0x3a, 1);
    }
    const std::int32_t scaled = distance * 10;
    set_half_at(m_, goals_lights_light_at(m_, self).field_54() + 0x54, u((scaled + ((scaled >> 31) & 0xf)) >> 4));
}

// ---- the light editor ----------------------------------------------------------------------------------------------------------------------------------

std::uint32_t GoalScreens::light_editor_construct(std::uint32_t self, std::uint32_t record, std::uint32_t font, std::int32_t x, std::int32_t y) {
    w_.window_construct(self, x, y, -1, -1);
    goals_lights_light_at(m_, self).set_field_0(static_cast<Word>(kVtLightEditor));
    std::uint32_t memory = operator_new(0x350);
    goals_lights_light_at(m_, self).set_field_ac(static_cast<Word>(memory != 0 ? menu_.scroll_construct(memory, 0x27, 0x8c, 0x100, 0x32, 1, 3, 2, 1) : 0u));
    memory = operator_new(0x17c);
    const std::uint32_t props = memory != 0 ? menu_.properties_construct(memory) : 0u;
    goals_lights_light_at(m_, self).set_field_230(static_cast<Word>(props));
    menu_.copy_font_fields(props + 4, font);
    set_word_at(m_, props + 0x18, 0x80);
    set_word_at(m_, props + 0x1c, 0x80);
    set_word_at(m_, props + 0x20, 0x80);
    set_word_at(m_, props + 0x34, 0x1000);
    set_word_at(m_, props + 0x150, 0x1400);
    set_word_at(m_, props + 0x154, 0x1000);
    for (std::uint32_t k = 0; k < 0x60; ++k) {
        set_word_at(m_, self + kERows + 4 * k, 0);
    }
    static const std::uint32_t names[14] = {0x0052e074, 0x0052e06c, 0x0052e064, 0x0052e05c, 0x0052e054, 0x0052e04c, 0x0052e044,
                                            0x0052e03c, 0x0052e034, 0x0052e02c, 0x0052e024, 0x0052e01c, 0x0052e014, 0x0052e00c};
    for (std::uint32_t k = 0; k < 14; ++k) {
        memory = operator_new(0x100);
        set_word_at(m_, self + kERows + 4 * k, memory != 0 ? menu_.row_construct(memory, k, names[k], goals_lights_light_at(m_, self).field_230(), 2, 0, 0) : 0u);
    }
    for (std::uint32_t k = 0; k < 0x60; ++k) {
        const std::uint32_t row = word_at(m_, self + kERows + 4 * k);
        if (row != 0) {
            vcall_list("list_v6", goals_lights_light_at(m_, self).field_ac(), 6, {row});
        }
    }
    vcall_list("list_v2", goals_lights_light_at(m_, self).field_ac(), 2);
    goals_lights_light_at(m_, self).set_field_234(static_cast<Word>(record));
    // the editor starts from the record's values
    set_global_word(m_, kLightValues, word_at(m_, record + 0x28));
    set_global_word(m_, kLightValues + 4, word_at(m_, record + 0x2c));
    set_global_word(m_, kLightValues + 8, word_at(m_, record + 0x30));
    set_global_word(m_, kLightValues + 0x24, u(short_at(m_, record + 0x14)));
    set_global_word(m_, kLightValues + 0x28, u(short_at(m_, record + 0x1a)));
    set_global_word(m_, kLightValues + 0x2c, u(short_at(m_, record + 0x20)));
    set_global_word(m_, kLightValues + 0x0c, u(short_at(m_, record + 0x16)));
    set_global_word(m_, kLightValues + 0x10, u(short_at(m_, record + 0x1c)));
    set_global_word(m_, kLightValues + 0x14, u(short_at(m_, record + 0x22)));
    set_half_at(m_, record + 0x18, global_half(m_, kLightValues + 0x18));
    set_global_word(m_, kLightValues + 0x18, u(global_short(m_, kLightValues + 0x18)));
    set_half_at(m_, record + 0x1e, global_half(m_, kLightValues + 0x1c));
    set_global_word(m_, kLightValues + 0x1c, u(global_short(m_, kLightValues + 0x1c)));
    set_half_at(m_, record + 0x24, global_half(m_, kLightValues + 0x20));
    set_global_word(m_, kLightValues + 0x20, u(global_short(m_, kLightValues + 0x20)));
    set_global_word(m_, kLightValues + 0x30, 0);
    set_global_word(m_, kLightValues + 0x34, 0);
    return self;
}

std::uint32_t GoalScreens::light_editor_delete(std::uint32_t self, std::uint32_t flag) {
    light_editor_destroy(self);
    if ((flag & 1) != 0) {
        operator_delete(self);
    }
    return self;
}

void GoalScreens::light_editor_destroy(std::uint32_t self) {
    goals_lights_light_at(m_, self).set_field_0(static_cast<Word>(kVtLightEditor));
    for (std::uint32_t k = 0; k < 0x60; ++k) {
        const std::uint32_t row = word_at(m_, self + kERows + 4 * k);
        if (row != 0) {
            vcall_item(kVirtualDelete, row, 0, {1});
        }
    }
    if (goals_lights_light_at(m_, self).field_230() != 0) {
        vcall_item(kVirtualDelete, goals_lights_light_at(m_, self).field_230(), 0, {1});
    }
    if (goals_lights_light_at(m_, self).field_ac() != 0) {
        vcall_item(kVirtualDelete, goals_lights_light_at(m_, self).field_ac(), 0, {1});
    }
    w_.window_destroy(self);
}

// 0x0045cd10: the pad moves the highlighted value, the values are printed in the rows and stored in the light record
void GoalScreens::light_editor_draw(std::uint32_t self, std::int32_t x, std::int32_t y) {
    const std::uint32_t list = goals_lights_light_at(m_, self).field_ac();
    auto adjust = [&](std::int32_t step) {
        std::uint32_t at = kLightValues + 4 * menu_.list_highlighted_id(list);
        set_word_at(m_, at, word_at(m_, at) + u(step));
        at = kLightValues + 4 * menu_.list_highlighted_id(list);
        if (int_at(m_, at) < 0) {
            set_word_at(m_, kLightValues + 4 * menu_.list_highlighted_id(list), 0);
        }
        at = kLightValues + 4 * menu_.list_highlighted_id(list);
        if (int_at(m_, at) > 0x2000) {
            set_word_at(m_, kLightValues + 4 * menu_.list_highlighted_id(list), 0x2000);
        }
    };
    if (global_byte(m_, 0x0056b1e4) != 0) {
        adjust(-0x40);
    } else if (global_byte(m_, 0x0056b1f4) != 0) {
        adjust(0x40);
    }
    if (global_byte(m_, 0x0056b255) != 0) {
        set_global_byte(m_, 0x0056b255, 0);
        const std::uint32_t angle1 = global_word(m_, kLightValues + 0x30);
        const std::uint32_t r1 = engine_value(0x004f3980, {angle1});
        const std::uint32_t r2 = engine_value(0x004f39b0, {angle1});
        (void)r1;
        (void)r2;
        s_.note("printf_log", {0x0052e07c});
        const std::uint32_t angle2 = global_word(m_, kLightValues + 0x34);
        (void)engine_value(0x004f3980, {angle2});
        (void)engine_value(0x004f39b0, {angle2});
        s_.note("printf_log", {0x0052e07c});
    }
    vcall_list("list_v3", list, 3);
    s_.note(kSvcPadEdges, {1, 0, 6});
    const std::uint32_t edge = static_cast<std::uint32_t>(s_.value(kSvcPadEdges));
    vcall_list("list_v5", list, 5, {edge});
    vcall_list("list_v4", list, 4);
    set_global_byte(m_, 0x0056b1e5, 0);
    set_global_byte(m_, 0x0056b1f5, 0);
    for (std::uint32_t k = 0; k < 14; ++k) {
        const std::string text = retail_format(text_of(m_, 0x00525764), {}, {int_at(m_, kLightValues + 4 * k)});
        scratch_line(text);
        s_.note("item_set_column", {word_at(m_, self + kERows + 4 * k), 1, 0x64, goals_lights_light_at(m_, self).field_230()}, text);
    }
    const std::uint32_t record = goals_lights_light_at(m_, self).field_234();
    set_word_at(m_, record + 0x28, global_word(m_, kLightValues));
    set_word_at(m_, record + 0x2c, global_word(m_, kLightValues + 4));
    set_word_at(m_, record + 0x30, global_word(m_, kLightValues + 8));
    set_half_at(m_, record + 0x14, global_half(m_, kLightValues + 0x24));
    set_half_at(m_, record + 0x1a, global_half(m_, kLightValues + 0x28));
    set_half_at(m_, record + 0x20, global_half(m_, kLightValues + 0x2c));
    set_half_at(m_, record + 0x16, global_half(m_, kLightValues + 0x0c));
    set_half_at(m_, record + 0x1c, global_half(m_, kLightValues + 0x10));
    set_half_at(m_, record + 0x22, global_half(m_, kLightValues + 0x14));
    set_half_at(m_, record + 0x18, global_half(m_, kLightValues + 0x18));
    set_half_at(m_, record + 0x1e, global_half(m_, kLightValues + 0x1c));
    set_half_at(m_, record + 0x24, global_half(m_, kLightValues + 0x20));
    set_half_at(m_, record + 0x0c, engine_value(0x004f39b0, {global_word(m_, kLightValues + 0x30)}));
    set_half_at(m_, record + 0x0e, 0);
    set_half_at(m_, record + 0x10, engine_value(0x004f3980, {global_word(m_, kLightValues + 0x30)}));
    vcall_list("list_v3", list, 3);
    w_.window_draw(self, x, y);
}

}  // namespace opentony::runtime::frontend
