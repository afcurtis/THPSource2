// THPSource2 launcher: read-only ISO 9660 (with Joliet names when present) for importing the game's files from a disc image. Sectors may be the plain
// 2048-byte kind (.iso) or raw 2352-byte ones (.bin/.img, Mode 1 or Mode 2 Form 1, as the disc image tony works from: game/THPS2.img).
#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace thpsource2 {

class DiscImage {
public:
    struct Entry {
        std::string name;   // without the ";1" version
        bool directory = false;
        std::uint32_t extent = 0;   // first sector
        std::uint32_t size = 0;     // bytes
    };

    static std::unique_ptr<DiscImage> open(const std::filesystem::path& path, std::string& error);

    const Entry& root() const { return root_; }
    std::vector<Entry> list(const Entry& directory);
    // "SETUP/data/ALL.PKR", any case; nullopt when a part of the path is missing
    std::optional<Entry> find(std::string_view path);
    // false when the image cannot be read or `keep_going` answers false (the partial file is removed)
    bool extract(const Entry& file, const std::filesystem::path& to, const std::function<bool(std::uint64_t)>& progress);

private:
    bool read_sector(std::uint32_t lba, std::uint8_t* out);
    std::ifstream file_;
    std::uint32_t sector_size_ = 2048;
    std::uint32_t data_offset_ = 0;
    bool joliet_ = false;
    Entry root_;
};

}  // namespace thpsource2
