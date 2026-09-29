#ifndef PERUNEJS_UNICODE_ID_H
#define PERUNEJS_UNICODE_ID_H

#include <cstdint>
#include <string>

namespace perunejs {
    bool is_identifier_start(char16_t unit);
    bool is_identifier_part(char16_t unit);
}

#endif //PERUNEJS_UNICODE_ID_H
