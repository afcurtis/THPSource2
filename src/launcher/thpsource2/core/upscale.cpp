#include "upscale.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <set>
#include <sstream>
#include <system_error>

namespace fs = std::filesystem;

namespace thpsource2::upscale {
namespace {

std::uint8_t clamp_byte(int value) { return static_cast<std::uint8_t>(std::clamp(value, 0, 255)); }

int wrap_index(int i, int n) { return ((i % n) + n) % n; }

bool is_key(const std::string& text) {
    return text.size() == 16 && std::all_of(text.begin(), text.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}

std::vector<std::string> split_tabs(const std::string& line) {
    std::vector<std::string> fields;
    std::string field;
    std::istringstream in(line);
    while (std::getline(in, field, '\t')) {
        fields.push_back(field);
    }
    return fields;
}

std::string hex16(std::uint64_t value) {
    char text[17];
    std::snprintf(text, sizeof text, "%016llx", static_cast<unsigned long long>(value));
    return text;
}

double catmull_rom(double t, int tap) {   // the weight of tap -1, 0, 1, 2 at fraction t
    const double t2 = t * t, t3 = t2 * t;
    switch (tap) {
        case -1: return (-t3 + 2.0 * t2 - t) * 0.5;
        case 0: return (3.0 * t3 - 5.0 * t2 + 2.0) * 0.5;
        case 1: return (-3.0 * t3 + 4.0 * t2 + t) * 0.5;
        default: return (t3 - t2) * 0.5;
    }
}

}  // namespace

bool has_transparency(const Image& image) {
    for (std::size_t k = 3; k < image.rgba.size(); k += 4) {
        if (image.rgba[k] != 255) {
            return true;
        }
    }
    return false;
}

Image bleed(const Image& image) {
    Image out = image;
    const int w = image.width, h = image.height;
    std::vector<std::uint8_t> filled(static_cast<std::size_t>(w) * static_cast<std::size_t>(h));
    std::size_t missing = 0;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const bool has = image.at(x, y)[3] != 0;
            filled[static_cast<std::size_t>(y) * w + x] = has ? 1 : 0;
            missing += has ? 0u : 1u;
        }
    }
    if (missing == filled.size()) {
        return out;   // nothing to spread from
    }
    std::vector<std::uint8_t> next = filled;
    while (missing != 0) {
        std::size_t changed = 0;
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                if (filled[static_cast<std::size_t>(y) * w + x]) {
                    continue;
                }
                int sum[3] = {0, 0, 0}, count = 0;
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int nx = x + dx, ny = y + dy;
                        if ((dx == 0 && dy == 0) || nx < 0 || ny < 0 || nx >= w || ny >= h || !filled[static_cast<std::size_t>(ny) * w + nx]) {
                            continue;
                        }
                        const std::uint8_t* p = out.at(nx, ny);
                        sum[0] += p[0];
                        sum[1] += p[1];
                        sum[2] += p[2];
                        ++count;
                    }
                }
                if (count != 0) {
                    std::uint8_t* p = out.at(x, y);
                    for (int c = 0; c < 3; ++c) {
                        p[c] = static_cast<std::uint8_t>((sum[c] + count / 2) / count);
                    }
                    next[static_cast<std::size_t>(y) * w + x] = 1;
                    ++changed;
                }
            }
        }
        if (changed == 0) {
            break;
        }
        missing -= changed;
        filled = next;
    }
    return out;
}

Image pad(const Image& image, int border, bool wrap) {
    Image out(image.width + 2 * border, image.height + 2 * border);
    for (int y = 0; y < out.height; ++y) {
        const int sy = wrap ? wrap_index(y - border, image.height) : std::clamp(y - border, 0, image.height - 1);
        for (int x = 0; x < out.width; ++x) {
            const int sx = wrap ? wrap_index(x - border, image.width) : std::clamp(x - border, 0, image.width - 1);
            std::copy_n(image.at(sx, sy), 4, out.at(x, y));
        }
    }
    return out;
}

Image crop(const Image& image, int border) {
    Image out(image.width - 2 * border, image.height - 2 * border);
    for (int y = 0; y < out.height; ++y) {
        std::copy_n(image.at(border, y + border), static_cast<std::size_t>(out.width) * 4u, out.at(0, y));
    }
    return out;
}

