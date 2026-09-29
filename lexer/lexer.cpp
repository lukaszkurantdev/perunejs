#include "lexer.h"
#include <cstdint>
#include <string_view>
#include "utils/unicode_id.h"
#include "utils/utf.h"

using namespace std;

namespace perunejs {
    Lexer::Lexer(string source) {
        this->source = std::move(source);
    }

    bool Lexer::is_end() const {
        return current >= source.length();
    }

    char Lexer::advance() {
        return source[current++];
    }

    char Lexer::peek(const uint8_t shift) const {
        const size_t i = current + shift;
        return i < source.length() ? source[i] : '\0';
    }

    uint32_t Lexer::peek_code_point(uint8_t &length) const {
        const auto first = static_cast<unsigned char>(peek());

        if (first < 0x80) { length = 1; return first; }

        uint8_t extra;
        uint32_t code;

        if ((first & 0xE0) == 0xC0)      { extra = 1; code = first & 0x1F; }
        else if ((first & 0xF0) == 0xE0) { extra = 2; code = first & 0x0F; }
        else if ((first & 0xF8) == 0xF0) { extra = 3; code = first & 0x07; }
        else throw std::runtime_error("invalid UTF-8 in source");

        for (uint8_t i = 1; i <= extra; ++i) {
            const auto byte = static_cast<unsigned char>(peek(i));
            if ((byte & 0xC0) != 0x80) throw std::runtime_error("invalid UTF-8 in source");

            code = (code << 6) | (byte & 0x3F);
        }

        length = static_cast<uint8_t>(extra + 1);
        return code;
    }

    bool Lexer::at_unicode_line_terminator() const {
        return static_cast<unsigned char>(peek())  == 0xE2
            && static_cast<unsigned char>(peek(1)) == 0x80
            && (static_cast<unsigned char>(peek(2)) == 0xA8
             || static_cast<unsigned char>(peek(2)) == 0xA9);
    }

    void Lexer::add_token(Token *token) {
        token->end = current;
        token->newline_before = pending_newline;
        pending_newline = false;
        tokens.push_back(token);
    }

    constexpr uint64_t hash_string(std::string_view s) {
        uint64_t h = 14695981039346656037ULL;
        for (const char c : s) {
            h ^= static_cast<unsigned char>(c);
            h *= 1099511628211ULL;
        }
        return h;
    }

    static bool is_unicode_whitespace(const uint32_t code) {
        return code == 0x00A0 || code == 0x1680 || code == 0x180E
            || (code >= 0x2000 && code <= 0x200A)
            || code == 0x202F || code == 0x205F || code == 0x3000 || code == 0xFEFF;
    }

    static bool is_unicode_line_terminator(const uint32_t code) {
        return code == 0x2028 || code == 0x2029;
    }

    int hex_digit_value(const char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }

    static TOKEN_TYPE keyword_type(std::string_view s) {
        switch (hash_string(s)) {
            case hash_string("async"):      return ASYNC;
            case hash_string("await"):      return AWAIT;
            case hash_string("break"):      return BREAK;
            case hash_string("case"):       return CASE;
            case hash_string("catch"):      return CATCH;
            case hash_string("class"):      return CLASS;
            case hash_string("const"):      return CONST;
            case hash_string("continue"):   return CONTINUE;
            case hash_string("debugger"):   return DEBUGGER;
            case hash_string("default"):    return DEFAULT;
            case hash_string("delete"):     return DELETE;
            case hash_string("do"):         return DO;
            case hash_string("else"):       return ELSE;
            case hash_string("enum"):       return ENUM;
            case hash_string("export"):     return EXPORT;
            case hash_string("extends"):    return EXTENDS;
            case hash_string("false"):      return FALSE;
            case hash_string("finally"):    return FINALLY;
            case hash_string("for"):        return FOR;
            case hash_string("function"):   return FUNCTION;
            case hash_string("if"):         return IF;
            case hash_string("import"):     return IMPORT;
            case hash_string("in"):         return IN;
            case hash_string("instanceof"): return INSTANCE_OF;
            case hash_string("let"):        return LET;
            case hash_string("new"):        return NEW;
            case hash_string("null"):       return NULL_T;
            case hash_string("return"):     return RETURN;
            case hash_string("super"):      return SUPER;
            case hash_string("switch"):     return SWITCH;
            case hash_string("this"):       return THIS;
            case hash_string("throw"):      return THROW;
            case hash_string("true"):       return TRUE;
            case hash_string("try"):        return TRY;
            case hash_string("typeof"):     return TYPEOF;
            case hash_string("var"):        return VAR;
            case hash_string("void"):       return VOID;
            case hash_string("while"):      return WHILE;
            case hash_string("with"):       return WITH;
            case hash_string("yield"):      return YIELD;
            default:                        return IDENTIFIER;
        }
    }

