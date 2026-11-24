#include "update_check.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <utility>
#include <vector>

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

// ---- the release ----

namespace {

// A small JSON reader for the one answer the launcher reads (a release object). Depth and size are bounded; a malformed document is no answer.
struct Json {
    enum class Kind { Null, Bool, Number, String, Array, Object } kind = Kind::Null;
    std::string text;   // a string's value, a number's digits
    std::vector<Json> items;
    std::vector<std::pair<std::string, Json>> members;

    const Json* find(std::string_view key) const {
        for (const auto& [name, value] : members) {
            if (name == key) {
                return &value;
            }
        }
        return nullptr;
    }
};

class JsonReader {
public:
    explicit JsonReader(std::string_view text) : text_(text) {}
    std::optional<Json> read() {
        Json value;
        if (!parse(value, 0)) {
            return std::nullopt;
        }
        skip();
        return at_ == text_.size() ? std::optional<Json>(std::move(value)) : std::nullopt;
    }

private:
    std::string_view text_;
    std::size_t at_ = 0;

    void skip() {
        while (at_ < text_.size() && (text_[at_] == ' ' || text_[at_] == '\t' || text_[at_] == '\n' || text_[at_] == '\r')) {
            ++at_;
        }
    }
    bool literal(std::string_view word) {
        if (text_.substr(at_, word.size()) != word) {
            return false;
        }
        at_ += word.size();
        return true;
    }
    static void put_utf8(std::string& out, unsigned code) {
        if (code < 0x80) {
            out += static_cast<char>(code);
        } else if (code < 0x800) {
            out += static_cast<char>(0xc0 | (code >> 6));
            out += static_cast<char>(0x80 | (code & 0x3f));
        } else if (code < 0x10000) {
            out += static_cast<char>(0xe0 | (code >> 12));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3f));
            out += static_cast<char>(0x80 | (code & 0x3f));
        } else {
            out += static_cast<char>(0xf0 | (code >> 18));
            out += static_cast<char>(0x80 | ((code >> 12) & 0x3f));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3f));
            out += static_cast<char>(0x80 | (code & 0x3f));
        }
    }
    bool hex4(unsigned& code) {
        if (at_ + 4 > text_.size()) {
            return false;
        }
        code = 0;
        for (int k = 0; k < 4; ++k) {
            const char c = text_[at_++];
            code <<= 4;
            if (c >= '0' && c <= '9') {
                code |= static_cast<unsigned>(c - '0');
            } else if (c >= 'a' && c <= 'f') {
                code |= static_cast<unsigned>(c - 'a' + 10);
            } else if (c >= 'A' && c <= 'F') {
                code |= static_cast<unsigned>(c - 'A' + 10);
            } else {
                return false;
            }
        }
        return true;
    }
    bool string(std::string& out) {
        if (at_ >= text_.size() || text_[at_] != '"') {
            return false;
        }
        ++at_;
        while (at_ < text_.size()) {
            const char c = text_[at_++];
            if (c == '"') {
                return true;
            }
            if (static_cast<unsigned char>(c) < 0x20) {
                return false;
            }
            if (c != '\\') {
                out += c;
                continue;
            }
            if (at_ >= text_.size()) {
                return false;
            }
            const char escape = text_[at_++];
            switch (escape) {
            case '"': case '\\': case '/': out += escape; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u': {
                unsigned code = 0;
                if (!hex4(code)) {
                    return false;
                }
                if (code >= 0xd800 && code < 0xdc00 && text_.substr(at_, 2) == "\\u") {   // a surrogate pair
                    at_ += 2;
                    unsigned low = 0;
                    if (!hex4(low) || low < 0xdc00 || low > 0xdfff) {
                        return false;
                    }
                    code = 0x10000 + ((code - 0xd800) << 10) + (low - 0xdc00);
                } else if (code >= 0xd800 && code <= 0xdfff) {
                    code = 0xfffd;
                }
                put_utf8(out, code);
                break;
            }
            default: return false;
            }
        }
        return false;
    }
    bool parse(Json& value, int depth) {
        if (depth > 16) {
            return false;
        }
        skip();
        if (at_ >= text_.size()) {
            return false;
        }
        const char c = text_[at_];
        if (c == '{') {
            ++at_;
            value.kind = Json::Kind::Object;
            skip();
            if (at_ < text_.size() && text_[at_] == '}') {
                ++at_;
                return true;
            }
            while (true) {
                skip();
                std::string key;
                if (!string(key)) {
                    return false;
                }
                skip();
                if (at_ >= text_.size() || text_[at_++] != ':') {
                    return false;
                }
                Json member;
                if (!parse(member, depth + 1)) {
                    return false;
                }
                value.members.emplace_back(std::move(key), std::move(member));
                skip();
                if (at_ < text_.size() && text_[at_] == ',') {
                    ++at_;
                    continue;
                }
                return at_ < text_.size() && text_[at_++] == '}';
            }
        }
        if (c == '[') {
            ++at_;
            value.kind = Json::Kind::Array;
            skip();
            if (at_ < text_.size() && text_[at_] == ']') {
                ++at_;
                return true;
            }
            while (true) {
                Json item;
                if (!parse(item, depth + 1)) {
                    return false;
                }
                value.items.push_back(std::move(item));
                skip();
                if (at_ < text_.size() && text_[at_] == ',') {
                    ++at_;
                    continue;
                }
                return at_ < text_.size() && text_[at_++] == ']';
            }
        }
        if (c == '"') {
            value.kind = Json::Kind::String;
            return string(value.text);
        }
        if (literal("true") || literal("false")) {
            value.kind = Json::Kind::Bool;
            return true;
        }
        if (literal("null")) {
            return true;
        }
        const std::size_t start = at_;
        while (at_ < text_.size() && (std::isdigit(static_cast<unsigned char>(text_[at_])) || text_[at_] == '-' || text_[at_] == '+' || text_[at_] == '.' ||
                                      text_[at_] == 'e' || text_[at_] == 'E')) {
            ++at_;
        }
        if (at_ == start) {
            return false;
        }
        value.kind = Json::Kind::Number;
        value.text = std::string(text_.substr(start, at_ - start));
        return true;
    }
};

}  // namespace

