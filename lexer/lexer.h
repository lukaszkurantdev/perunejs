#ifndef PERUNEJS_LEXER_H
#define PERUNEJS_LEXER_H

#include <iostream>
#include <vector>

#include "token.h"

namespace perunejs {
        class Lexer {
                std::vector<Token*> tokens;
                std::string source;
                uint32_t start = 0;
                uint32_t current = 0;
                uint32_t line = 1;
                bool pending_newline = false;

                bool is_end() const;
                char advance();
                char peek(uint8_t shift = 0) const;
                void add_token(Token *token);
                void scan_token();

                bool regexp_allowed() const;
                void scan_regexp(uint32_t local_start);
                static void validate_flags(std::string_view flags);

                uint32_t scan_hex_escape(int count);
                void scan_exponent();
                void check_numeric_boundary() const;

                bool at_unicode_line_terminator() const;
                uint32_t peek_code_point(uint8_t &length) const;
                void scan_identifier(uint32_t local_start);
        public:
                Lexer(std::string source);
                std::vector<Token*> scan_tokens();
        };
}

#endif //PERUNEJS_LEXER_H