Image nearest(const Image& image, int scale) {
    Image out(image.width * scale, image.height * scale);
    for (int y = 0; y < out.height; ++y) {
        for (int x = 0; x < out.width; ++x) {
            std::copy_n(image.at(x / scale, y / scale), 4, out.at(x, y));
        }
    }
    return out;
}

Image bicubic(const Image& image, int scale) {
    Image out(image.width * scale, image.height * scale);
    for (int y = 0; y < out.height; ++y) {
        const double v = (y + 0.5) / scale - 0.5;
        const int y0 = static_cast<int>(std::floor(v));
        const double ty = v - y0;
        for (int x = 0; x < out.width; ++x) {
            const double u = (x + 0.5) / scale - 0.5;
            const int x0 = static_cast<int>(std::floor(u));
            const double tx = u - x0;
            double sum[4] = {0, 0, 0, 0};
            for (int j = -1; j <= 2; ++j) {
                const double wy = catmull_rom(ty, j);
                const int sy = std::clamp(y0 + j, 0, image.height - 1);
                for (int i = -1; i <= 2; ++i) {
                    const double weight = wy * catmull_rom(tx, i);
                    const std::uint8_t* p = image.at(std::clamp(x0 + i, 0, image.width - 1), sy);
                    for (int c = 0; c < 4; ++c) {
                        sum[c] += weight * p[c];
                    }
                }
            }
            std::uint8_t* d = out.at(x, y);
            for (int c = 0; c < 4; ++c) {
                d[c] = clamp_byte(static_cast<int>(std::lround(sum[c])));
            }
        }
    }
    return out;
}

Image box_down(const Image& image, int factor) {
    Image out(image.width / factor, image.height / factor);
    const int area = factor * factor;
    for (int y = 0; y < out.height; ++y) {
        for (int x = 0; x < out.width; ++x) {
            int sum[4] = {0, 0, 0, 0};
            for (int dy = 0; dy < factor; ++dy) {
                for (int dx = 0; dx < factor; ++dx) {
                    const std::uint8_t* p = image.at(x * factor + dx, y * factor + dy);
                    for (int c = 0; c < 4; ++c) {
                        sum[c] += p[c];
                    }
                }
            }
            std::uint8_t* d = out.at(x, y);
            for (int c = 0; c < 4; ++c) {
                d[c] = static_cast<std::uint8_t>((sum[c] + area / 2) / area);
            }
        }
    }
    return out;
}

void match_colour(Image& upscaled, const Image& original, int scale) {
    const int area = scale * scale;
    for (int pass = 0; pass < 2; ++pass) {
        for (int y = 0; y < original.height; ++y) {
            for (int x = 0; x < original.width; ++x) {
                int sum[3] = {0, 0, 0};
                for (int dy = 0; dy < scale; ++dy) {
                    for (int dx = 0; dx < scale; ++dx) {
                        const std::uint8_t* p = upscaled.at(x * scale + dx, y * scale + dy);
                        sum[0] += p[0];
                        sum[1] += p[1];
                        sum[2] += p[2];
                    }
                }
                const std::uint8_t* o = original.at(x, y);
                int shift[3];
                for (int c = 0; c < 3; ++c) {
                    const int mean_times_area = sum[c];
                    const int want = o[c] * area;
                    shift[c] = (want - mean_times_area) >= 0 ? (want - mean_times_area + area / 2) / area : -((mean_times_area - want + area / 2) / area);
                }
                if (shift[0] == 0 && shift[1] == 0 && shift[2] == 0) {
                    continue;
                }
                for (int dy = 0; dy < scale; ++dy) {
                    for (int dx = 0; dx < scale; ++dx) {
                        std::uint8_t* p = upscaled.at(x * scale + dx, y * scale + dy);
                        for (int c = 0; c < 3; ++c) {
                            p[c] = clamp_byte(p[c] + shift[c]);
                        }
                    }
                }
            }
        }
    }
}

Image alpha_as_grey(const Image& image) {
    Image out(image.width, image.height);
    for (std::size_t k = 0; k < image.rgba.size(); k += 4) {
        out.rgba[k] = out.rgba[k + 1] = out.rgba[k + 2] = image.rgba[k + 3];
        out.rgba[k + 3] = 255;
    }
    return out;
}

