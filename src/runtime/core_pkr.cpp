#include "core_views.hpp"
#include "seam_addresses_generated.hpp"

#include <stdexcept>

// Phase 4.14c: the relocatable-module loader (0x004ac020..), the PKR2 package wrappers (0x004e9150..0x004e9b80: the run-length and zlib compression of the resource files, the package header
// load and unload), the small stream object at 0x004ebf10 and the explosion object / head file pieces of `exp.cpp` and `fileio`. The allocator, the C runtime, zlib and the package streams are
// services; the compression itself, the run-length coding and the relocation arithmetic run in the port.

namespace opentony::runtime::core {

using namespace views;

namespace {

using Word = std::uint32_t;

constexpr Word kMalloc = seam::crt::CRT_malloc;             // CRT_malloc(size)
constexpr Word kFree = seam::crt::CRT_free;               // ffree(block)
constexpr Word kSprintf = seam::crt::CRT_sprintf;            // CRT_sprintf(buffer, format, ...)
constexpr Word kZlibCompress = seam::zlib::zlib_compress;       // (destination, &destination length, source, source length) -> status
constexpr Word kZlibUncompress = 0x004f9d50;     // (destination, &destination length, source, source length) -> status
constexpr Word kLogError = seam::crt::CRT_log_error;           // FUN_00502688(format, ...): the error print
constexpr Word kOperatorNew = seam::crt::CRT_operator_new;
constexpr Word kOperatorDelete = seam::crt::FUN_005002f3;
constexpr Word kStreamClose = 0x00500e7b;        // ResourceStream_Close(stream)
constexpr Word kOpenPackageStream = seam::crt::PKR2_OpenPackageStream;  // PKR2_OpenPackageStream(name, mode) -> stream
constexpr Word kReadResource = 0x00502358;       // PKR2_ReadResource(destination, size, count, stream)
constexpr Word kFileOpenLoad = 0x00448dd0;       // FileIO_OpenLoad(path, argument) -> the loaded block

constexpr Word kModuleList = 0x0056b89c;         // the loaded modules (nodes of 0x34 bytes: +4 entry, +8 hash, +0x2c next, +0x30 previous)
constexpr Word kCurrentPackage = 0x006a7578;     // the loaded package's name (0x100 bytes)
constexpr Word kPackageEntries = 0x006a7678;     // the package's resource table (0x28 bytes each)
constexpr Word kPackageFiles = 0x006a767c;       // the package's file table (0x30 bytes each)
constexpr Word kEmptyName = 0x0055fe10;          // the empty string
constexpr Word kPackageMode = 0x0054b348;        // the mode text handed to the stream opener
constexpr Word kCompressTable = 0x0054b408;      // the three compression wrappers by method
constexpr Word kUncompressedFormat = 0x0054b4cc; // "%s Uncompressed"-style text the decompressors format into a local
constexpr Word kModuleVtable = 0x004ac060;       // what a loaded module's node points at

constexpr Word kMsgZlibSmall = 0x0054b420;
constexpr Word kMsgZlibMemory = 0x0054b468;
constexpr Word kMsgZlibUnexpected = 0x0054b4a4;
constexpr Word kMsgUnzlibSmall = 0x0054b4dc;
constexpr Word kMsgUnzlibMemory = 0x0054b518;
constexpr Word kMsgUnzlibData = 0x0054b558;
constexpr Word kMsgUnzlibUnexpected = 0x0054b59c;
constexpr Word kMsgBadMethod = 0x0054b5c8;

constexpr Word kLocalA = pcplatform::kScratchBase + 0x60;       // stack locals of the retail functions
constexpr Word kLocalHeader = pcplatform::kScratchBase + 0x90;
constexpr Word kLocalText = pcplatform::kScratchBase + 0xc0;

constexpr Word kWrapperBibd = 0x004e9150;
constexpr Word kWrapperWibd = 0x004e9200;
constexpr Word kWrapperZlib = 0x004e92c0;

}  // namespace

int CoreKit::compare_strings(Word a, Word b) const {
    for (;; ++a, ++b) {
        const std::uint8_t x = byte_at(m_, a);
        const std::uint8_t y = byte_at(m_, b);
        if (x != y) {
            return x < y ? -1 : 1;
        }
        if (x == 0) {
            return 0;
        }
    }
}

void CoreKit::copy_string(Word to, Word from) {
    std::vector<std::uint8_t> bytes;
    for (Word at = from;; ++at) {
        bytes.push_back(byte_at(m_, at));
        if (bytes.back() == 0) {
            break;
        }
    }
    m_.write(to, bytes);
}

void CoreKit::fill_bytes(Word destination, Word count, std::uint8_t value) {
    if (count != 0) {
        m_.fill(destination, value, count);
    }
}

// a service that filled `bytes` bytes at `destination`: the probe scripted them as dwords under the service's name, the last one padded
void CoreKit::take_filled(Word destination, Word bytes, const std::string& service) {
    if (s_.live()) {
        return;  // (the real service already filled the bytes; the scripted words are replay-only)
    }
    std::vector<std::uint8_t> data;
    for (Word k = 0; k < (bytes + 3) / 4; ++k) {
        const auto word = static_cast<Word>(s_.value(service));
        for (unsigned b = 0; b < 4; ++b) {
            data.push_back(static_cast<std::uint8_t>(word >> (8 * b)));
        }
    }
    data.resize(bytes);
    if (bytes != 0) {
        m_.write(destination, data);
    }
}

void CoreKit::read_resource(Word destination, Word size, Word count, Word stream) {
    engine(kReadResource, {destination, size, count, stream});
    take_filled(destination, size * count, pcplatform::engine_name(kReadResource));
}

// ---- the relocatable-module loader ------------------------------------------------------------------------------------------------------------------------------------------

Word CoreKit::relocation_hash(Word name) {
    Word hash = 0xffffffffu;
    for (Word at = name;; ++at) {
        const std::uint8_t c = byte_at(m_, at);
        if (c == 0) {
            break;
        }
        std::uint8_t cl = static_cast<std::uint8_t>(c ^ (hash & 0xff));
        for (int k = 0; k < 8; ++k) {
            hash = (hash << 1) | (hash >> 31);
            if ((cl & 1) != 0) {
                hash ^= 0xedb88320u;
            }
            cl = static_cast<std::uint8_t>(static_cast<std::int8_t>(cl) >> 1);
        }
    }
    return hash;
}

// 0x00448cb0: the same bitwise CRC as the module hash, over the lower-cased name (a logical shift of the byte), -1 for the empty name
Word CoreKit::file_name_hash(Word name) {
    Word hash = 0xffffffffu;
    for (Word at = name;; ++at) {
        std::uint8_t c = byte_at(m_, at);
        if (c == 0) {
            break;
        }
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<std::uint8_t>(c + 0x20);
        }
        std::uint8_t cl = static_cast<std::uint8_t>(c ^ (hash & 0xff));
        for (int k = 0; k < 8; ++k) {
            hash = (hash << 1) | (hash >> 31);
            if ((cl & 1) != 0) {
                hash ^= 0xedb88320u;
            }
            cl = static_cast<std::uint8_t>(cl >> 1);
        }
    }
    return hash;
}

