// Replays a retail file-I/O trace (see scripts/make_fileio_trace_fixture.py) through the
// native package backend and PRE manager.
//
//   opentony_resource_retail_trace_test FIXTURE.txt ALL.PKR
//
// For every backend open the native path split must equal what retail passed to
// PKR2_OpenResource, the native lookup must find the same file record (marker, offset, both
// sizes), succeed or fail exactly when retail did, and land in the same slot with the same
// size. Seeks, reads (count and CRC-32 of the bytes), closes, PRE loads, unloads and embedded
// lookups replay in retail order.
//
// Exit codes: 0 all match, 1 mismatch, 2 usage/format error, 77 the archive is not available.
#include "pkr_asset.hpp"
#include "pre_runtime.hpp"
#include "resource_runtime.hpp"

#include <zlib.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace opentony::assets;

namespace {

struct Lookup {
    bool found{};
    std::string directory;
    std::string name;
    std::uint32_t marker{};
    std::uint64_t offset{};
    std::uint32_t stored{};
    std::uint32_t decoded{};
};

int failures = 0;
std::map<char, int> counts;

void fail(long line, const std::string& message) {
    ++failures;
    if (failures <= 20) {
        std::cerr << "line " << line << ": " << message << "\n";
    }
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
    ResourceBackend backend(package);
    PreRuntimeManager pre;
    ResourceLoader loader(backend, &pre);
    // The base directory is empty until FileIO_Init (record I) sets it from the
    // "data\????????.???" template.
    std::string expect_path;
    int expect_exists = -1;
    Lookup pending;
    bool have_pending = false;
    std::string last_path;

    std::string text;
    long line = 0;
    while (std::getline(fixture, text)) {
        ++line;
        if (text.empty() || text[0] == '#') {
            continue;
        }
        std::istringstream in(text);
        char kind = 0;
        in >> kind;
        ++counts[kind];
        if (kind == 'Z') {
            std::uintmax_t size = 0;
            in >> size;
            if (std::filesystem::file_size(argv[2]) != size) {
                std::cerr << "ALL.PKR size differs from the trace's archive\n";
                return 2;
            }
        } else if (kind == 'K') {
            int found = 0;
            in >> found >> pending.directory >> pending.name;
            pending.found = found != 0;
            if (pending.found) {
                in >> std::hex >> pending.marker >> std::dec >> pending.offset >> pending.stored
                    >> pending.decoded;
            }
            have_pending = true;
        } else if (kind == 'I') {
            std::size_t base_length = 0;
            in >> base_length;
            const std::string base = "data\\";
            if (base_length != base.size()) {
                fail(line, "FileIO_Init base length differs");
            }
            loader.set_base_dir(base);
        } else if (kind == 'D') {
            std::string sub;
            in >> sub;
            try {
                loader.set_sub_dir(sub);
            } catch (const ResourceRuntimeError& error) {
                fail(line, std::string("sub directory rejected: ") + error.what());
            }
        } else if (kind == 'A') {
            std::string name;
            long size = 0;
            char how = 0;
            in >> name >> size >> how;
            if (how == 'p') {
                try {
                    if (loader.open(name) != static_cast<std::size_t>(size)
                        || loader.source_kind() != ResourceSourceKind::PreEmbedded) {
                        fail(line, "native did not serve " + name + " from the PRE container");
                    }
                    loader.close();
                } catch (const ResourceRuntimeError&) {
                    fail(line, "native failed to open PRE resource " + name);
                }
            } else {
                if (ResourceLoader::eligible_for_pre_lookup(name) && pre.find_embedded(name)) {
                    fail(line, "native would take the PRE path for " + name);
                }
                expect_path = loader.backend_path(name);
            }
        } else if (kind == 'X') {
            std::string name;
            in >> name >> expect_exists;
            expect_path = loader.backend_path(name);
        } else if (kind == 'O') {
            char owner = 0;
            long slot = 0;
            long size = 0;
            std::string path;
            in >> owner >> slot >> size;
            in.get();
            std::getline(in, path);
            if (owner == 'r' || owner == 'e') {
                if (expect_path.empty()) {
                    fail(line, "game-owned open without its call record: " + path);
                } else if (path != expect_path) {
                    fail(line, "backend path differs: native '" + expect_path + "', retail '"
                        + path + "'");
                }
                if (owner == 'e' && expect_exists != -1 && (slot >= 0) != (expect_exists != 0)) {
                    fail(line, "FileIO_Exists result differs for " + path);
                }
                expect_path.clear();
                expect_exists = -1;
            }
            if (!have_pending) {
                fail(line, "open without a preceding package lookup");
                continue;
            }
            have_pending = false;
            const auto split = ResourceBackend::split_path(path);
            if (split.directory != pending.directory || split.name != pending.name) {
                fail(line, "path split differs for " + path + ": native '" + split.directory
                    + "' + '" + split.name + "', retail '" + pending.directory + "' + '"
                    + pending.name + "'");
            }
            const PkrFileEntry* entry = package.find_resource(pending.directory, pending.name);
            if ((entry != nullptr) != pending.found) {
                fail(line, "lookup result differs for " + path);
            } else if (entry != nullptr
                && (entry->marker != pending.marker || entry->payload_offset != pending.offset
                    || entry->stored_size != pending.stored
                    || entry->decoded_size != pending.decoded)) {
                fail(line, "file record differs for " + path);
            }
            try {
                const ResourceHandle handle = backend.open(path);
                if (slot < 0) {
                    fail(line, "native opened " + path + " but retail failed");
                } else if (static_cast<long>(handle) != slot) {
                    fail(line, "slot differs for " + path);
                } else if (static_cast<long>(backend.size(handle)) != size) {
                    fail(line, "size differs for " + path);
                }
                last_path = path;
            } catch (const ResourceRuntimeError&) {
                if (slot >= 0) {
                    fail(line, "native failed to open " + path);
                }
            }
        } else if (kind == 'S') {
            long slot = 0;
            long offset = 0;
            int whence = 0;
            long ret = 0;
            in >> slot >> offset >> whence >> ret;
            try {
                backend.seek(
                    static_cast<ResourceHandle>(slot), offset,
                    whence == 0 ? ResourceSeekOrigin::Begin
                                : whence == 1 ? ResourceSeekOrigin::Current
                                              : ResourceSeekOrigin::End);
                if (static_cast<long>(backend.position(static_cast<ResourceHandle>(slot))) != ret) {
                    fail(line, "seek result differs");
                }
            } catch (const ResourceRuntimeError& error) {
                fail(line, std::string("seek failed: ") + error.what());
            }
        } else if (kind == 'R') {
            long slot = 0;
            std::size_t count = 0;
            long ret = 0;
            std::string crc;
            in >> slot >> count >> ret >> crc;
            try {
                std::vector<std::byte> buffer(count);
                const std::size_t got = backend.read(static_cast<ResourceHandle>(slot), buffer);
                if (static_cast<long>(got) != ret) {
                    fail(line, "read count differs");
                } else if (crc != "-") {
                    const uLong native_crc = ::crc32(
                        0, reinterpret_cast<const Bytef*>(buffer.data()),
                        static_cast<uInt>(got));
                    if (native_crc != std::stoul(crc, nullptr, 16)) {
                        fail(line, "read bytes differ");
                    }
                }
            } catch (const ResourceRuntimeError& error) {
                fail(line, std::string("read failed: ") + error.what());
            }
        } else if (kind == 'C') {
            long slot = 0;
            long ret = 0;
            in >> slot >> ret;
            try {
                backend.close(static_cast<ResourceHandle>(slot));
                if (ret != 0) {
                    fail(line, "retail close failed but native succeeded");
                }
            } catch (const ResourceRuntimeError&) {
                if (ret == 0) {
                    fail(line, "native close failed");
                }
            }
        } else if (kind == 'L') {
            std::string name;
            long slot = 0;
            in >> name >> slot;
            try {
                ResourceLoader loader(backend);
                const std::vector<std::byte> bytes = loader.load_owned(last_path);
                if (static_cast<long>(pre.load(name, bytes)) != slot) {
                    fail(line, "PRE slot differs for " + name);
                }
            } catch (const std::exception& error) {
                fail(line, std::string("PRE load failed: ") + error.what());
            }
        } else if (kind == 'U') {
            std::string name;
            in >> name;
            try {
                pre.unload(name);
            } catch (const std::exception&) {
                fail(line, "PRE unload failed for " + name);
            }
        } else if (kind == 'Q') {
            std::string name;
            long size = 0;
            in >> name >> size;
            const auto view = pre.find_embedded(name);
            if (view.has_value() != (size >= 0)
                || (view.has_value() && static_cast<long>(view->payload.size()) != size)) {
                fail(line, "embedded lookup differs for " + name);
            }
        } else {
            std::cerr << "line " << line << ": unknown record '" << kind << "'\n";
            return 2;
        }
    }

    std::cout << "replayed";
    for (const auto& [kind, count] : counts) {
        std::cout << ' ' << kind << '=' << count;
    }
    std::cout << " failures=" << failures << "\n";
    return failures == 0 ? 0 : 1;
}
