#pragma once

// 11c, Kit mmu (mmu_manager.cpp, mmu_setup.cpp, mmu_screens.cpp): the typed views of the card manager, the save screen and what the screen builds (the elements, the font, the menu style), and the
// helpers for the words that no recovered layout holds (single globals, the scripted scratch locals, the loaded replay buffer's tail). Included by those .cpp files only (never by a header: the
// generated views are large and the Kit's header is included by half the runtime).

#include "guest_views_generated.hpp"
#include "mmu_kit.hpp"

namespace opentony::runtime::mmu::views {

using guest::GuestPtr;
using guest::RuntimeFontView;
using guest::RuntimeFrontElement;
using guest::RuntimeFrontElementView;
using guest::RuntimeFrontMenuProperties;
using guest::RuntimeFrontMenuPropertiesView;
using guest::RuntimeMmuFileRecord;
using guest::RuntimeMmuFileRecordView;
using guest::RuntimeMmuPromptGroup;
using guest::RuntimeMmuPromptGroupView;
using guest::RuntimeMmuSaveManagerView;
using guest::RuntimeMmuScreenView;
using guest::RuntimeReplayHeaderFieldsView;
using guest::read_scalar;
using guest::write_scalar;

using Word = std::uint32_t;

// The constants of mmu_kit.hpp are the offsets of the manager's fields: the layouts in re/types decide, and this is where the two are tied.
static_assert(manager::kRecords == guest::kRuntimeMmuSaveManager_file_records);
static_assert(manager::kRecordStride == guest::kRuntimeMmuFileRecordSize);
static_assert(manager::kRecordSize == manager::kRecords + guest::kRuntimeMmuFileRecord_file_size);
static_assert(manager::kTypes == guest::kRuntimeMmuSaveManager_file_types);
static_assert(manager::kNames == guest::kRuntimeMmuSaveManager_display_names);
static_assert(manager::kNameStride == guest::kRuntimeMmuDisplayNameSize);
static_assert(manager::kCount == guest::kRuntimeMmuSaveManager_file_count);
static_assert(manager::kState == guest::kRuntimeMmuSaveManager_card_state);
static_assert(manager::kFreeBlocks == guest::kRuntimeMmuSaveManager_free_card_blocks);
static_assert(manager::kBufferCount == guest::kRuntimeMmuSaveManager_save_buffer_count);
static_assert(manager::kBuffers == guest::kRuntimeMmuSaveManager_save_buffer_pointers);
static_assert(manager::kBufferSizes == guest::kRuntimeMmuSaveManager_save_buffer_sizes);
static_assert(manager::kObjectSize == guest::kRuntimeMmuScreenSize);
static_assert(manager::kInScreen == guest::kRuntimeMmuScreen_card);
static_assert(guest::kRuntimeMmuScreen_card + guest::kRuntimeMmuSaveManagerSize == guest::kRuntimeMmuScreen_banner);

// The card manager (the whole screen object holds it at kInScreen: the manager's own methods are given the sub-object).
[[nodiscard]] inline RuntimeMmuSaveManagerView card_at(RetailMemory& m, Word self) { return RuntimeMmuSaveManagerView(m, self); }
[[nodiscard]] inline RuntimeMmuScreenView screen_at(RetailMemory& m, Word self) { return RuntimeMmuScreenView(m, self); }
[[nodiscard]] inline RuntimeFrontElementView element_at(RetailMemory& m, Word at) { return RuntimeFrontElementView(m, at); }
[[nodiscard]] inline RuntimeFrontMenuPropertiesView props_at(RetailMemory& m, Word at) { return RuntimeFrontMenuPropertiesView(m, at); }
[[nodiscard]] inline RuntimeMmuFileRecordView file_at(RetailMemory& m, Word at) { return RuntimeMmuFileRecordView(m, at); }
// The manager's arrays by index, unchecked: the retail code asserts the index (the log line is all that happens) and goes on, so a file index past the last record or a fifth buffer reads and writes the
// words after the array (the fifth buffer pointer is the first buffer size). The address arithmetic wraps at 32 bits like the retail code's.
[[nodiscard]] inline Word display_name_at(RetailMemory& m, Word self, std::int32_t index) { return card_at(m, self).display_names_address(static_cast<std::size_t>(static_cast<Word>(index))); }
[[nodiscard]] inline std::int32_t file_type_at(RetailMemory& m, Word self, Word index) { return read_scalar<std::int32_t>(m, card_at(m, self).file_types_address(index)); }
inline void set_file_type_at(RetailMemory& m, Word self, Word index, std::int32_t value) { write_scalar<std::int32_t>(m, card_at(m, self).file_types_address(index), value); }
[[nodiscard]] inline Word save_buffer_pointer_at(RetailMemory& m, Word self, Word index) { return read_scalar<Word>(m, card_at(m, self).save_buffer_pointers_address(index)); }
inline void set_save_buffer_pointer_at(RetailMemory& m, Word self, Word index, Word value) { write_scalar<Word>(m, card_at(m, self).save_buffer_pointers_address(index), value); }
[[nodiscard]] inline Word save_buffer_size_at(RetailMemory& m, Word self, Word index) { return read_scalar<Word>(m, card_at(m, self).save_buffer_sizes_address(index)); }
inline void set_save_buffer_size_at(RetailMemory& m, Word self, Word index, Word value) { write_scalar<Word>(m, card_at(m, self).save_buffer_sizes_address(index), value); }

// A single global named by its retail address (no recovered layout contains it): one explicit-width access.
[[nodiscard]] inline Word global_word(const RetailMemory& m, Word at) { return read_scalar<Word>(m, at); }
[[nodiscard]] inline std::int32_t global_int(const RetailMemory& m, Word at) { return read_scalar<std::int32_t>(m, at); }
[[nodiscard]] inline std::uint8_t global_byte(const RetailMemory& m, Word at) { return read_scalar<std::uint8_t>(m, at); }
inline void set_global_word(RetailMemory& m, Word at, Word value) { write_scalar<Word>(m, at, value); }
inline void set_global_half(RetailMemory& m, Word at, Word value) { write_scalar<std::uint16_t>(m, at, static_cast<std::uint16_t>(value)); }
inline void set_global_byte(RetailMemory& m, Word at, Word value) { write_scalar<std::uint8_t>(m, at, static_cast<std::uint8_t>(value)); }

// The words of untyped buffers and objects (the scripted scratch locals, the pad's action bytes, the text box's font pointer, the card buffers' bytes).
[[nodiscard]] inline Word word_at(const RetailMemory& m, Word at) { return read_scalar<Word>(m, at); }
[[nodiscard]] inline std::int32_t int_at(const RetailMemory& m, Word at) { return read_scalar<std::int32_t>(m, at); }
[[nodiscard]] inline std::uint8_t byte_at(const RetailMemory& m, Word at) { return read_scalar<std::uint8_t>(m, at); }
inline void set_word_at(RetailMemory& m, Word at, Word value) { write_scalar<Word>(m, at, value); }
inline void set_byte_at(RetailMemory& m, Word at, Word value) { write_scalar<std::uint8_t>(m, at, static_cast<std::uint8_t>(value)); }

}  // namespace opentony::runtime::mmu::views