bool read_index(const fs::path& index, std::map<std::string, DumpRow>& rows, std::string& error) {
    std::ifstream in(index);
    if (!in) {
        error = "cannot open " + index.string();
        return false;
    }
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty() || line[0] == '#') {
            continue;
        }
        const std::vector<std::string> f = split_tabs(line);   // key width height format alpha use wrap mod2x volatile tag
        if (f.size() < 9 || !is_key(f[0])) {
            continue;
        }
        DumpRow row;
        row.width = std::atoi(f[1].c_str());
        row.height = std::atoi(f[2].c_str());
        row.alpha = f[4] == "cutout" ? Alpha::Cutout : f[4] == "smooth" ? Alpha::Smooth : Alpha::Opaque;
        row.wrap = f[6] == "wrap" ? Wrap::Wrap : f[6] == "clamp" ? Wrap::Clamp : f[6] == "both" ? Wrap::Both : Wrap::Unknown;
        row.is_volatile = f[8] == "1";
        rows[f[0]] = row;
    }
    return true;
}

bool read_classes(const fs::path& table, std::map<std::string, Class>& classes, std::uint64_t& table_hash, std::string& error) {
    std::ifstream in(table, std::ios::binary);
    if (!in) {
        error = "cannot open " + table.string();
        return false;
    }
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    table_hash = 0xcbf29ce484222325ull;
    for (const unsigned char c : text) {
        table_hash = (table_hash ^ c) * 0x100000001b3ull;
    }
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty() || line[0] == '#') {
            continue;
        }
        const std::vector<std::string> f = split_tabs(line);   // key width height class evidence census
        if (f.size() < 4 || !is_key(f[0]) || (f[3] != "world" && f[3] != "2d")) {
            continue;
        }
        classes[f[0]] = f[3] == "world" ? Class::World : Class::TwoD;
    }
    if (classes.empty()) {
        error = "no rows in " + table.string();
        return false;
    }
    return true;
}

std::string stamp_text(const Options& options, const std::string& tool, std::uint64_t table_hash) {
    std::ostringstream out;
    out << "pipeline " << kPipelineVersion << "\n"
        << "tool " << tool << "\n"
        << "world " << options.model_world << "\n"
        << "2d " << options.model_2d << "\n"
        << "scale " << options.scale << "\n"
        << "alpha " << (options.alpha == AlphaMethod::Model ? "model" : "bicubic") << "\n"
        << "border " << options.border << "\n"
        << "min-world " << options.min_world << "\n"
        << "table " << hex16(table_hash) << "\n";
    if (!options.originals.empty()) {
        out << "originals true-colour\n";
    }
    return out.str();
}

