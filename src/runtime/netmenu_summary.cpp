#include "auto_views.hpp"
#include "netmenu.hpp"

namespace opentony::runtime::frontend {

using namespace opentony::runtime::autoviews;

using namespace netmenu;

namespace {

std::int32_t s(std::uint32_t v) { return static_cast<std::int32_t>(v); }

constexpr std::uint32_t kMainFont = 0x00525458;
constexpr std::uint32_t kEmpty = 0x005252f0;
constexpr std::uint32_t kManager = 0x0056c580;
constexpr std::uint32_t kSummary = 0x0056af20;             // the summary window the screen keeps
constexpr std::uint32_t kNetActive = 0x006a3b21;
constexpr std::uint32_t kNetLost = 0x006a3b30;
constexpr std::uint32_t kNetPlayers = 0x006a18b0;
constexpr std::uint32_t kNetPlayerCount = 0x006a3a90;
constexpr std::uint32_t kPadBack = 0x0056aff9;
constexpr std::uint32_t kGameMode = 0x00533f38;
constexpr std::uint32_t kLevel = 0x00561c90;
constexpr std::uint32_t kLevels = 0x0053c1f8;
constexpr std::uint32_t kSelectionRecords = 0x0053a514;
constexpr std::uint32_t kCharacterNames = 0x0053a3d8;

// the summary window's fields
constexpr std::uint32_t kFontLabel = 0xb0;
constexpr std::uint32_t kFontPrompt = 0x1fc;
constexpr std::uint32_t kStyle = 0x348;

}  // namespace

// 0x0047f250: the summary window: two fonts, the style, the select and accept prompts, the "versus" picture and the two players' faces
std::uint32_t NetMenu::summary_construct(std::uint32_t self) {
    w_.window_construct(self, 0, 0, -1, -1);
    font_construct(self + kFontLabel);
    font_construct(self + kFontPrompt);
    netmenu_summary_summary_at(m_, self).set_field_0(static_cast<Word>(kVtNetSummary));
    s_.note(kSvcFontLoad, {kMainFont}, m_.cstring(kMainFont, 0x40));
    menu_.copy_font_fields(self + kFontLabel, static_cast<std::uint32_t>(s_.value(kSvcFontLoad)));
    set_word_at(m_, self + kFontLabel + 4, 1);
    set_word_at(m_, self + kFontLabel + 8, 1);
    set_word_at(m_, self + kFontLabel + 0x30, 0x800);
    set_word_at(m_, self + kFontLabel + 0x14, 0xff);
    set_word_at(m_, self + kFontLabel + 0x18, 99);
    set_word_at(m_, self + kFontLabel + 0x1c, 0x2c);
    netmenu_summary_summary_at(m_, self).set_field_ac(0);
    copy_font_named(self + kFontPrompt, kMainFont);
    set_word_at(m_, self + kFontPrompt + 4, 1);
    set_word_at(m_, self + kFontPrompt + 8, 1);
    f_.set_rgb(self + kFontPrompt, 200, 200, 200);
    set_byte_at(m_, self + kFontPrompt + 0x21, 0);
    set_word_at(m_, self + kFontPrompt + 0xc, 0);
    set_word_at(m_, self + kFontPrompt + 0x10, 0);
    set_word_at(m_, self + kFontPrompt + 0x30, 0x960);
    const std::uint32_t style = make_style(self + kFontLabel);
    netmenu_summary_summary_at(m_, self).set_field_348(static_cast<Word>(style));
    set_word_at(m_, style + 8, 0);
    set_word_at(m_, style + 0xc, 3);
    set_word_at(m_, style + 0x18, 0xff);
    set_word_at(m_, style + 0x1c, 0x60);
    set_word_at(m_, style + 0x20, 0x20);
    set_byte_at(m_, style + 0x25, 0);
    set_word_at(m_, style + 0x10, 0);
    set_word_at(m_, style + 0x150, 0x800);
    set_word_at(m_, style + 0x154, 0x800);
    w_.window_add(self, prompt(0x0052d878, 0x005316b8, self + kFontPrompt, 0x9f, 0xdc));
    w_.window_add(self, prompt(0x0052d868, 0x005318e0, self + kFontPrompt, 0x110, 0xdc));
    auto picture = [&](std::uint32_t file, std::int32_t x) {
        const std::uint32_t memory = operator_new(0x2c);
        return memory != 0 ? w_.sprite_construct(memory, file, x, 0x6e, -1, -1, 0, 0) : 0u;
    };
    w_.window_add(self, picture(0x0052df4c, 0xdc));
    auto face = [&](std::uint32_t slot, std::uint32_t player, std::int32_t x) {
        const std::uint32_t memory = operator_new(0x2c);
        std::uint32_t sprite = 0;
        if (memory != 0) {
            const std::uint32_t bitmap = engine_value(0x004b1360, {kManager, player});
            sprite = w_.sprite_construct(memory, bitmap, x, 0x6e, -1, -1, 0, 0);
        }
        set_word_at(m_, self + slot, sprite);
    };
    face(0x350, 0, 0x6c);
    face(0x354, 1, 0x16c);
    w_.window_add(self, netmenu_summary_summary_at(m_, self).field_350());
    w_.window_add(self, netmenu_summary_summary_at(m_, self).field_354());
    return self;
}

std::uint32_t NetMenu::summary_delete(std::uint32_t self, std::uint32_t flag) {
    summary_destruct(self);
    if ((flag & 1) != 0) {
        operator_delete(self);
    }
    return self;
}

// 0x0047f790
void NetMenu::summary_destruct(std::uint32_t self) {
    netmenu_summary_summary_at(m_, self).set_field_0(static_cast<Word>(kVtNetSummary));
    font_unload(kMainFont);
    if (netmenu_summary_summary_at(m_, self).field_348() != 0) {
        delete_object(netmenu_summary_summary_at(m_, self).field_348());
    }
    font_destroy(self + kFontPrompt);
    font_destroy(self + kFontLabel);
    w_.window_destroy(self);
}

// 0x0047f840: one player's line: the character's name (a custom skater's own), and "Player n" below, blinking while the network layer says it is that player's turn
std::uint32_t NetMenu::summary_draw_player(std::uint32_t self, std::uint32_t player) {
    bool blink = false;
    if ((engine_value(0x004de040) & 0xff) != 0 && player == 0) {
        blink = true;
    }
    if ((engine_value(0x004de060) & 0xff) != 0 && player != 0) {
        blink = true;
    }
    const std::uint32_t seat = player != 0 ? 1u : 0u;
    const std::uint32_t x = (player != 0 ? 0x100u : 0u) + 0x76;
    const std::uint32_t slot = engine_value(0x004b18a0, {seat});
    std::uint32_t name = kCharacterNames + slot * 0x170;
    if ((byte_at(m_, kSelectionRecords + slot * 0x170) & 2) != 0) {
        engine(0x004b0bc0, {kManager, seat});
        name = 0x0056a488 + (engine_value(0x004b0c50, {kManager}) << 7);
    }
    const std::uint32_t font = self + kFontLabel;
    s_.note(kFontDrawService, {font, x, 0x5a, name, 0}, m_.cstring(name, 0x40));
    const std::uint32_t label = scratch_line(retail_format("%s %d", {m_.cstring(global_word(m_, 0x0052eaf4), 0x20)}, {static_cast<std::int32_t>(seat) + 1}));
    if (!blink || (global_word(m_, kGlobalSimTick) % 0x3c) > 0x1e) {
        s_.note(kFontDrawService, {font, x, 0xb4, label, 0}, m_.cstring(label, 0x40));
    }
    return 0;
}

// 0x0047f930: the summary: "Begin Game", the level (and its subtitle), the two players
void NetMenu::summary_draw(std::uint32_t self) {
    const std::uint32_t font = self + kFontLabel;
    const std::uint32_t begin = global_word(m_, 0x0052eaf8);
    s_.note(kFontDrawService, {font, 0x100, 0x38, begin, 0}, m_.cstring(begin, 0x40));
    const std::uint32_t level = global_word(m_, kLevel);
    const std::uint32_t name = kLevels + level * 0x1ac;
    const std::uint32_t subtitle = word_at(m_, 0x0053c238 + level * 0x1ac);
    std::string line;
    if (subtitle != 0 && m_.cstring(subtitle, 0x100).size() > 1) {
        line = retail_format("%s, %s", {m_.cstring(name, 0x40), m_.cstring(subtitle, 0x80)}, {});
    } else {
        line = retail_format("%s", {m_.cstring(name, 0x40)}, {});
    }
    const std::uint32_t buffer = scratch_line(line);
    s_.note(kFontDrawService, {font, 0x100, 0x46, buffer, 0}, line);
    summary_draw_player(self, 0);
    summary_draw_player(self, 1);
    engine(0x004de040);
    w_.window_draw(self, 0, 0);
}

// 0x0047fad0: the back button: only in a network game; true when the connection was lost, there is no session, or the last player left (and it raises the back edge)
bool NetMenu::back_pressed() {
    bool result = false;
    if (pack_new_free()) {
        const std::int32_t players = s(global_word(m_, kNetPlayers));
        if (players != 0 && players <= 5) {
            result = true;
        }
        if (global_byte(m_, kNetLost) != 0 || global_byte(m_, kNetActive) == 0) {
            set_global_byte(m_, kPadBack, 1);
            return true;
        }
        if (result) {
            set_global_byte(m_, kPadBack, 1);
        }
    }
    return result;
}

// 0x0047fb20 (NETMENU_InitSummary): the summary window is built when a network game starts
void NetMenu::init_summary(std::uint32_t /*unused*/) {
    s_.note(kSvcPrintfLog, {0x00535ad4});
    if (global_byte(m_, kNetActive) != 0) {
        if (global_byte(m_, kPadBack) == 0 && s(global_word(m_, kNetPlayerCount)) > 5) {
            engine(0x004dfe50);
            engine(0x004dffb0);
        }
        const std::uint32_t memory = operator_new(kSummarySize);
        set_global_word(m_, kSummary, memory != 0 ? summary_construct(memory) : 0u);
        if (global_byte(m_, kPadBack) != 0 || s(global_word(m_, kNetPlayers)) <= 5) {
            set_global_byte(m_, 0x006a1417, 1);
            return;
        }
        if ((engine_value(0x004de060) & 0xff) != 0) {
            engine(0x004de2b0);
        }
    }
    if (pack_new_free() || global_word(m_, kGameMode) == 7) {
        engine(0x004de320);
    }
}

// 0x0047fc00
void NetMenu::summary_update() {
    if (global_byte(m_, kNetActive) != 0) {
        engine(0x004e02d0);
        if (global_word(m_, kSummary) != 0) {
            summary_draw(global_word(m_, kSummary));
        }
    }
}

// 0x0047fc20
void NetMenu::close_summary(std::uint32_t flag) {
    if (global_word(m_, kSummary) != 0) {
        if (seams_) {
            delete_object(global_word(m_, kSummary));
        } else {
            summary_delete(global_word(m_, kSummary), 1);
        }
        set_global_word(m_, kSummary, 0);
    }
    if ((flag & 0xff) != 0) {
        if (pack_new_free() || global_word(m_, kGameMode) == 7) {
            engine(0x004de420);
            engine(0x004de9c0);
            engine(0x004de270);
        }
    }
}

}  // namespace opentony::runtime::frontend
