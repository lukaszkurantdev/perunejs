#ifndef PERUNEJS_SYNTAX_ERROR_H
#define PERUNEJS_SYNTAX_ERROR_H
#include <stdexcept>

class SyntaxError : public std::runtime_error {
    uint32_t offset;

public:
    SyntaxError(const std::string& message, uint32_t offset) : std::runtime_error(message), offset(offset) {}
    uint32_t get_offset() const { return offset; }
};

#endif //PERUNEJS_SYNTAX_ERROR_H
