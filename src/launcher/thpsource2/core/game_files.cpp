#include "game_files.hpp"

#include "ini.hpp"
#include "iso9660.hpp"

#include <array>
#include <cstring>
#include <fstream>
#include <memory>
#include <system_error>

namespace thpsource2 {

namespace fs = std::filesystem;

// ---------------------------------------------------------------- SHA-256 (FIPS 180-4)

namespace {

class Sha256 {
public:
    void update(const std::uint8_t* data, std::size_t size) {
        length_ += size;
        while (size > 0) {
            const std::size_t take = std::min<std::size_t>(64 - used_, size);
            std::memcpy(block_ + used_, data, take);
            used_ += take;
            data += take;
            size -= take;
            if (used_ == 64) {
                compress(block_);
                used_ = 0;
            }
        }
    }
    std::string hex() {
        const std::uint64_t bits = length_ * 8;
        const std::uint8_t one = 0x80;
        update(&one, 1);
        const std::uint8_t zero = 0;
        while (used_ != 56) {
            update(&zero, 1);
        }
        std::uint8_t tail[8];
        for (int k = 0; k < 8; ++k) {
            tail[k] = static_cast<std::uint8_t>(bits >> (56 - 8 * k));
        }
        update(tail, 8);
        static const char* const digits = "0123456789abcdef";
        std::string out;
        for (std::uint32_t word : state_) {
            for (int shift = 28; shift >= 0; shift -= 4) {
                out.push_back(digits[(word >> shift) & 0xf]);
            }
        }
        return out;
    }

private:
    static std::uint32_t rotr(std::uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
    void compress(const std::uint8_t* p) {
        static const std::uint32_t k[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
            0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
            0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
            0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
            0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
            0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
        std::uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<std::uint32_t>(p[4 * i]) << 24) | (p[4 * i + 1] << 16) | (p[4 * i + 2] << 8) | p[4 * i + 3];
        }
        for (int i = 16; i < 64; ++i) {
            const std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        std::uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3], e = state_[4], f = state_[5], g = state_[6], h = state_[7];
        for (int i = 0; i < 64; ++i) {
            const std::uint32_t t1 = h + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + k[i] + w[i];
            const std::uint32_t t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
            h = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }
        state_[0] += a;
        state_[1] += b;
        state_[2] += c;
        state_[3] += d;
        state_[4] += e;
        state_[5] += f;
        state_[6] += g;
        state_[7] += h;
    }
    std::uint32_t state_[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    std::uint8_t block_[64] = {};
    std::size_t used_ = 0;
    std::uint64_t length_ = 0;
};

}  // namespace

std::string sha256_bytes(const void* data, std::size_t size) {
    Sha256 hash;
    hash.update(static_cast<const std::uint8_t*>(data), size);
    return hash.hex();
}

std::string sha256_file(const fs::path& path, const std::function<bool(std::uint64_t)>& progress) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    Sha256 hash;
    std::vector<std::uint8_t> buffer(1 << 20);
    while (in) {
        in.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
        const auto got = static_cast<std::size_t>(in.gcount());
        if (got == 0) {
            break;
        }
        hash.update(buffer.data(), got);
        if (progress && !progress(got)) {
            return {};
        }
    }
    return hash.hex();
}

// ---------------------------------------------------------------- the game folder

fs::path find_child(const fs::path& directory, std::string_view name) {
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(directory, ec)) {
        if (equal_nocase(entry.path().filename().string(), name)) {
            return entry.path();
        }
    }
    return {};
}

GameBuild build_of_sha256(const std::string& sha256) {
    if (sha256 == kCanonicalSha256) {
        return GameBuild::ValueRerelease;
    }
    if (sha256 == kOriginal2000Sha256) {
        return GameBuild::Original2000;
    }
    if (sha256 == kProtectedDiscSha256) {
        return GameBuild::Original2000Protected;
    }
    return GameBuild::Unknown;
}

bool build_supported(GameBuild build) { return build == GameBuild::ValueRerelease || build == GameBuild::Original2000; }

std::string build_problem(GameBuild build, const std::string& sha256) {
    switch (build) {
    case GameBuild::ValueRerelease:
    case GameBuild::Original2000:
        return {};
    case GameBuild::Original2000Protected:
        return "THawk2.exe is the copy-protected one of the original 2000 disc (SafeDisc 2: its data is encrypted). Put the original release's unprotected "
               "THawk2.exe in its place, or use the Activision Value re-release's";
    case GameBuild::Unknown:
        break;
    }
    return "THawk2.exe (sha256 " + sha256.substr(0, 16) + "...) is neither the Activision Value re-release's nor the original 2000 release's unprotected one";
}

