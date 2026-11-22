// Replays a retail font trace (see scripts/make_font_trace_fixture.py) through the native
// font parser and eight-slot manager.
//
//   opentony_fnt_retail_trace_test FIXTURE.txt ALL.PKR
//
// Retail fonts come either from a loose data/*.FNT file or from a PRE-embedded copy, and the
// two differ, so the bytes are found by (size, CRC-32) among every FNT in the package and
// every FNT embedded in a package PRE. For each Font_Create the native slot, runtime entry
// bytes (+4/+5/+6, including the sentinel), glyph pixel offsets and dimensions
// (FontGlyph_ConstructImage arguments) and the palette-allocator word must match; Font_Find
// and Font_Unload replay in order. Every FNT in the package (bar one documented invalid
// file) must also parse strictly and its glyph pixels must end exactly at the end of the file.
//
// Exit codes: 0 all match, 1 mismatch, 2 usage/format error, 77 the archive is not available.
#include "fnt_asset.hpp"
#include "pkr_asset.hpp"
#include "pre_asset.hpp"

#include <zlib.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using namespace opentony::assets;

namespace {

int failures = 0;
int excluded = 0;

// The one shipped font whose records do not describe a runtime font: glyph pixels needed
// by record 14 onward run past the end of the file, and no string in THawk2.exe names it.
// Retail would read beyond the buffer if it were ever loaded; the native parser rejects it.
constexpr const char* kKnownInvalidFont = "data/LEVSEL.FNT";

void fail(long line, const std::string& message) {
    ++failures;
    if (failures <= 20) {
        std::cerr << "line " << line << ": " << message << "\n";
    }
}

bool ends_with_ignore_case(const std::string& text, const std::string& suffix) {
    return text.size() >= suffix.size()
        && std::equal(suffix.rbegin(), suffix.rend(), text.rbegin(), [](char a, char b) {
               return std::tolower(static_cast<unsigned char>(a))
                   == std::tolower(static_cast<unsigned char>(b));
           });
}

std::uint32_t crc_of(const std::vector<std::byte>& bytes) {
    return static_cast<std::uint32_t>(::crc32(
        0, reinterpret_cast<const Bytef*>(bytes.data()), static_cast<uInt>(bytes.size())));
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: " << argv[0] << " FIXTURE.txt ALL.PKR\n";
        return 2;
    }
    std::ifstream fixture(argv[1]);
    if (!fixture) {
        std::cerr << "cannot open fixture " << argv[1] << "\n";
        return 2;
    }
    if (!std::filesystem::exists(argv[2])) {
        std::cerr << "skipping: " << argv[2] << " is not available\n";
        return 77;
    }

    const PkrArchive package = PkrArchive::load(argv[2]);
    std::map<std::pair<std::size_t, std::uint32_t>, std::vector<std::byte>> sources;
    std::size_t corpus = 0;
    const auto add_source = [&](const std::string& label, std::vector<std::byte> bytes) {
        ++corpus;
        try {
            const FntRuntimeFont font = FntRuntimeFont::parse(bytes, label);
            std::size_t covered = 0;
            for (const RuntimeFontGlyphEntry& entry : font.entries()) {
                covered += entry.packed_size;
            }
            if (covered != font.packed_glyph_data().size()) {
                fail(0, label + ": glyph pixels do not end at the end of the file");
            }
        } catch (const std::exception& error) {
            if (label == kKnownInvalidFont) {
                ++excluded;
            } else {
                fail(0, label + ": " + error.what());
            }
        }
        sources.emplace(std::make_pair(bytes.size(), crc_of(bytes)), std::move(bytes));
    };
    for (std::size_t index = 0; index < package.entries().size(); ++index) {
        const PkrFileEntry& entry = package.entries()[index];
        if (ends_with_ignore_case(entry.name, ".fnt")) {
            add_source(entry.archive_path(), package.decode(index));
        } else if (ends_with_ignore_case(entry.name, ".pre")) {
            const PreArchive pre = PreArchive::parse(package.decode(index), entry.archive_path());
            for (std::size_t inner = 0; inner < pre.entries().size(); ++inner) {
                if (ends_with_ignore_case(pre.entries()[inner].name, ".fnt")) {
                    const auto payload = pre.payload(inner);
                    add_source(
                        entry.archive_path() + "/" + pre.entries()[inner].name,
                        std::vector<std::byte>(payload.begin(), payload.end()));
                }
            }
        }
    }

