#pragma once

#include "pkr_asset.hpp"
#include "pre_runtime.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace opentony::assets {

class ResourceRuntimeError final : public std::runtime_error {
public:
    explicit ResourceRuntimeError(const std::string& message)
        : std::runtime_error(message) {}
};

using ResourceHandle = std::size_t;
inline constexpr ResourceHandle kInvalidResourceHandle = static_cast<ResourceHandle>(-1);

enum class ResourceSourceKind : std::uint8_t {
    DirectFile,
    PkrPackage,
    PreEmbedded,
};

enum class ResourceSeekOrigin : std::uint8_t {
    Begin,
    Current,
    End,
};

// The retail backend exposes an abstract slot rather than a Win32 file
// descriptor. A native backend owns the bytes for one open resource so a
// direct file and a decoded PKR2 entry have the same seek/read lifetime.
class ResourceBackend final {
public:
    // getFreeFileIndex (0x004e78e0) scans ten slots; a full table makes
    // FileIO_BackendOpen (0x004e75b0) return -1, reported here as an error.
    inline static constexpr std::size_t kSlotCapacity = 10;

    explicit ResourceBackend(std::filesystem::path direct_root = {});
    explicit ResourceBackend(const PkrArchive& package);
    // Package first, then a loose file below loose_root when the package has
    // no such entry: the retail mode-0 open falls back to a file stream.
    ResourceBackend(const PkrArchive& package, std::filesystem::path loose_root);

    // _splitpath as used by 0x004e75b0 (0x00501fd5): a leading "X:" drive is
    // dropped, the directory keeps its trailing separator with every '\' turned
    // into '/', and the file name (with extension) is cut at 255 bytes.
    struct SplitPath {
        std::string directory;
        std::string name;
    };
    [[nodiscard]] static SplitPath split_path(std::string_view path);

    [[nodiscard]] ResourceHandle open(std::string_view path);
    void seek(
        ResourceHandle handle,
        std::int64_t offset,
        ResourceSeekOrigin origin = ResourceSeekOrigin::Begin);
    [[nodiscard]] std::size_t read(
        ResourceHandle handle,
        std::span<std::byte> destination);
    void read_exact(ResourceHandle handle, std::span<std::byte> destination);
    void close(ResourceHandle handle);

    [[nodiscard]] bool is_open(ResourceHandle handle) const noexcept;
    [[nodiscard]] std::size_t size(ResourceHandle handle) const;
    [[nodiscard]] std::size_t position(ResourceHandle handle) const;
    [[nodiscard]] ResourceSourceKind source_kind(ResourceHandle handle) const;
    [[nodiscard]] const std::string& name(ResourceHandle handle) const;

private:
    struct Slot {
        std::string name;
        std::vector<std::byte> bytes;
        std::size_t position{};
        ResourceSourceKind source_kind{};
    };

    std::filesystem::path direct_root_;
    bool loose_fallback_{};
    const PkrArchive* package_{};
    std::vector<std::optional<Slot>> slots_;

    [[nodiscard]] ResourceHandle add_slot(
        std::string name,
        std::vector<std::byte> bytes,
        ResourceSourceKind source_kind);
    [[nodiscard]] const Slot& slot(ResourceHandle handle) const;
    [[nodiscard]] Slot& slot(ResourceHandle handle);
    [[nodiscard]] std::vector<std::byte> read_file(std::string_view path) const;
};

// Native adapter for the observed 0x38-byte package stream. That object is the
// MSVC CRT FILE (cursor, remaining count, buffer, flag word 0x10c = _IOMYBUF |
// _IONBF | _IOYOURBUF, handle, buffer size) and 0x00502387 is its fread, so the
// buffering flags and transfer block stay CRT internals here; reads keep the
// element multiplication and fread's return value (complete elements read).
class ResourceStream final {
public:
    static ResourceStream open(ResourceBackend& backend, std::string_view path);

    ResourceStream(ResourceBackend& backend, ResourceHandle handle) noexcept;
    ~ResourceStream();

