#include "runtime/builtins.h"

#include "memory/js_array.h"
#include "memory/js_map.h"
#include "memory/js_string.h"
#include "utils/utf.h"

#include <cmath>
#include <limits>


namespace perunejs {
    namespace {
        Completion this_text(Evaluator &evaluator, const JSValue &this_value, std::u16string &out) {
            if (this_value.type() == JSValueType::Undefined || this_value.type() == JSValueType::Null) {
                return evaluator.throw_error(evaluator.type_error_prototype,
                    "String.prototype method called on null or undefined");
            }

            Completion text = evaluator.to_string(this_value);
            if (text.is_abrupt()) return text;

            out = text.get_value_or_undefined().to_u16string();

            return Completion::empty();
        }

        Completion clamped_position(Evaluator &evaluator, const JSValue &value,
                                    std::size_t length, std::size_t fallback, std::size_t &out) {
            if (value.type() == JSValueType::Undefined) {
                out = fallback;
                return Completion::empty();
            }

            Completion number = evaluator.to_number(value);
            if (number.is_abrupt()) return number;

            const double raw = number.get_value_or_undefined().to_number();

            if (std::isnan(raw) || raw <= 0) out = 0;
            else if (raw >= static_cast<double>(length)) out = length;
            else out = static_cast<std::size_t>(raw);

            return Completion::empty();
        }

        Completion string_starts_with(Evaluator &e, const JSValue &self, const std::vector<JSValue> &args) {
            std::u16string text;
            if (Completion c = this_text(e, self, text); c.is_abrupt()) return c;

            Completion search = e.to_string(argument_at(args, 0));
            if (search.is_abrupt()) return search;

            const std::u16string needle = search.get_value_or_undefined().to_u16string();

            std::size_t at = 0;
            if (Completion c = clamped_position(e, argument_at(args, 1), text.size(), 0, at); c.is_abrupt()) return c;

            if (at + needle.size() > text.size()) return Completion::normal(JSValue::boolean(false));

            return Completion::normal(JSValue::boolean(text.compare(at, needle.size(), needle) == 0));
        }

        Completion string_ends_with(Evaluator &e, const JSValue &self, const std::vector<JSValue> &args) {
            std::u16string text;
            if (Completion c = this_text(e, self, text); c.is_abrupt()) return c;

            Completion search = e.to_string(argument_at(args, 0));
            if (search.is_abrupt()) return search;

            const std::u16string needle = search.get_value_or_undefined().to_u16string();

            std::size_t end = 0;
            if (Completion c = clamped_position(e, argument_at(args, 1), text.size(), text.size(), end);
                c.is_abrupt()) return c;

            if (needle.size() > end) return Completion::normal(JSValue::boolean(false));

            return Completion::normal(
                JSValue::boolean(text.compare(end - needle.size(), needle.size(), needle) == 0));
        }

        Completion string_includes(Evaluator &e, const JSValue &self, const std::vector<JSValue> &args) {
            std::u16string text;
            if (Completion c = this_text(e, self, text); c.is_abrupt()) return c;

            Completion search = e.to_string(argument_at(args, 0));
            if (search.is_abrupt()) return search;

            std::size_t at = 0;
            if (Completion c = clamped_position(e, argument_at(args, 1), text.size(), 0, at); c.is_abrupt()) return c;

            return Completion::normal(
                JSValue::boolean(text.find(search.get_value_or_undefined().to_u16string(), at)
                                 != std::u16string::npos));
        }

        Completion string_repeat(Evaluator &e, const JSValue &self, const std::vector<JSValue> &args) {
            std::u16string text;
            if (Completion c = this_text(e, self, text); c.is_abrupt()) return c;

            Completion number = e.to_number(argument_at(args, 0));
            if (number.is_abrupt()) return number;

            const double raw = number.get_value_or_undefined().to_number();

            if (raw < 0 || std::isinf(raw)) {
                return e.throw_error(e.range_error_prototype, "Invalid count value");
            }

            const auto count = static_cast<std::size_t>(std::isnan(raw) ? 0 : raw);

            std::u16string out;
            out.reserve(text.size() * count);
            for (std::size_t i = 0; i < count; ++i) out += text;

            return Completion::normal(JSValue::string(out));
        }

