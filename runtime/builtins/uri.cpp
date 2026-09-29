#include "runtime/builtins.h"

#include "utils/utf.h"

namespace perunejs {
    namespace {
        constexpr const char16_t *URI_KEEP           = u"-_.!~*'();/?:@&=+$,#";
        constexpr const char16_t *URI_COMPONENT_KEEP = u"-_.!~*'()";
        constexpr const char16_t *URI_PRESERVE           = u";/?:@&=+$,#";
        constexpr const char16_t *URI_COMPONENT_PRESERVE = u"";

        bool in_set(char16_t unit, const char16_t *set) {
            for (const char16_t *at = set; *at != 0; ++at) {
                if (*at == unit) return true;
            }

            return false;
        }

        bool is_alpha_numeric(char16_t unit) {
            return (unit >= u'a' && unit <= u'z')
                || (unit >= u'A' && unit <= u'Z')
                || (unit >= u'0' && unit <= u'9');
        }

        int hex_value(char16_t unit) {
            if (unit >= u'0' && unit <= u'9') return unit - u'0';
            if (unit >= u'a' && unit <= u'f') return unit - u'a' + 10;
            if (unit >= u'A' && unit <= u'F') return unit - u'A' + 10;

            return -1;
        }

        void append_percent(std::u16string &out, unsigned char byte) {
            static const char16_t *DIGITS = u"0123456789ABCDEF";

            out += u'%';
            out += DIGITS[byte >> 4];
            out += DIGITS[byte & 0x0F];
        }

        Completion encode(Evaluator &evaluator, const JSValue &value, const char16_t *keep) {
            Completion text = evaluator.to_string(value);
            if (text.is_abrupt()) return text;

            const std::u16string input = text.get_value_or_undefined().to_u16string();
            std::u16string result;

            for (std::size_t at = 0; at < input.size(); ++at) {
                const char16_t unit = input[at];

                if (is_alpha_numeric(unit) || in_set(unit, keep)) {
                    result += unit;
                    continue;
                }

                uint32_t code = unit;
                if (is_low_surrogate(unit)) {
                    return evaluator.throw_error(evaluator.uri_error_prototype, "URI malformed");
                }

                if (is_high_surrogate(unit)) {
                    if (at + 1 >= input.size() || !is_low_surrogate(input[at + 1])) {
                        return evaluator.throw_error(evaluator.uri_error_prototype, "URI malformed");
                    }

                    code = 0x10000u + ((static_cast<uint32_t>(unit) - 0xD800u) << 10)
                                    + (static_cast<uint32_t>(input[at + 1]) - 0xDC00u);
                    ++at;
                }

                if (code < 0x80) {
                    append_percent(result, static_cast<unsigned char>(code));
                } else if (code < 0x800) {
                    append_percent(result, static_cast<unsigned char>(0xC0 | (code >> 6)));
                    append_percent(result, static_cast<unsigned char>(0x80 | (code & 0x3F)));
                } else if (code < 0x10000) {
                    append_percent(result, static_cast<unsigned char>(0xE0 | (code >> 12)));
                    append_percent(result, static_cast<unsigned char>(0x80 | ((code >> 6) & 0x3F)));
                    append_percent(result, static_cast<unsigned char>(0x80 | (code & 0x3F)));
                } else {
                    append_percent(result, static_cast<unsigned char>(0xF0 | (code >> 18)));
                    append_percent(result, static_cast<unsigned char>(0x80 | ((code >> 12) & 0x3F)));
                    append_percent(result, static_cast<unsigned char>(0x80 | ((code >> 6) & 0x3F)));
                    append_percent(result, static_cast<unsigned char>(0x80 | (code & 0x3F)));
                }
            }

            return Completion::normal(JSValue::string(result));
        }

        Completion decode(Evaluator &evaluator, const JSValue &value, const char16_t *preserve) {
            Completion text = evaluator.to_string(value);
            if (text.is_abrupt()) return text;

            const std::u16string input = text.get_value_or_undefined().to_u16string();
            std::u16string result;

            const auto malformed = [&] {
                return evaluator.throw_error(evaluator.uri_error_prototype, "URI malformed");
            };

            for (std::size_t at = 0; at < input.size(); ++at) {
                if (input[at] != u'%') {
                    result += input[at];
                    continue;
                }

                if (at + 2 >= input.size()) return malformed();

                const int high = hex_value(input[at + 1]);
                const int low  = hex_value(input[at + 2]);
                if (high < 0 || low < 0) return malformed();

                const std::size_t start = at;
                const unsigned char first = static_cast<unsigned char>(high * 16 + low);
                at += 2;

                if (first < 0x80) {
                    if (in_set(static_cast<char16_t>(first), preserve)) {
                        result += input.substr(start, 3);
                    } else {
                        result += static_cast<char16_t>(first);
                    }

                    continue;
                }

                int length = 0;
                for (unsigned char mask = 0x80; (first & mask) != 0; mask >>= 1) ++length;

                if (length < 2 || length > 4) return malformed();

                uint32_t code = first & (0x7F >> length);

                for (int i = 1; i < length; ++i) {
                    if (at + 3 >= input.size() || input[at + 1] != u'%') return malformed();

                    const int next_high = hex_value(input[at + 2]);
                    const int next_low  = hex_value(input[at + 3]);
                    if (next_high < 0 || next_low < 0) return malformed();

                    const unsigned char byte = static_cast<unsigned char>(next_high * 16 + next_low);
                    if ((byte & 0xC0) != 0x80) return malformed();

                    code = (code << 6) | (byte & 0x3F);
                    at += 3;
                }

                if (code > 0x10FFFF) return malformed();
                if (length == 2 && code < 0x80) return malformed();
                if (length == 3 && code < 0x800) return malformed();
                if (length == 4 && code < 0x10000) return malformed();
                if (code >= 0xD800 && code <= 0xDFFF) return malformed();

                if (code < 0x10000) {
                    result += static_cast<char16_t>(code);
                } else {
                    code -= 0x10000;
                    result += static_cast<char16_t>(0xD800 + (code >> 10));
                    result += static_cast<char16_t>(0xDC00 + (code & 0x3FF));
                }
            }

            return Completion::normal(JSValue::string(result));
        }

        Completion native_encode_uri(Evaluator &e, const JSValue&, const std::vector<JSValue> &args) {
            return encode(e, argument_at(args, 0), URI_KEEP);
        }
        Completion native_encode_uri_component(Evaluator &e, const JSValue&, const std::vector<JSValue> &args) {
            return encode(e, argument_at(args, 0), URI_COMPONENT_KEEP);
        }
        Completion native_decode_uri(Evaluator &e, const JSValue&, const std::vector<JSValue> &args) {
            return decode(e, argument_at(args, 0), URI_PRESERVE);
        }
        Completion native_decode_uri_component(Evaluator &e, const JSValue&, const std::vector<JSValue> &args) {
            return decode(e, argument_at(args, 0), URI_COMPONENT_PRESERVE);
        }
    }

    void install_uri(const Builtins &b) {
        b.global_function("encodeURI",
            JSValue::object(b.function("encodeURI", native_encode_uri, 1)));
        b.global_function("encodeURIComponent",
            JSValue::object(b.function("encodeURIComponent", native_encode_uri_component, 1)));
        b.global_function("decodeURI",
            JSValue::object(b.function("decodeURI", native_decode_uri, 1)));
        b.global_function("decodeURIComponent",
            JSValue::object(b.function("decodeURIComponent", native_decode_uri_component, 1)));
    }
}