#include "js_value.h"

#include <charconv>
#include <cstdio>
#include <cstdlib>

#include "js_object.h"
#include "nodes.h"

namespace perunejs {
    constexpr double TWO_32 = 4294967296.0;   // 2^32
    constexpr double TWO_31 = 2147483648.0;   // 2^31

    namespace {
        [[noreturn]] void primitive_expected(const char *who) {
            throw std::logic_error(
                std::string(who) + ": wywołane na obiekcie. To jest błąd wywołującego, "
                "nie błąd JavaScriptu — obiekt musi najpierw przejść przez "
                "Evaluator::to_primitive / to_number / to_string.");
        }
    }

    JSValue JSValue::undefined() {
        return {Undefined()};
    }

    JSValue JSValue::null() {
        return {Null()};
    }

    JSValue JSValue::boolean(bool value) {
        return {value};
    }

    JSValue JSValue::number(double value) {
        return {value};
    }

    JSValue JSValue::string(std::u16string value) { return JSValue(std::move(value)); }
    JSValue JSValue::string(std::string_view utf8) { return JSValue(utf8_to_utf16(utf8)); }

    JSValue JSValue::object(JSObject *value) {
        return {value};
    }

    JSValue JSValue::symbol(const JSSymbolData *value) {
        return {value};
    }

    const JSSymbolData *JSValue::as_symbol() const {
        return holds_alternative<const JSSymbolData *>(value)
            ? get<const JSSymbolData *>(value)
            : nullptr;
    }

    bool JSValue::to_boolean() const {
        switch (this->type()) {
            case JSValueType::Symbol:
                return true;
            case JSValueType::Undefined:
                return false;
            case JSValueType::Null:
                return false;
            case JSValueType::Boolean:
                return get<bool>(this->value);
            case JSValueType::Number: {
                const double n = get<double>(this->value);
                return !(n == 0 || std::isnan(n));
            }
            case JSValueType::String: {
                return !get<std::u16string>(this->value).empty();
            }
            case JSValueType::Object: return true;
        }

        return false;
    }

    static double string_to_number(std::string_view s) {
        const double nan = std::numeric_limits<double>::quiet_NaN();
        const double inf = std::numeric_limits<double>::infinity();

        const auto is_ws = [](const char c) {
            switch (c) {
                case ' ': case '\t': case '\n': case '\v': case '\f': case '\r': return true;
                default: return false;
            }
        };
        while (!s.empty() && is_ws(s.front())) s.remove_prefix(1);
        while (!s.empty() && is_ws(s.back()))  s.remove_suffix(1);

        if (s.empty()) return 0.0;

        if (s.size() >= 2 && s[0] == '0') {
            int radix = 0;
            switch (s[1]) {
                case 'x': case 'X': radix = 16; break;
                case 'o': case 'O': radix = 8;  break;
                case 'b': case 'B': radix = 2;  break;
                default: break;
            }
            if (radix != 0) {
                const std::string_view body = s.substr(2);
                if (body.empty()) return nan;

                double result = 0;
                for (const char c : body) {
                    int digit;
                    if      (c >= '0' && c <= '9') digit = c - '0';
                    else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
                    else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
                    else return nan;
                    if (digit >= radix) return nan;
                    result = result * radix + digit;
                }
                return result;
            }
        }

        double sign = 1.0;
        if (s.front() == '+' || s.front() == '-') {
            if (s.front() == '-') sign = -1.0;
            s.remove_prefix(1);
            if (s.empty()) return nan;
        }

        if (s == "Infinity") return sign * inf;

        if (!(s.front() >= '0' && s.front() <= '9') && s.front() != '.')
            return nan;

        if (s.size() >= 2 && s[0] == '0' &&
            (s[1]=='x'||s[1]=='X'||s[1]=='o'||s[1]=='O'||s[1]=='b'||s[1]=='B'))
            return nan;

        const std::string text(s);
        char* end = nullptr;
        const double result = std::strtod(text.c_str(), &end);

        if (end != text.c_str() + text.size()) return nan;
        return sign * result;
    }

    double JSValue::to_number() const {
        switch (this->type()) {
            case JSValueType::Symbol:
                return std::numeric_limits<double>::quiet_NaN();
            case JSValueType::Undefined:
                return std::numeric_limits<double>::quiet_NaN();
            case JSValueType::Null:
                return 0.0;
            case JSValueType::Boolean:
                return get<bool>(this->value) ? 1 : 0;
            case JSValueType::Number:
                return get<double>(this->value);
            case JSValueType::String: {
                return string_to_number(to_string());
            }
            case JSValueType::Object:
                primitive_expected("JSValue::to_number");
        }
        return 0;
    }