        Completion pad(Evaluator &e, const JSValue &self, const std::vector<JSValue> &args, bool at_start) {
            std::u16string text;
            if (Completion c = this_text(e, self, text); c.is_abrupt()) return c;

            Completion number = e.to_number(argument_at(args, 0));
            if (number.is_abrupt()) return number;

            const double raw = number.get_value_or_undefined().to_number();
            const std::size_t target = std::isnan(raw) || raw < 0 ? 0 : static_cast<std::size_t>(raw);

            if (target <= text.size()) return Completion::normal(JSValue::string(text));

            std::u16string filler = u" ";
            if (argument_at(args, 1).type() != JSValueType::Undefined) {
                Completion given = e.to_string(args[1]);
                if (given.is_abrupt()) return given;

                filler = given.get_value_or_undefined().to_u16string();
            }

            if (filler.empty()) return Completion::normal(JSValue::string(text));

            std::u16string padding;
            while (padding.size() < target - text.size()) padding += filler;
            padding.resize(target - text.size());

            return Completion::normal(JSValue::string(at_start ? padding + text : text + padding));
        }

        Completion string_pad_start(Evaluator &e, const JSValue &s, const std::vector<JSValue> &a) {
            return pad(e, s, a, true);
        }
        Completion string_pad_end(Evaluator &e, const JSValue &s, const std::vector<JSValue> &a) {
            return pad(e, s, a, false);
        }

        Completion trim_side(Evaluator &e, const JSValue &self, bool front) {
            std::u16string text;
            if (Completion c = this_text(e, self, text); c.is_abrupt()) return c;

            std::size_t from = 0;
            std::size_t to = text.size();

            if (front) while (from < to && is_js_whitespace(text[from])) ++from;
            else       while (to > from && is_js_whitespace(text[to - 1])) --to;

            return Completion::normal(JSValue::string(text.substr(from, to - from)));
        }

        Completion string_trim_start(Evaluator &e, const JSValue &s, const std::vector<JSValue>&) {
            return trim_side(e, s, true);
        }
        Completion string_trim_end(Evaluator &e, const JSValue &s, const std::vector<JSValue>&) {
            return trim_side(e, s, false);
        }

        // ---- Array.prototype (22.1.3) ----

        Completion this_array(Evaluator &e, const JSValue &self, JSObject *&out, uint32_t &length) {
            Completion object = e.to_object(self);
            if (object.is_abrupt()) return object;

            out = object.get_value_or_undefined().as_object();

            Completion raw = out->get(e, "length");
            if (raw.is_abrupt()) return raw;

            Completion number = e.to_number(raw.get_value_or_undefined());
            if (number.is_abrupt()) return number;

            const double counted = number.get_value_or_undefined().to_number();
            length = std::isnan(counted) || counted < 0 ? 0 : static_cast<uint32_t>(counted);

            return Completion::empty();
        }

        Completion find_in_array(Evaluator &e, const JSValue &self,
                                 const std::vector<JSValue> &args, bool want_index) {
            JSObject *object = nullptr;
            uint32_t length = 0;
            if (Completion c = this_array(e, self, object, length); c.is_abrupt()) return c;

            const JSValue predicate = argument_at(args, 0);
            if (predicate.type() != JSValueType::Object || !predicate.as_object()->is_callable()) {
                return e.throw_error(e.type_error_prototype, "predicate is not a function");
            }

            const JSValue this_arg = argument_at(args, 1);

            for (uint32_t i = 0; i < length; ++i) {
                Completion element = object->get(e, std::to_string(i));
                if (element.is_abrupt()) return element;

                MarkedVector call_args(e.heap);
                call_args.push_back(element.get_value_or_undefined());
                call_args.push_back(JSValue::number(i));
                call_args.push_back(JSValue::object(object));

                Completion verdict = predicate.as_object()->call(e, this_arg, call_args);
                if (verdict.is_abrupt()) return verdict;

                if (verdict.get_value_or_undefined().to_boolean()) {
                    return Completion::normal(want_index
                        ? JSValue::number(i)
                        : element.get_value_or_undefined());
                }
            }

            return Completion::normal(want_index ? JSValue::number(-1) : JSValue::undefined());
        }

