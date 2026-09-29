#ifndef PERUNEJS_UTF_H
#define PERUNEJS_UTF_H

#include <cstdint>
#include <string>
#include <string_view>

namespace perunejs {
    std::u16string utf8_to_utf16(std::string_view text);
    std::string utf16_to_utf8(std::u16string_view text);

    inline bool is_high_surrogate(char16_t unit) { return unit >= 0xD800 && unit <= 0xDBFF; }
    inline bool is_low_surrogate(char16_t unit)  { return unit >= 0xDC00 && unit <= 0xDFFF; }
}

#endif //PERUNEJS_UTF_H