// 0x004ac060: the module object's first virtual method: an assertion that always fails
void CoreKit::module_destroy() { check(false, 0x00537828); }

// 0x004ac0c0: a module is a code block (name.bin) and a relocation table (name.rel); the code block is relocated to where it was loaded, the table released and the entry point called
void CoreKit::load_module_pair(Word name, Word argument) {
    const Word hash = relocation_hash(name);
    for (Word node = global_word(m_, kModuleList); node != 0; node = module_at(m_, node).next().address) {
        if (module_at(m_, node).name_hash() == hash) {
            return;
        }
    }
    const Word node = alloc(0x34, 0, 1, 0);
    const Word head = global_word(m_, kModuleList);
    const RuntimeRelocatableModuleNodeView created = module_at(m_, node);
    created.set_next(GuestPtr<guest::RuntimeRelocatableModuleNode>{head});
    created.set_previous(GuestPtr<guest::RuntimeRelocatableModuleNode>{0});
    if (head != 0) {
        module_at(m_, head).set_previous(GuestPtr<guest::RuntimeRelocatableModuleNode>{node});
    }
    set_global_word(m_, kModuleList, node);
    created.set_name_hash(hash);
    created.set_vtable(GuestPtr<void>{kModuleVtable});
    std::string path;
    for (Word at = name; byte_at(m_, at) != 0; ++at) {
        path.push_back(static_cast<char>(byte_at(m_, at)));
    }
    set_string(kLocalText, path + ".bin");
    created.set_entry(GuestPtr<void>{engine_value(kFileOpenLoad, {kLocalText, argument}, path + ".bin")});
    set_string(kLocalText, path + ".rel");
    const Word table = engine_value(kFileOpenLoad, {kLocalText, argument}, path + ".rel");
    apply_relocations(table, created.entry().address);
    mem_delete(table);
    engine(created.entry().address, {node});
}