    FntRuntimeManager fonts;
    std::string text;
    long line = 0;
    int loads = 0;
    int lookups = 0;
    while (std::getline(fixture, text)) {
        ++line;
        if (text.empty() || text[0] == '#') {
            continue;
        }
        std::istringstream in(text);
        char kind = 0;
        in >> kind;
        if (kind == 'Z') {
            std::uintmax_t size = 0;
            in >> size;
            if (std::filesystem::file_size(argv[2]) != size) {
                std::cerr << "ALL.PKR size differs from the trace's archive\n";
                return 2;
            }
        } else if (kind == 'F') {
            std::string name;
            std::size_t size = 0;
            std::uint32_t count = 0;
            long slot = 0;
            std::uint32_t crc = 0;
            unsigned word = 0;
            int pal_x = 0;
            int pal_y = 0;
            int free_slot = 0;
            in >> name >> size >> std::hex >> crc >> std::dec >> count >> std::hex >> word
               >> std::dec >> slot >> pal_x >> pal_y >> free_slot;
            ++loads;

            std::vector<std::array<int, 3>> entries;
            std::vector<std::array<long, 3>> glyphs;
            for (std::uint32_t index = 0; index <= count; ++index) {
                std::getline(fixture, text);
                ++line;
                std::istringstream row(text);
                char tag = 0;
                std::array<int, 3> entry{};
                row >> tag >> entry[0] >> entry[1] >> entry[2];
                entries.push_back(entry);
            }
            for (std::uint32_t index = 0; index <= count; ++index) {
                std::getline(fixture, text);
                ++line;
                std::istringstream row(text);
                char tag = 0;
                std::string offset;
                std::array<long, 3> glyph{};
                row >> tag >> offset >> glyph[1] >> glyph[2];
                glyph[0] = offset == "-" ? -1 : std::stol(offset);
                glyphs.push_back(glyph);
            }

            const auto found = sources.find({size, crc});
            if (found == sources.end()) {
                fail(line, "no package font matches the bytes retail loaded for " + name);
                continue;
            }
            const std::uint16_t expected_word = FntRuntimeFont::clut_word(pal_x, pal_y, free_slot);
            if (expected_word != word) {
                fail(line, "atlas word formula differs for " + name);
            }
            try {
                const std::size_t native_slot = fonts.load(name, found->second, word);
                if (static_cast<long>(native_slot) != slot) {
                    fail(line, "slot differs for " + name);
                }
                const FntRuntimeFont& font = fonts.font(native_slot);
                if (font.glyph_count() != count || font.entries().size() != count + 1U) {
                    fail(line, "glyph count differs for " + name);
                    continue;
                }
                const std::size_t data_start = 4U + static_cast<std::size_t>(count) * 0x10U + 0x20U;
                for (std::uint32_t index = 0; index <= count; ++index) {
                    const RuntimeFontGlyphEntry& entry = font.entries()[index];
                    if (entry.source_byte_0 != entries[index][0]
                        || entry.source_byte_1 != entries[index][1]
                        || entry.source_byte_2 != entries[index][2]) {
                        fail(line, "entry bytes differ for " + name + " glyph " + std::to_string(index));
                    }
                    if (glyphs[index][1] != entry.source_byte_0
                        || glyphs[index][2] != entry.source_byte_1) {
                        fail(line, "glyph size differs for " + name + " glyph " + std::to_string(index));
                    }
                    if (index < count
                        && glyphs[index][0] != static_cast<long>(data_start + entry.packed_offset)) {
                        fail(line, "glyph pixel offset differs for " + name + " glyph "
                            + std::to_string(index));
                    }
                }
            } catch (const std::exception& error) {
                fail(line, "font load failed for " + name + ": " + error.what());
            }
        } else if (kind == 'N') {
            std::string action;
            std::string name;
            in >> action >> name;
            ++lookups;
            if (action == "find") {
                int expected = 0;
                in >> expected;
                if ((fonts.find(name) != nullptr) != (expected != 0)) {
                    fail(line, "Font_Find differs for " + name);
                }
            } else {
                try {
                    fonts.unload(name);
                } catch (const std::exception&) {
                    fail(line, "Font_Unload failed for " + name);
                }
            }
        } else {
            std::cerr << "line " << line << ": unknown record '" << kind << "'\n";
            return 2;
        }
    }

    std::cout << "replayed " << loads << " font loads, " << lookups << " lookups; parsed "
              << corpus << " package fonts (" << excluded << " known-invalid); failures="
              << failures << "\n";
    return failures == 0 ? 0 : 1;
}