    void Lexer::scan_identifier(const uint32_t local_start) {
        current = local_start;

        std::string text;
        bool escaped = false;
        bool first = true;

        while (!is_end()) {
            const auto byte = static_cast<unsigned char>(peek());

            if (byte == '\\') {
                if (peek(1) != 'u') break;

                advance();
                advance();

                const auto unit = static_cast<char16_t>(scan_hex_escape(4));

                if (!(first ? is_identifier_start(unit) : is_identifier_part(unit))) {
                    throw std::runtime_error("invalid Unicode escape in identifier");
                }

                text += utf16_to_utf8(std::u16string(1, unit));
                escaped = true;
                first = false;
                continue;
            }

            if (byte < 0x80) {
                const auto unit = static_cast<char16_t>(byte);
                if (!(first ? is_identifier_start(unit) : is_identifier_part(unit))) break;

                text.push_back(advance());
                first = false;
                continue;
            }

            uint8_t length = 0;
            const uint32_t code = peek_code_point(length);

            if (code > 0xFFFF) break;

            const auto unit = static_cast<char16_t>(code);
            if (!(first ? is_identifier_start(unit) : is_identifier_part(unit))) break;

            for (uint8_t i = 0; i < length; ++i) text.push_back(advance());
            first = false;
        }

        const TOKEN_TYPE type = escaped ? IDENTIFIER : keyword_type(text);

        if (type == IDENTIFIER) {
            add_token(new Token(IDENTIFIER, local_start, text));
        } else {
            add_token(new Token(type, local_start, monostate()));
        }
    }

