// Replays a retail trick-database trace (Phase 4.7b) through the native database code:
//
//   opentony_trick_database_retail_trace_test FIXTURE.txt TRICKS.BIN
//
// The fixture (scripts/make_trickdb_trace_fixture.py) holds what the retail code left behind:
// every manager image Tricks_InitializeManager built, every character record
// TrickConfig_InitializeCharacter produced, the alternate binding block, and the key tables
// TrickTable_BuildForPlayer wrote. The native code starts from the pristine TRICKS.BIN alone and
// must reproduce each of them, including the names the retail constructor lowercases in place.
//
// Exit codes: 0 all match, 1 mismatch, 2 usage/format error, 77 TRICKS.BIN not available.
#include "trick_database.hpp"

#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace opentony::runtime;

namespace {

int failures = 0;
int checks = 0;

void fail(const std::string& what) {
    ++failures;
    if (failures <= 20) {
        std::cerr << "MISMATCH: " << what << '\n';
    }
}

void expect(bool condition, const std::string& what) {
    ++checks;
    if (!condition) {
        fail(what);
    }
}

std::vector<std::uint8_t> from_hex(const std::string& hex) {
    std::vector<std::uint8_t> out;
    for (std::size_t i = 0; i + 1 < hex.size(); i += 2) {
        out.push_back(static_cast<std::uint8_t>(std::stoi(hex.substr(i, 2), nullptr, 16)));
    }
    return out;
}

std::vector<std::uint8_t> read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

// A small SHA-256 so the fixture can name the exact TRICKS.BIN it was recorded against.
std::string sha256(const std::vector<std::uint8_t>& data) {
    static const std::uint32_t k[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
    std::uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                          0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    std::vector<std::uint8_t> msg = data;
    msg.push_back(0x80);
    while (msg.size() % 64 != 56) {
        msg.push_back(0);
    }
    const std::uint64_t bits = static_cast<std::uint64_t>(data.size()) * 8;
    for (int i = 7; i >= 0; --i) {
        msg.push_back(static_cast<std::uint8_t>(bits >> (8 * i)));
    }
    auto rotr = [](std::uint32_t x, int n) { return (x >> n) | (x << (32 - n)); };
    for (std::size_t chunk = 0; chunk < msg.size(); chunk += 64) {
        std::uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            w[i] = (std::uint32_t{msg[chunk + 4 * i]} << 24) | (std::uint32_t{msg[chunk + 4 * i + 1]} << 16) |
                   (std::uint32_t{msg[chunk + 4 * i + 2]} << 8) | msg[chunk + 4 * i + 3];
        }
        for (int i = 16; i < 64; ++i) {
            const std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i) {
            const std::uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            const std::uint32_t ch = (e & f) ^ (~e & g);
            const std::uint32_t t1 = hh + s1 + ch + k[i] + w[i];
            const std::uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t t2 = s0 + maj;
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
    std::ostringstream out;
    for (std::uint32_t x : h) {
        out.width(8);
        out.fill('0');
        out << std::hex << x;
    }
    return out.str();
}

struct Fixture {
    std::string sha;
    std::vector<std::pair<std::size_t, std::uint8_t>> changes;

    struct Manager {
        std::vector<TrickRecord> records;
        int times{};
    };
    std::vector<Manager> managers;

    struct Character {
        int index{};
        std::uint32_t class_mask{};
        std::vector<std::uint8_t> costs;
        std::vector<std::uint8_t> record;
    };
    std::vector<Character> characters;

    struct Alternate {
        std::size_t stream{};
        std::vector<std::uint8_t> block;
    };
    std::vector<Alternate> alternates;

    struct Table {
        int character{};
        std::size_t stream{};
        std::vector<std::uint8_t> block;
        std::vector<std::vector<std::string>> combos;
        std::vector<std::uint16_t> words;
    };
    std::vector<Table> tables;
};

bool parse(const std::string& path, Fixture& fx) {
    std::ifstream in(path);
    if (!in) {
        return false;
    }
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') {
            continue;
        }
        std::istringstream tokens(line);
        std::string kind;
        tokens >> kind;
        if (kind == "S") {
            tokens >> fx.sha;
        } else if (kind == "M") {
            std::string item;
            while (tokens >> item) {
                const auto colon = item.find(':');
                fx.changes.emplace_back(std::stoul(item.substr(0, colon), nullptr, 16),
                                        static_cast<std::uint8_t>(std::stoul(item.substr(colon + 1), nullptr, 16)));
            }
        } else if (kind == "D") {
            Fixture::Manager m;
            std::size_t count = 0;
            tokens >> count >> m.times;
            fx.managers.push_back(std::move(m));
            fx.managers.back().records.reserve(count);
        } else if (kind == "R") {
            std::string hexname;
            TrickRecord r;
            unsigned source = 0, cost = 0, flags = 0, descriptor = 0;
            tokens >> hexname >> source >> cost >> flags >> descriptor;
            const auto bytes = from_hex(hexname);
            r.name.assign(bytes.begin(), bytes.end());
            r.source = static_cast<std::uint16_t>(source);
            r.cost = static_cast<std::uint16_t>(cost);
            r.flags = static_cast<std::uint16_t>(flags);
            r.descriptor = static_cast<std::uint16_t>(descriptor);
            fx.managers.back().records.push_back(std::move(r));
        } else if (kind == "K") {
            Fixture::Character c;
            std::string costs, record;
            tokens >> c.index >> c.class_mask >> costs >> record;
            c.costs = from_hex(costs);
            c.record = from_hex(record);
            fx.characters.push_back(std::move(c));
        } else if (kind == "A") {
            Fixture::Alternate a;
            std::string block;
            tokens >> a.stream >> block;
            a.block = from_hex(block);
            fx.alternates.push_back(std::move(a));
        } else if (kind == "T") {
            Fixture::Table t;
            std::string block;
            tokens >> t.character >> t.stream >> block;
            t.block = from_hex(block);
            fx.tables.push_back(std::move(t));
        } else if (kind == "C") {
            std::vector<std::string> words;
            std::string w;
            while (tokens >> w) {
                words.push_back(w);
            }
            fx.tables.back().combos.push_back(std::move(words));
        } else if (kind == "W") {
            unsigned w = 0;
            while (tokens >> w) {
                fx.tables.back().words.push_back(static_cast<std::uint16_t>(w));
            }
        } else {
            return false;
        }
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: opentony_trick_database_retail_trace_test FIXTURE.txt TRICKS.BIN\n";
        return 2;
    }
    Fixture fx;
    if (!parse(argv[1], fx)) {
        std::cerr << "cannot parse fixture " << argv[1] << '\n';
        return 2;
    }
    auto pristine = read_file(argv[2]);
    if (pristine.empty()) {
        std::cout << "SKIP: TRICKS.BIN is not extracted\n";
        return 77;
    }
    expect(sha256(pristine) == fx.sha, "TRICKS.BIN is not the file the trace ran against");

    // Tricks_InitializeManager over the pristine file; the loaded buffer ends up as retail's.
    auto image = pristine;
    const auto db = TrickDatabase::build(image);
    auto expected_image = pristine;
    for (const auto& [offset, value] : fx.changes) {
        expected_image.at(offset) = value;
    }
    expect(image == expected_image, "the buffer after the manager differs from retail's lowercased buffer");

    int manager_images = 0;
    for (const auto& m : fx.managers) {
        manager_images += m.times;
        expect(db.count() == m.records.size(), "record count " + std::to_string(db.count()) + " vs retail " +
                                                   std::to_string(m.records.size()));
        for (std::size_t i = 0; i < m.records.size() && i < db.count(); ++i) {
            const TrickRecord& got = db.record(static_cast<int>(i));
            const TrickRecord& want = m.records[i];
            expect(got == want, "record " + std::to_string(i) + " native '" + got.name + "' " +
                                    std::to_string(got.source) + "/" + std::to_string(got.cost) + "/" +
                                    std::to_string(got.flags) + "/" + std::to_string(got.descriptor) + " retail '" +
                                    want.name + "' " + std::to_string(want.source) + "/" +
                                    std::to_string(want.cost) + "/" + std::to_string(want.flags) + "/" +
                                    std::to_string(want.descriptor));
        }
    }

    // TrickScript_CommandSize: every trick's script walks command by command to its 0x07 end marker
    // without leaving the image (the retail parser and the interpreter agree on every width).
    int scripts_walked = 0;
    for (const auto& r : db.records()) {
        std::size_t pos = r.source;
        std::vector<std::string> notes;
        std::size_t guard = 0;
        try {
            while (image.at(pos) != 0x07 && ++guard < 100000) {
                pos = trick_command_size(image, pos, &notes);
            }
            expect(image.at(pos) == 0x07 && notes.empty(), "script of '" + r.name + "' does not walk to its end marker");
            ++scripts_walked;
        } catch (const std::exception&) {
            fail("script of '" + r.name + "' leaves the image");
        }
    }

    // TrickConfig_InitializeCharacter: the native record must equal retail's byte for byte.
    std::map<int, std::vector<std::uint8_t>> character_blocks;
    for (const auto& c : fx.characters) {
        RetailImage config(kTrickConfigSize);
        std::array<std::uint8_t, 10> costs{};
        for (std::size_t i = 0; i < 10 && i < c.costs.size(); ++i) {
            costs[i] = c.costs[i];
        }
        trick_config_initialize_character(db, image, config, c.index, costs, c.class_mask == 0x3ff);
        const bool same = config.bytes() == c.record;
        expect(same, "character " + std::to_string(c.index) + " record differs");
        if (!same) {
            for (std::size_t i = 0; i < kTrickConfigSize; ++i) {
                if (config.bytes()[i] != c.record[i]) {
                    std::cerr << "  first difference at +" << std::hex << i << " native " << int(config.bytes()[i])
                              << " retail " << int(c.record[i]) << std::dec << '\n';
                    break;
                }
            }
        }
        character_blocks[c.index] = std::vector<std::uint8_t>(c.record.begin() + kConfigBindings,
                                                              c.record.begin() + kConfigBindings + kBindingBlockSize);
    }

    // The alternate block (section 6 stream).
    for (const auto& a : fx.alternates) {
        std::array<std::uint8_t, kBindingBlockSize> block{};
        expect(a.stream == trick_section_offset(image, kSectionAlternateKeys), "alternate stream offset");
        trick_apply_character_bindings(db, image, a.stream, block, 0);
        expect(std::vector<std::uint8_t>(block.begin(), block.end()) == a.block, "alternate binding block differs");
    }

    // TrickTable_BuildForPlayer: the key stream and block are the seam; the table is reproduced.
    int table_words = 0;
    for (const auto& t : fx.tables) {
        if (t.character < 0) {
            expect(t.stream == trick_section_offset(image, kSectionAlternateKeys), "alternate key stream offset");
            bool found = false;
            for (const auto& a : fx.alternates) {
                found = found || a.block == t.block;
            }
            expect(found, "the alternate block given to the builder is not the initialised alternate block");
        } else {
            expect(t.stream == trick_character_key_stream(image, t.character),
                   "character " + std::to_string(t.character) + " key stream offset");
            const auto known = character_blocks.find(t.character);
            if (known != character_blocks.end()) {
                expect(known->second == t.block, "the block given to the builder is not character " +
                                                     std::to_string(t.character) + "'s initialised block");
            }
        }
        const auto combos = trick_table_return_combos(db, image, t.stream);
        expect(combos.size() == t.combos.size(), "return-combo count");
        for (std::size_t i = 0; i < combos.size() && i < t.combos.size(); ++i) {
            for (std::size_t w = 0; w < 6; ++w) {
                if (t.combos[i][w] != "?") {
                    expect(std::to_string(combos[i][w]) == t.combos[i][w],
                           "return combo " + std::to_string(i) + " word " + std::to_string(w));
                }
            }
        }
        const auto table = trick_table_build_for_player(db, image, t.stream, t.block);
        table_words += static_cast<int>(table.size());
        expect(table == t.words, "player table differs (native " + std::to_string(table.size()) + " words, retail " +
                                     std::to_string(t.words.size()) + ")");
    }

    std::cout << "manager_images=" << manager_images << " records=" << db.count() << " scripts=" << scripts_walked << " characters="
              << fx.characters.size() << " alternates=" << fx.alternates.size() << " tables=" << fx.tables.size()
              << " table_words=" << table_words << " checks=" << checks << " failures=" << failures << '\n';
    return failures == 0 ? 0 : 1;
}
