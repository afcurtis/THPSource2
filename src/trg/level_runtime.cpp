#include "level_runtime.hpp"

#include "../assets/resource_runtime.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <vector>

namespace {

[[nodiscard]] std::string upper_copy(std::string value) {
    std::transform(
        value.begin(), value.end(), value.begin(),
        [](unsigned char character) {
            return static_cast<char>(std::toupper(character));
        });
    return value;
}

[[nodiscard]] std::optional<std::size_t> package_psx_entry(
    const opentony::assets::PkrArchive& package,
    std::string_view stem) {
    const std::string wanted = upper_copy(std::string(stem) + ".PSX");
    for (std::size_t index = 0; index < package.entries().size(); ++index) {
        const auto& entry = package.entries()[index];
        if (upper_copy(entry.name) == wanted) {
            return index;
        }
    }
    return std::nullopt;
}

[[nodiscard]] const opentony::assets::PkrFileEntry* package_resource_entry(
    const opentony::assets::PkrArchive& package,
    std::string_view requested) {
    const std::string wanted = upper_copy(std::string(requested));
    const std::string wanted_stem = upper_copy(
        std::filesystem::path(std::string(requested)).stem().string());
    for (const opentony::assets::PkrFileEntry& entry : package.entries()) {
        const std::string entry_name = upper_copy(entry.name);
        const std::string entry_stem = upper_copy(
            std::filesystem::path(entry.name).stem().string());
        if (entry_name == wanted || entry_stem == wanted_stem) {
            return &entry;
        }
    }
    return nullptr;
}

[[nodiscard]] std::vector<std::byte> load_package_resource(
    const opentony::assets::PkrArchive& package,
    std::string_view name) {
    opentony::assets::ResourceBackend backend(package);
    opentony::assets::ResourceLoader loader(backend);
    return loader.load_owned(name);
}

} // namespace

namespace opentony::trg {

// The live object world of a level: it turns the trigger state's node pulses
// and messages into ScriptWorld operations, and supplies the script services
// (skater position, frame time, Rnd, career flags, outward pulses).
class LevelRuntime::World final : public ScriptWorldEnvironment, public TriggerWorldObserver {
public:
    explicit World(LevelRuntime& owner)
        : owner_(owner), script_world_(owner.runtime_.file(), *this), random_(owner.random_seed_) {}

    ScriptWorld& script_world() noexcept { return script_world_; }
    const ScriptWorld& script_world() const noexcept { return script_world_; }
    std::size_t faulted_pulses() const noexcept { return faulted_pulses_; }

    void begin_frame(std::uint32_t milliseconds) noexcept {
        // Retail frame time is milliseconds in 12-bit fixed point.
        frame_delta_ = milliseconds << 12U;
    }

    // ScriptWorldEnvironment
    [[nodiscard]] std::size_t player_count() const override { return 1; }
    [[nodiscard]] ScriptVec3 player_position(std::size_t) const override { return owner_.player_position_; }
    [[nodiscard]] ScriptVec3 player_velocity() const override { return owner_.player_velocity_; }
    [[nodiscard]] std::uint32_t frame_delta() const override { return frame_delta_; }
    // Platform waits count simulation time; without the retail timer model the
    // level clock in milliseconds stands in for it.
    [[nodiscard]] std::uint32_t simulation_time() const override {
        return static_cast<std::uint32_t>(owner_.state_.time_ms());
    }
    [[nodiscard]] std::int32_t tick_counter() const override { return static_cast<std::int32_t>(owner_.tick_counter_); }
    // Models resolve against the level's own PSX (region 0).
    [[nodiscard]] ScriptModelInfo model_info(std::uint8_t region, std::uint16_t index) override {
        const auto& models = owner_.archive_.models();
        if (region != 0 || index >= models.size()) {
            return {};
        }
        return {(models[index].flags & 0x10U) != 0, models[index].bounds};
    }
    // Thrown props probe the level's static collision. This is the conservative
    // geometric segment test of PsxCollisionWorld, not yet the exact retail
    // query (see the moving-prop-collision slice).
    [[nodiscard]] ScriptCollisionHit line_collision(const ScriptVec3& start, const ScriptVec3& end) override {
        const std::optional<assets::PsxCollisionHit> hit = owner_.collision_.trace_segment(start, end);
        if (!hit.has_value()) {
            return {};
        }
        return {true, hit->position, hit->normal};
    }
    [[nodiscard]] std::uint16_t find_model_index(std::uint32_t checksum, std::uint8_t) override {
        const auto& names = owner_.archive_.model_names();
        const auto found = std::find(names.begin(), names.end(), checksum);
        return found == names.end() ? std::uint16_t{0} : static_cast<std::uint16_t>(found - names.begin());
    }
    [[nodiscard]] bool has_target() const override { return true; }
    [[nodiscard]] ScriptVec3 target_position() const override { return owner_.player_position_; }
    [[nodiscard]] std::uint32_t random(std::uint16_t range) override {
        return static_cast<std::uint32_t>(random_.next(range));
    }
    // SFX_Grunt draws Rnd(3) for its sound; the draw is kept so the random
    // stream stays where retail leaves it.
    void skater_grunt(std::uint32_t) override { (void)random_.next(3); }
    [[nodiscard]] bool level_flag(std::uint16_t flag) const override {
        return owner_.state_.career_flag(flag);
    }
    void set_level_flag(std::uint16_t flag) override { owner_.state_.set_career_flag(flag); }
    void send_pulse(std::uint16_t node) override {
        try {
            owner_.runtime_.pulse_node(node);
        } catch (const FormatError&) {
            // The same isolation TriggerRuntime applies to link fan-out: a
            // pulse into a dead PlayStation record is dropped, not fatal.
            ++faulted_pulses_;
        }
    }