    ResourceStream(const ResourceStream&) = delete;
    ResourceStream& operator=(const ResourceStream&) = delete;
    ResourceStream(ResourceStream&& other) noexcept;
    ResourceStream& operator=(ResourceStream&& other) noexcept;

    void seek(
        std::int64_t offset,
        ResourceSeekOrigin origin = ResourceSeekOrigin::Begin);
    [[nodiscard]] std::size_t read(std::span<std::byte> destination);
    [[nodiscard]] std::size_t read_elements(
        std::size_t element_count,
        std::size_t element_size,
        std::span<std::byte> destination);
    void close() noexcept;

    [[nodiscard]] bool is_open() const noexcept;
    [[nodiscard]] std::size_t size() const;
    [[nodiscard]] std::size_t position() const;

private:
    ResourceBackend* backend_{};
    ResourceHandle handle_{kInvalidResourceHandle};
};

// Counterpart of the game-owned 0x00449030/0x00449230/0x00449660 boundary.
// FileIO_Init (0x00448f50) keeps a base directory ("data\\") in front of every
// name, FileIO_SetSubDir (0x00449700) a sub directory (at most 15 characters,
// empty or ending in a backslash), and the backend path is base + sub + name.
// Both default to empty, which passes a name through unchanged.
// The native result is a value-owned byte vector. It deliberately leaves the
// retail allocator's count prefix and asynchronous spool implementation as
// platform adapters while preserving their ownership seam.
class ResourceLoader final {
public:
    // 0x004a9410 copies the name into a 16-byte local, so 0x00449030 tries the
    // PRE fast path only for names shorter than that, and never for a name that
    // contains "bmp" (a case-insensitive substring test, 0x004b82b0): bitmaps
    // take the backend path. Separators are not tested.
    inline static constexpr std::size_t kEmbeddedNameLimit = 0x10;
    inline static constexpr std::size_t kSubDirLimit = 0x10;
    [[nodiscard]] static bool eligible_for_pre_lookup(std::string_view name) noexcept;

    ResourceLoader(ResourceBackend& backend, const PreRuntimeManager* pre = nullptr) noexcept;
    ~ResourceLoader();

    ResourceLoader(const ResourceLoader&) = delete;
    ResourceLoader& operator=(const ResourceLoader&) = delete;

    void set_base_dir(std::string_view base);
    // Throws when the name is 16 or more characters or does not end in '\'
    // (retail only logs those assertions).
    void set_sub_dir(std::string_view sub);
    [[nodiscard]] const std::string& sub_dir() const noexcept { return sub_dir_; }
    // The path the backend receives for a name: base + sub directory + name.
    [[nodiscard]] std::string backend_path(std::string_view name) const;
    // FileIO_Exists (0x00449460): a backend open and close, never the PRE path.
    [[nodiscard]] bool exists(std::string_view name);

    [[nodiscard]] std::size_t open(std::string_view name);
    void load(std::span<std::byte> destination);
    void synchronize();
    void close() noexcept;
    [[nodiscard]] std::vector<std::byte> load_owned(std::string_view name);

    [[nodiscard]] bool active() const noexcept { return active_.has_value(); }
    [[nodiscard]] bool load_started() const noexcept;
    [[nodiscard]] std::size_t size() const;
    [[nodiscard]] ResourceSourceKind source_kind() const;
    [[nodiscard]] const std::string& name() const;

private:
    struct ActiveLoad {
        std::string name;
        ResourceSourceKind source_kind{};
        std::span<const std::byte> embedded_payload{};
        ResourceHandle backend_handle{kInvalidResourceHandle};
        bool load_started{};
    };

    ResourceBackend* backend_{};
    const PreRuntimeManager* pre_{};
    std::string base_dir_;
    std::string sub_dir_;
    std::optional<ActiveLoad> active_;

    void close_active() noexcept;
    [[nodiscard]] const ActiveLoad& require_active() const;
    [[nodiscard]] ActiveLoad& require_active();
};

} // namespace opentony::assets