std::string GameCheck::summary() const {
    if (pkr.empty()) {
        return "ALL.PKR is missing";
    }
    if (exe.empty()) {
        return prepared ? "ready (THawk2.exe is gone, but its data was already read)" : "THawk2.exe is missing";
    }
    if (!exe_supported()) {
        return build_problem(build, exe_sha256);
    }
    const std::string release = build == GameBuild::Original2000 ? " (original 2000 release)" : "";
    return prepared ? "ready" + release : "found" + release + "; the game data is unpacked on the first run (about 200 MB, once)";
}

GameCheck check_game_folder(const fs::path& folder, const fs::path& unpacked, bool hash) {
    GameCheck check;
    std::error_code ec;
    if (!fs::is_directory(folder, ec)) {
        return check;
    }
    check.exe = find_child(folder, "THawk2.exe");
    check.pkr = find_child(folder, "ALL.PKR");
    if (!check.exe.empty()) {
        if (hash) {
            check.exe_sha256 = sha256_file(check.exe);
            check.build = build_of_sha256(check.exe_sha256);
        } else {
            check.build = GameBuild::ValueRerelease;
        }
    }
    check.prepared = fs::is_regular_file(unpacked / "thawk2_static.img", ec) && fs::is_directory(unpacked / "data", ec);
    const fs::path music = find_child(folder, "Music");
    check.music = !music.empty() && fs::is_directory(music, ec);
    const fs::path med = find_child(folder, "med");
    check.movies = !med.empty() && !find_child(med, "Movies").empty();
    return check;
}

// ---------------------------------------------------------------- import

std::uint64_t ImportPlan::total_bytes() const {
    std::uint64_t total = 0;
    for (const ImportItem& item : items) {
        total += item.size;
    }
    return total;
}

namespace {

// a relative path of `root`, '/' separated, found whatever the case of each part
fs::path find_path(const fs::path& root, std::string_view relative) {
    fs::path current = root;
    std::size_t start = 0;
    while (start <= relative.size()) {
        std::size_t end = relative.find('/', start);
        if (end == std::string_view::npos) {
            end = relative.size();
        }
        const std::string_view part = relative.substr(start, end - start);
        start = end + 1;
        if (part.empty()) {
            if (end == relative.size()) {
                break;
            }
            continue;
        }
        current = find_child(current, part);
        if (current.empty()) {
            return {};
        }
    }
    return current;
}

void add_folder_tree(const fs::path& directory, const std::string& source_prefix, const std::string& target_prefix, ImportPlan& plan) {
    std::error_code ec;
    for (const auto& entry : fs::recursive_directory_iterator(directory, ec)) {
        if (!entry.is_regular_file(ec)) {
            continue;
        }
        const std::string relative = fs::relative(entry.path(), directory, ec).generic_string();
        plan.items.push_back({source_prefix + "/" + relative, target_prefix + "/" + relative, entry.file_size(ec), false});
    }
}

void add_image_tree(DiscImage& image, const DiscImage::Entry& directory, const std::string& source_prefix, const std::string& target_prefix, ImportPlan& plan,
                    int depth = 0) {
    if (depth > 8) {
        return;
    }
    for (const DiscImage::Entry& entry : image.list(directory)) {
        const std::string source = source_prefix + "/" + entry.name;
        const std::string target = target_prefix + "/" + entry.name;
        if (entry.directory) {
            add_image_tree(image, entry, source, target, plan, depth + 1);
        } else {
            plan.items.push_back({source, target, entry.size, false});
        }
    }
}

}  // namespace