    std::string number_to_string(double x) {
        if (std::isnan(x)) return "NaN";
        if (x == 0)        return "0";
        if (x < 0)         return "-" + number_to_string(-x);
        if (std::isinf(x)) return "Infinity";

        std::array<char, 40> buf{};
        int written = 0;
        for (int precision = 0; precision <= 16; ++precision) {
            written = std::snprintf(buf.data(), buf.size(), "%.*e", precision, x);
            if (std::strtod(buf.data(), nullptr) == x) break;
        }
        const std::string_view sci(buf.data(), static_cast<size_t>(written));

        const size_t e_pos = sci.find('e');

        std::string digits(sci.substr(0, e_pos));
        if (const size_t dot = digits.find('.'); dot != std::string::npos)
            digits.erase(dot, 1);

        const char* exp_begin = sci.data() + e_pos + 1;
        const bool exp_negative = (*exp_begin == '-');
        if (*exp_begin == '+' || *exp_begin == '-') ++exp_begin;

        int exponent = 0;
        std::from_chars(exp_begin, sci.data() + sci.size(), exponent);
        if (exp_negative) exponent = -exponent;

        const int k = static_cast<int>(digits.size());
        const int n = exponent + 1;

        if (k <= n && n <= 21)  // 123 -> "123", 1e20 -> "100000..."
            return digits + std::string(static_cast<size_t>(n - k), '0');

        if (0 < n && n <= 21) // 3.5 -> "3.5"
            return digits.substr(0, static_cast<size_t>(n)) + "." +
                   digits.substr(static_cast<size_t>(n));

        if (-6 < n && n <= 0)  // 0.1 -> "0.1"
            return "0." + std::string(static_cast<size_t>(-n), '0') + digits;

        const int e = n - 1;
        const std::string mantissa = (k == 1)
            ? digits
            : digits.substr(0, 1) + "." + digits.substr(1);

        return mantissa + "e" + (e < 0 ? "-" : "+") + std::to_string(std::abs(e));
    }

    std::string JSValue::to_string() const {
        switch (this->type()) {
            case JSValueType::Symbol:
                return get<const JSSymbolData *>(this->value)->uid;
            case JSValueType::Undefined:
                return "undefined";
            case JSValueType::Null:
                return "null";
            case JSValueType::Boolean:
                return get<bool>(this->value) ? "true" : "false";
            case JSValueType::Number:
                return number_to_string(get<double>(this->value));
            case JSValueType::String: {
                return utf16_to_utf8(get<std::u16string>(this->value));
            }
            case JSValueType::Object:
                primitive_expected("JSValue::to_string");
        }

        return "";
    }

    std::u16string JSValue::to_u16string() const {
        if (type() == JSValueType::String) return get<std::u16string>(value);

        return utf8_to_utf16(to_string());
    }

    JSValue JSValue::to_numeric() const {
        return {this->to_number()};
    }

    double to_uint32_bits(double number) {
        if (std::isnan(number) || std::isinf(number) || number == 0) return 0;

        double pos_int = std::trunc(number);
        double int32_bit = std::fmod(pos_int, TWO_32);
        if (int32_bit < 0) int32_bit += TWO_32;
        return int32_bit;
    }

    int32_t JSValue::to_int32() const {
        double int32_bit = to_uint32_bits(to_number());

        if (int32_bit >= TWO_31) return static_cast<int32_t>(int32_bit - TWO_32);

        return static_cast<int32_t>(int32_bit);
    }

    uint32_t JSValue::to_uint32() const {
        return static_cast<uint32_t>(to_uint32_bits(to_number()));
    }

    std::string JSValue::get_type() const {
        switch (this->type()) {
            case JSValueType::Undefined: return "undefined";
            case JSValueType::Null: return "object";
            case JSValueType::Number: return "number";
            case JSValueType::String: return "string";
            case JSValueType::Boolean: return "boolean";
            case JSValueType::Object: return this->as_object()->is_callable() ? "function" : "object";
            case JSValueType::Symbol: return "symbol";
        }
        throw std::logic_error("JSValue::get_type(): type not supported yet");
    }

    JSObject * JSValue::as_object() const {
        if (this->type() == JSValueType::Object) {
            return get<JSObject*>(this->value);
        }

        return nullptr;
    }

