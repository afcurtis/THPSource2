#include "auto_views.hpp"
#include "credits.hpp"
#include "seam_addresses_generated.hpp"

#include <cstdio>
#include <string>
#include <vector>

namespace opentony::runtime::frontend {

using namespace opentony::runtime::autoviews;

namespace {

constexpr std::uint32_t kVtRootClass = 0x005184b4;
constexpr std::uint32_t kRunLocal = 0x7ff00000;          // the frame of Credits_Run: the manager and the background sprite object
constexpr std::uint32_t kRunSprite = kRunLocal + 0x1000;
// Live, the frame lives where nothing else of the scratch window does (a replay's frame at kRunLocal overlaps the shared stack local 0x7ff00700, which the bitmap loads
// write: a credit record's slot was overwritten); the background sprite object has its own block.
constexpr std::uint32_t kLiveRunLocal = 0x7ff0d000;     // 0x1000 bytes
constexpr std::uint32_t kLiveRunSprite = 0x7ff0f600;    // 0x5c bytes

std::int32_t add(std::int32_t a, std::int32_t b) { return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) + static_cast<std::uint32_t>(b)); }
std::int32_t sub(std::int32_t a, std::int32_t b) { return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) - static_cast<std::uint32_t>(b)); }
std::uint32_t u(std::int32_t v) { return static_cast<std::uint32_t>(v); }

}  // namespace

void Credits::engine(std::uint32_t address, std::vector<std::uint32_t> args) { s_.note(engine_name(address), std::move(args)); }

std::uint32_t Credits::engine_value(std::uint32_t address, std::vector<std::uint32_t> args) {
    engine(address, std::move(args));
    return static_cast<std::uint32_t>(s_.value(engine_name(address)));
}

void Credits::frame_tick() {
    engine(seam::hal::Game_FrameTick);
    set_global_word(m_, 0x0056e31c, static_cast<std::uint32_t>(s_.value(engine_name(seam::hal::Game_FrameTick))));
}

void Credits::poll_input() {
    engine(0x004f5ff0);
    if (s_.live()) {
        return;   // the call has rewritten the edges itself; a replay scripts them
    }
    for (const std::uint32_t edge : {0x0056b029u, 0x0056b0d9u, 0x0056aff9u, 0x0056b195u, 0x0056b245u, 0x0056b165u}) {
        set_byte_at(m_, edge, static_cast<std::uint32_t>(s_.value(engine_name(0x004f5ff0))));
    }
}

// ---- the records ---------------------------------------------------------------------------------------------------

// A virtual call on a record: the three record classes run natively, anything else is reported as the service `name`.
void Credits::vcall_record(const char* name, std::uint32_t object, std::uint32_t slot, std::vector<std::uint32_t> args) {
    const std::uint32_t vt = word_at(m_, object);
    const std::uint32_t a0 = args.empty() ? 0 : args[0];
    if (vt == kCreditsVtText) {
        if (slot == 0) {
            text_delete(object, a0);
            return;
        }
        if (slot == 1) {
            text_update(object, static_cast<std::int32_t>(a0));
            return;
        }
    } else if (vt == kCreditsVtBitmap) {
        if (slot == 0) {
            bitmap_delete(object, a0);
            return;
        }
        if (slot == 1) {
            bitmap_update(object, static_cast<std::int32_t>(a0));
            return;
        }
    } else if (vt == kCreditsVtMusic) {
        if (slot == 0) {
            music_delete(object, a0);
            return;
        }
        if (slot == 1) {
            music_update(object, static_cast<std::int32_t>(a0));
            return;
        }
    }
    std::vector<std::uint32_t> all{object};
    all.insert(all.end(), args.begin(), args.end());
    s_.note(name, std::move(all));
}

void Credits::record_destroy(std::uint32_t self) { set_word_at(m_, self, kVtRootClass); }

std::uint32_t Credits::record_set_offset(std::uint32_t self, std::uint32_t offset) {
    set_word_at(m_, self + 4, offset);
    return word_at(m_, self + 8);
}

void Credits::record_nothing() {}