// 0x004ac2e0: the table is a list of words (offset | kind) ended by -1; kind 0 adds the base to a word, 1 sets the low half of a word to the high half of (the next table word + 0x8000 + base), 2 adds
// the base to the low half of a word, 3 adds it to a jump target
void CoreKit::apply_relocations(Word table, Word base) {
    Word word = word_at(m_, table);
    while (word != 0xffffffffu) {
        Word next = table + 4;
        const Word target = (word & 0xfffffffcu) + base;
        switch (word & 3) {
        case 0:
            set_word_at(m_, target, word_at(m_, target) + base);
            break;
        case 1: {
            const Word value = word_at(m_, next);
            next = table + 8;
            set_word_at(m_, target, ((value + 0x8000 + base) >> 16) | (word_at(m_, target) & 0xffff0000u));
            break;
        }
        case 2: {
            const Word old = word_at(m_, target);
            set_word_at(m_, target, (((old + base) ^ old) & 0xffff) ^ old);
            break;
        }
        default: {
            const Word old = word_at(m_, target);
            set_word_at(m_, target, ((base + old * 4) >> 2 & 0x3ffffff) | (old & 0xfc000000u));
            break;
        }
        }
        word = word_at(m_, next);
        table = next;
    }
}

// ---- the run-length wrappers -------------------------------------------------------------------------------------------------------------------------------------------------

// 0x004e9150 / 0x004e9200: runs of equal bytes as (count, byte) pairs, the count one byte (BIBD) or two (WIBD) wide; the result is copied into a block of its exact size
Word CoreKit::rle_compress(Word source, std::int32_t length, Word out_length, unsigned width) {
    const Word limit = width == 1 ? 0xffu : 0xffffu;
    const Word buffer = engine_value(kMalloc, {static_cast<Word>(length) * (width + 1)});
    if (buffer == 0) {
        return 0;
    }
    auto store = [&](Word at, Word value) {
        if (width == 1) {
            set_byte_at(m_, at, static_cast<std::uint8_t>(value));
        } else {
            write_scalar<std::uint16_t>(m_, at, static_cast<std::uint16_t>(value));
        }
    };
    Word count = 1;
    std::uint8_t current = byte_at(m_, source);
    Word pos = 0;
    for (std::int32_t i = 1; i < length; ++i) {
        const std::uint8_t byte = byte_at(m_, source + static_cast<Word>(i));
        if (byte == current) {
            ++count;
            if (count > limit) {
                store(buffer + pos, limit);
                count = 1;
                set_byte_at(m_, buffer + pos + width, current);
                pos += width + 1;
            }
        } else {
            store(buffer + pos, count);
            count = 1;
            set_byte_at(m_, buffer + pos + width, current);
            current = byte;
            pos += width + 1;
        }
    }
    store(buffer + pos, count);
    set_byte_at(m_, buffer + pos + width, current);
    const Word size = pos + width + 1;
    if (out_length != 0) {
        set_word_at(m_, out_length, size);
    }
    const Word block = engine_value(kMalloc, {size});
    if (block != 0) {
        m_.copy(block, buffer, size);
    }
    engine(kFree, {buffer});
    return block;
}

Word CoreKit::compress_bibd(Word source, std::int32_t length, Word out_length) { return rle_compress(source, length, out_length, 1); }
Word CoreKit::compress_wibd(Word source, std::int32_t length, Word out_length) { return rle_compress(source, length, out_length, 2); }

