#include "hal.h"

namespace {

const char *const kServiceNames[OT_HAL_SERVICE_COUNT] = {"clock", "window", "input", "video", "audio", "movie"};

bool same_word(const char *text, unsigned length, const char *word) {
    unsigned index = 0;
    for (; index < length && word[index] != 0; ++index) {
        char left = text[index];
        if (left >= 'A' && left <= 'Z') {
            left = static_cast<char>(left - 'A' + 'a');
        }
        if (left != word[index]) {
            return false;
        }
    }
    return index == length && word[index] == 0;
}

} // namespace

extern "C" const char *ot_hal_service_name(int service) {
    return service >= 0 && service < OT_HAL_SERVICE_COUNT ? kServiceNames[service] : 0;
}

extern "C" int ot_hal_service_parse(const char *text, unsigned length) {
    for (int service = 0; service < OT_HAL_SERVICE_COUNT; ++service) {
        if (same_word(text, length, kServiceNames[service])) {
            return service;
        }
    }
    return -1;
}