        Completion array_find(Evaluator &e, const JSValue &s, const std::vector<JSValue> &a) {
            return find_in_array(e, s, a, false);
        }
        Completion array_find_index(Evaluator &e, const JSValue &s, const std::vector<JSValue> &a) {
            return find_in_array(e, s, a, true);
        }

        Completion array_includes(Evaluator &e, const JSValue &self, const std::vector<JSValue> &args) {
            JSObject *object = nullptr;
            uint32_t length = 0;
            if (Completion c = this_array(e, self, object, length); c.is_abrupt()) return c;

            const JSValue wanted = argument_at(args, 0);

            for (uint32_t i = 0; i < length; ++i) {
                Completion element = object->get(e, std::to_string(i));
                if (element.is_abrupt()) return element;

                if (same_value_zero(element.get_value_or_undefined(), wanted)) {
                    return Completion::normal(JSValue::boolean(true));
                }
            }

            return Completion::normal(JSValue::boolean(false));
        }

        Completion array_fill(Evaluator &e, const JSValue &self, const std::vector<JSValue> &args) {
            JSObject *object = nullptr;
            uint32_t length = 0;
            if (Completion c = this_array(e, self, object, length); c.is_abrupt()) return c;

            const JSValue filler = argument_at(args, 0);

            std::size_t from = 0;
            std::size_t to = length;
            if (Completion c = clamped_position(e, argument_at(args, 1), length, 0, from); c.is_abrupt()) return c;
            if (Completion c = clamped_position(e, argument_at(args, 2), length, length, to); c.is_abrupt()) return c;

            for (std::size_t i = from; i < to; ++i) {
                if (Completion c = object->put(e, std::to_string(i), filler, true); c.is_abrupt()) return c;
            }

            return Completion::normal(JSValue::object(object));
        }

        Completion array_from(Evaluator &e, const JSValue&, const std::vector<JSValue> &args) {
            Completion source = e.to_object(argument_at(args, 0));
            if (source.is_abrupt()) return source;

            JSObject *object = source.get_value_or_undefined().as_object();

            Completion raw = object->get(e, "length");
            if (raw.is_abrupt()) return raw;

            Completion counted = e.to_number(raw.get_value_or_undefined());
            if (counted.is_abrupt()) return counted;

            const double size = counted.get_value_or_undefined().to_number();
            const auto length = static_cast<uint32_t>(std::isnan(size) || size < 0 ? 0 : size);

            const JSValue mapper = argument_at(args, 1);
            const bool has_mapper = mapper.type() == JSValueType::Object && mapper.as_object()->is_callable();

            auto *result = e.heap.allocate<JSArray>();
            result->prototype = e.array_prototype;
            result->define_own_property("length",
                PropertyDescriptor::data(JSValue::number(0), true, false, false));

            for (uint32_t i = 0; i < length; ++i) {
                Completion element = object->get(e, std::to_string(i));
                if (element.is_abrupt()) return element;

                JSValue value = element.get_value_or_undefined();

                if (has_mapper) {
                    MarkedVector call_args(e.heap);
                    call_args.push_back(value);
                    call_args.push_back(JSValue::number(i));

                    Completion mapped = mapper.as_object()->call(e, argument_at(args, 2), call_args);
                    if (mapped.is_abrupt()) return mapped;

                    value = mapped.get_value_or_undefined();
                }

                if (Completion c = result->put(e, std::to_string(i), value, true); c.is_abrupt()) return c;
            }

            return Completion::normal(JSValue::object(result));
        }

        Completion array_of(Evaluator &e, const JSValue&, const std::vector<JSValue> &args) {
            auto *result = e.heap.allocate<JSArray>();
            result->prototype = e.array_prototype;
            result->define_own_property("length",
                PropertyDescriptor::data(JSValue::number(0), true, false, false));

            for (std::size_t i = 0; i < args.size(); ++i) {
                if (Completion c = result->put(e, std::to_string(i), args[i], true); c.is_abrupt()) return c;
            }

            return Completion::normal(JSValue::object(result));
        }

