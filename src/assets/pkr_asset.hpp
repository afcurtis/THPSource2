#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace opentony::assets {

class PkrFormatError final : public std::runtime_error {
public:
    explicit PkrFormatError(const std::string& message)
        : std::runtime_error(message) {}
};

inline constexpr std::uint32_t kPkrRawMarker = 0xfffffffeU;

struct PkrDirectory {
    std::string name;
    std::size_t entries_offset{};
    std::uint32_t file_count{};
};

struct PkrFileEntry {
    std::string directory;
    std::string name;
    std::uint32_t marker{};
    std::size_t payload_offset{};
    std::uint32_t stored_size{};
    std::uint32_t decoded_size{};

    [[nodiscard]] std::string archive_path() const {
        if (directory.empty()) {
            return name;
        }
        return directory.back() == '/' ? directory + name : directory + "/" + name;
    }
};

// Native package/backend boundary for the PC PKR2 container. Directory and
// file records are retained as value data; decode() returns a caller-owned
// resource buffer matching the runtime entry-payload dispatch.
class PkrArchive final {
public:
    static PkrArchive load(const std::string& path);
    static PkrArchive parse(std::vector<std::byte> bytes, std::string source = {});

    [[nodiscard]] std::uint32_t version() const noexcept { return version_; }
    [[nodiscard]] const std::vector<PkrDirectory>& directories() const noexcept {
        return directories_;
    }
    [[nodiscard]] const std::vector<PkrFileEntry>& entries() const noexcept {
        return entries_;
    }
    [[nodiscard]] const PkrFileEntry& entry(std::size_t index) const;
    [[nodiscard]] const PkrFileEntry* find(std::string_view archive_path) const noexcept;
    // Runtime lookup of PKR2_OpenResource (0x004e9940): the first directory
    // record whose name matches, then the first file inside that directory,
    // both with an ASCII case-insensitive compare (0x0050b7a0). Retail passes
    // the directory with its trailing separator ("data/"); one trailing '/'
    // on either side is ignored here so a bare directory name also matches.
    [[nodiscard]] const PkrFileEntry* find_resource(
        std::string_view directory,
        std::string_view name) const noexcept;
    [[nodiscard]] std::span<const std::byte> stored_payload(std::size_t index) const;
    [[nodiscard]] std::vector<std::byte> decode(std::size_t index) const;
    [[nodiscard]] std::vector<std::byte> decode(std::string_view archive_path) const;

private:
    std::vector<std::byte> bytes_;
    std::string source_;
    std::uint32_t version_{};
    std::vector<PkrDirectory> directories_;
    std::vector<PkrFileEntry> entries_;
};

} // namespace opentony::assets