namespace {

struct Job {
    std::string key;
    DumpRow row;
    Class cls = Class::World;
};

// The original of a key: the true-colour picture when there is one of the dump's size, the dump's otherwise
bool read_original(const fs::path& dump, const Options& options, const std::string& key, Image& image, std::string& error) {
    Image dumped;
    if (!png::read(dump / (key + ".png"), dumped, error)) {
        return false;
    }
    std::error_code ec;
    if (!options.originals.empty() && fs::is_regular_file(options.originals / (key + ".png"), ec)) {
        Image colour;
        std::string ignored;
        if (png::read(options.originals / (key + ".png"), colour, ignored) && colour.width == dumped.width && colour.height == dumped.height) {
            image = std::move(colour);
            return true;
        }
    }
    image = std::move(dumped);
    return true;
}

struct Prepared {
    const Job* job = nullptr;
    Image original;
    Image bled;
    bool transparent = false;
    bool wrapped = true;
};

void fail(Report& report, const std::string& what) {
    ++report.failed;
    if (report.errors.size() < 20) {
        report.errors.push_back(what);
    }
}

void write_result(Report& report, const fs::path& path, const Image& image, bool alpha) {
    std::string error;
    if (!png::write(path, image, alpha, error)) {
        fail(report, path.filename().string() + ": " + error);
        return;
    }
    ++report.written;
    std::error_code ec;
    report.bytes += fs::file_size(path, ec);
}

// One batch of one class: the built-in nearest in process, a model through `model` over a work folder.
void run_batch(const std::vector<const Job*>& batch, const std::string& method, const fs::path& dump, const fs::path& out, const Options& options,
               const ModelRunner& model, Report& report, const std::atomic<bool>* cancel) {
    const int s = options.scale, b = options.border;
    if (method == kNearest) {
        for (const Job* job : batch) {
            if (cancel != nullptr && cancel->load()) {
                report.cancelled = true;
                return;
            }
            Image original;
            std::string error;
            if (!read_original(dump, options, job->key, original, error)) {
                fail(report, job->key + ": " + error);
                continue;
            }
            write_result(report, out / (job->key + ".png"), nearest(original, s), has_transparency(original));
        }
        return;
    }
    const fs::path work = out / ".work";
    std::error_code ec;
    fs::remove_all(work, ec);
    fs::create_directories(work / "in", ec);
    fs::create_directories(work / "out", ec);
    std::vector<Prepared> prepared;
    prepared.reserve(batch.size());
    for (const Job* job : batch) {
        Prepared p;
        p.job = job;
        std::string error;
        if (!read_original(dump, options, job->key, p.original, error)) {
            fail(report, job->key + ": " + error);
            continue;
        }
        p.transparent = has_transparency(p.original);
        p.bled = p.transparent ? bleed(p.original) : p.original;
        p.wrapped = job->row.wrap != Wrap::Clamp;
        if (!png::write(work / "in" / (job->key + ".png"), pad(p.bled, b, p.wrapped), false, error)) {
            fail(report, job->key + ": " + error);
            continue;
        }
        if (p.transparent && job->cls == Class::World && options.alpha == AlphaMethod::Model &&
            !png::write(work / "in" / (job->key + "_a.png"), alpha_as_grey(pad(p.original, b, p.wrapped)), false, error)) {
            fail(report, job->key + ": " + error);
            continue;
        }
        prepared.push_back(std::move(p));
    }
    if (prepared.empty()) {
        return;
    }
    std::string error;
    if (!model(method, work / "in", work / "out", error)) {
        if (cancel != nullptr && cancel->load()) {   // the runner stopped the tool for a cancel: nothing failed
            report.cancelled = true;
            return;
        }
        for (std::size_t k = 0; k < prepared.size(); ++k) {
            fail(report, method + ": " + error);
        }
        return;
    }
    for (Prepared& p : prepared) {
        if (cancel != nullptr && cancel->load()) {
            report.cancelled = true;
            break;
        }
        const Job& job = *p.job;
        const int want_w = (p.original.width + 2 * b) * 4, want_h = (p.original.height + 2 * b) * 4;
        Image result;
        if (!png::read(work / "out" / (job.key + ".png"), result, error) || result.width != want_w || result.height != want_h) {
            fail(report, job.key + ": the model's picture is missing or not 4x" + (error.empty() ? "" : " (" + error + ")"));
            error.clear();
            continue;
        }
        Image colour = crop(result, b * 4);
        if (s == 2) {
            colour = box_down(colour, 2);
        }
        match_colour(colour, p.bled, s);
        if (!p.transparent) {
            for (std::size_t k = 3; k < colour.rgba.size(); k += 4) {
                colour.rgba[k] = 255;
            }
        } else {
            Image alpha;
            if (job.cls == Class::TwoD) {
                alpha = nearest(p.original, s);   // the exact outline: a cut-out stays 0 and 255
            } else if (options.alpha == AlphaMethod::Model) {
                Image grey;
                if (!png::read(work / "out" / (job.key + "_a.png"), grey, error) || grey.width != want_w || grey.height != want_h) {
                    fail(report, job.key + ": the model's alpha picture is missing or not 4x");
                    error.clear();
                    continue;
                }
                alpha = crop(grey, b * 4);
                if (s == 2) {
                    alpha = box_down(alpha, 2);
                }
                for (std::size_t k = 0; k < alpha.rgba.size(); k += 4) {   // a model's grey is not exactly grey: the mean of its three channels
                    alpha.rgba[k + 3] = static_cast<std::uint8_t>((alpha.rgba[k] + alpha.rgba[k + 1] + alpha.rgba[k + 2] + 1) / 3);
                }
            } else {
                alpha = crop(bicubic(pad(p.original, b, p.wrapped), s), b * s);
            }
            for (std::size_t k = 3; k < colour.rgba.size(); k += 4) {
                colour.rgba[k] = alpha.rgba[k];
            }
        }
        write_result(report, out / (job.key + ".png"), colour, p.transparent);
    }
    fs::remove_all(work, ec);
}

}  // namespace