std::uint32_t Credits::text_construct(std::uint32_t self, std::uint32_t text, std::uint32_t font) {
    credits_text_at(m_, self).set_field_0(static_cast<Word>(kCreditsVtText));
    const std::string value = m_.cstring(text, 0x1000);
    std::vector<std::uint8_t> bytes(value.begin(), value.end());
    bytes.push_back(0);
    m_.write(self + 0xc, bytes);   // sprintf(self + 0xc, "%s", text)
    credits_text_at(m_, self).set_field_40(static_cast<Word>(font));
    const std::uint32_t measured = credits_text_at(m_, self).field_c() == 0 ? 0x00526b78u : self + 0xc;
    credits_text_at(m_, self).set_field_8(static_cast<Word>(u(add(f_.text_extent(font, measured), 2))));
    return self;
}

void Credits::text_destroy(std::uint32_t self) {
    credits_text_at(m_, self).set_field_0(static_cast<Word>(kCreditsVtText));
    record_destroy(self);
}

std::uint32_t Credits::text_delete(std::uint32_t self, std::uint32_t flag) {
    text_destroy(self);
    if ((flag & 1) != 0) {
        s_.note(kOperatorDelete, {self});
    }
    return self;
}

void Credits::text_update(std::uint32_t self, std::int32_t position) {
    const std::int32_t y = sub(credits_text_at(m_, self).field_4(), position);
    if (add(static_cast<std::int32_t>(credits_text_at(m_, self).field_8()), y) > 0 && y < 0x100) {
        s_.note(kFontDrawService, {credits_text_at(m_, self).field_40(), 0x100, u(y), self + 0xc, 0}, m_.cstring(self + 0xc, 0x40));
    }
}

std::uint32_t Credits::bitmap_construct(std::uint32_t self, std::uint32_t file) {
    credits_bitmap_at(m_, self).set_field_0(static_cast<Word>(kCreditsVtBitmap));
    const std::uint32_t memory = operator_new(0x5c);
    std::uint32_t sprite = 0;
    if (memory != 0) {
        s_.note(kSpriteConstruct, {memory, file, 1, 0, 0}, m_.cstring(file));
        sprite = static_cast<std::uint32_t>(s_.value(kSpriteConstruct));
    }
    credits_bitmap_at(m_, self).set_field_c(static_cast<Word>(sprite));
    set_byte_at(m_, sprite + 0x0a, 1);
    s_.note(kSpriteHeight, {credits_bitmap_at(m_, self).field_c()});
    credits_bitmap_at(m_, self).set_field_8(static_cast<Word>(u(add(static_cast<std::int32_t>(s_.value(kSpriteHeight)), 2))));
    return self;
}

void Credits::bitmap_destroy(std::uint32_t self) {
    credits_bitmap_at(m_, self).set_field_0(static_cast<Word>(kCreditsVtBitmap));
    if (credits_bitmap_at(m_, self).field_c() != 0) {
        s_.note(kVirtualDelete, {credits_bitmap_at(m_, self).field_c(), 1});
    }
    record_destroy(self);
}

std::uint32_t Credits::bitmap_delete(std::uint32_t self, std::uint32_t flag) {
    bitmap_destroy(self);
    if ((flag & 1) != 0) {
        s_.note(kOperatorDelete, {self});
    }
    return self;
}

void Credits::bitmap_update(std::uint32_t self, std::int32_t position) {
    const std::int32_t y = sub(credits_bitmap_at(m_, self).field_4(), position);
    if (add(static_cast<std::int32_t>(credits_bitmap_at(m_, self).field_8()), y) > 0 && y < 0x100) {
        s_.note(kSpriteDrawService, {credits_bitmap_at(m_, self).field_c(), 0x100, u(y), 0});
    }
}

std::uint32_t Credits::music_construct(std::uint32_t self, std::uint32_t track, std::uint32_t second) {
    credits_music_at(m_, self).set_field_c(static_cast<Word>(track));
    credits_music_at(m_, self).set_field_0(static_cast<Word>(kCreditsVtMusic));
    credits_music_at(m_, self).set_field_10(static_cast<Word>(second));
    credits_music_at(m_, self).set_field_14(0);
    credits_music_at(m_, self).set_field_8(0);
    return self;
}

void Credits::music_base_destroy(std::uint32_t self) { record_destroy(self); }

std::uint32_t Credits::music_delete(std::uint32_t self, std::uint32_t flag) {
    music_base_destroy(self);
    if ((flag & 1) != 0) {
        s_.note(kOperatorDelete, {self});
    }
    return self;
}

