#include "utf.h"

namespace perunejs {
    std::u16string utf8_to_utf16(std::string_view text) {
        std::u16string result;
        result.reserve(text.size());

        for (std::size_t i = 0; i < text.size();) {
            const auto lead = static_cast<unsigned char>(text[i]);

            uint32_t code = 0;
            std::size_t length = 1;

            if (lead < 0x80)                { code = lead;        length = 1; }
            else if ((lead & 0xE0) == 0xC0) { code = lead & 0x1F;  length = 2; }
            else if ((lead & 0xF0) == 0xE0) { code = lead & 0x0F;  length = 3; }
            else if ((lead & 0xF8) == 0xF0) { code = lead & 0x07;  length = 4; }
            else { result.push_back(0xFFFD); ++i; continue; }

            if (i + length > text.size()) { result.push_back(0xFFFD); break; }

            for (std::size_t k = 1; k < length; ++k) {
                code = (code << 6) | (static_cast<unsigned char>(text[i + k]) & 0x3F);
            }
            i += length;

            if (code > 0xFFFF) {
                code -= 0x10000;
                result.push_back(static_cast<char16_t>(0xD800 + (code >> 10)));
                result.push_back(static_cast<char16_t>(0xDC00 + (code & 0x3FF)));
            } else {
                result.push_back(static_cast<char16_t>(code));
            }
        }

        return result;
    }

    std::string utf16_to_utf8(std::u16string_view text) {
        std::string result;
        result.reserve(text.size());

        for (std::size_t i = 0; i < text.size(); ++i) {
            uint32_t code = text[i];

            if (is_high_surrogate(text[i]) && i + 1 < text.size() && is_low_surrogate(text[i + 1])) {
                code = 0x10000 + ((code - 0xD800) << 10) + (text[i + 1] - 0xDC00);
                ++i;
            }

            if (code < 0x80) {
                result.push_back(static_cast<char>(code));
            } else if (code < 0x800) {
                result.push_back(static_cast<char>(0xC0 | (code >> 6)));
                result.push_back(static_cast<char>(0x80 | (code & 0x3F)));
            } else if (code < 0x10000) {
                result.push_back(static_cast<char>(0xE0 | (code >> 12)));
                result.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
                result.push_back(static_cast<char>(0x80 | (code & 0x3F)));
            } else {
                result.push_back(static_cast<char>(0xF0 | (code >> 18)));
                result.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
                result.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
                result.push_back(static_cast<char>(0x80 | (code & 0x3F)));
            }
        }

        return result;
    }
}