Report run(const fs::path& dump, const fs::path& table, const fs::path& out, const Options& options, const std::string& tool, const ModelRunner& model,
           const std::function<void(const std::string&)>& log, const std::atomic<bool>* cancel,
           const std::function<void(int done, int total)>& progress) {
    Report report;
    std::map<std::string, DumpRow> rows;
    std::map<std::string, Class> classes;
    std::uint64_t table_hash = 0;
    std::string error;
    if (options.scale != 2 && options.scale != 4) {
        fail(report, "scale must be 2 or 4");
        return report;
    }
    if (!read_index(dump / "index.tsv", rows, error) || !read_classes(table, classes, table_hash, error)) {
        fail(report, error);
        return report;
    }
    std::error_code ec;
    fs::create_directories(out, ec);
    const std::string stamp = stamp_text(options, tool, table_hash);
    {
        std::ifstream in(out / "stamp.txt", std::ios::binary);
        const std::string old((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (old != stamp) {   // another tool, model, scale or table: what is there is not this set
            int removed = 0;
            for (const fs::directory_entry& entry : fs::directory_iterator(out, ec)) {
                const fs::path& path = entry.path();
                if (path.extension() == ".png" && is_key(path.stem().string())) {
                    fs::remove(path, ec);
                    ++removed;
                }
            }
            if (removed != 0) {
                log("removed " + std::to_string(removed) + " textures of another stamp");
            }
            std::ofstream stamp_out(out / "stamp.txt", std::ios::binary | std::ios::trunc);
            stamp_out << stamp;
        }
    }
    const std::set<std::string> only(options.keys.begin(), options.keys.end());
    std::vector<Job> jobs;
    for (const auto& [key, cls] : classes) {
        if (!only.empty() && only.count(key) == 0) {
            continue;
        }
        ++report.in_table;
        const std::string& method = cls == Class::World ? options.model_world : options.model_2d;
        if (method == kNone) {
            ++report.left_out;
            continue;
        }
        const auto row = rows.find(key);
        if (row == rows.end() || !fs::is_regular_file(dump / (key + ".png"), ec)) {
            ++report.not_dumped;
            continue;
        }
        if (cls == Class::World && std::min(row->second.width, row->second.height) < options.min_world) {
            ++report.small;
            continue;
        }
        if (fs::is_regular_file(out / (key + ".png"), ec)) {
            ++report.done_before;
            continue;
        }
        jobs.push_back(Job{key, row->second, cls});
        ++report.selected;
    }
    log(std::to_string(report.in_table) + " in the table: " + std::to_string(report.selected) + " to upscale, " + std::to_string(report.done_before) +
        " done before, " + std::to_string(report.small) + " world under " + std::to_string(options.min_world) + " px, " + std::to_string(report.not_dumped) +
        " not dumped, " + std::to_string(report.left_out) + " left out");
    int finished = 0;
    if (progress) {
        progress(0, report.selected);
    }
    for (const Class cls : {Class::World, Class::TwoD}) {
        const std::string& method = cls == Class::World ? options.model_world : options.model_2d;
        const char* name = cls == Class::World ? "world" : "2d";
        std::vector<const Job*> of_class;
        for (const Job& job : jobs) {
            if (job.cls == cls) {
                of_class.push_back(&job);
            }
        }
        for (std::size_t from = 0; from < of_class.size() && !report.cancelled; from += static_cast<std::size_t>(options.batch)) {
            if (cancel != nullptr && cancel->load()) {
                report.cancelled = true;
                break;
            }
            const std::size_t to = std::min(of_class.size(), from + static_cast<std::size_t>(options.batch));
            const std::vector<const Job*> batch(of_class.begin() + static_cast<std::ptrdiff_t>(from), of_class.begin() + static_cast<std::ptrdiff_t>(to));
            run_batch(batch, method, dump, out, options, model, report, cancel);
            finished += static_cast<int>(batch.size());
            if (progress) {
                progress(finished, report.selected);
            }
            log(std::string(name) + " (" + method + "): " + std::to_string(to) + " of " + std::to_string(of_class.size()) + ", " +
                std::to_string(report.failed) + " failed so far");
        }
    }
    fs::remove_all(out / ".work", ec);
    log("written " + std::to_string(report.written) + " (" + std::to_string(report.bytes / (1024u * 1024u)) + " MB), failed " + std::to_string(report.failed) +
        (report.cancelled ? ", cancelled" : ""));
    return report;
}

}  // namespace thpsource2::upscale