// 0x004e9390 / 0x004e9480
Word CoreKit::rle_decompress(Word source, std::int32_t length, Word size, Word name, unsigned width) {
    engine(kSprintf, {kLocalText, kUncompressedFormat, name});
    const Word out = engine_value(kMalloc, {size});
    if (out == 0 || length <= 0) {
        return out;
    }
    Word at = source + width;
    Word pos = 0;
    for (;;) {
        const std::uint8_t value = byte_at(m_, at);
        const Word count = width == 1 ? byte_at(m_, at - 1) : read_scalar<std::uint16_t>(m_, at - 2);
        const Word end = count + pos;
        if (static_cast<std::int32_t>(size) < static_cast<std::int32_t>(end)) {
            fill_bytes(out + pos, size - pos, value);
            return out;
        }
        at += width + 1;
        fill_bytes(out + pos, count, value);
        pos = end;
        if (length <= static_cast<std::int32_t>(at - width - source)) {
            return out;
        }
    }
}

Word CoreKit::decompress_bibd(Word source, std::int32_t length, Word size, Word name) { return rle_decompress(source, length, size, name, 1); }
Word CoreKit::decompress_wibd(Word source, std::int32_t length, Word size, Word name) { return rle_decompress(source, length, size, name, 2); }

// ---- the zlib wrappers ----------------------------------------------------------------------------------------------------------------------------------------------------------

// 0x004e92c0: a buffer of 1.5 times the source (signed), zlib's compress into it (the service writes the used length through the pointer and the bytes into the buffer), then a block of exactly that length
Word CoreKit::compress_zlib(Word source, std::int32_t length, Word out_length) {
    const std::int32_t bound = (length * 3 - (length * 3 >> 31)) >> 1;       // (3 * length) / 2 toward zero, as the compiler spells it
    set_word_at(m_, kLocalA, static_cast<Word>(bound));
    const Word buffer = engine_value(kMalloc, {static_cast<Word>(bound)});
    engine(kZlibCompress, {buffer, kLocalA, source, static_cast<Word>(length)});
    const std::string service = pcplatform::engine_name(kZlibCompress);
    const Word used = s_.live() ? word_at(m_, kLocalA) : static_cast<Word>(s_.value(service));   // (live, the service stored the used length itself)
    set_word_at(m_, kLocalA, used);
    take_filled(buffer, used, service);
    const std::int32_t status = s_.value(service);
    Word block = 0;
    if (status == -5) {
        engine(kLogError, {kMsgZlibSmall});
    } else if (status == -4) {
        engine(kLogError, {kMsgZlibMemory});
    } else if (status != 0) {
        engine(kLogError, {kMsgZlibUnexpected, static_cast<Word>(status)});
        engine(kFree, {buffer});
        return 0;
    } else {
        block = engine_value(kMalloc, {word_at(m_, kLocalA)});
        if (block != 0) {
            m_.copy(block, buffer, word_at(m_, kLocalA));
        }
        if (out_length != 0) {
            set_word_at(m_, out_length, word_at(m_, kLocalA));
            engine(kFree, {buffer});
            return block;
        }
    }
    engine(kFree, {buffer});
    return block;
}

// 0x004e9570
Word CoreKit::decompress_zlib(Word source, Word length, Word size, Word name) {
    engine(kSprintf, {kLocalText, kUncompressedFormat, name});
    set_word_at(m_, kLocalA, size);
    const Word out = engine_value(kMalloc, {size});
    if (out == 0) {
        return 0;
    }
    engine(kZlibUncompress, {out, kLocalA, source, length});
    const std::string service = pcplatform::engine_name(kZlibUncompress);
    const Word used = s_.live() ? word_at(m_, kLocalA) : static_cast<Word>(s_.value(service));
    set_word_at(m_, kLocalA, used);
    take_filled(out, used, service);
    const std::int32_t status = s_.value(service);
    if (status == 0) {
        return out;
    }
    engine(kFree, {out});
    if (status == -5) {
        engine(kLogError, {kMsgUnzlibSmall});
    } else if (status == -4) {
        engine(kLogError, {kMsgUnzlibMemory});
    } else if (status == -3) {
        engine(kLogError, {kMsgUnzlibData});
    } else {
        engine(kLogError, {kMsgUnzlibUnexpected, static_cast<Word>(status)});
    }
    return 0;
}