void Credits::music_update(std::uint32_t self, std::int32_t position) {
    const std::uint32_t state = credits_music_at(m_, self).field_14();
    if (state == 0) {
        if (sub(credits_music_at(m_, self).field_4(), position) < 0x100) {
            s_.note(kSvcPrintfLog, {0x00526b7c});
            if (global_byte(m_, kCreditsMusicPlaying) != 0) {
                engine(0x004ab9c0);
            }
            credits_music_at(m_, self).set_field_14(1);
        }
    } else if (state == 1) {
        if (global_word(m_, 0x0053778c) == 0 || global_byte(m_, kCreditsMusicPlaying) == 0) {
            s_.note(kSvcPrintfLog, {0x00526b94});
            engine(0x004abc80);
            engine(0x004abb00, {credits_music_at(m_, self).field_c(), credits_music_at(m_, self).field_10()});
            set_global_byte(m_, kCreditsMusicPlaying, 1);
            credits_music_at(m_, self).set_field_14(2);
        }
    }
}

// ---- the static fonts ------------------------------------------------------------------------------------------------

void Credits::statics_a_construct() { s_.note(kSvcFontConstruct, {kCreditsFontA}); }
void Credits::statics_a_atexit() { s_.note(kSvcAtexit, {0x00426df0}); }
void Credits::statics_a_init() {
    statics_a_construct();
    statics_a_atexit();
}
void Credits::statics_a_destroy() {
    const std::uint32_t flags = global_byte(m_, kCreditsFlags);
    if ((flags & 1) == 0) {
        set_global_byte(m_, kCreditsFlags, flags | 1);
        s_.note(kSvcFontDestroy, {kCreditsFontA});
    }
}
void Credits::statics_b_construct() { s_.note(kSvcFontConstruct, {kCreditsFontB}); }
void Credits::statics_b_atexit() { s_.note(kSvcAtexit, {0x00426e40}); }
void Credits::statics_b_init() {
    statics_b_construct();
    statics_b_atexit();
}
void Credits::statics_b_destroy() {
    const std::uint32_t flags = global_byte(m_, kCreditsFlags);
    if ((flags & 2) == 0) {
        set_global_byte(m_, kCreditsFlags, flags | 2);
        s_.note(kSvcFontDestroy, {kCreditsFontB});
    }
}

// ---- the manager -----------------------------------------------------------------------------------------------------

std::uint32_t Credits::parse_tag(std::uint32_t self, std::uint32_t text) {
    if (byte_at(m_, text) != '@') {
        return 0;
    }
    const std::int32_t tag = static_cast<std::int8_t>(byte_at(m_, text + 1));
    const std::int32_t index = tag - 0x42;
    if (index >= 0 && index <= 0x2b) {
        // the original's switch: a byte table over 'B'..'m' picks one of three handlers (the table is the executable's, at 0x00426d58)
        static const std::uint8_t kClass[0x2c] = {0, 3, 3, 3, 1, 3, 3, 3, 3, 3, 3, 2, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
                                                  3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 0, 3, 3, 3, 1, 3, 3, 3, 3, 3, 3, 2};
        switch (kClass[index]) {
        case 0:
            set_word_at(m_, self + 0xfe4, 1);
            return 3;
        case 1:
            set_word_at(m_, self + 0xfe8, kCreditsFontB);
            return 4;
        case 2:
            set_word_at(m_, self + 0xfe4, 2);
            set_word_at(m_, self + 0xfec, u(static_cast<std::int8_t>(byte_at(m_, text + 2)) - 0x30));
            set_word_at(m_, self + 0xff0, u(static_cast<std::int8_t>(byte_at(m_, text + 4)) - 0x30));
            return 5;
        default:
            break;
        }
    }
    check(false, kCreditsMsgBadTag);
    return 0;
}

