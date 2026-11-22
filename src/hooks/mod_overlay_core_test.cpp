#include "mod_overlay_core.h"

#include "tests/test_check.hpp"

#include <string>

namespace {

std::string key_of(const char* path) {
    char key[OT_OVERLAY_KEY_MAX];
    const unsigned length = ot_overlay_make_key(path, key, sizeof(key));
    return length == 0 ? std::string("<none>") : std::string(key, length);
}

} // namespace

int main() {
    // The game's paths: base directory, sub directory, name.
    CHECK(key_of("data\\SKWARE_T.TRG") == "skware_t.trg");
    CHECK(key_of("data\\logos\\Logo1.BMP") == "logos/logo1.bmp");
    CHECK(key_of("DATA/logos/x.bmp") == "logos/x.bmp");
    CHECK(key_of("skware.psx") == "skware.psx");
    CHECK(key_of("./data/./a.wav") == "a.wav");
    CHECK(key_of("data//lti//m.bmp") == "lti/m.bmp");
    // Only the leading base directory is dropped, and only once.
    CHECK(key_of("data\\data\\a.bmp") == "data/a.bmp");
    CHECK(key_of("database\\a.bmp") == "database/a.bmp");
    // Never a usable key: empty, absolute, outside the data directory, or not a file name.
    CHECK(key_of("") == "<none>");
    CHECK(key_of("data\\") == "<none>");
    CHECK(key_of("C:\\Windows\\x.ini") == "<none>");
    CHECK(key_of("\\x.bmp") == "<none>");
    CHECK(key_of("/x.bmp") == "<none>");
    CHECK(key_of("data\\..\\x.bmp") == "<none>");
    CHECK(key_of("..\\x.bmp") == "<none>");
    CHECK(key_of("a\\..") == "<none>");
    CHECK(key_of(nullptr) == "<none>");
    {
        char tiny[4];
        CHECK(ot_overlay_make_key("data\\abcdef.bmp", tiny, sizeof(tiny)) == 0);
        char exact[8];
        CHECK(ot_overlay_make_key("abcdefg", exact, sizeof(exact)) == 7);
        CHECK(ot_overlay_make_key("abcdefgh", exact, sizeof(exact)) == 0);
    }

    OtOverlayIndex index{};
    CHECK(ot_overlay_find(&index, "x.bmp") == nullptr);

    CHECK(ot_overlay_add(&index, "skware_t.trg", "mods/a/Data/SKWARE_T.TRG", 0) == 1);
    CHECK(ot_overlay_add(&index, "logos/logo1.bmp", "mods/a/Data/logos/logo1.bmp", 0) == 1);
    CHECK(ot_overlay_add(&index, "common.wav", "mods/a/Data/common.wav", 0) == 1);
    CHECK(index.count == 3);

    // Exact key.
    const OtOverlayEntry* trg = ot_overlay_find(&index, "skware_t.trg");
    CHECK(trg != nullptr && std::string(trg->path) == "mods/a/Data/SKWARE_T.TRG" && trg->mod == 0);
    const OtOverlayEntry* logo = ot_overlay_find(&index, "logos/logo1.bmp");
    CHECK(logo != nullptr && std::string(logo->path) == "mods/a/Data/logos/logo1.bmp");
    // Only the exact path answers: a file at the root of Data does not stand in for the same name
    // in another directory (the game asks for optional variants such as newbmp/s2bgd0.bmp).
    CHECK(ot_overlay_find(&index, "sfx/common.wav") == nullptr);
    CHECK(ot_overlay_find(&index, "common.wav") != nullptr);
    CHECK(ot_overlay_find(&index, "logo1.bmp") == nullptr);
    CHECK(ot_overlay_find(&index, "other/logo1.bmp") == nullptr);
    CHECK(ot_overlay_find(&index, "missing.trg") == nullptr);
    CHECK(ot_overlay_add(&index, "sfx/common.wav", "mods/a/Data/sfx/common.wav", 0) == 1);
    const OtOverlayEntry* wav = ot_overlay_find(&index, "sfx/common.wav");
    CHECK(wav != nullptr && std::string(wav->path) == "mods/a/Data/sfx/common.wav");

    // A later mod replaces an earlier mod's file for the same key.
    CHECK(ot_overlay_add(&index, "skware_t.trg", "mods/b/Data/skware_t.trg", 1) == 0);
    trg = ot_overlay_find(&index, "skware_t.trg");
    CHECK(trg != nullptr && std::string(trg->path) == "mods/b/Data/skware_t.trg" && trg->mod == 1);
    CHECK(index.count == 4);

    // Growth past the first allocation keeps every entry.
    for (unsigned i = 0; i < 1000; ++i) {
        const std::string key = "bulk/file" + std::to_string(i) + ".bmp";
        CHECK(ot_overlay_add(&index, key.c_str(), "p", 2) == 1);
    }
    CHECK(index.count == 1004);
    CHECK(ot_overlay_find(&index, "bulk/file999.bmp") != nullptr);
    CHECK(ot_overlay_find(&index, "bulk/file1000.bmp") == nullptr);

    // Over-long names are refused, not truncated.
    const std::string long_key(OT_OVERLAY_KEY_MAX, 'a');
    CHECK(ot_overlay_add(&index, long_key.c_str(), "p", 0) == -1);

    ot_overlay_free(&index);
    CHECK(index.count == 0 && index.entries == nullptr);
    return 0;
}
