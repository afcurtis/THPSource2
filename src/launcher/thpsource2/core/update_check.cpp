#include "update_check.hpp"

#include <cctype>

namespace thpsource2::update {

namespace {

bool equal_ignoring_case(std::string_view a, std::string_view b) {
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

}  // namespace

std::optional<Version> parse_version(std::string_view text) {
    if (!text.empty() && (text.front() == 'v' || text.front() == 'V')) {
        text.remove_prefix(1);
    }
    Version version;
    std::size_t count = 0;
    std::size_t at = 0;
    while (true) {
        std::size_t digits = 0;
        int value = 0;
        while (at < text.size() && std::isdigit(static_cast<unsigned char>(text[at]))) {
            value = value * 10 + (text[at] - '0');
            ++at;
            if (++digits > 6) {
                return std::nullopt;
            }
        }
        if (digits == 0 || count == version.part.size()) {
            return std::nullopt;
        }
        version.part[count++] = value;
        if (at == text.size()) {
            return version;
        }
        if (text[at] != '.') {
            return std::nullopt;
        }
        ++at;
    }
}

int compare(const Version& a, const Version& b) {
    for (std::size_t k = 0; k < a.part.size(); ++k) {
        if (a.part[k] != b.part[k]) {
            return a.part[k] < b.part[k] ? -1 : 1;
        }
    }
    return 0;
}

std::optional<std::string> tag_from_redirect(std::string_view location, std::string_view repository_url) {
    while (!location.empty() && (location.back() == '\n' || location.back() == '\r' || location.back() == ' ')) {
        location.remove_suffix(1);
    }
    while (!repository_url.empty() && repository_url.back() == '/') {
        repository_url.remove_suffix(1);
    }
    constexpr std::string_view kTagPath = "/releases/tag/";
    if (repository_url.empty() || location.size() <= repository_url.size() + kTagPath.size() ||
        !equal_ignoring_case(location.substr(0, repository_url.size()), repository_url) ||
        location.substr(repository_url.size(), kTagPath.size()) != kTagPath) {
        return std::nullopt;
    }
    const std::string_view tag = location.substr(repository_url.size() + kTagPath.size());
    if (!parse_version(tag)) {   // also keeps out '/', '%', '?', '#': a version has digits, dots and one leading v
        return std::nullopt;
    }
    return std::string(tag);
}

bool is_newer(std::string_view latest_tag, std::string_view running) {
    const std::optional<Version> latest = parse_version(latest_tag);
    const std::optional<Version> current = parse_version(running);
    return latest && current && compare(*latest, *current) > 0;
}

}  // namespace thpsource2::update