std::optional<Release> parse_release(std::string_view json, std::string_view expected_tag) {
    if (json.size() > (1u << 20)) {
        return std::nullopt;
    }
    const std::optional<Json> root = JsonReader(json).read();
    if (!root || root->kind != Json::Kind::Object) {
        return std::nullopt;
    }
    const Json* tag = root->find("tag_name");
    if (tag == nullptr || tag->kind != Json::Kind::String || tag->text != expected_tag) {
        return std::nullopt;
    }
    Release release;
    release.tag = tag->text;
    if (const Json* body = root->find("body"); body != nullptr && body->kind == Json::Kind::String) {
        release.notes = plain_notes(body->text);
    }
    if (const Json* assets = root->find("assets"); assets != nullptr && assets->kind == Json::Kind::Array) {
        for (const Json& asset : assets->items) {
            const Json* name = asset.find("name");
            const Json* size = asset.find("size");
            if (name != nullptr && name->text == kPackageName && size != nullptr && size->kind == Json::Kind::Number) {
                release.zip_size = std::strtoull(size->text.c_str(), nullptr, 10);
            }
        }
    }
    return release;
}

std::string plain_notes(std::string_view markdown, std::size_t limit) {
    // text first: a character at a time, so a link's "[text](address)" and emphasis can be taken out line by line below
    std::string text;
    for (std::size_t k = 0; k < markdown.size();) {
        const unsigned char c = static_cast<unsigned char>(markdown[k]);
        if (c == '\r') {
            ++k;
        } else if (c < 0x80) {
            text += static_cast<char>(c);
            ++k;
        } else {
            const std::size_t length = c >= 0xf0 ? 4 : c >= 0xe0 ? 3 : c >= 0xc0 ? 2 : 1;
            const std::string_view sequence = markdown.substr(k, length);
            k += length;
            if (sequence == "\xe2\x80\x99" || sequence == "\xe2\x80\x98") {
                text += '\'';
            } else if (sequence == "\xe2\x80\x9c" || sequence == "\xe2\x80\x9d") {
                text += '"';
            } else if (sequence == "\xe2\x80\x93" || sequence == "\xe2\x80\x94") {
                text += '-';
            } else if (sequence == "\xe2\x80\xa6") {
                text += "...";
            } else if (sequence == "\xe2\x80\xa2") {
                text += "- ";
            } else if (length <= 2) {
                text += sequence;   // Latin letters keep their accents
            }                       // 3- and 4-byte symbols, emoji and the rest: the launcher's font may not have them
        }
    }
    std::string out;
    int blank = 0;
    std::size_t start = 0;
    while (start <= text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string::npos) {
            end = text.size();
        }
        std::string line = text.substr(start, end - start);
        start = end + 1;
        while (!line.empty() && (line.back() == ' ' || line.back() == '\t')) {
            line.pop_back();
        }
        std::size_t lead = 0;
        while (lead < line.size() && line[lead] == ' ') {
            ++lead;
        }
        std::string body = line.substr(lead);
        while (!body.empty() && body.front() == '>') {
            body.erase(0, 1);
            if (!body.empty() && body.front() == ' ') {
                body.erase(0, 1);
            }
        }
        if (!body.empty() && body.front() == '#') {
            while (!body.empty() && body.front() == '#') {
                body.erase(0, 1);
            }
            while (!body.empty() && body.front() == ' ') {
                body.erase(0, 1);
            }
            if (!out.empty() && blank == 0) {
                out += '\n';   // a heading stands apart from what is above it
                blank = 1;
            }
        } else if (body.size() >= 2 && (body[0] == '*' || body[0] == '+') && body[1] == ' ') {
            body[0] = '-';
        }
        // links and images "[text](address)": the text; then the marks of emphasis and code
        for (std::size_t at = body.find("]("); at != std::string::npos; at = body.find("](")) {
            const std::size_t open = body.rfind('[', at);
            const std::size_t close = body.find(')', at);
            if (open == std::string::npos || close == std::string::npos) {
                break;
            }
            const std::size_t from = open > 0 && body[open - 1] == '!' ? open - 1 : open;
            body = body.substr(0, from) + body.substr(open + 1, at - open - 1) + body.substr(close + 1);
        }
        for (const char* mark : {"**", "__", "`", "~~"}) {
            const std::size_t length = std::char_traits<char>::length(mark);
            for (std::size_t at = body.find(mark); at != std::string::npos; at = body.find(mark, at)) {
                body.erase(at, length);
            }
        }
        const std::string kept = std::string(lead, ' ') + body;
        if (body.empty()) {
            if (++blank > 1 || out.empty()) {
                continue;
            }
            out += '\n';
            continue;
        }
        blank = 0;
        out += kept + '\n';
    }
    while (!out.empty() && out.back() == '\n') {
        out.pop_back();
    }
    if (out.size() > limit) {
        out.resize(out.rfind('\n', limit) == std::string::npos ? limit : out.rfind('\n', limit));
        out += "\n...";
    }
    return out;
}

