#include "pre_runtime.hpp"

#include <algorithm>
#include <cctype>
#include <utility>

namespace opentony::assets {
namespace {

[[nodiscard]] unsigned char lower_ascii(unsigned char value) noexcept {
    return static_cast<unsigned char>(std::tolower(value));
}

} // namespace

void PreRuntimeManager::validate_container_name(std::string_view name) {
    if (name.empty()) {
        throw PreFormatError("PRE manager name is empty");
    }
    if (name.size() >= kRuntimePreNameSize) {
        throw PreFormatError("PRE manager name exceeds the 16-byte slot");
    }
}

std::size_t PreRuntimeManager::load(
    std::string name,
    std::vector<std::byte> bytes) {
    validate_container_name(name);
    std::size_t slot = slots_.size();
    for (std::size_t index = 0; index < slots_.size(); ++index) {
        if (!slots_[index].has_value()) {
            slot = index;
            break;
        }
    }
    if (slot == slots_.size()) {
        throw PreFormatError("PRE manager has no free slots");
    }

    PreArchive archive = PreArchive::parse(std::move(bytes), name);
    slots_[slot].emplace(Slot{std::move(name), std::move(archive)});
    ++loaded_count_;
    return slot;
}

std::size_t PreRuntimeManager::load_file(
    std::string name,
    const std::string& path) {
    validate_container_name(name);
    std::size_t slot = slots_.size();
    for (std::size_t index = 0; index < slots_.size(); ++index) {
        if (!slots_[index].has_value()) {
            slot = index;
            break;
        }
    }
    if (slot == slots_.size()) {
        throw PreFormatError("PRE manager has no free slots");
    }

    PreArchive archive = PreArchive::load(path);
    slots_[slot].emplace(Slot{std::move(name), std::move(archive)});
    ++loaded_count_;
    return slot;
}

const PreArchive& PreRuntimeManager::container(std::size_t slot) const {
    if (slot >= slots_.size() || !slots_[slot].has_value()) {
        throw PreFormatError("PRE manager slot is not loaded");
    }
    return slots_[slot]->archive;
}

const std::string& PreRuntimeManager::container_name(std::size_t slot) const {
    if (slot >= slots_.size() || !slots_[slot].has_value()) {
        throw PreFormatError("PRE manager slot is not loaded");
    }
    return slots_[slot]->name;
}

std::optional<PreEmbeddedResourceView> PreRuntimeManager::find_embedded(
    std::string_view resource_name) const {
    if (resource_name.empty()) {
        return std::nullopt;
    }
    // 0x004a9410 lowercases its copy of the query, then 0x004a9480 compares
    // the stored entry names byte for byte, so an entry whose stored name has
    // a capital letter can never be found (none does in the shipped PREs).
    std::string query(resource_name);
    for (char& character : query) {
        character = static_cast<char>(lower_ascii(static_cast<unsigned char>(character)));
    }
    for (std::size_t slot = 0; slot < slots_.size(); ++slot) {
        if (!slots_[slot].has_value()) {
            continue;
        }
        const PreArchive& archive = slots_[slot]->archive;
        for (std::size_t entry_index = 0; entry_index < archive.entries().size(); ++entry_index) {
            const PreEntry& entry = archive.entries()[entry_index];
            if (entry.name != query) {
                continue;
            }
            return PreEmbeddedResourceView{
                slot,
                slots_[slot]->name,
                entry.name,
                archive.payload(entry_index),
            };
        }
    }
    return std::nullopt;
}

void PreRuntimeManager::unload(std::string_view name) {
    // 0x004a9330 compares the stored 16-byte name with a plain byte compare:
    // the unload name must match the load name exactly.
    for (std::optional<Slot>& slot : slots_) {
        if (!slot.has_value() || slot->name != name) {
            continue;
        }
        slot.reset();
        --loaded_count_;
        return;
    }
    throw PreFormatError("PRE file is not loaded: " + std::string(name));
}

} // namespace opentony::assets
