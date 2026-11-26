#include "linux_guest_memory.hpp"

#include <sys/mman.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <system_error>
#include <vector>

namespace opentony::kitdll {
namespace {

constexpr std::uint32_t kTimestamp = 0x3a54c1f6;  // canonical PC executable
constexpr std::uint32_t kFirstDataAddress = 0x00518000;  // start of .rdata
constexpr std::size_t kHeaderSize = 24;
constexpr std::size_t kEntrySize = 8;

std::uint32_t read_word(const std::uint8_t* bytes) {
    return std::uint32_t{bytes[0]} | (std::uint32_t{bytes[1]} << 8) |
           (std::uint32_t{bytes[2]} << 16) | (std::uint32_t{bytes[3]} << 24);
}

std::size_t page_size() {
    const long size = sysconf(_SC_PAGESIZE);
    if (size <= 0 || arena::kNullGuardEnd % size != 0) {
        throw std::runtime_error("unsupported page size for the guest arena");
    }
    return static_cast<std::size_t>(size);
}

struct Section {
    std::uint32_t address;
    std::span<const std::uint8_t> bytes;
};

}  // namespace

LinuxGuestMemory::LinuxGuestMemory()
    : page_size_(page_size()),
      heap_(arena::kHeapBase, arena::kHeapSize, [this](std::uint32_t address, std::uint32_t size) {
          return commit(address, size);
      }),
      mapping_(mmap(nullptr, arena::kArenaBytes, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0)) {
    if (mapping_ == MAP_FAILED) {
        throw std::system_error(errno, std::generic_category(), "reserve guest arena");
    }
    if (!commit(arena::kImageBase, arena::kZonesEnd - arena::kImageBase) ||
        !commit(arena::kScratchWindow, arena::kScratchWindowSize)) {
        const int error = errno;
        munmap(mapping_, arena::kArenaBytes);
        throw std::system_error(error, std::generic_category(), "commit guest startup pages");
    }
}

LinuxGuestMemory::~LinuxGuestMemory() {
    munmap(mapping_, arena::kArenaBytes);
}

bool LinuxGuestMemory::commit(std::uint32_t address, std::uint32_t size) const noexcept {
    const std::uint64_t end = std::uint64_t{address} + size;
    if (address < arena::kNullGuardEnd || end > 0x100000000ull) {
        errno = EINVAL;
        return false;
    }
    if (size == 0) {
        return true;
    }
    const std::uint64_t first_page = address - address % page_size_;
    const std::uint64_t end_page = ((end + page_size_ - 1) / page_size_) * page_size_;
    return mprotect(reinterpret_cast<void*>(base() + first_page), end_page - first_page, PROT_READ | PROT_WRITE) == 0;
}

std::size_t LinuxGuestMemory::load_static_data(std::span<const std::uint8_t> image) {
    if (image.size() < kHeaderSize || std::memcmp(image.data(), "OTSD", 4) != 0) {
        throw std::runtime_error("expected an OTSD static-data image");
    }
    if (read_word(image.data() + 4) != 1 || read_word(image.data() + 8) != arena::kImageBase ||
        read_word(image.data() + 12) != kTimestamp ||
        read_word(image.data() + 16) != arena::kImageEnd - arena::kImageBase) {
        throw std::runtime_error("static data targets a different retail build");
    }
    const std::size_t count = read_word(image.data() + 20);
    if (count == 0 || count > (image.size() - kHeaderSize) / kEntrySize) {
        throw std::runtime_error("invalid static-data section table");
    }
    std::size_t payload = kHeaderSize + count * kEntrySize;
    const std::size_t data_start = payload;
    std::uint32_t previous_end = kFirstDataAddress;
    std::vector<Section> sections;
    for (std::size_t index = 0; index < count; ++index) {
        const auto* entry = image.data() + kHeaderSize + index * kEntrySize;
        const std::uint32_t address = read_word(entry);
        const std::uint32_t size = read_word(entry + 4);
        if (address < previous_end || std::uint64_t{address} + size > arena::kImageEnd ||
            size > image.size() - payload) {
            throw std::runtime_error("static-data section overlaps another section or falls outside the image");
        }
        sections.push_back({address, image.subspan(payload, size)});
        previous_end = address + size;
        payload += size;
    }
    if (payload != image.size() || payload == data_start) {
        throw std::runtime_error("invalid static-data payload size");
    }
    for (const auto& section : sections) {
        std::memcpy(reinterpret_cast<void*>(base() + section.address), section.bytes.data(), section.bytes.size());
    }
    return payload - data_start;
}

}  // namespace opentony::kitdll
