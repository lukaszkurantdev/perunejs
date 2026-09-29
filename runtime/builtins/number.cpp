#include "runtime/builtins.h"

#include "memory/js_wrapper.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace perunejs {
    namespace {
        Completion this_number(Evaluator &evaluator, const JSValue &this_value, double &out) {
            if (this_value.type() == JSValueType::Number) {
                out = this_value.to_number();
                return Completion::empty();
            }

            if (this_value.type() == JSValueType::Object) {
                auto *wrapper = dynamic_cast<JSPrimitiveWrapper*>(this_value.as_object());

                if (wrapper != nullptr && std::strcmp(wrapper->label, "Number") == 0) {
                    out = wrapper->primitive.to_number();
                    return Completion::empty();
                }
            }

            return evaluator.throw_error(evaluator.type_error_prototype,
                "Number.prototype method called on incompatible receiver");
        }

        Completion optional_integer(Evaluator &evaluator, const std::vector<JSValue> &args,
                                    std::size_t index, bool &present, double &out) {
            present = argument_at(args, index).type() != JSValueType::Undefined;
            if (!present) return Completion::empty();

            Completion number = evaluator.to_number(args[index]);
            if (number.is_abrupt()) return number;

            const double raw = number.get_value_or_undefined().to_number();
            out = std::isnan(raw) ? 0 : std::trunc(raw);

            return Completion::empty();
        }

        bool special_form(double value, std::string &out) {
            if (std::isnan(value)) { out = "NaN"; return true; }

            if (std::isinf(value)) {
                out = value < 0 ? "-Infinity" : "Infinity";
                return true;
            }

            return false;
        }

        struct Decimal {
            std::string digits;
            int exponent = 0;
            bool zero = false;
        };

        Decimal exact_decimal(double value) {
            Decimal result;

            if (value == 0) { result.zero = true; return result; }


            std::string buffer(900, '\0');
            const int written = std::snprintf(buffer.data(), buffer.size(), "%.*e", 800, value);
            buffer.resize(written);

            const std::size_t marker = buffer.find('e');

            result.digits = buffer.substr(0, 1) + buffer.substr(2, marker - 2);
            result.exponent = std::atoi(buffer.c_str() + marker + 1) + 1;

            while (!result.digits.empty() && result.digits.back() == '0') result.digits.pop_back();
            if (result.digits.empty()) result.zero = true;

            return result;
        }

        void round_to(Decimal &number, int significant) {
            if (number.zero) return;

            if (significant < 0) { number.zero = true; return; }

            if (static_cast<std::size_t>(significant) >= number.digits.size()) {
                number.digits.append(significant - number.digits.size(), '0');
                return;
            }

            const bool round_up = number.digits[significant] >= '5';
            number.digits.resize(significant);

            if (!round_up) {
                if (significant == 0) number.zero = true;
                return;
            }

            int at = significant - 1;
            while (at >= 0) {
                if (number.digits[at] != '9') { ++number.digits[at]; return; }

                number.digits[at] = '0';
                --at;
            }

            number.digits.insert(number.digits.begin(), '1');
            number.digits.resize(significant == 0 ? 1 : significant);
            ++number.exponent;
        }

        int shortest_significant(double value) {
            char buffer[64];

            for (int digits = 1; digits < 17; ++digits) {
                std::snprintf(buffer, sizeof buffer, "%.*e", digits - 1, value);
                if (std::strtod(buffer, nullptr) == value) return digits;
            }

            return 17;
        }

        std::string fixed_text(const Decimal &number, int fraction) {
            if (number.zero) {
                return fraction > 0 ? "0." + std::string(fraction, '0') : "0";
            }

            std::string integer_part;
            std::string fraction_part;

            if (number.exponent <= 0) {
                integer_part = "0";
                fraction_part = std::string(-number.exponent, '0') + number.digits;
            } else if (static_cast<std::size_t>(number.exponent) >= number.digits.size()) {
                integer_part = number.digits
                             + std::string(number.exponent - number.digits.size(), '0');
            } else {
                integer_part = number.digits.substr(0, number.exponent);
                fraction_part = number.digits.substr(number.exponent);
            }

            fraction_part.resize(fraction, '0');

            return fraction > 0 ? integer_part + "." + fraction_part : integer_part;
        }

        std::string exponential_text(const Decimal &number) {
            const int power = number.zero ? 0 : number.exponent - 1;

            std::string mantissa = number.zero ? std::string("0") : number.digits;
            if (mantissa.size() > 1) mantissa.insert(1, ".");

            return mantissa + "e" + (power < 0 ? "-" : "+") + std::to_string(std::abs(power));
        }

        Completion native_number(Evaluator &evaluator, const JSValue&, const std::vector<JSValue> &args) {
            if (args.empty()) return Completion::normal(JSValue::number(0));

            return evaluator.to_number(args[0]);
        }

        Completion construct_number(Evaluator &evaluator, const JSValue&, const std::vector<JSValue> &args) {
            JSValue value = JSValue::number(0);

            if (!args.empty()) {
                Completion number = evaluator.to_number(args[0]);
                if (number.is_abrupt()) return number;

                value = number.get_value_or_undefined();
            }

            auto *wrapper = evaluator.heap.allocate<JSPrimitiveWrapper>(value, "Number");
            wrapper->prototype = evaluator.number_prototype;

            return Completion::normal(JSValue::object(wrapper));
        }

        std::string radix_text(double value, int radix) {
            static const char *DIGITS = "0123456789abcdefghijklmnopqrstuvwxyz";

            const bool negative = value < 0;
            if (negative) value = -value;

            double integer = std::floor(value);
            double fraction = value - integer;

            double delta = 0.5 * (std::nextafter(value, std::numeric_limits<double>::infinity()) - value);
            delta = std::max(std::nextafter(0.0, 1.0), delta);

            std::string fraction_text;

            if (fraction >= delta) {
                fraction_text = ".";

                do {
                    fraction *= radix;
                    delta *= radix;

                    int digit = static_cast<int>(fraction);
                    fraction -= digit;

                    if (fraction > 0.5 || (fraction == 0.5 && (digit & 1))) {
                        if (fraction + delta > 1) {
                            ++digit;

                            while (digit == radix) {
                                digit = 0;

                                if (fraction_text.size() == 1) { integer += 1; break; }

                                digit = fraction_text.back() - (fraction_text.back() > '9'
                                      ? 'a' - 10 : '0');
                                fraction_text.pop_back();
                                ++digit;
                            }

                            fraction_text += DIGITS[digit];
                            break;
                        }
                    }

                    fraction_text += DIGITS[digit];
                } while (fraction >= delta);
            }

            std::string integer_text;

            if (integer == 0) {
                integer_text = "0";
            } else {
                while (integer / radix >= 9007199254740992.0) {
                    integer /= radix;
                    integer_text += '0';
                }

                while (integer > 0) {
                    const double remainder = std::fmod(integer, radix);
                    integer_text += DIGITS[static_cast<int>(remainder)];
                    integer = (integer - remainder) / radix;
                }

                std::reverse(integer_text.begin(), integer_text.end());
            }

            return (negative ? "-" : "") + integer_text + fraction_text;
        }

        Completion number_to_string(Evaluator &evaluator, const JSValue &this_value,
                                    const std::vector<JSValue> &args) {
            double value = 0;
            if (Completion c = this_number(evaluator, this_value, value); c.is_abrupt()) return c;

            double radix = 10;
            bool present = false;
            if (Completion c = optional_integer(evaluator, args, 0, present, radix); c.is_abrupt()) return c;

            if (present && (radix < 2 || radix > 36)) {
                return evaluator.throw_error(evaluator.range_error_prototype,
                    "toString() radix argument must be between 2 and 36");
            }

            if (!present || radix == 10) {
                return evaluator.to_string(JSValue::number(value));
            }

            std::string special;
            if (special_form(value, special)) return Completion::normal(JSValue::string(special));

            return Completion::normal(JSValue::string(radix_text(value, static_cast<int>(radix))));
        }

        Completion number_value_of(Evaluator &evaluator, const JSValue &this_value,
                                   const std::vector<JSValue>&) {
            double value = 0;
            if (Completion c = this_number(evaluator, this_value, value); c.is_abrupt()) return c;

            return Completion::normal(JSValue::number(value));
        }

        Completion number_to_fixed(Evaluator &evaluator, const JSValue &this_value,
                                   const std::vector<JSValue> &args) {
            double value = 0;
            if (Completion c = this_number(evaluator, this_value, value); c.is_abrupt()) return c;

            double fraction = 0;
            bool present = false;
            if (Completion c = optional_integer(evaluator, args, 0, present, fraction); c.is_abrupt()) return c;

            if (fraction < 0 || fraction > 20) {
                return evaluator.throw_error(evaluator.range_error_prototype,
                    "toFixed() digits argument must be between 0 and 20");
            }

            std::string special;
            if (special_form(value, special)) return Completion::normal(JSValue::string(special));

            if (std::abs(value) >= 1e21) {
                return evaluator.to_string(JSValue::number(value));
            }

            const bool negative = value < 0;
            Decimal number = exact_decimal(std::abs(value));

            round_to(number, number.exponent + static_cast<int>(fraction));

            const std::string text = fixed_text(number, static_cast<int>(fraction));

            return Completion::normal(JSValue::string(negative ? "-" + text : text));
        }

        Completion number_to_exponential(Evaluator &evaluator, const JSValue &this_value,
                                         const std::vector<JSValue> &args) {
            double value = 0;
            if (Completion c = this_number(evaluator, this_value, value); c.is_abrupt()) return c;

            double fraction = 0;
            bool present = false;
            if (Completion c = optional_integer(evaluator, args, 0, present, fraction); c.is_abrupt()) return c;

            std::string special;
            if (special_form(value, special)) return Completion::normal(JSValue::string(special));

            if (present && (fraction < 0 || fraction > 20)) {
                return evaluator.throw_error(evaluator.range_error_prototype,
                    "toExponential() argument must be between 0 and 20");
            }

            const bool negative = value < 0;
            Decimal number = exact_decimal(std::abs(value));

            if (present) {
                round_to(number, static_cast<int>(fraction) + 1);
            } else if (!number.zero) {
                round_to(number, shortest_significant(std::abs(value)));
            }

            if (number.zero && present) {
                number.digits = std::string(static_cast<std::size_t>(fraction) + 1, '0');
                number.exponent = 1;
                number.zero = false;
            }

            const std::string text = exponential_text(number);

            return Completion::normal(JSValue::string(negative ? "-" + text : text));
        }

        Completion number_to_precision(Evaluator &evaluator, const JSValue &this_value,
                                       const std::vector<JSValue> &args) {
            double value = 0;
            if (Completion c = this_number(evaluator, this_value, value); c.is_abrupt()) return c;

            double precision = 0;
            bool present = false;
            if (Completion c = optional_integer(evaluator, args, 0, present, precision); c.is_abrupt()) return c;

            if (!present) return evaluator.to_string(JSValue::number(value));

            std::string special;
            if (special_form(value, special)) return Completion::normal(JSValue::string(special));

            if (precision < 1 || precision > 21) {
                return evaluator.throw_error(evaluator.range_error_prototype,
                    "toPrecision() argument must be between 1 and 21");
            }

            const bool negative = value < 0;
            const int digits = static_cast<int>(precision);

            Decimal number = exact_decimal(std::abs(value));
            round_to(number, digits);

            if (number.zero) {
                number.digits = std::string(digits, '0');
                number.exponent = 1;
                number.zero = false;
            }


            const int power = number.exponent - 1;
            const std::string text = (power < -6 || power >= digits)
                                   ? exponential_text(number)
                                   : fixed_text(number, digits - number.exponent);

            return Completion::normal(JSValue::string(negative ? "-" + text : text));
        }

        Completion number_to_locale_string(Evaluator &evaluator, const JSValue &this_value,
                                           const std::vector<JSValue>&) {
            double value = 0;
            if (Completion c = this_number(evaluator, this_value, value); c.is_abrupt()) return c;

            return evaluator.to_string(JSValue::number(value));
        }
    }

    void install_number(const Builtins &b) {
        JSObject *prototype = b.evaluator.number_prototype;

        JSObject *number_constructor =
            b.constructor("Number", native_number, prototype, construct_number);


        b.constant(number_constructor, "MAX_VALUE",
            JSValue::number(std::numeric_limits<double>::max()));
        b.constant(number_constructor, "MIN_VALUE",
            JSValue::number(std::numeric_limits<double>::denorm_min()));
        b.constant(number_constructor, "NaN", JSValue::number(std::nan("")));
        b.constant(number_constructor, "POSITIVE_INFINITY",
            JSValue::number(std::numeric_limits<double>::infinity()));
        b.constant(number_constructor, "NEGATIVE_INFINITY",
            JSValue::number(-std::numeric_limits<double>::infinity()));

        b.method(prototype, "toString",       number_to_string, 1);
        b.method(prototype, "toLocaleString", number_to_locale_string);
        b.method(prototype, "valueOf",        number_value_of);
        b.method(prototype, "toFixed",        number_to_fixed, 1);
        b.method(prototype, "toExponential",  number_to_exponential, 1);
        b.method(prototype, "toPrecision",    number_to_precision, 1);
    }
}