#include "ini.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <system_error>

namespace thpsource2 {

bool equal_nocase(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t k = 0; k < a.size(); ++k) {
        if (std::tolower(static_cast<unsigned char>(a[k])) != std::tolower(static_cast<unsigned char>(b[k]))) {
            return false;
        }
    }
    return true;
}

std::string trim(std::string_view text) {
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(text[begin]))) {
        ++begin;
    }
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) {
        --end;
    }
    return std::string(text.substr(begin, end - begin));
}

IniFile IniFile::parse(std::string_view text) {
    IniFile file;
    if (text.find("\r\n") == std::string_view::npos && text.find('\n') != std::string_view::npos) {
        file.newline_ = "\n";
    }
    std::string section;
    std::size_t start = 0;
    while (start < text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string_view::npos) {
            end = text.size();
        }
        std::string raw(text.substr(start, end - start));
        if (!raw.empty() && raw.back() == '\r') {
            raw.pop_back();
        }
        start = end + 1;
        Line line;
        line.raw = raw;
        const std::string stripped = trim(raw);
        if (stripped.size() >= 2 && stripped.front() == '[' && stripped.find(']') != std::string::npos) {
            section = trim(std::string_view(stripped).substr(1, stripped.find(']') - 1));
            line.is_section = true;
        } else if (!stripped.empty() && stripped.front() != ';' && stripped.front() != '#' && stripped.find('=') != std::string::npos) {
            line.key = trim(std::string_view(stripped).substr(0, stripped.find('=')));
        }
        line.section = section;
        file.lines_.push_back(std::move(line));
    }
    return file;
}

IniFile IniFile::load(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    const std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    return parse(text);
}

std::optional<std::string> IniFile::get(std::string_view section, std::string_view key) const {
    for (const Line& line : lines_) {
        if (!line.is_section && !line.key.empty() && equal_nocase(line.section, section) && equal_nocase(line.key, key)) {
            return trim(std::string_view(line.raw).substr(line.raw.find('=') + 1));
        }
    }
    return std::nullopt;
}

std::string IniFile::get_string(std::string_view section, std::string_view key, std::string_view fallback) const {
    const auto value = get(section, key);
    return value ? *value : std::string(fallback);
}

int IniFile::get_int(std::string_view section, std::string_view key, int fallback) const {
    const auto value = get(section, key);
    if (!value || value->empty()) {
        return fallback;
    }
    char* end = nullptr;
    const long number = std::strtol(value->c_str(), &end, 10);
    return end == value->c_str() ? fallback : static_cast<int>(number);
}

void IniFile::set(std::string_view section, std::string_view key, std::string_view value) {
    const std::string raw = std::string(key) + "=" + std::string(value);
    std::size_t last_of_section = lines_.size();
    bool section_found = false;
    for (std::size_t k = 0; k < lines_.size(); ++k) {
        Line& line = lines_[k];
        if (!equal_nocase(line.section, section)) {
            continue;
        }
        if (line.is_section) {
            section_found = true;
        }
        if (!line.is_section && !line.key.empty() && equal_nocase(line.key, key)) {
            line.raw = raw;
            return;
        }
        if (section_found && (line.is_section || !trim(line.raw).empty())) {
            last_of_section = k;
        }
    }
    Line line;
    line.raw = raw;
    line.section = std::string(section);
    line.key = std::string(key);
    if (section_found) {
        lines_.insert(lines_.begin() + static_cast<std::ptrdiff_t>(last_of_section + 1), std::move(line));
        return;
    }
    if (!lines_.empty() && !trim(lines_.back().raw).empty()) {
        lines_.push_back(Line{"", lines_.back().section, "", false});
    }
    Line header;
    header.raw = "[" + std::string(section) + "]";
    header.section = std::string(section);
    header.is_section = true;
    lines_.push_back(std::move(header));
    lines_.push_back(std::move(line));
}

std::vector<std::string> IniFile::sections() const {
    std::vector<std::string> names;
    for (const Line& line : lines_) {
        if (line.is_section && std::none_of(names.begin(), names.end(), [&](const std::string& name) { return equal_nocase(name, line.section); })) {
            names.push_back(line.section);
        }
    }
    return names;
}

std::string IniFile::text() const {
    std::string out;
    for (const Line& line : lines_) {
        out += line.raw;
        out += newline_;
    }
    return out;
}

bool IniFile::save(const std::filesystem::path& path) const {
    std::error_code ec;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), ec);
    }
    std::filesystem::path temporary = path;
    temporary += ".new";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        const std::string body = text();
        out.write(body.data(), static_cast<std::streamsize>(body.size()));
        if (!out) {
            return false;
        }
    }
    std::filesystem::rename(temporary, path, ec);
    if (ec) {
        std::filesystem::remove(temporary, ec);
        return false;
    }
    return true;
}

}  // namespace thpsource2