// the wrapper a method number names: the port's own when the table holds one of ours, else a service
Word CoreKit::call_wrapper(Word target, Word a, Word b, Word c) {
    switch (target) {
    case kWrapperBibd:
        return compress_bibd(a, static_cast<std::int32_t>(b), c);
    case kWrapperWibd:
        return compress_wibd(a, static_cast<std::int32_t>(b), c);
    case kWrapperZlib:
        return compress_zlib(a, static_cast<std::int32_t>(b), c);
    default:
        return engine_value(target, {a, b, c});
    }
}

// 0x004e96b0: compress a resource the way its entry says (method at +0x20, -2 for none, the size at +0x28, the compressed size written to +0x2c)
Word CoreKit::compress_resource(Word entry, Word data, Word release) {
    const RuntimePkrFileRecordView record(m_, entry);       // (the method is the record's payload marker word)
    if (record.payload_marker() < -2) {
        engine(kLogError, {kMsgBadMethod});
        record.set_payload_marker(-2);
    }
    const std::int32_t method = record.payload_marker();
    if (method == -2) {
        record.set_payload_size_duplicate(record.payload_size());
        return data;
    }
    const Word target = word_at(m_, kCompressTable + static_cast<Word>(method) * 4);
    const Word result = call_wrapper(target, data, record.payload_size(), record.payload_size_duplicate_address());
    if (result == 0) {
        const Word size = record.payload_size();
        record.set_payload_marker(-2);
        record.set_payload_size_duplicate(size);
        return data;
    }
    if (release != 0) {
        engine(kFree, {data});
    }
    return result;
}

// 0x004e9630: the size a method compresses `source` to (the length for "none", -1 for a method below that); the compressed block is released
std::int32_t CoreKit::compressed_size(Word source, std::int32_t length, std::int32_t method) {
    if (method < -2) {
        return -1;
    }
    if (method == -2) {
        return length;
    }
    const Word slot = pcplatform::kScratchBase + 0x70;
    set_word_at(m_, slot, static_cast<Word>(length));
    const Word block = call_wrapper(word_at(m_, kCompressTable + static_cast<Word>(method) * 4), source, static_cast<Word>(length), slot);
    engine(kFree, {block});
    return static_cast<std::int32_t>(word_at(m_, slot));
}

// 0x004e9670: the method (0..2) that compresses to the fewest bytes, else -2; the smallest size is written to `size_out` when it is given
std::int32_t CoreKit::best_method(Word source, std::int32_t length, Word size_out) {
    std::int32_t best = -2;
    std::int32_t smallest = length;
    for (std::int32_t method = 0; method < 3; ++method) {
        const std::int32_t size = compressed_size(source, length, method);
        if (size < smallest) {
            smallest = size;
            best = method;
        }
    }
    if (size_out != 0) {
        set_word_at(m_, size_out, static_cast<Word>(smallest));
    }
    return best;
}

// ---- the package header ----------------------------------------------------------------------------------------------------------------------------------------------------------

// 0x004e9780: load a package's tables unless it is the one already loaded
Word CoreKit::pkr_preload(Word name) {
    if (compare_strings(kCurrentPackage, kEmptyName) != 0) {
        if (compare_strings(kCurrentPackage, name) == 0) {
            return 1;
        }
        pkr_unload(kCurrentPackage);
    }
    copy_string(kCurrentPackage, name);
    const Word stream = engine_value(kOpenPackageStream, {name, kPackageMode});
    if (stream != 0) {
        read_resource(kLocalHeader, 0x10, 1, stream);
        const RuntimePkrPackageHeaderView header(m_, kLocalHeader);
        if (header.signature() == 0x32524b50u) {
            const Word entries = header.entry_count();
            const Word files = header.file_count();
            set_global_word(m_, kPackageEntries, engine_value(kOperatorNew, {entries * 0x28}));
            read_resource(global_word(m_, kPackageEntries), 0x28, entries, stream);
            set_global_word(m_, kPackageFiles, engine_value(kOperatorNew, {files * 0x30}));
            read_resource(global_word(m_, kPackageFiles), 0x30, files, stream);
            engine(kStreamClose, {stream});
            return 1;
        }
        set_global_word(m_, kPackageEntries, 0);
        set_global_word(m_, kPackageFiles, 0);
        engine(kStreamClose, {stream});
        return 0;
    }
    set_global_word(m_, kPackageEntries, 0);
    set_global_word(m_, kPackageFiles, 0);
    return 0;
}