ImportPlan plan_import(const fs::path& source) {
    ImportPlan plan;
    plan.source = source;
    std::error_code ec;
    if (fs::is_directory(source, ec)) {
        // an installed game: THawk2.exe and ALL.PKR side by side; the disc (or a copy of it): SETUP\data and SETUP\med
        std::string data, med;
        if (!find_child(source, "ALL.PKR").empty()) {
            data = "";
            med = "med";
        } else if (!find_path(source, "SETUP/data/ALL.PKR").empty()) {
            data = "SETUP/data";
            med = "SETUP/med";
        } else if (!find_path(source, "data/ALL.PKR").empty()) {   // the SETUP folder itself was picked
            data = "data";
            med = "med";
        } else {
            plan.error = "no ALL.PKR in " + source.string() + " (nor in its SETUP\\data folder)";
            return plan;
        }
        const std::string prefix = data.empty() ? "" : data + "/";
        for (const char* name : {"THawk2.exe", "ALL.PKR"}) {
            const fs::path file = find_path(source, prefix + name);
            if (file.empty()) {
                plan.error = std::string(name) + " is missing from " + (source / data).string();
                return plan;
            }
            plan.items.push_back({fs::relative(file, source, ec).generic_string(), name, fs::file_size(file, ec), true});
        }
        if (const fs::path music = find_path(source, prefix + "Music"); !music.empty()) {
            add_folder_tree(music, fs::relative(music, source, ec).generic_string(), "Music", plan);
        }
        if (const fs::path movies = find_path(source, med + "/Movies"); !movies.empty()) {
            add_folder_tree(movies,fs::relative(movies, source, ec).generic_string(), "med/Movies", plan);
        }
        return plan;
    }
    if (!fs::is_regular_file(source, ec)) {
        plan.error = source.string() + " does not exist";
        return plan;
    }
    plan.from_image = true;
    std::string error;
    auto image = DiscImage::open(source, error);
    if (!image) {
        plan.error = error;
        return plan;
    }
    for (const char* name : {"THawk2.exe", "ALL.PKR"}) {
        const auto file = image->find(std::string("SETUP/data/") + name);
        if (!file || file->directory) {
            plan.error = std::string("the disc image has no SETUP\\data\\") + name + " (is it a Tony Hawk's Pro Skater 2 PC disc?)";
            return plan;
        }
        plan.items.push_back({std::string("SETUP/data/") + file->name, name, file->size, true});
    }
    if (const auto music = image->find("SETUP/data/Music"); music && music->directory) {
        add_image_tree(*image, *music, "SETUP/data/" + music->name, "Music", plan);
    }
    if (const auto movies = image->find("SETUP/med/Movies"); movies && movies->directory) {
        add_image_tree(*image, *movies, "SETUP/med/Movies", "med/Movies", plan);
    }
    return plan;
}

bool run_import(const ImportPlan& plan, const fs::path& destination, ImportProgress& progress, std::string& error) {
    progress.total = plan.total_bytes();
    progress.done = 0;
    std::error_code ec;
    fs::create_directories(destination, ec);
    std::unique_ptr<DiscImage> image;
    if (plan.from_image) {
        image = DiscImage::open(plan.source, error);
        if (!image) {
            return false;
        }
    }
    std::string deferred;   // a refusal reported after the rest is copied
    const auto keep_going = [&progress](std::uint64_t bytes) {
        progress.done += bytes;
        return !progress.cancel.load();
    };
    for (const ImportItem& item : plan.items) {
        const fs::path target = destination / fs::path(item.target);
        if (fs::is_regular_file(target, ec) && fs::file_size(target, ec) == item.size) {
            progress.done += item.size;   // already there (an earlier import, or the same folder)
        } else if (plan.from_image) {
            const auto entry = image->find(item.source);
            if (!entry || !image->extract(*entry, target, keep_going)) {
                error = progress.cancel ? "cancelled" : "cannot copy " + item.source + " out of the disc image";
                return false;
            }
        } else if (const fs::path from = plan.source / fs::path(item.source); fs::equivalent(from, target, ec)) {
            progress.done += item.size;   // the game folder is the source itself
        } else {
            fs::create_directories(target.parent_path(), ec);
            fs::path partial = target;
            partial += ".part";
            std::ifstream in(from, std::ios::binary);
            std::ofstream out(partial, std::ios::binary | std::ios::trunc);
            std::vector<char> buffer(1 << 20);
            while (in && out) {
                in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
                const auto got = in.gcount();
                if (got <= 0) {
                    break;
                }
                out.write(buffer.data(), got);
                if (!keep_going(static_cast<std::uint64_t>(got))) {
                    break;
                }
            }
            out.close();
            if (!out || progress.cancel || fs::file_size(partial, ec) != item.size) {
                fs::remove(partial, ec);
                error = progress.cancel ? "cancelled" : "cannot copy " + from.string() + " to " + target.string();
                return false;
            }
            fs::rename(partial, target, ec);
            if (ec) {
                error = "cannot write " + target.string() + ": " + ec.message();
                return false;
            }
        }
        if (item.target == "THawk2.exe") {
            const std::string found = sha256_file(target);
            const GameBuild build = build_of_sha256(found);
            if (build == GameBuild::Original2000Protected) {
                // the original 2000 disc: everything else on it is usable, so the copy goes on; only the executable has to be replaced
                deferred = build_problem(build, found) + " (in " + destination.string() + ").";
            } else if (!build_supported(build)) {
                error = build_problem(build, found) + ".";
                return false;
            }
        }
    }
    if (!deferred.empty()) {
        error = deferred;
        return false;
    }
    return true;
}

}  // namespace thpsource2