std::int32_t Credits::parse_line(std::uint32_t self, std::uint32_t text) {
    set_word_at(m_, self + 0xfe8, kCreditsFontA);
    set_word_at(m_, self + 0xfe4, 0);
    std::int32_t at = 0;
    for (;;) {
        const std::uint32_t used = parse_tag(self, text + u(at));
        if (used == 0) {
            break;
        }
        at = add(at, static_cast<std::int32_t>(used));
    }
    std::int32_t length = 0;
    for (;;) {
        const std::uint8_t c = byte_at(m_, text + u(at));
        if (c == 0x0d || c == '#') {
            break;
        }
        set_byte_at(m_, self + 0xfb0 + u(length), c);
        ++length;
        ++at;
        if (!(length < 0x31)) {
            break;
        }
    }
    set_byte_at(m_, self + 0xfb0 + u(length), 0);
    if (byte_at(m_, text + u(at)) == 0x0d) {
        at = add(at, 2);
    }
    if (byte_at(m_, text + u(at)) == '#') {
        return -1;
    }
    return at;
}

void Credits::load_font_into(std::uint32_t dest, std::uint32_t name) {
    s_.note(kSvcFontFind, {name}, m_.cstring(name));
    copy_font(dest, static_cast<std::uint32_t>(s_.value(kSvcFontFind)));
}

std::uint32_t Credits::load_text(std::uint32_t self, std::uint32_t file) {
    load_font_into(kCreditsFontA, 0x0052544c);
    set_global_word(m_, kCreditsFontA + 4, 1);
    set_global_word(m_, kCreditsFontA + 0x24, 1);
    set_global_word(m_, kCreditsFontA + 0x28, 1);
    set_global_word(m_, kCreditsFontA + 0x0c, 2);
    set_global_word(m_, kCreditsFontA + 0x10, 2);
    set_global_word(m_, kCreditsFontA + 0x30, 0x800);
    load_font_into(kCreditsFontB, 0x00526acc);
    set_global_word(m_, kCreditsFontB + 4, 1);
    set_global_word(m_, kCreditsFontB + 0x24, 3);
    set_global_word(m_, kCreditsFontB + 0x28, 3);
    set_global_word(m_, kCreditsFontB + 0x30, 0x800);
    set_global_word(m_, kCreditsFontB + 0x0c, 4);
    set_global_word(m_, kCreditsFontB + 0x10, 4);
    s_.note(engine_name(0x00449030), {file}, m_.cstring(file));
    const std::uint32_t handle = static_cast<std::uint32_t>(s_.value(engine_name(0x00449030)));
    check(handle != 0, kCreditsMsgNoFile);
    s_.note(kAllocBuffer, {handle, 1, 1, 0});
    const std::uint32_t buffer = static_cast<std::uint32_t>(s_.value(kAllocBuffer));
    check(buffer != 0, kCreditsMsgNoMemory);
    engine(0x00449230, {buffer});
    engine(0x00449660);
    std::uint32_t cursor = buffer;
    std::int32_t offset = 0;
    std::int32_t count = 0;
    std::uint32_t slot = self + 4;
    do {
        const std::int32_t used = parse_line(self, cursor);
        if (used == -1) {
            break;
        }
        cursor += u(used);
        const std::uint32_t type = word_at(m_, self + 0xfe4);
        if (type == 0) {
            const std::uint32_t memory = operator_new(0x44);
            std::uint32_t record = 0;
            if (memory != 0) {
                record = text_construct(memory, self + 0xfb0, word_at(m_, self + 0xfe8));
            }
            set_word_at(m_, slot, record);
        } else if (type == 1) {
            const std::uint32_t memory = operator_new(0x10);
            std::uint32_t record = 0;
            if (memory != 0) {
                record = bitmap_construct(memory, self + 0xfb0);
            }
            set_word_at(m_, slot, record);
        } else if (type == 2) {
            const std::uint32_t memory = operator_new(0x18);
            std::uint32_t record = 0;
            if (memory != 0) {
                record = music_construct(memory, word_at(m_, self + 0xfec), word_at(m_, self + 0xff0));
            }
            set_word_at(m_, slot, record);
        }
        offset = add(offset, static_cast<std::int32_t>(record_set_offset(word_at(m_, slot), u(offset))));
        ++count;
        slot += 4;
    } while (count < 1000);
    s_.note(kMemDeleteService, {buffer});
    set_word_at(m_, self + 0xfa4, u(count));
    set_word_at(m_, self + 0xfa8, u(offset));
    set_word_at(m_, self, 0xfffffc00u);
    set_word_at(m_, self + 0xfac, global_word(m_, 0x0056e31c));
    set_global_byte(m_, kCreditsMusicPlaying, 0);
    return self;
}