// 0x004e9b80
void CoreKit::pkr_unload(Word name) {
    (void)compare_strings(kCurrentPackage, name);          // (a mismatch only logs, through an empty function)
    copy_string(kCurrentPackage, kEmptyName);
    engine(kOperatorDelete, {global_word(m_, kPackageEntries)});
    engine(kOperatorDelete, {global_word(m_, kPackageFiles)});
    set_global_word(m_, kPackageEntries, 0);
    set_global_word(m_, kPackageFiles, 0);
}

// ---- the stream object ---------------------------------------------------------------------------------------------------------------------------------------------------------

Word CoreKit::stream_construct(Word self) {
    const RuntimeBufferedStreamObjectView stream(m_, self);
    stream.set_vtable(GuestPtr<void>{0x00519a18});
    stream.set_flag_245c(0);
    stream.set_flag_245d(0);
    stream.set_position_marker(-1);
    return self;
}

void CoreKit::stream_destruct(Word self) {
    const RuntimeBufferedStreamObjectView stream(m_, self);
    stream.set_vtable(GuestPtr<void>{0x00519a18});
    if (stream.open_state() != 0) {
        engine(seam::hal::FUN_004ebfe0, {self});
    }
}

Word CoreKit::stream_scalar_delete(Word self, Word flags) {
    stream_destruct(self);
    if ((flags & 1) != 0) {
        engine(kOperatorDelete, {self});
    }
    return self;
}

// ---- exp.cpp and the head file -----------------------------------------------------------------------------------------------------------------------------------------------

// 0x00448860: an item of the environment is hit. A listed item is looked up (the walk needs it to be in the list); its model description says whether it shatters as a whole
void CoreKit::exp_hit_env_item(Word object, Word face, Word whole) {
    if (object == 0) {
        return;
    }
    Word at = global_word(m_, globals::kEnvironmentList);
    if (at == 0) {
        return;
    }
    while (at != object) {
        at = next_object(m_, at);
        if (at == 0) {
            return;
        }
    }
    const RuntimeListObjectView item = object_at(m_, object);
    const Word region = item.region();
    check(region_slot(m_, region).region_type() != 0, 0x0052b9b4);
    const Word table = region_slot(m_, region).model_pointers().address;
    const Word description = word_at(m_, table + item.model_index() * 4);       // (a table with no fixed length)
    const std::uint16_t flags = RuntimePsxModelPacketHeaderView(m_, description).model_flags();
    if ((flags & 2) != 0) {
        // (the flag byte is read as `ax >> 24` of a 16-bit register: always 0, so only a whole-item hit does anything)
        if (whole == 0xffff) {
            engine(0x004af0b0, {object, 1, 1});
        }
        return;
    }
    if (face != 0) {
        engine(0x004ae090, {object, face, 1, 1, 1, 1, 1});
    }
}

// 0x00448990: the explosion object: a vector-and-colour burst of `count` entries (at +0x40 / +0x44), all starting at the position
Word CoreKit::exp_construct(Word self, Word position, Word start, Word red, Word green, Word blue, Word base_argument, Word extra) {
    engine(0x0040a740, {self, base_argument});
    const RuntimeExplosionObjectView burst(m_, self);
    const RuntimeIntVector3View from(m_, position);
    burst.set_vtable(GuestPtr<void>{0x005189ec});
    for (Word k = 0; k < 3; ++k) {
        set_component(burst.position(), k, component(from, k));
    }
    burst.set_extra(extra);
    burst.set_start(start);
    for (Word k = 0; k < 3; ++k) {
        set_component(burst.origin(), k, component(from, k));
    }
    burst.set_green(static_cast<std::uint8_t>(green));
    burst.set_red(static_cast<std::uint8_t>(red));
    burst.set_blue(static_cast<std::uint8_t>(blue));
    const std::int32_t count = burst.particle_count();
    for (std::int32_t i = 0; i < count; ++i) {
        const Word entries = burst.particles().address;
        const RuntimeExplosionParticleView particle(m_, entries + 16 * static_cast<Word>(i));
        particle.set_red(static_cast<std::uint8_t>(red));
        particle.set_green(static_cast<std::uint8_t>(green));
        particle.set_blue(static_cast<std::uint8_t>(blue));
        for (Word k = 0; k < 3; ++k) {
            set_component(particle.position(), k, component(from, k));
        }
    }
    burst.set_random_phase(static_cast<Word>(rnd(0x1000)));
    engine(0x0040a900, {self});
    return self;
}