    void Lexer::scan_token() {
        uint32_t local_start = current;
        auto c = advance();
        auto next = peek();

        switch (c) {
            case '(': add_token(new Token(LPAREN, local_start, monostate())); break;
            case ')': add_token(new Token(RPAREN, local_start, monostate())); break;
            case '{': add_token(new Token(LBRACE, local_start, monostate())); break;
            case '}': add_token(new Token(RBRACE, local_start, monostate())); break;
            case '[': add_token(new Token(LBRACK, local_start, monostate())); break;
            case ']': add_token(new Token(RBRACK, local_start, monostate())); break;
            case '/': {
                if (next == '/') {
                    while (!is_end() && peek() != '\n' && peek() != '\r'
                           && !at_unicode_line_terminator()) {
                        advance();
                        }
                } else if (next == '*') {
                    advance();
                    while (true) {
                        if (is_end()) throw std::runtime_error("unterminated comment");

                        if (peek() == '*' && peek(1) == '/') {
                            advance(); advance();
                            break;
                        }
                        if (peek() == '\n' || peek() == '\r' || at_unicode_line_terminator()) {
                            line++;
                            pending_newline = true;
                        }
                        advance();
                    }
                } else if (regexp_allowed()) {
                    scan_regexp(local_start);
                } else if (next == '=') {
                    advance();
                    add_token(new Token(ASSIGN_DIV, local_start, monostate()));
                } else {
                    add_token(new Token(DIV, local_start, monostate()));
                }
            } break;
            case '*': {
                if (next == '*') {
                    auto third = peek(1);

                    if (third == '=') {
                        advance();
                        advance();
                        add_token(new Token(ASSIGN_EXP, local_start, monostate()));
                    } else {
                        advance();
                        add_token(new Token(EXP, local_start, monostate()));
                    }
                } else if (next == '=') {
                    advance();
                    add_token(new Token(ASSIGN_MUL, local_start, monostate()));
                } else {
                    add_token(new Token(MUL, local_start, monostate()));
                }
            } break;
            case '%': {
                if (next == '=') {
                    advance();
                    add_token(new Token(ASSIGN_MOD, local_start, monostate()));
                } else {
                    add_token(new Token(MOD, local_start, monostate()));
                }
            } break;
            case '+': {
                if (next == '=') {
                    advance();
                    add_token(new Token(ASSIGN_ADD, local_start, monostate()));
                } else if (next == '+') {
                    advance();
                    add_token(new Token(INCREMENT, local_start, monostate()));
                } else {
                    add_token(new Token(ADD, local_start, monostate()));
                }
            } break;
            case '-': {
                if (next == '=') {
                    advance();
                    add_token(new Token(ASSIGN_SUB, local_start, monostate()));
                } else if (next == '-') {
                    advance();
                    add_token(new Token(DECREMENT, local_start, monostate()));
                } else {
                    add_token(new Token(SUB, local_start, monostate()));
                }
            } break;
            case '<': {
                if (next == '=') {
                    advance();
                    add_token(new Token(LTE, local_start, monostate()));
                } else if (next == '<') {
                    auto third = peek(1);

                    if (third == '=') {
                        advance();
                        advance();
                        add_token(new Token(ASSIGN_SHL, local_start, monostate()));
                    } else {
                        advance();
                        add_token(new Token(SHL, local_start, monostate()));
                    }
                } else {
                    add_token(new Token(LT, local_start, monostate()));
                }
            } break;
            case '>': {
                if (next == '=') {
                    advance();
                    add_token(new Token(GTE, local_start, monostate()));
                } else if (next == '>') {
                    auto third = peek(1);
                    auto fourth = peek(2);

                    if (third == '>' && fourth == '=') {
                        advance();
                        advance();
                        advance();
                        add_token(new Token(ASSIGN_SHR, local_start, monostate()));
                    } else if (third == '>') {
                        advance();
                        advance();
                        add_token(new Token(SHR, local_start, monostate()));
                    } else if (third == '=') {
                        advance();
                        advance();
                        add_token(new Token(ASSIGN_SAR, local_start, monostate()));
                    } else {
                        advance();
                        add_token(new Token(SAR, local_start, monostate()));
                    }
                } else {
                    add_token(new Token(GT, local_start, monostate()));
                }
            } break;
            case '=': {
                if (next == '=') {
                    auto third = peek(1);

                    if (third == '=') {
                        advance();
                        advance();
                        add_token(new Token(EQ_STRICT, local_start, monostate()));
                    } else {
                        advance();
                        add_token(new Token(EQ, local_start, monostate()));
                    }
                } else if (next == '>') {
                    advance();
                    add_token(new Token(ARROW, local_start, monostate()));
                } else {
                    add_token(new Token(ASSIGN, local_start, monostate()));
                }
            } break;
            case '!': {
                if (next == '=') {
                    auto third = peek(1);

                    if (third == '=') {
                        advance();
                        advance();
                        add_token(new Token(NE_STRICT, local_start, monostate()));
                    } else {
                        advance();
                        add_token(new Token(NE, local_start, monostate()));
                    }
                } else {
                    add_token(new Token(NOT, local_start, monostate()));
                }
            } break;
            case '&': {

                if (next == '&') {
                    auto third = peek(1);

                    if (third == '=') {
                        advance();
                        advance();
                        add_token(new Token(ASSIGN_AND, local_start, monostate()));
                    } else {
                        advance();
                        add_token(new Token(AND, local_start, monostate()));
                    }
                } else if (next == '=') {
                    advance();
                    add_token(new Token(ASSIGN_BIT_AND, local_start, monostate()));
                } else {
                    add_token(new Token(BIT_AND, local_start, monostate()));
                }
            } break;
            case '|': {
                if (next == '|') {
                    auto third = peek(1);

                    if (third == '=') {
                        advance();
                        advance();
                        add_token(new Token(ASSIGN_OR, local_start, monostate()));
                    } else {
                        advance();
                        add_token(new Token(OR, local_start, monostate()));
                    }
                } else if (next == '=') {
                    advance();
                    add_token(new Token(ASSIGN_BIT_OR, local_start, monostate()));
                } else {
                    add_token(new Token(BIT_OR, local_start, monostate()));
                }
            } break;
            case '^': {
                if (next == '=') {
                    advance();
                    add_token(new Token(ASSIGN_BIT_XOR, local_start, monostate()));
                } else {
                    add_token(new Token(BIT_XOR, local_start, monostate()));
                }
            } break;
            case '~': {
                add_token(new Token(BIT_NOT, local_start, monostate()));
            } break;
            case '?': {
                if (next == '?') {
                    auto third = peek(1);

                    if (third == '=') {
                        advance();
                        advance();
                        add_token(new Token(ASSIGN_NULLISH, local_start, monostate()));
                    } else {
                        advance();
                        add_token(new Token(NULLISH, local_start, monostate()));
                    }
                } else if (next == '.') {
                    advance();
                    add_token(new Token(OPTIONAL, local_start, monostate()));
                } else {
                    add_token(new Token(CONDITIONAL, local_start, monostate()));
                }
            } break;
            case '.': {

                auto third = peek(1);

                if (next == '.' && third == '.') {
                    advance();
                    advance();
                    add_token(new Token(ELLIPSIS, local_start, monostate()));
                } else if (isdigit(static_cast<unsigned char>(next))) {
                    while (isdigit(static_cast<unsigned char>(peek())))
                        advance();

                    scan_exponent();
                    check_numeric_boundary();

                    const std::string number_str = source.substr(local_start, current - local_start);
                    add_token(new Token(NUMBER, local_start, std::strtod(number_str.c_str(), nullptr)));
                } else {
                    add_token(new Token(PERIOD, local_start, monostate()));
                }
            } break;
            case ',': {
                add_token(new Token(COMMA, local_start, monostate()));
            } break;
            case '`': {
                add_token(new Token(TEMPLATE, local_start, monostate()));
            } break;
            case ':': {
                add_token(new Token(COLON, local_start, monostate()));
            } break;
            case ';': {
                add_token(new Token(SEMICOLON, local_start, monostate()));
            } break;
            case '@': {
                add_token(new Token(AT, local_start, monostate()));
            } break;
            case ' ':
            case '\t':
            case '\f':
            case '\v': {} break;
            case '\r': {
                if (peek() == '\n') advance();
                line++;
                pending_newline = true;
            } break;
            case '\n': {
                line++;
                pending_newline = true;
            } break;
            case 'a' ... 'z':
            case 'A' ... 'Z':
            case '_':
            case '$': {
                scan_identifier(local_start);
            } break;
            case '\\': {
                if (peek() != 'u') throw std::runtime_error("unexpected character");

                scan_identifier(local_start);
            } break;
            case '0': case '1': case '2': case '3': case '4':
            case '5': case '6': case '7': case '8':
            case '9': {
                if (c == '0' && (peek() == 'x' || peek() == 'X')) {
                    advance();

                    if (hex_digit_value(peek()) < 0) {
                        throw std::runtime_error("missing digits after 0x");
                    }

                    double value = 0;
                    while (hex_digit_value(peek()) >= 0) {
                        value = value * 16 + hex_digit_value(advance());
                    }

                    check_numeric_boundary();

                    add_token(new Token(NUMBER, local_start, value));
                    break;
                }

                if (c == '0' && isdigit(static_cast<unsigned char>(peek()))) {
                    bool octal = true;
                    for (std::size_t at = current;
                         at < source.size() && isdigit(static_cast<unsigned char>(source[at])); ++at) {
                        if (source[at] > '7') { octal = false; break; }
                         }

                    if (octal) {
                        double value = 0;
                        while (peek() >= '0' && peek() <= '7') {
                            value = value * 8 + (advance() - '0');
                        }

                        check_numeric_boundary();

                        auto *token = new Token(NUMBER, local_start, value);
                        token->legacy_octal = true;
                        add_token(token);
                        break;
                    }

                    while (isdigit(static_cast<unsigned char>(peek()))) advance();

                    const std::string decimal = source.substr(local_start, current - local_start);

                    auto *token = new Token(NUMBER, local_start, std::strtod(decimal.c_str(), nullptr));
                    token->legacy_octal = true;
                    add_token(token);
                    break;
                }

                while (isdigit(static_cast<unsigned char>(peek()))) advance();

                if (peek() == '.') {

                    advance();
                    while (isdigit(static_cast<unsigned char>(peek()))) advance();
                }

                scan_exponent();
                check_numeric_boundary();

                const std::string number_str = source.substr(local_start, current - local_start);
                add_token(new Token(NUMBER, local_start, std::strtod(number_str.c_str(), nullptr)));
            } break;
            case '\'':
            case '\"': {
                const char quote = c;
                std::u16string literal;
                std::string chunk;
                bool octal_escape = false;

                const auto flush_chunk = [&]() {
                    if (chunk.empty()) return;
                    literal += utf8_to_utf16(chunk);
                    chunk.clear();
                };

                while (true) {
                    if (is_end())
                        throw std::runtime_error("unterminated string literal");

                    const char ch = peek();

                    if (ch == quote) { advance(); break; }

                    if (ch == '\n' || ch == '\r' || at_unicode_line_terminator()) {
                        throw std::runtime_error("unterminated string literal");
                    }

                    if (ch != '\\') { chunk.push_back(advance()); continue; }

                    flush_chunk();
                    advance();
                    if (is_end()) throw std::runtime_error("unterminated string literal");

                    if (at_unicode_line_terminator()) {
                        current += 3;
                        line++;
                        continue;
                    }

                    const char esc = advance();

                    switch (esc) {

                        case 'b':  literal.push_back('\b'); break;
                        case 't':  literal.push_back('\t'); break;
                        case 'n':  literal.push_back('\n'); break;
                        case 'v':  literal.push_back('\v'); break;
                        case 'f':  literal.push_back('\f'); break;
                        case 'r':  literal.push_back('\r'); break;
                        case '"':  literal.push_back('"');  break;
                        case '\'': literal.push_back('\''); break;
                        case '\\': literal.push_back('\\'); break;

                        case '\r':
                            if (peek() == '\n') advance();
                            line++;
                            break;
                        case '\n':
                            line++;
                            break;
                        case '0': case '1': case '2': case '3':
                        case '4': case '5': case '6': case '7': {
                            if (esc == '0' && !isdigit(static_cast<unsigned char>(peek()))) {
                                literal.push_back('\0');
                                break;
                            }

                            uint32_t value = esc - '0';
                            const int limit = esc <= '3' ? 3 : 2;

                            for (int digits = 1; digits < limit; ++digits) {
                                if (peek() < '0' || peek() > '7') break;

                                value = value * 8 + (advance() - '0');
                            }

                            literal.push_back(static_cast<char16_t>(value));
                            octal_escape = true;
                        } break;
                        case 'x': {
                            const uint32_t code = scan_hex_escape(2);
                            literal.push_back(static_cast<char16_t>(code));
                        } break;
                        case 'u': {
                            const uint32_t code = scan_hex_escape(4);
                            literal.push_back(static_cast<char16_t>(code));
                        } break;
                        default: literal.push_back(esc); break;
                    }
                }
                flush_chunk();

                auto *token = new Token(STRING, local_start, literal);
                token->legacy_octal = octal_escape;
                add_token(token);
            } break;
            default: {
                if (static_cast<unsigned char>(c) < 0x80) {
                    throw std::runtime_error("unexpected character");
                }

                current = local_start;

                uint8_t length = 0;
                const uint32_t code = peek_code_point(length);

                if (is_unicode_whitespace(code)) { current += length; break; }

                if (is_unicode_line_terminator(code)) {
                    current += length;
                    line++;
                    pending_newline = true;
                    break;
                }

                if (code <= 0xFFFF && is_identifier_start(static_cast<char16_t>(code))) {
                    scan_identifier(local_start);
                    break;
                }

                throw std::runtime_error("unexpected character");
            }
        }
    }

