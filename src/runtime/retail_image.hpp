#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace opentony::runtime {

// A little-endian byte image of one retail object (the gameplay skater is 0x3538
// bytes). The trick modules read and write it at the retail offsets so that a
// captured image can be replayed word for word; semantic names live next to the
// code that uses each offset.
class RetailImage final {
public:
    explicit RetailImage(std::size_t size = 0) : bytes_(size) {}

    [[nodiscard]] std::size_t size() const noexcept { return bytes_.size(); }
    [[nodiscard]] const std::vector<std::uint8_t>& bytes() const noexcept { return bytes_; }
    [[nodiscard]] std::vector<std::uint8_t>& bytes() noexcept { return bytes_; }

    [[nodiscard]] std::uint8_t u8(std::size_t offset) const { return bytes_.at(offset); }
    [[nodiscard]] std::int8_t s8(std::size_t offset) const { return static_cast<std::int8_t>(u8(offset)); }
    [[nodiscard]] std::uint16_t u16(std::size_t offset) const { return read<std::uint16_t>(offset); }
    [[nodiscard]] std::int16_t s16(std::size_t offset) const { return static_cast<std::int16_t>(u16(offset)); }
    [[nodiscard]] std::uint32_t u32(std::size_t offset) const { return read<std::uint32_t>(offset); }
    [[nodiscard]] std::int32_t s32(std::size_t offset) const { return static_cast<std::int32_t>(u32(offset)); }

    void set8(std::size_t offset, std::uint32_t value) { bytes_.at(offset) = static_cast<std::uint8_t>(value); }
    void set16(std::size_t offset, std::uint32_t value) { write<std::uint16_t>(offset, static_cast<std::uint16_t>(value)); }
    void set32(std::size_t offset, std::uint32_t value) { write<std::uint32_t>(offset, value); }

    friend bool operator==(const RetailImage&, const RetailImage&) = default;

private:
    template <typename T>
    [[nodiscard]] T read(std::size_t offset) const {
        if (offset + sizeof(T) > bytes_.size()) {
            throw std::out_of_range("retail image read");
        }
        T value{};
        std::memcpy(&value, bytes_.data() + offset, sizeof(T));
        return value;
    }
    template <typename T>
    void write(std::size_t offset, T value) {
        if (offset + sizeof(T) > bytes_.size()) {
            throw std::out_of_range("retail image write");
        }
        std::memcpy(bytes_.data() + offset, &value, sizeof(T));
    }

    std::vector<std::uint8_t> bytes_;
};

} // namespace opentony::runtime