        Completion object_assign(Evaluator &e, const JSValue&, const std::vector<JSValue> &args) {
            Completion target = e.to_object(argument_at(args, 0));
            if (target.is_abrupt()) return target;

            JSObject *destination = target.get_value_or_undefined().as_object();

            for (std::size_t i = 1; i < args.size(); ++i) {
                if (args[i].type() == JSValueType::Undefined || args[i].type() == JSValueType::Null) continue;

                Completion converted = e.to_object(args[i]);
                if (converted.is_abrupt()) return converted;

                JSObject *source = converted.get_value_or_undefined().as_object();

                for (const std::string &key : source->own_keys()) {
                    const PropertyDescriptor *descriptor = source->get_own_property(key);
                    if (descriptor == nullptr || !descriptor->is_enumerable()) continue;

                    Completion value = source->get(e, key);
                    if (value.is_abrupt()) return value;

                    if (Completion c = destination->put(e, key, value.get_value_or_undefined(), true);
                        c.is_abrupt()) return c;
                }
            }

            return Completion::normal(JSValue::object(destination));
        }

        Completion object_is(Evaluator&, const JSValue&, const std::vector<JSValue> &args) {
            return Completion::normal(
                JSValue::boolean(JSValue::same_value(argument_at(args, 0), argument_at(args, 1))));
        }

        Completion own_enumerable(Evaluator &e, const JSValue &value, bool as_pairs, JSValue &out) {
            Completion converted = e.to_object(value);
            if (converted.is_abrupt()) return converted;

            JSObject *source = converted.get_value_or_undefined().as_object();

            auto *result = e.heap.allocate<JSArray>();
            result->prototype = e.array_prototype;
            result->define_own_property("length",
                PropertyDescriptor::data(JSValue::number(0), true, false, false));

            uint32_t written = 0;

            for (const std::string &key : source->own_keys()) {
                const PropertyDescriptor *descriptor = source->get_own_property(key);
                if (descriptor == nullptr || !descriptor->is_enumerable()) continue;

                Completion value_of = source->get(e, key);
                if (value_of.is_abrupt()) return value_of;

                JSValue element = value_of.get_value_or_undefined();

                if (as_pairs) {
                    auto *pair = e.heap.allocate<JSArray>();
                    pair->prototype = e.array_prototype;
                    pair->define_own_property("length",
                        PropertyDescriptor::data(JSValue::number(0), true, false, false));

                    if (Completion c = pair->put(e, "0", JSValue::string(key), true); c.is_abrupt()) return c;
                    if (Completion c = pair->put(e, "1", element, true); c.is_abrupt()) return c;

                    element = JSValue::object(pair);
                }

                if (Completion c = result->put(e, std::to_string(written++), element, true);
                    c.is_abrupt()) return c;
            }

            out = JSValue::object(result);

            return Completion::empty();
        }

        Completion object_values(Evaluator &e, const JSValue&, const std::vector<JSValue> &args) {
            JSValue out;
            if (Completion c = own_enumerable(e, argument_at(args, 0), false, out); c.is_abrupt()) return c;

            return Completion::normal(out);
        }

        Completion object_entries(Evaluator &e, const JSValue&, const std::vector<JSValue> &args) {
            JSValue out;
            if (Completion c = own_enumerable(e, argument_at(args, 0), true, out); c.is_abrupt()) return c;

            return Completion::normal(out);
        }

        bool set_prototype(JSObject *object, JSObject *prototype) {
            if (object->prototype == prototype) return true;
            if (!object->extensible) return false;

            for (const JSObject *step = prototype; step != nullptr; step = step->prototype) {
                if (step == object) return false;
            }

            object->prototype = prototype;

            return true;
        }

        bool is_prototype_candidate(const JSValue &value) {
            return value.type() == JSValueType::Object || value.type() == JSValueType::Null;
        }

        // 19.1.2.20
        Completion object_set_prototype_of(Evaluator &e, const JSValue&, const std::vector<JSValue> &args) {
            const JSValue target = argument_at(args, 0);
            const JSValue prototype = argument_at(args, 1);

            if (target.type() == JSValueType::Undefined || target.type() == JSValueType::Null) {
                return e.throw_error(e.type_error_prototype, "Object.setPrototypeOf called on null or undefined");
            }

            if (!is_prototype_candidate(prototype)) {
                return e.throw_error(e.type_error_prototype, "Object prototype may only be an Object or null");
            }

            if (target.type() != JSValueType::Object) return Completion::normal(target);

            if (!set_prototype(target.as_object(), prototype.as_object())) {
                return e.throw_error(e.type_error_prototype, "Cannot set prototype");
            }

            return Completion::normal(target);
        }

