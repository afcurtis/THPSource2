#include "psx_asset.hpp"

#include "tests/test_assets.hpp"
#include "tests/test_check.hpp"
#include <filesystem>
#include <iostream>

int main() {
    const std::filesystem::path path =
        opentony::test::data_dir() / "C_VAN.PSX";
    if (!opentony::test::have_file(path)) {
        return opentony::test::exit_code();
    }

    const opentony::assets::PsxArchive archive =
        opentony::assets::PsxArchive::load(path.string());
    CHECK(!archive.textures().empty());
    const auto& source = archive.textures().front();
    const auto decoded = archive.decode_texture(0);
    const std::uint32_t alignment = source.color_count == 16U
        ? 3U
        : source.color_count == 256U ? 1U : 0U;
    const std::uint32_t expected_width =
        (static_cast<std::uint32_t>(source.width) + alignment) & ~alignment;
    const std::uint32_t expected_height =
        (static_cast<std::uint32_t>(source.height) + alignment) & ~alignment;
    CHECK(decoded.width == expected_width);
    CHECK(decoded.height == expected_height);
    CHECK(decoded.rgb.size()
        == static_cast<std::size_t>(decoded.width) * decoded.height * 3U);
    CHECK(!decoded.rgb.empty());

    std::cout << "PSX texture tests passed\n";
    return opentony::test::exit_code();
}