    bool JSValue::is_loosely_equal(const JSValue &x, const JSValue &y) {
        auto x_type = x.type();
        auto y_type = y.type();

        if (x_type == y_type) {
            return is_strictly_equal(x, y);
        }

        if ((x_type == JSValueType::Undefined && y_type == JSValueType::Null)
            || (x_type == JSValueType::Null && y_type == JSValueType::Undefined)) {
            return true;
            }

        if (x_type == JSValueType::Number && y_type == JSValueType::String) {
            return is_loosely_equal(x, JSValue::number(y.to_number()));
        }

        if (x_type == JSValueType::String && y_type == JSValueType::Number) {
            return is_loosely_equal(JSValue::number(x.to_number()), y);
        }

        if (x_type == JSValueType::Boolean) {
            return is_loosely_equal(JSValue::number(x.to_number()), y);
        }

        if (y_type == JSValueType::Boolean) {
            return is_loosely_equal(x, JSValue::number(y.to_number()));
        }

        if ((x_type == JSValueType::String || x_type == JSValueType::Number) && y_type == JSValueType::Object) {
            primitive_expected("JSValue::is_loosely_equal");
        }

        if ((y_type == JSValueType::String || y_type == JSValueType::Number) && x_type == JSValueType::Object) {
            primitive_expected("JSValue::is_loosely_equal");
        }

        return false;
    }

    bool JSValue::is_strictly_equal(const JSValue &x, const JSValue &y) {
        if (x.type() != y.type()) {
            return false;
        }

        switch (x.type()) {
            case JSValueType::Undefined:
                return true;
            case JSValueType::Null:
                return true;
            case JSValueType::Boolean:
                return get<bool>(x.value) ==  get<bool>(y.value);
            case JSValueType::Number:
                return get<double>(x.value) ==  get<double>(y.value);
            case JSValueType::String: {
                return get<std::u16string>(x.value) == get<std::u16string>(y.value);
            }
            case JSValueType::Object: {
                return get<JSObject*>(x.value) == get<JSObject*>(y.value);
            }
            case JSValueType::Symbol: {
                return get<const JSSymbolData*>(x.value) == get<const JSSymbolData*>(y.value);
            }
        }

        throw std::logic_error("JSValue::is_strictly_equal: type not supported yet");
    }

    bool JSValue::same_value(const JSValue &x, const JSValue &y) {
        if (x.type() != y.type()) return false;

        if (x.type() == JSValueType::Number) {
            const double a = get<double>(x.value);
            const double b = get<double>(y.value);

            if (std::isnan(a) && std::isnan(b)) return true;
            if (a == 0 && b == 0) return std::signbit(a) == std::signbit(b);
            return a == b;
        }

        return is_strictly_equal(x, y);
    }

    std::optional<bool> JSValue::is_less_than(const JSValue &px, const JSValue &py) {
        if (px.type() == JSValueType::Object || py.type() == JSValueType::Object) {
            primitive_expected("JSValue::is_less_than");
        }

        if (px.type() == JSValueType::String && py.type() == JSValueType::String) {
            return px.to_u16string() < py.to_u16string();
        }

        double nx = px.to_number();
        double ny = py.to_number();

        if (std::isnan(nx) || std::isnan(ny)) return std::nullopt;

        return nx < ny;
    }

    JSValue JSValue::apply_string_or_numeric_binary_operator(
        const JSValue& lval,
        BinaryOperator op,
        const JSValue& rval) {

        if (op == BinaryOperator::Add) {
            if (lval.type() == JSValueType::String || rval.type() == JSValueType::String) {
                return JSValue::string(lval.to_u16string() + rval.to_u16string());
            }
        }

        auto l_num = lval.to_numeric();
        auto r_num = rval.to_numeric();

        if (l_num.type() != r_num.type()) {
            throw std::runtime_error("Operator error");
        }

        auto l_num_value = lval.to_number();
        auto r_num_value = rval.to_number();

        switch (op) {
            case BinaryOperator::Add:
                return JSValue::number(l_num_value + r_num_value);
            case BinaryOperator::Sub:
                return JSValue::number(l_num_value - r_num_value);
            case BinaryOperator::Mul:
                return JSValue::number(l_num_value * r_num_value);
            case BinaryOperator::Div:
                return JSValue::number(l_num_value / r_num_value);
            case BinaryOperator::Mod:
                return JSValue::number(std::fmod(l_num_value, r_num_value));
            case BinaryOperator::BitAnd:
                return JSValue::number(lval.to_int32() & rval.to_int32());
            case BinaryOperator::BitOr:
                return JSValue::number(lval.to_int32() | rval.to_int32());
            case BinaryOperator::BitXor:
                return JSValue::number(lval.to_int32() ^ rval.to_int32());
            case BinaryOperator::Sar:
                return JSValue::number(lval.to_int32() >> (rval.to_uint32() & 0x1F));
            case BinaryOperator::Shr:
                return JSValue::number(lval.to_uint32() >> (rval.to_uint32() & 0x1F));
            case BinaryOperator::Shl:
                return JSValue::number(lval.to_int32() << (rval.to_uint32() & 0x1F));
            default:
                break;
        }

        return JSValue::undefined();
    }
}