        Completion object_proto_get(Evaluator &e, const JSValue &self, const std::vector<JSValue>&) {
            Completion object = e.to_object(self);
            if (object.is_abrupt()) return object;

            JSObject *prototype = object.get_value_or_undefined().as_object()->prototype;

            return Completion::normal(prototype != nullptr ? JSValue::object(prototype) : JSValue::null());
        }

        Completion object_proto_set(Evaluator &e, const JSValue &self, const std::vector<JSValue> &args) {
            if (self.type() == JSValueType::Undefined || self.type() == JSValueType::Null) {
                return e.throw_error(e.type_error_prototype, "__proto__ setter called on null or undefined");
            }

            const JSValue prototype = argument_at(args, 0);

            if (!is_prototype_candidate(prototype) || self.type() != JSValueType::Object) {
                return Completion::normal(JSValue::undefined());
            }

            if (!set_prototype(self.as_object(), prototype.as_object())) {
                return e.throw_error(e.type_error_prototype, "Cannot set prototype");
            }

            return Completion::normal(JSValue::undefined());
        }

        Completion number_is_integer(Evaluator&, const JSValue&, const std::vector<JSValue> &args) {
            const JSValue value = argument_at(args, 0);
            if (value.type() != JSValueType::Number) return Completion::normal(JSValue::boolean(false));

            const double number = value.to_number();

            return Completion::normal(JSValue::boolean(
                std::isfinite(number) && std::trunc(number) == number));
        }

        Completion number_is_safe_integer(Evaluator&, const JSValue&, const std::vector<JSValue> &args) {
            const JSValue value = argument_at(args, 0);
            if (value.type() != JSValueType::Number) return Completion::normal(JSValue::boolean(false));

            const double number = value.to_number();

            return Completion::normal(JSValue::boolean(
                std::isfinite(number) && std::trunc(number) == number && std::abs(number) <= 9007199254740991.0));
        }

        Completion number_is_finite(Evaluator&, const JSValue&, const std::vector<JSValue> &args) {
            const JSValue value = argument_at(args, 0);

            return Completion::normal(JSValue::boolean(
                value.type() == JSValueType::Number && std::isfinite(value.to_number())));
        }

        Completion number_is_nan(Evaluator&, const JSValue&, const std::vector<JSValue> &args) {
            const JSValue value = argument_at(args, 0);

            return Completion::normal(JSValue::boolean(
                value.type() == JSValueType::Number && std::isnan(value.to_number())));
        }

        using UnaryMath = double (*)(double);

        template <UnaryMath FN>
        Completion math_unary(Evaluator &e, const JSValue&, const std::vector<JSValue> &args) {
            Completion number = e.to_number(argument_at(args, 0));
            if (number.is_abrupt()) return number;

            return Completion::normal(JSValue::number(FN(number.get_value_or_undefined().to_number())));
        }

        double math_sign_of(double x) {
            if (std::isnan(x) || x == 0) return x;
            return x < 0 ? -1.0 : 1.0;
        }

        Completion math_hypot(Evaluator &e, const JSValue&, const std::vector<JSValue> &args) {
            double total = 0;

            for (const JSValue &argument : args) {
                Completion number = e.to_number(argument);
                if (number.is_abrupt()) return number;

                const double value = number.get_value_or_undefined().to_number();
                total += value * value;
            }

            return Completion::normal(JSValue::number(std::sqrt(total)));
        }

        Completion math_imul(Evaluator &e, const JSValue&, const std::vector<JSValue> &args) {
            Completion left = e.to_number(argument_at(args, 0));
            if (left.is_abrupt()) return left;

            Completion right = e.to_number(argument_at(args, 1));
            if (right.is_abrupt()) return right;

            const int32_t a = left.get_value_or_undefined().to_int32();
            const int32_t b = right.get_value_or_undefined().to_int32();

            return Completion::normal(JSValue::number(
                static_cast<int32_t>(static_cast<uint32_t>(a) * static_cast<uint32_t>(b))));
        }
    }

