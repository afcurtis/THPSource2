#include "skater_asset_runtime.hpp"

#include "tests/test_assets.hpp"
#include "tests/test_check.hpp"
#include <filesystem>
#include <iostream>
#include <string>

namespace {

[[nodiscard]] std::string asset_path(const char* relative) {
    const std::filesystem::path root =
        opentony::test::data_dir();
    return (root / relative).string();
}

} // namespace

int main() {
    const std::string animation = asset_path("SK2ANIM.PSH");
    const std::string model = asset_path("HAWK2.PSH");
    const std::string psx = asset_path("HAWK2.PSX");
    if (!opentony::test::have_file(animation)
        || !opentony::test::have_file(model)
        || !opentony::test::have_file(psx)) {
        return opentony::test::exit_code();
    }

    opentony::runtime::SkaterAssetRuntime assets(animation, model, psx, 6);
    CHECK(assets.region_slot() == 6);
    CHECK(assets.animation_manifest().parts().size() == 19);
    CHECK(assets.model_manifest().parts().size() == 19);
    CHECK(assets.part_matches().size() == 19);
    const auto remap = assets.match_for_animation_part(1);
    CHECK(remap.has_value());
    CHECK(remap->model_index == 3);
    CHECK(assets.runtime().object_count() == 19);
    CHECK(assets.runtime().model_count() == 19);
    CHECK(&assets.model_for_part(1)
        == assets.runtime().model_pointer(3));

    opentony::runtime::SkaterRuntimeObject skater(0, 0, 0);
    assets.bind(skater, 1);
    CHECK(skater.psx_region_slot() == 6);
    CHECK(skater.model_index() == 3);

    std::cout << "Skater asset runtime tests passed\n";
    return opentony::test::exit_code();
}
