#ifndef PERUNEJS_UNICODE_CASE_H
#define PERUNEJS_UNICODE_CASE_H

#include <cstdint>
#include <string>

namespace perunejs {
    char16_t to_upper_unit(char16_t unit);
    char16_t to_lower_unit(char16_t unit);

    std::u16string change_case_full(const std::u16string &text, bool upper);
}

#endif //PERUNEJS_UNICODE_CASE_H