void CoreKit::exp_destruct(Word self) {
    RuntimeExplosionObjectView(m_, self).set_vtable(GuestPtr<void>{0x005189ec});
    engine(0x0040a880, {self});
}

Word CoreKit::exp_scalar_delete(Word self, Word flags) {
    exp_destruct(self);
    if ((flags & 1) != 0) {
        engine(0x00403dc0, {self});
    }
    return self;
}

// 0x00448ad0 Exp_Move: the age (+0x5c) grows by its step (+0x60); at the limit (+0x58) it is held there and the object dies (CBit_Die). The burst's end point (+0x48, +0x50) is the centre (+0x10, +0x18)
// moved by the age along the phase's cosine and sine (rounded towards zero to whole 0x1000 units); each of the `count` entries (+0x40; 16 bytes each at +0x44) gets the colour (+0x54..+0x56)
// faded by (limit - age) / limit, and its point is the same circle at the phase advanced by 0x1000 / count a step; the last entry is the end point itself.
void CoreKit::exp_move(Word self) {
    const auto s = [](Word v) { return static_cast<std::int32_t>(v); };
    const auto round_units = [](Word product) { return static_cast<Word>(static_cast<std::int32_t>(product + (static_cast<std::int32_t>(product) < 0 ? 0xfffu : 0u)) >> 12) << 12; };
    const Word age = word_at(m_, self + 0x5c) + word_at(m_, self + 0x60);
    set_word_at(m_, self + 0x5c, age);
    if (s(age) >= s(word_at(m_, self + 0x58))) {
        set_word_at(m_, self + 0x5c, word_at(m_, self + 0x58));
        engine(0x00403e40, {self});
    }
    const std::int32_t limit = s(word_at(m_, self + 0x58));
    const std::int32_t now = s(word_at(m_, self + 0x5c));
    Word phase = word_at(m_, self + 0x64);
    const std::int32_t count = s(word_at(m_, self + 0x40));
    if (limit == 0 || count == 0) {
        throw std::runtime_error("division fault in Exp_Move");   // (retail faults on a zero divisor)
    }
    const std::int32_t fade = s(static_cast<Word>(limit - now) << 8) / limit;
    const std::int32_t step = 0x1000 / count;
    const auto circle_x = [&](Word at) { return round_units(static_cast<Word>(math_.cos_q12(s(at))) * static_cast<Word>(now)) + word_at(m_, self + 0x10); };
    const auto circle_z = [&](Word at) { return round_units(static_cast<Word>(math_.sin_q12(s(at))) * static_cast<Word>(now)) + word_at(m_, self + 0x18); };
    set_word_at(m_, self + 0x48, circle_x(phase));
    set_word_at(m_, self + 0x50, circle_z(phase));
    for (std::int32_t i = 0; i < count; ++i) {
        const Word entry = word_at(m_, self + 0x44) + 16 * static_cast<Word>(i);
        for (Word k = 0; k < 3; ++k) {
            set_byte_at(m_, entry + 0xc + k, static_cast<std::uint8_t>((static_cast<std::int32_t>(byte_at(m_, self + 0x54 + k)) * fade) >> 8));
        }
        phase += static_cast<Word>(step);
        if (i == count - 1) {
            for (Word k = 0; k < 3; ++k) {
                set_word_at(m_, entry + 4 * k, word_at(m_, self + 0x48 + 4 * k));
            }
        } else {
            set_word_at(m_, entry, circle_x(phase));
            set_word_at(m_, entry + 8, circle_z(phase));
        }
    }
}

// 0x00448c60 / 0x00448c90: the file directory block (the size of the file head, rounded up to 0x800)
void CoreKit::load_head() {
    if (global_word(m_, 0x005615d8) == 0) {
        const Word size = (global_word(m_, 0x0056152c) + 0x7ff) >> 11 << 11;
        set_global_word(m_, 0x005615d8, alloc(size, 1, 1, 0));
    }
}

void CoreKit::unload_head() {
    if (global_word(m_, 0x005615d8) != 0) {
        mem_delete(global_word(m_, 0x005615d8));
        set_global_word(m_, 0x005615d8, 0);
    }
}

}  // namespace opentony::runtime::core