    void Lexer::validate_flags(const std::string_view flags) {
        std::string seen;
        for (const char f : flags) {
            if (std::string_view("gim").find(f) == std::string_view::npos)
                throw std::runtime_error("invalid regular expression flag");
            if (seen.find(f) != std::string::npos)
                throw std::runtime_error("duplicate regular expression flag");
            seen.push_back(f);
        }
    }

    void Lexer::scan_regexp(const uint32_t local_start) {
        std::string pattern;
        bool in_class = false;

        while (true) {
            if (is_end() || peek() == '\n' || peek() == '\r')
                throw std::runtime_error("unterminated regular expression literal");

            const char ch = peek();

            if (ch == '\\') {
                pattern.push_back(advance());
                if (is_end() || peek() == '\n')
                    throw std::runtime_error("unterminated regular expression literal");

                pattern.push_back(advance());
                continue;
            }

            if (ch == '[')  in_class = true;
            else if (ch == ']')  in_class = false;
            else if (ch == '/' && !in_class) { advance(); break; }

            pattern.push_back(advance());
        }

        std::string flags;

        while (isalnum(static_cast<unsigned char>(peek())) || peek() == '_' || peek() == '$')
            flags.push_back(advance());

        validate_flags(flags);
        add_token(new Token(REGEXP, local_start, RegExpValue{std::move(pattern), std::move(flags)}));
    }

