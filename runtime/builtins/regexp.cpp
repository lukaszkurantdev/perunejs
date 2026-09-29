#include "runtime/builtins.h"

#include "memory/js_array.h"
#include "memory/js_regexp.h"
#include "utils/utf.h"

#include <cmath>

namespace perunejs {
    namespace {
        JSRegExp *as_regexp(const JSValue &value) {
            if (value.type() != JSValueType::Object) return nullptr;

            return dynamic_cast<JSRegExp*>(value.as_object());
        }

        Completion this_regexp(Evaluator &evaluator, const JSValue &this_value, JSRegExp *&out) {
            out = as_regexp(this_value);
            if (out != nullptr) return Completion::empty();

            return evaluator.throw_error(evaluator.type_error_prototype,
                "RegExp.prototype method called on incompatible receiver");
        }

        Completion construct_regexp(Evaluator &evaluator, const JSValue&,
                                    const std::vector<JSValue> &args) {
            const JSValue pattern = argument_at(args, 0);
            const JSValue flags = argument_at(args, 1);

            if (JSRegExp *existing = as_regexp(pattern)) {
                if (flags.type() != JSValueType::Undefined) {
                    return evaluator.throw_error(evaluator.type_error_prototype,
                        "Cannot supply flags when constructing one RegExp from another");
                }

                return evaluator.make_regexp(utf8_to_utf16(existing->pattern), existing->flags);
            }

            std::u16string source;
            if (pattern.type() != JSValueType::Undefined) {
                Completion text = evaluator.to_string(pattern);
                if (text.is_abrupt()) return text;

                source = text.get_value_or_undefined().to_u16string();
            }

            std::string letters;
            if (flags.type() != JSValueType::Undefined) {
                Completion text = evaluator.to_string(flags);
                if (text.is_abrupt()) return text;

                letters = text.get_value_or_undefined().to_string();
            }

            return evaluator.make_regexp(source, letters);
        }

        Completion native_regexp(Evaluator &evaluator, const JSValue&,
                                 const std::vector<JSValue> &args) {
            if (as_regexp(argument_at(args, 0)) != nullptr
                && argument_at(args, 1).type() == JSValueType::Undefined) {
                return Completion::normal(args[0]);
            }

            return construct_regexp(evaluator, JSValue::undefined(), args);
        }

        Completion regexp_exec(Evaluator &evaluator, const JSValue &this_value,
                               const std::vector<JSValue> &args) {
            JSRegExp *regexp = nullptr;
            if (Completion c = this_regexp(evaluator, this_value, regexp); c.is_abrupt()) return c;

            Completion text = evaluator.to_string(argument_at(args, 0));
            if (text.is_abrupt()) return text;

            const std::u16string input = text.get_value_or_undefined().to_u16string();

            Completion last = regexp->get(evaluator, "lastIndex");
            if (last.is_abrupt()) return last;

            Completion number = evaluator.to_number(last.get_value_or_undefined());
            if (number.is_abrupt()) return number;

            const double raw = number.get_value_or_undefined().to_number();
            const double start = std::isnan(raw) ? 0 : std::trunc(raw);

            const bool global = regexp->has_flag('g');
            const double from = global ? start : 0;

            bool found = false;
            RegExpMatch match;

            if (from >= 0 && from <= static_cast<double>(input.size())) {
                Completion searched = regexp_search(evaluator, regexp, input,
                                                    static_cast<int>(from), found, match);
                if (searched.is_abrupt()) return searched;
            }

            if (!found) {
                if (global) {
                    Completion stored = regexp->put(evaluator, "lastIndex", JSValue::number(0), true);
                    if (stored.is_abrupt()) return stored;
                }

                return Completion::normal(JSValue::null());
            }

            if (global) {
                Completion stored = regexp->put(evaluator, "lastIndex",
                                                JSValue::number(match.ends[0]), true);
                if (stored.is_abrupt()) return stored;
            }

            return Completion::normal(JSValue::object(
                make_match_result(evaluator, input, match)));
        }

        Completion regexp_test(Evaluator &evaluator, const JSValue &this_value,
                               const std::vector<JSValue> &args) {
            Completion result = regexp_exec(evaluator, this_value, args);
            if (result.is_abrupt()) return result;

            return Completion::normal(JSValue::boolean(
                result.get_value_or_undefined().type() != JSValueType::Null));
        }

        Completion regexp_to_string(Evaluator &evaluator, const JSValue &this_value,
                                    const std::vector<JSValue>&) {
            JSRegExp *regexp = nullptr;
            if (Completion c = this_regexp(evaluator, this_value, regexp); c.is_abrupt()) return c;

            std::string text = "/" + regexp->pattern + "/";
            for (const char flag : {'g', 'i', 'm'}) {
                if (regexp->has_flag(flag)) text += flag;
            }

            return Completion::normal(JSValue::string(text));
        }
    }

    Completion regexp_search(Evaluator &evaluator, JSRegExp *regexp, const std::u16string &input,
                             const int from, bool &found, RegExpMatch &out) {
        try {
            found = regexp->search(input, from, out);
        } catch (const std::exception &error) {
            return evaluator.throw_error(evaluator.range_error_prototype, error.what());
        }

        return Completion::empty();
    }

    Completion regexp_match_at(Evaluator &evaluator, JSRegExp *regexp, const std::u16string &input,
                               const int at, bool &found, RegExpMatch &out) {
        try {
            found = regexp->engine != nullptr && regexp->engine->match_at(input, at, out);
        } catch (const std::exception &error) {
            return evaluator.throw_error(evaluator.range_error_prototype, error.what());
        }

        return Completion::empty();
    }

    Completion to_regexp(Evaluator &evaluator, const JSValue &value, JSRegExp *&out) {
        if (JSRegExp *existing = as_regexp(value)) {
            out = existing;
            return Completion::empty();
        }

        std::u16string source;
        if (value.type() != JSValueType::Undefined) {
            Completion text = evaluator.to_string(value);
            if (text.is_abrupt()) return text;

            source = text.get_value_or_undefined().to_u16string();
        }

        Completion created = evaluator.make_regexp(source, "");
        if (created.is_abrupt()) return created;

        out = static_cast<JSRegExp*>(created.get_value_or_undefined().as_object());
        return Completion::empty();
    }

    JSObject *make_match_result(Evaluator &evaluator, const std::u16string &input,
                                const RegExpMatch &match) {
        auto *result = evaluator.heap.allocate<JSArray>();
        result->prototype = evaluator.array_prototype;

        result->define_own_property("length",
            PropertyDescriptor::data(JSValue::number(0), true, false, false));

        for (std::size_t i = 0; i < match.starts.size(); ++i) {
            const JSValue value = match.starts[i] < 0
                ? JSValue::undefined()
                : JSValue::string(input.substr(match.starts[i],
                                               match.ends[i] - match.starts[i]));

            result->define_own_property(std::to_string(i),
                PropertyDescriptor::data(value, true, true, true));
        }

        result->define_own_property("index",
            PropertyDescriptor::data(JSValue::number(match.starts[0]), true, true, true));
        result->define_own_property("input",
            PropertyDescriptor::data(JSValue::string(input), true, true, true));

        return result;
    }

    void install_regexp(const Builtins &b) {
        JSObject *prototype = b.evaluator.regexp_prototype;

        b.constructor("RegExp", native_regexp, prototype, construct_regexp, 2);

        b.method(prototype, "exec",     regexp_exec, 1);
        b.method(prototype, "test",     regexp_test, 1);
        b.method(prototype, "toString", regexp_to_string);
    }
}