std::optional<std::string> checksum_from_file(std::string_view text, std::string_view file) {
    if (text.size() < 64) {
        return std::nullopt;
    }
    std::string hash;
    for (std::size_t k = 0; k < 64; ++k) {
        const char c = text[k];
        if (!std::isxdigit(static_cast<unsigned char>(c))) {
            return std::nullopt;
        }
        hash += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    std::string_view rest = text.substr(64);
    if (rest.empty() || (rest[0] != ' ' && rest[0] != '\t')) {
        return std::nullopt;
    }
    while (!rest.empty() && (rest.front() == ' ' || rest.front() == '*' || rest.front() == '\t')) {   // "  name" and sha256sum's binary "*name"
        rest.remove_prefix(1);
    }
    const std::size_t end = rest.find_first_of("\r\n");
    if (rest.substr(0, end) != file) {
        return std::nullopt;
    }
    return hash;
}

std::string api_url(std::string_view repository_url, std::string_view tag) {
    constexpr std::string_view kSite = "https://github.com/";
    while (!repository_url.empty() && repository_url.back() == '/') {
        repository_url.remove_suffix(1);
    }
    if (repository_url.substr(0, kSite.size()) != kSite) {
        return {};
    }
    return "https://api.github.com/repos/" + std::string(repository_url.substr(kSite.size())) + "/releases/tags/" + std::string(tag);
}

std::string asset_url(std::string_view repository_url, std::string_view tag, std::string_view file) {
    while (!repository_url.empty() && repository_url.back() == '/') {
        repository_url.remove_suffix(1);
    }
    return std::string(repository_url) + "/releases/download/" + std::string(tag) + "/" + std::string(file);
}

std::string apply_script(std::string_view tag) {
    std::string script =
        "@echo off\r\n"
        "rem THPSource2's updater (docs/LAUNCHER.md, \"Updates\"): run by the launcher with its process id, it waits for the launcher to close, puts the unpacked release\r\n"
        "rem over this install and starts the launcher again. It never touches Config, Data, Saves, Mods, Screenshots or Logs.\r\n"
        "setlocal\r\n"
        "set \"STAGE=%~dp0unpack\\thpsource2-win64\"\r\n"
        "cd /d \"%~dp0..\\..\"\r\n"
        "if not exist Logs mkdir Logs\r\n"
        "set \"LOG=Logs\\update.txt\"\r\n"
        "del \"%~dp0failed.txt\" 2>nul\r\n"
        "echo %date% %time% update to @TAG@: waiting for the launcher to close>>\"%LOG%\"\r\n"
        "set /a waited=0\r\n"
        ":wait\r\n"
        "tasklist /FI \"PID eq %1\" /FO CSV /NH 2>nul | find \"\"\"%1\"\"\" >nul\r\n"
        "if errorlevel 1 goto copy\r\n"
        "set /a waited+=1\r\n"
        "if %waited% gtr 60 goto failed\r\n"
        ">nul ping -n 2 127.0.0.1\r\n"
        "goto wait\r\n"
        ":copy\r\n"
        "robocopy \"%STAGE%\\Build\" Build /MIR /R:5 /W:1 /NFL /NDL /NJH /NJS /NP>>\"%LOG%\"\r\n"
        "if errorlevel 8 goto failed\r\n"
        "robocopy \"%STAGE%\\Licenses\" Licenses /MIR /R:5 /W:1 /NFL /NDL /NJH /NJS /NP>>\"%LOG%\"\r\n"
        "if errorlevel 8 goto failed\r\n"
        "copy /y \"%STAGE%\\README.md\" README.md >nul\r\n"
        "copy /y \"%STAGE%\\THPSource2.exe\" THPSource2.exe >nul\r\n"
        "if errorlevel 1 goto failed\r\n"
        "echo %date% %time% updated to @TAG@>>\"%LOG%\"\r\n"
        "rmdir /s /q \"%~dp0unpack\" 2>nul\r\n"
        "del \"%~dp0@PACKAGE@\" 2>nul\r\n"
        "start \"\" THPSource2.exe\r\n"
        "exit /b 0\r\n"
        ":failed\r\n"
        "echo %date% %time% FAILED: the update to @TAG@ did not finish; the files that were being replaced may be a mix of both versions, extract the release zip over this folder>>\"%LOG%\"\r\n"
        "echo failed>\"%~dp0failed.txt\"\r\n"
        "if exist THPSource2.exe start \"\" THPSource2.exe\r\n"
        "exit /b 1\r\n";
    for (const auto& [from, to] : {std::pair<std::string_view, std::string_view>{"@TAG@", tag}, {"@PACKAGE@", kPackageName}}) {
        for (std::size_t at = script.find(from); at != std::string::npos; at = script.find(from, at + to.size())) {
            script.replace(at, from.size(), to);
        }
    }
    return script;
}

}  // namespace thpsource2::update