    bool Lexer::regexp_allowed() const {
        if (tokens.empty()) return true;

        switch (tokens.back()->type) {
            case IDENTIFIER:
            case NUMBER:
            case STRING:
            case BIGINT:
            case RPAREN:
            case RBRACK:
            case INCREMENT:
            case DECREMENT:
            case THIS:
            case SUPER:
            case TRUE:
            case FALSE:
            case NULL_T:
            case TEMPLATE:
                return false;

            default:
                return true;
        }
    }

    vector<Token*> Lexer::scan_tokens() {
        while (!is_end()) {
            start = current;
            scan_token();
        }

        add_token(new Token(END_OF_FILE, current, monostate()));
        return tokens;
    }



    uint32_t Lexer::scan_hex_escape(const int count) {
        uint32_t value = 0;

        for (int i = 0; i < count; ++i) {
            int digit = hex_digit_value(peek());

            if (digit < 0) throw std::runtime_error("invalid hex escape sequence");

            value = value * 16 + static_cast<uint32_t>(digit);
            advance();
        }

        return value;
    }

    void Lexer::scan_exponent() {
        if (peek() != 'e' && peek() != 'E') return;

        advance();
        if (peek() == '+' || peek() == '-') advance();

        if (!isdigit(static_cast<unsigned char>(peek()))) {
            throw std::runtime_error("missing digits in exponent");
        }

        while (isdigit(static_cast<unsigned char>(peek())))
            advance();
    }

    void Lexer::check_numeric_boundary() const {
        const char c = peek();

        if (isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '$') {
            throw std::runtime_error("identifier cannot start immediately after a number");
        }
    }

}