void Credits::unload(std::uint32_t self) {
    for (std::int32_t i = 0; i < int_at(m_, self + 0xfa4); ++i) {
        const std::uint32_t record = word_at(m_, self + 4 + 4 * u(i));
        if (record != 0) {
            vcall_record(kVirtualDelete, record, 0, {1});
        }
    }
}

bool Credits::advance_timeline(std::uint32_t self) {
    if (global_byte(m_, kCreditsMusicPlaying) != 0) {
        engine(0x004abe50);
    }
    const std::int32_t delta = sub(static_cast<std::int32_t>(global_word(m_, 0x0056e31c)), int_at(m_, self + 0xfac));
    set_word_at(m_, self + 0xfac, global_word(m_, 0x0056e31c));
    std::int32_t position = int_at(m_, self);
    position = global_byte(m_, 0x0056b0a8) != 0 ? add(position, delta * 4) : add(position, delta);
    set_word_at(m_, self, u(position));
    return (position >> 2) <= int_at(m_, self + 0xfa8);
}

void Credits::update_records(std::uint32_t self) {
    for (std::int32_t i = 0; i < int_at(m_, self + 0xfa4); ++i) {
        vcall_record("credit_record_update", word_at(m_, self + 4 + 4 * u(i)), 1, {u(int_at(m_, self) >> 2)});
    }
}

void Credits::run(std::uint32_t mode_pointer) {
    const std::uint32_t local = s_.live() ? kLiveRunLocal : kRunLocal;
    const std::uint32_t sprite_object = s_.live() ? kLiveRunSprite : kRunSprite;
    if (!s_.live()) {
        m_.ensure_region(kRunLocal, 0x1100, 0x1100);
    }
    s_.note(kSvcPrintfLog, {0x00526b00});
    s_.note(kSvcPrintfLog, {0x00526ae8});
    engine(0x004b2390);
    engine(0x0048a420);
    engine(0x00489490);
    s_.note("pad_clear", {0x0056aff8});
    s_.note("pad_clear", {0x0056b164});
    s_.note(engine_name(0x00448dd0), {0x00526ad8, 1}, m_.cstring(0x00526ad8));
    const std::uint32_t handle = static_cast<std::uint32_t>(s_.value(engine_name(0x00448dd0)));
    engine(seam::hal::D3DSprite_Create, {sprite_object, handle, 0x00526ad8, 1});
    s_.note("font_load", {0x0052544c}, m_.cstring(0x0052544c));
    s_.note("font_load", {0x00526acc}, m_.cstring(0x00526acc));
    const std::int32_t mode = int_at(m_, mode_pointer);
    check(mode >= 0 && mode <= 1, kCreditsMsgBadParameter);
    load_text(local, mode == 0 ? 0x00526a84u : 0x00526a78u);
    for (;;) {
        frame_tick();
        poll_input();
        engine(0x004dae00);
        engine(0x00418150);
        engine(0x004304b0);
        engine(0x00466be0);
        engine(0x00489a10);
        if (global_byte(m_, 0x0056b029) != 0 || global_byte(m_, 0x0056aff9) != 0 || global_byte(m_, 0x0056b0d9) != 0) {
            break;
        }
        if (!advance_timeline(local)) {
            break;
        }
        update_records(local);
        engine(0x004d7990, {sprite_object, word_at(m_, global_word(m_, 0x00560fd4) + 0x88), 0, 0});
        engine(0x004e8740, {0});
        engine(0x004c9300, {1});
        if (!(global_word(m_, 0x0056e31c) >= global_word(m_, 0x00568624) + 2)) {
            do {
                frame_tick();
            } while (global_word(m_, 0x0056e31c) < global_word(m_, 0x00568624) + 2);
        }
        set_global_word(m_, 0x00568624, global_word(m_, 0x0056e31c));
        engine(0x00430510);
        engine(0x00466c00);
    }
    engine(0x004abc80);
    s_.note("pad_clear", {0x0056aff8});
    s_.note("pad_clear", {0x0056b164});
    engine(0x0046a600, {0x00560e58, global_word(m_, 0x0052e61c)});
    s_.note("font_unload", {0x0052544c}, m_.cstring(0x0052544c));
    s_.note("font_unload", {0x00526acc}, m_.cstring(0x00526acc));
    unload(local);
    engine(seam::hal::D3DSprite_Destroy, {sprite_object});
}

}  // namespace opentony::runtime::frontend