    // TriggerWorldObserver
    void world_pulse(std::size_t node) override { (void)script_world_.pulse_node(node); }
    void world_activate_or_suspend(
        std::size_t, std::uint16_t opcode, std::span<const std::uint16_t> targets) override {
        script_world_.activate_or_suspend(targets, opcode);
    }
    void world_signal(std::size_t, std::span<const std::uint16_t> targets) override {
        script_world_.signal(targets);
    }
    void world_visible(std::size_t, std::uint16_t value, std::span<const std::uint16_t> targets) override {
        script_world_.visible(targets, value);
    }
    void world_kill(std::size_t, std::uint16_t opcode, std::span<const std::uint16_t> targets) override {
        script_world_.kill(targets, opcode == 0x000c ? 1 : 0);
    }

private:
    LevelRuntime& owner_;
    ScriptWorld script_world_;
    RetailRandom random_;
    std::uint32_t frame_delta_{};
    std::size_t faulted_pulses_{};
};

LevelRuntime::~LevelRuntime() {
    state_.set_world_observer(nullptr);
}

ScriptWorld* LevelRuntime::objects() noexcept {
    return world_ ? &world_->script_world() : nullptr;
}

const ScriptWorld* LevelRuntime::objects() const noexcept {
    return world_ ? &world_->script_world() : nullptr;
}

void LevelRuntime::set_player_position(const ScriptVec3& position) noexcept {
    player_position_ = position;
}

void LevelRuntime::set_player_velocity(const ScriptVec3& velocity) noexcept {
    player_velocity_ = velocity;
}

void LevelRuntime::seed_random(RetailRandom::State state) noexcept {
    random_seed_ = state;
}

std::size_t LevelRuntime::object_faulted_pulses() const noexcept {
    return world_ ? world_->faulted_pulses() : 0;
}

LevelRuntime::LevelRuntime(
    const std::string& trg_path,
    const std::string& psx_path,
    const std::string& asset_root)
    : state_(),
      runtime_(TrgFile::load(trg_path), state_),
      archive_(assets::PsxArchive::load(psx_path)),
      scene_runtime_(assets::PsxRuntimeEnvironment::build(archive_)),
      collision_(assets::PsxCollisionWorld::build(archive_)),
      asset_root_(asset_root),
      catalog_(asset_root.empty()
          ? std::nullopt
          : std::optional<assets::PsxAssetCatalog>(assets::PsxAssetCatalog::scan(asset_root))),
      pre_catalog_(asset_root.empty()
          ? std::nullopt
          : std::optional<assets::PreAssetCatalog>(assets::PreAssetCatalog::scan(asset_root))),
      scene_(),
      package_(nullptr) {}

LevelRuntime::LevelRuntime(
    const assets::PkrArchive& package,
    std::string_view trg_entry,
    std::string_view psx_entry,
    const std::string& asset_root)
    : state_(),
      runtime_(TrgFile::parse(load_package_resource(package, trg_entry)), state_),
      archive_(assets::PsxArchive::parse(
          load_package_resource(package, psx_entry), std::string(psx_entry))),
      scene_runtime_(assets::PsxRuntimeEnvironment::build(archive_)),
      collision_(assets::PsxCollisionWorld::build(archive_)),
      asset_root_(asset_root),
      catalog_(asset_root.empty()
          ? std::nullopt
          : std::optional<assets::PsxAssetCatalog>(assets::PsxAssetCatalog::scan(asset_root))),
      pre_catalog_(asset_root.empty()
          ? std::nullopt
          : std::optional<assets::PreAssetCatalog>(assets::PreAssetCatalog::scan(asset_root))),
      scene_(),
      package_(&package) {}

void LevelRuntime::initialize(bool two_player, const GapTable* gap_table) {
    state_.set_world_observer(nullptr);
    world_.reset();
    state_.reset();
    state_.set_gap_table(gap_table);
    // The world must exist before the autoexec pulses: they create objects.
    world_ = std::make_unique<World>(*this);
    state_.set_world_observer(world_.get());
    runtime_.initialize(two_player);
    camera_points_.build(runtime_.file());
    rails_.build(runtime_.file());
    rails_.synchronize_states(state_);
    if (catalog_.has_value()) {
        traffic_.build(runtime_.file(), &*catalog_);
    } else if (package_ != nullptr) {
        traffic_.build(runtime_.file(), *package_);
    } else {
        traffic_.build(runtime_.file());
    }
    factory_objects_.build(runtime_.file(), &state_);
    factory_objects_.synchronize(state_);
    powerups_.build(runtime_.file(), catalog_.has_value() ? &*catalog_ : nullptr);
    item_archive_.reset();
    medal_archive_.reset();
    bits_archive_.reset();
    item_runtime_.reset();
    medal_runtime_.reset();
    item_texture_runtime_.reset();
    medal_texture_runtime_.reset();
    if (catalog_.has_value() && catalog_->contains("items")) {
        item_runtime_ = assets::PsxRuntimeEnvironment::build(
            catalog_->load("items"));
    }
    if (catalog_.has_value() && catalog_->contains("skmedals")) {
        medal_runtime_ = assets::PsxRuntimeEnvironment::build(
            catalog_->load("skmedals"));
    }
    if (!catalog_.has_value() && package_ != nullptr) {
        if (const auto entry = package_psx_entry(*package_, "ITEMS");
            entry.has_value()) {
            item_archive_ = assets::PsxArchive::parse(
                package_->decode(*entry),
                package_->entry(*entry).archive_path());
            item_runtime_ = assets::PsxRuntimeEnvironment::build(*item_archive_);
        }
        if (const auto entry = package_psx_entry(*package_, "SKMEDALS");
            entry.has_value()) {
            medal_archive_ = assets::PsxArchive::parse(
                package_->decode(*entry),
                package_->entry(*entry).archive_path());
            medal_runtime_ = assets::PsxRuntimeEnvironment::build(*medal_archive_);
        }
    }
    const auto build_texture_region = [this](
        const std::optional<assets::PsxRuntimeEnvironment>& region)
        -> std::optional<assets::PcTextureRuntime> {
        if (!region.has_value()) {
            return std::nullopt;
        }
        std::optional<assets::PcTextureRuntime> textures;
        if (catalog_.has_value()) {
            textures = assets::PcTextureRuntime::build_external(
                *region, asset_root_);
        } else if (package_ != nullptr) {
            textures = assets::PcTextureRuntime::build_external(
                *region, *package_);
        } else {
            textures = assets::PcTextureRuntime::build_inline(*region);
        }
        textures->add_inline_missing(*region);
        return textures;
    };
    item_texture_runtime_ = build_texture_region(item_runtime_);
    medal_texture_runtime_ = build_texture_region(medal_runtime_);
    powerups_.resolve_runtime_models(
        item_runtime_.has_value() ? &*item_runtime_ : nullptr,
        medal_runtime_.has_value() ? &*medal_runtime_ : nullptr);
    bits_runtime_.reset();
    if (catalog_.has_value() && catalog_->contains("bits")) {
        bits_runtime_.emplace();
        bits_runtime_->build(catalog_->load("bits"));
    }
    if (!catalog_.has_value() && package_ != nullptr) {
        if (const auto entry = package_psx_entry(*package_, "BITS");
            entry.has_value()) {
            bits_archive_ = assets::PsxArchive::parse(
                package_->decode(*entry),
                package_->entry(*entry).archive_path());
            bits_runtime_.emplace();
            bits_runtime_->build(*bits_archive_);
        }
    }
    state_.bind_psx_models(archive_);
    if (catalog_.has_value()) {
        state_.resolve_pickup_assets(*catalog_);
    }
    scene_.build(state_, archive_);
    if (catalog_.has_value()) {
        texture_runtime_ = assets::PcTextureRuntime::build_external(
            scene_runtime_, asset_root_);
    } else if (package_ != nullptr) {
        texture_runtime_ = assets::PcTextureRuntime::build_external(
            scene_runtime_, *package_);
    } else {
        texture_runtime_ = assets::PcTextureRuntime::build_inline(scene_runtime_);
    }
    texture_runtime_->add_inline_missing(scene_runtime_);
    if (catalog_.has_value()) {
        scene_.resolve_factory_assets(*catalog_);
    }
    refresh_resource_bindings();
    initialized_ = true;
}

void LevelRuntime::tick(std::uint32_t milliseconds) {
    if (!initialized_) {
        throw FormatError("level runtime was ticked before initialize");
    }
    state_.advance_time(milliseconds);
    ++tick_counter_;
    if (world_) {
        world_->begin_frame(milliseconds);
        world_->script_world().run_frame();
        rails_.synchronize_states(state_);
    }
    scene_.sync(state_);
    factory_objects_.synchronize(state_);
}

void LevelRuntime::pulse_node(std::size_t node) {
    if (!initialized_) {
        throw FormatError("level runtime was pulsed before initialize");
    }
    runtime_.pulse_node(node);
    rails_.synchronize_states(state_);
    scene_.sync(state_);
    factory_objects_.synchronize(state_);
    refresh_resource_bindings();
}

void LevelRuntime::pulse_checksum(std::uint32_t checksum) {
    if (!initialized_) {
        throw FormatError("level runtime was pulsed before initialize");
    }
    const CommandPointRuntime* point = runtime_.command_point_by_checksum(checksum);
    if (point == nullptr) {
        throw FormatError("no command point has the requested checksum");
    }
    pulse_node(point->source_node);
}

void LevelRuntime::execute_restart(std::size_t node) {
    if (!initialized_) {
        throw FormatError("level runtime executed a restart before initialize");
    }
    runtime_.execute_restart(node);
    rails_.synchronize_states(state_);
    scene_.sync(state_);
    factory_objects_.synchronize(state_);
}

void LevelRuntime::execute_restart(std::string_view name) {
    if (!initialized_) {
        throw FormatError("level runtime executed a restart before initialize");
    }
    const std::size_t node = runtime_.find_restart_by_name(name);
    if (node == CommandPointRuntime::npos) {
        throw FormatError("restart name was not found in the level");
    }
    execute_restart(node);
}

const assets::PsxAssetCatalog* LevelRuntime::asset_catalog() const noexcept {
    return catalog_.has_value() ? &*catalog_ : nullptr;
}

const assets::PreAssetCatalog* LevelRuntime::pre_catalog() const noexcept {
    return pre_catalog_.has_value() ? &*pre_catalog_ : nullptr;
}

void LevelRuntime::refresh_resource_bindings() {
    resources_.clear();
    resources_.reserve(state_.resources().size());
    for (const TriggerResourceRequest& request : state_.resources()) {
        LevelResourceBinding binding{};
        binding.request = request;
        if (catalog_.has_value()) {
            if (const std::string* path = catalog_->path_for(request.name);
                path != nullptr) {
                binding.asset_path = *path;
                binding.asset_available = true;
                // Keep the trigger resource boundary lazy at the catalog
                // level, but make a requested resource's parse observable at
                // the level-runtime boundary. PsxAssetCatalog owns the cache,
                // so repeated script requests do not duplicate the archive.
                const assets::PsxArchive& archive = catalog_->load(request.name);
                binding.asset_loaded = true;
                binding.asset_object_count = archive.objects().size();
                binding.asset_model_count = archive.model_names().size();
            }
        } else if (package_ != nullptr) {
            if (const assets::PkrFileEntry* entry = package_resource_entry(
                    *package_, request.name);
                entry != nullptr) {
                binding.asset_path = entry->archive_path();
                binding.asset_available = true;
            }
        }
        resources_.push_back(std::move(binding));
    }
}

} // namespace opentony::trg
