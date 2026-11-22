#include "iso9660.hpp"

#include "ini.hpp"

#include <cstring>
#include <system_error>

namespace thpsource2 {

namespace {

constexpr std::uint32_t kSector = 2048;

std::uint32_t le32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<std::uint32_t>(p[3]) << 24));
}

// Joliet names are UCS-2, big-endian; the game's files are ASCII, anything else becomes UTF-8
std::string ucs2_to_utf8(const std::uint8_t* p, std::size_t bytes) {
    std::string out;
    for (std::size_t k = 0; k + 1 < bytes; k += 2) {
        const unsigned c = (p[k] << 8) | p[k + 1];
        if (c < 0x80) {
            out.push_back(static_cast<char>(c));
        } else if (c < 0x800) {
            out.push_back(static_cast<char>(0xc0 | (c >> 6)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3f)));
        } else {
            out.push_back(static_cast<char>(0xe0 | (c >> 12)));
            out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3f)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3f)));
        }
    }
    return out;
}

DiscImage::Entry entry_of(const std::uint8_t* record, bool joliet) {
    DiscImage::Entry e;
    e.extent = le32(record + 2);
    e.size = le32(record + 10);
    e.directory = (record[25] & 0x02) != 0;
    const std::uint8_t length = record[32];
    e.name = joliet ? ucs2_to_utf8(record + 33, length) : std::string(reinterpret_cast<const char*>(record + 33), length);
    if (const auto semicolon = e.name.find(';'); semicolon != std::string::npos) {
        e.name.resize(semicolon);
    }
    if (!e.name.empty() && e.name.back() == '.') {
        e.name.pop_back();
    }
    return e;
}

}  // namespace

std::unique_ptr<DiscImage> DiscImage::open(const std::filesystem::path& path, std::string& error) {
    auto image = std::unique_ptr<DiscImage>(new DiscImage());
    image->file_.open(path, std::ios::binary);
    if (!image->file_) {
        error = "cannot open " + path.string();
        return nullptr;
    }
    // the first volume descriptor is sector 16; its "CD001" says how the sectors are laid out
    const struct { std::uint32_t size, offset; } layouts[] = {{2048, 0}, {2352, 16}, {2352, 24}};
    bool found = false;
    for (const auto& layout : layouts) {
        std::uint8_t header[8] = {};
        image->file_.clear();
        image->file_.seekg(static_cast<std::streamoff>(16) * layout.size + layout.offset);
        image->file_.read(reinterpret_cast<char*>(header), sizeof header);
        if (image->file_ && std::memcmp(header + 1, "CD001", 5) == 0) {
            image->sector_size_ = layout.size;
            image->data_offset_ = layout.offset;
            found = true;
            break;
        }
    }
    if (!found) {
        error = path.filename().string() + " is not an ISO 9660 disc image (no CD001 volume descriptor)";
        return nullptr;
    }
    std::uint8_t sector[kSector];
    bool have_primary = false;
    for (std::uint32_t lba = 16; lba < 64; ++lba) {
        if (!image->read_sector(lba, sector) || std::memcmp(sector + 1, "CD001", 5) != 0) {
            break;
        }
        const std::uint8_t type = sector[0];
        if (type == 255) {
            break;
        }
        const bool joliet = type == 2 && sector[88] == '%' && sector[89] == '/' && (sector[90] == '@' || sector[90] == 'C' || sector[90] == 'E');
        if ((type == 1 && !have_primary) || joliet) {
            image->root_ = entry_of(sector + 156, false);
            image->root_.name.clear();
            image->joliet_ = joliet;
            have_primary = true;
        }
    }
    if (!have_primary) {
        error = path.filename().string() + " has no primary volume descriptor";
        return nullptr;
    }
    return image;
}

bool DiscImage::read_sector(std::uint32_t lba, std::uint8_t* out) {
    file_.clear();
    file_.seekg(static_cast<std::streamoff>(lba) * sector_size_ + data_offset_);
    file_.read(reinterpret_cast<char*>(out), kSector);
    return static_cast<bool>(file_);
}

std::vector<DiscImage::Entry> DiscImage::list(const Entry& directory) {
    std::vector<Entry> entries;
    std::uint8_t sector[kSector];
    const std::uint32_t sectors = (directory.size + kSector - 1) / kSector;
    for (std::uint32_t s = 0; s < sectors; ++s) {
        if (!read_sector(directory.extent + s, sector)) {
            break;
        }
        std::uint32_t at = 0;
        while (at < kSector) {
            const std::uint8_t length = sector[at];
            if (length == 0 || at + length > kSector || length < 34) {
                break;   // the records of a sector never cross into the next; a zero length ends this one
            }
            const std::uint8_t name_length = sector[at + 32];
            const bool self_or_parent = name_length == 1 && (sector[at + 33] == 0 || sector[at + 33] == 1);
            if (!self_or_parent) {
                entries.push_back(entry_of(sector + at, joliet_));
            }
            at += length;
        }
    }
    return entries;
}

std::optional<DiscImage::Entry> DiscImage::find(std::string_view path) {
    Entry current = root_;
    std::size_t start = 0;
    while (start < path.size()) {
        std::size_t end = path.find_first_of("/\\", start);
        if (end == std::string_view::npos) {
            end = path.size();
        }
        const std::string_view part = path.substr(start, end - start);
        start = end + 1;
        if (part.empty()) {
            continue;
        }
        if (!current.directory) {
            return std::nullopt;
        }
        bool found = false;
        for (const Entry& entry : list(current)) {
            if (equal_nocase(entry.name, part)) {
                current = entry;
                found = true;
                break;
            }
        }
        if (!found) {
            return std::nullopt;
        }
    }
    return current;
}

bool DiscImage::extract(const Entry& file, const std::filesystem::path& to, const std::function<bool(std::uint64_t)>& progress) {
    std::error_code ec;
    std::filesystem::create_directories(to.parent_path(), ec);
    std::filesystem::path partial = to;
    partial += ".part";
    std::ofstream out(partial, std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }
    std::uint8_t sector[kSector];
    std::uint64_t left = file.size;
    std::uint32_t lba = file.extent;
    while (left > 0) {
        if (!read_sector(lba++, sector)) {
            out.close();
            std::filesystem::remove(partial, ec);
            return false;
        }
        const std::uint32_t chunk = left < kSector ? static_cast<std::uint32_t>(left) : kSector;
        out.write(reinterpret_cast<const char*>(sector), chunk);
        left -= chunk;
        if (progress && !progress(chunk)) {
            out.close();
            std::filesystem::remove(partial, ec);
            return false;
        }
    }
    out.close();
    if (!out) {
        std::filesystem::remove(partial, ec);
        return false;
    }
    std::filesystem::rename(partial, to, ec);
    return !ec;
}

}  // namespace thpsource2