    void install_es2015(const Builtins &b) {
        Evaluator &evaluator = b.evaluator;

        evaluator.object_prototype->define_own_property("__proto__",
            PropertyDescriptor::accessor(b.function("get __proto__", object_proto_get, 0),
                                         b.function("set __proto__", object_proto_set, 1),
                                         false, true));

        JSObject *string_prototype = evaluator.string_prototype;
        b.method(string_prototype, "startsWith", string_starts_with, 1);
        b.method(string_prototype, "endsWith",   string_ends_with, 1);
        b.method(string_prototype, "includes",   string_includes, 1);
        b.method(string_prototype, "repeat",     string_repeat, 1);
        b.method(string_prototype, "padStart",   string_pad_start, 1);
        b.method(string_prototype, "padEnd",     string_pad_end, 1);
        b.method(string_prototype, "trimStart",  string_trim_start, 0);
        b.method(string_prototype, "trimEnd",    string_trim_end, 0);

        JSObject *array_prototype = evaluator.array_prototype;
        b.method(array_prototype, "find",      array_find, 1);
        b.method(array_prototype, "findIndex", array_find_index, 1);
        b.method(array_prototype, "includes",  array_includes, 1);
        b.method(array_prototype, "fill",      array_fill, 1);

        Completion array_constructor = evaluator.global_object->get(evaluator, "Array");
        if (array_constructor.type == COMPLETION_TYPE::NORMAL
            && array_constructor.get_value_or_undefined().type() == JSValueType::Object) {
            JSObject *target = array_constructor.get_value_or_undefined().as_object();
            b.method(target, "from", array_from, 1);
            b.method(target, "of",   array_of, 0);
        }

        Completion object_constructor = evaluator.global_object->get(evaluator, "Object");
        if (object_constructor.type == COMPLETION_TYPE::NORMAL
            && object_constructor.get_value_or_undefined().type() == JSValueType::Object) {
            JSObject *target = object_constructor.get_value_or_undefined().as_object();
            b.method(target, "assign",  object_assign, 2);
            b.method(target, "is",      object_is, 2);
            b.method(target, "values",  object_values, 1);
            b.method(target, "entries", object_entries, 1);
            b.method(target, "setPrototypeOf", object_set_prototype_of, 2);
        }

        Completion number_constructor = evaluator.global_object->get(evaluator, "Number");
        if (number_constructor.type == COMPLETION_TYPE::NORMAL
            && number_constructor.get_value_or_undefined().type() == JSValueType::Object) {
            JSObject *target = number_constructor.get_value_or_undefined().as_object();
            b.method(target, "isInteger",     number_is_integer, 1);
            b.method(target, "isSafeInteger", number_is_safe_integer, 1);
            b.method(target, "isFinite",      number_is_finite, 1);
            b.method(target, "isNaN",         number_is_nan, 1);

            b.constant(target, "EPSILON", JSValue::number(std::numeric_limits<double>::epsilon()));
            b.constant(target, "MAX_SAFE_INTEGER", JSValue::number(9007199254740991.0));
            b.constant(target, "MIN_SAFE_INTEGER", JSValue::number(-9007199254740991.0));
        }

        Completion math_object = evaluator.global_object->get(evaluator, "Math");
        if (math_object.type == COMPLETION_TYPE::NORMAL
            && math_object.get_value_or_undefined().type() == JSValueType::Object) {
            JSObject *target = math_object.get_value_or_undefined().as_object();
            b.method(target, "trunc", math_unary<std::trunc>, 1);
            b.method(target, "sign",  math_unary<math_sign_of>, 1);
            b.method(target, "log2",  math_unary<std::log2>, 1);
            b.method(target, "log10", math_unary<std::log10>, 1);
            b.method(target, "log1p", math_unary<std::log1p>, 1);
            b.method(target, "expm1", math_unary<std::expm1>, 1);
            b.method(target, "cbrt",  math_unary<std::cbrt>, 1);
            b.method(target, "sinh",  math_unary<std::sinh>, 1);
            b.method(target, "cosh",  math_unary<std::cosh>, 1);
            b.method(target, "tanh",  math_unary<std::tanh>, 1);
            b.method(target, "asinh", math_unary<std::asinh>, 1);
            b.method(target, "acosh", math_unary<std::acosh>, 1);
            b.method(target, "atanh", math_unary<std::atanh>, 1);
            b.method(target, "hypot", math_hypot, 2);
            b.method(target, "imul",  math_imul, 2);
        }
    }
}
