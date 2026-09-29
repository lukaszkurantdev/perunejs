#include "runtime/builtins.h"

#include <cmath>
#include <cstdlib>
#include <limits>

namespace perunejs {
    namespace {
        Completion native_queue_microtask(Evaluator &evaluator, const JSValue&,
                                          const std::vector<JSValue> &args) {
            const JSValue job = argument_at(args, 0);

            if (job.type() != JSValueType::Object || !job.as_object()->is_callable()) {
                return evaluator.throw_error(evaluator.type_error_prototype,
                    "queueMicrotask expects a function");
            }

            evaluator.enqueue_microtask(job.as_object());

            return Completion::normal(JSValue::undefined());
        }

        Completion native_print(Evaluator &evaluator, const JSValue&, const std::vector<JSValue> &args) {
            for (std::size_t i = 0; i < args.size(); ++i) {
                if (i > 0) evaluator.output << ' ';

                Completion text = evaluator.to_string(args[i]);
                if (text.is_abrupt()) return text;

                evaluator.output << text.get_value_or_undefined().to_string();
            }

            evaluator.output << '\n';
            return Completion::normal(JSValue::undefined());
        }

        Completion native_eval(Evaluator &evaluator, const JSValue&, const std::vector<JSValue> &args) {
            return evaluator.eval_code(argument_at(args, 0), evaluator.global_env, /*keep_this*/ false);
        }

        JSValue nan_value() {
            return JSValue::number(std::numeric_limits<double>::quiet_NaN());
        }

        int digit_value(char16_t unit) {
            if (unit >= u'0' && unit <= u'9') return unit - u'0';
            if (unit >= u'a' && unit <= u'z') return unit - u'a' + 10;
            if (unit >= u'A' && unit <= u'Z') return unit - u'A' + 10;

            return -1;
        }

        std::size_t skip_whitespace(const std::u16string &text) {
            std::size_t at = 0;
            while (at < text.size() && is_js_whitespace(text[at])) ++at;

            return at;
        }

        Completion native_parse_int(Evaluator &evaluator, const JSValue&,
                                    const std::vector<JSValue> &args) {
            Completion text = evaluator.to_string(argument_at(args, 0));
            if (text.is_abrupt()) return text;

            const std::u16string input = text.get_value_or_undefined().to_u16string();
            std::size_t at = skip_whitespace(input);

            double sign = 1;
            if (at < input.size() && (input[at] == u'+' || input[at] == u'-')) {
                if (input[at] == u'-') sign = -1;
                ++at;
            }

            Completion radix_value = evaluator.to_number(argument_at(args, 1));
            if (radix_value.is_abrupt()) return radix_value;

            int32_t radix = radix_value.get_value_or_undefined().to_int32();
            bool strip_prefix = true;

            if (radix != 0) {
                if (radix < 2 || radix > 36) return Completion::normal(nan_value());

                if (radix != 16) strip_prefix = false;
            } else {
                radix = 10;
            }

            if (strip_prefix && at + 1 < input.size() && input[at] == u'0'
                && (input[at + 1] == u'x' || input[at + 1] == u'X')) {
                at += 2;
                radix = 16;
            }

            const std::size_t start = at;
            while (at < input.size()) {
                const int digit = digit_value(input[at]);
                if (digit < 0 || digit >= radix) break;

                ++at;
            }

            if (at == start) return Completion::normal(nan_value());

            std::string digits;
            digits.reserve(at - start);
            for (std::size_t i = start; i < at; ++i) digits += static_cast<char>(input[i]);

            double value = 0;
            if (radix == 10) {
                value = std::strtod(digits.c_str(), nullptr);
            } else {
                for (const char digit : digits) value = value * radix + digit_value(digit);
            }

            return Completion::normal(JSValue::number(sign * value));
        }

        std::size_t decimal_prefix(const std::u16string &text, std::size_t at) {
            const std::size_t begin = at;

            if (at < text.size() && (text[at] == u'+' || text[at] == u'-')) ++at;

            if (text.compare(at, 8, u"Infinity") == 0) return at + 8 - begin;

            const auto digits = [&] {
                const std::size_t from = at;
                while (at < text.size() && text[at] >= u'0' && text[at] <= u'9') ++at;

                return at - from;
            };

            const std::size_t whole = digits();

            if (whole > 0) {
                if (at < text.size() && text[at] == u'.') { ++at; digits(); }
            } else {
                if (at >= text.size() || text[at] != u'.') return 0;

                ++at;
                if (digits() == 0) return 0;
            }

            if (at < text.size() && (text[at] == u'e' || text[at] == u'E')) {
                const std::size_t before = at;
                ++at;

                if (at < text.size() && (text[at] == u'+' || text[at] == u'-')) ++at;
                if (digits() == 0) at = before;
            }

            return at - begin;
        }

        Completion native_parse_float(Evaluator &evaluator, const JSValue&,
                                      const std::vector<JSValue> &args) {
            Completion text = evaluator.to_string(argument_at(args, 0));
            if (text.is_abrupt()) return text;

            const std::u16string input = text.get_value_or_undefined().to_u16string();
            const std::size_t at = skip_whitespace(input);

            const std::size_t length = decimal_prefix(input, at);
            if (length == 0) return Completion::normal(nan_value());

            std::string digits;
            digits.reserve(length);
            for (std::size_t i = at; i < at + length; ++i) digits += static_cast<char>(input[i]);

            return Completion::normal(JSValue::number(std::strtod(digits.c_str(), nullptr)));
        }

        Completion native_is_nan(Evaluator &evaluator, const JSValue&, const std::vector<JSValue> &args) {
            Completion number = evaluator.to_number(argument_at(args, 0));
            if (number.is_abrupt()) return number;

            return Completion::normal(JSValue::boolean(
                std::isnan(number.get_value_or_undefined().to_number())));
        }

        Completion native_is_finite(Evaluator &evaluator, const JSValue&, const std::vector<JSValue> &args) {
            Completion number = evaluator.to_number(argument_at(args, 0));
            if (number.is_abrupt()) return number;

            return Completion::normal(JSValue::boolean(
                std::isfinite(number.get_value_or_undefined().to_number())));
        }
    }

    void install_global_functions(const Builtins &b) {
        b.global_constant("undefined", JSValue::undefined());
        b.global_constant("NaN",       JSValue::number(std::numeric_limits<double>::quiet_NaN()));
        b.global_constant("Infinity",  JSValue::number(std::numeric_limits<double>::infinity()));

        b.global_function("parseInt",   JSValue::object(b.function("parseInt",   native_parse_int, 2)));
        b.global_function("parseFloat", JSValue::object(b.function("parseFloat", native_parse_float, 1)));
        b.global_function("isNaN",      JSValue::object(b.function("isNaN",      native_is_nan, 1)));
        b.global_function("isFinite",   JSValue::object(b.function("isFinite",   native_is_finite, 1)));

        b.global_function("print", JSValue::object(b.function("print", native_print, 1)));
        b.global_function("queueMicrotask",
            JSValue::object(b.function("queueMicrotask", native_queue_microtask, 1)));

        JSObject *eval_builtin = b.function("eval", native_eval, 1);
        b.evaluator.eval_builtin = eval_builtin;
        b.global_function("eval", JSValue::object(eval_builtin));
    }
}