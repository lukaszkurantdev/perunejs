#include "runtime/builtins.h"

#include "memory/js_array.h"
#include "memory/marked_vector.h"

namespace perunejs {
    namespace {
        JSObject *new_array(Evaluator &evaluator) {
            auto *array = evaluator.heap.allocate<JSArray>();
            array->prototype = evaluator.array_prototype;

            array->define_own_property("length",
                PropertyDescriptor::data(JSValue::number(0), true, false, false));

            return array;
        }

        Completion this_object(Evaluator &evaluator, const JSValue &this_value, JSObject *&out) {
            Completion object = evaluator.to_object(this_value);
            if (object.is_abrupt()) return object;

            out = object.get_value_or_undefined().as_object();
            return Completion::empty();
        }

        Completion length_of(Evaluator &evaluator, JSObject *object, uint32_t &out) {
            Completion length = object->get(evaluator, "length");
            if (length.is_abrupt()) return length;

            Completion number = evaluator.to_number(length.get_value_or_undefined());
            if (number.is_abrupt()) return number;

            out = number.get_value_or_undefined().to_uint32();
            return Completion::empty();
        }

        Completion get_index(Evaluator &evaluator, JSObject *object, uint32_t index, JSValue &out) {
            Completion element = object->get(evaluator, std::to_string(index));
            if (element.is_abrupt()) return element;

            out = element.get_value_or_undefined();
            return Completion::empty();
        }

        Completion put_index(Evaluator &evaluator, JSObject *object, uint32_t index, const JSValue &value) {
            return object->put(evaluator, std::to_string(index), value, true);
        }

        Completion put_length(Evaluator &evaluator, JSObject *object, double length) {
            return object->put(evaluator, "length", JSValue::number(length), true);
        }

        uint32_t relative_index(double relative, uint32_t length) {
            if (relative < 0) {
                const double from_end = static_cast<double>(length) + relative;
                return from_end < 0 ? 0 : static_cast<uint32_t>(from_end);
            }

            return relative > static_cast<double>(length) ? length : static_cast<uint32_t>(relative);
        }

        Completion to_relative(Evaluator &evaluator, const JSValue &value, uint32_t length, uint32_t &out) {
            Completion number = evaluator.to_number(value);
            if (number.is_abrupt()) return number;

            const double raw = number.get_value_or_undefined().to_number();
            out = std::isnan(raw) ? 0 : relative_index(std::trunc(raw), length);

            return Completion::empty();
        }

        bool callable(const JSValue &value) {
            return value.type() == JSValueType::Object && value.as_object()->is_callable();
        }

        Completion native_array(Evaluator &evaluator, const JSValue&, const std::vector<JSValue> &args) {
            JSObject *array = new_array(evaluator);

            if (args.size() == 1 && args[0].type() == JSValueType::Number) {
                const double raw = args[0].to_number();
                const uint32_t length = args[0].to_uint32();

                if (static_cast<double>(length) != raw) {
                    return evaluator.throw_error(evaluator.range_error_prototype, "Invalid array length");
                }

                Completion stored = put_length(evaluator, array, length);
                if (stored.is_abrupt()) return stored;

                return Completion::normal(JSValue::object(array));
            }

            for (std::size_t i = 0; i < args.size(); ++i) {
                array->define_own_property(std::to_string(i),
                    PropertyDescriptor::data(args[i], true, true, true));
            }

            return Completion::normal(JSValue::object(array));
        }

        Completion array_is_array(Evaluator&, const JSValue&, const std::vector<JSValue> &args) {
            const JSValue value = argument_at(args, 0);
            const bool is_array = value.type() == JSValueType::Object
                               && dynamic_cast<JSArray*>(value.as_object()) != nullptr;

            return Completion::normal(JSValue::boolean(is_array));
        }

        Completion array_join(Evaluator &evaluator, const JSValue &this_value,
                              const std::vector<JSValue> &args) {
            JSObject *object = nullptr;
            if (Completion c = this_object(evaluator, this_value, object); c.is_abrupt()) return c;

            uint32_t length = 0;
            if (Completion c = length_of(evaluator, object, length); c.is_abrupt()) return c;

            std::u16string separator = u",";
            if (argument_at(args, 0).type() != JSValueType::Undefined) {
                Completion text = evaluator.to_string(args[0]);
                if (text.is_abrupt()) return text;

                separator = text.get_value_or_undefined().to_u16string();
            }

            std::u16string result;
            for (uint32_t i = 0; i < length; ++i) {
                if (i > 0) result += separator;

                JSValue element;
                if (Completion c = get_index(evaluator, object, i, element); c.is_abrupt()) return c;

                if (element.type() == JSValueType::Undefined || element.type() == JSValueType::Null) continue;

                Completion text = evaluator.to_string(element);
                if (text.is_abrupt()) return text;

                result += text.get_value_or_undefined().to_u16string();
            }

            return Completion::normal(JSValue::string(result));
        }

        Completion array_to_string(Evaluator &evaluator, const JSValue &this_value,
                                   const std::vector<JSValue>&) {
            JSObject *object = nullptr;
            if (Completion c = this_object(evaluator, this_value, object); c.is_abrupt()) return c;

            Completion join = object->get(evaluator, "join");
            if (join.is_abrupt()) return join;

            if (!callable(join.get_value_or_undefined())) {
                return Completion::normal(JSValue::string(
                    std::string("[object ") + object->class_name() + "]"));
            }

            return join.get_value_or_undefined().as_object()->call(evaluator, this_value, {});
        }

        Completion array_concat(Evaluator &evaluator, const JSValue &this_value,
                                const std::vector<JSValue> &args) {
            JSObject *object = nullptr;
            if (Completion c = this_object(evaluator, this_value, object); c.is_abrupt()) return c;

            JSObject *result = new_array(evaluator);
            uint32_t next = 0;

            const auto append = [&](const JSValue &value) -> Completion {
                if (value.type() == JSValueType::Object
                    && dynamic_cast<JSArray*>(value.as_object()) != nullptr) {
                    JSObject *source = value.as_object();

                    uint32_t length = 0;
                    if (Completion c = length_of(evaluator, source, length); c.is_abrupt()) return c;

                    for (uint32_t i = 0; i < length; ++i) {
                        if (!source->has_property(std::to_string(i))) { ++next; continue; }

                        JSValue element;
                        if (Completion c = get_index(evaluator, source, i, element); c.is_abrupt()) return c;

                        result->define_own_property(std::to_string(next++),
                            PropertyDescriptor::data(element, true, true, true));
                    }

                    return Completion::empty();
                }

                result->define_own_property(std::to_string(next++),
                    PropertyDescriptor::data(value, true, true, true));

                return Completion::empty();
            };

            if (Completion c = append(JSValue::object(object)); c.is_abrupt()) return c;
            for (const JSValue &value : args) {
                if (Completion c = append(value); c.is_abrupt()) return c;
            }

            if (Completion c = put_length(evaluator, result, next); c.is_abrupt()) return c;

            return Completion::normal(JSValue::object(result));
        }

        Completion array_slice(Evaluator &evaluator, const JSValue &this_value,
                               const std::vector<JSValue> &args) {
            JSObject *object = nullptr;
            if (Completion c = this_object(evaluator, this_value, object); c.is_abrupt()) return c;

            uint32_t length = 0;
            if (Completion c = length_of(evaluator, object, length); c.is_abrupt()) return c;

            uint32_t start = 0;
            if (Completion c = to_relative(evaluator, argument_at(args, 0), length, start); c.is_abrupt()) return c;

            uint32_t end = length;
            if (argument_at(args, 1).type() != JSValueType::Undefined) {
                if (Completion c = to_relative(evaluator, args[1], length, end); c.is_abrupt()) return c;
            }

            JSObject *result = new_array(evaluator);
            uint32_t next = 0;

            for (uint32_t i = start; i < end; ++i, ++next) {
                if (!object->has_property(std::to_string(i))) continue;

                JSValue element;
                if (Completion c = get_index(evaluator, object, i, element); c.is_abrupt()) return c;

                result->define_own_property(std::to_string(next),
                    PropertyDescriptor::data(element, true, true, true));
            }

            if (Completion c = put_length(evaluator, result, next); c.is_abrupt()) return c;

            return Completion::normal(JSValue::object(result));
        }

        Completion array_push(Evaluator &evaluator, const JSValue &this_value,
                              const std::vector<JSValue> &args) {
            JSObject *object = nullptr;
            if (Completion c = this_object(evaluator, this_value, object); c.is_abrupt()) return c;

            uint32_t length = 0;
            if (Completion c = length_of(evaluator, object, length); c.is_abrupt()) return c;

            double next = length;
            for (const JSValue &value : args) {
                Completion stored = object->put(evaluator, std::to_string(static_cast<uint32_t>(next)), value, true);
                if (stored.is_abrupt()) return stored;

                next += 1;
            }

            if (Completion c = put_length(evaluator, object, next); c.is_abrupt()) return c;

            return Completion::normal(JSValue::number(next));
        }

        Completion array_pop(Evaluator &evaluator, const JSValue &this_value, const std::vector<JSValue>&) {
            JSObject *object = nullptr;
            if (Completion c = this_object(evaluator, this_value, object); c.is_abrupt()) return c;

            uint32_t length = 0;
            if (Completion c = length_of(evaluator, object, length); c.is_abrupt()) return c;

            if (length == 0) {
                if (Completion c = put_length(evaluator, object, 0); c.is_abrupt()) return c;
                return Completion::normal(JSValue::undefined());
            }

            JSValue element;
            if (Completion c = get_index(evaluator, object, length - 1, element); c.is_abrupt()) return c;

            object->delete_own_property(std::to_string(length - 1));

            if (Completion c = put_length(evaluator, object, length - 1); c.is_abrupt()) return c;

            return Completion::normal(element);
        }

        Completion array_shift(Evaluator &evaluator, const JSValue &this_value, const std::vector<JSValue>&) {
            JSObject *object = nullptr;
            if (Completion c = this_object(evaluator, this_value, object); c.is_abrupt()) return c;

            uint32_t length = 0;
            if (Completion c = length_of(evaluator, object, length); c.is_abrupt()) return c;

            if (length == 0) {
                if (Completion c = put_length(evaluator, object, 0); c.is_abrupt()) return c;
                return Completion::normal(JSValue::undefined());
            }

            JSValue first;
            if (Completion c = get_index(evaluator, object, 0, first); c.is_abrupt()) return c;

            for (uint32_t i = 1; i < length; ++i) {
                const std::string from = std::to_string(i);
                const std::string to   = std::to_string(i - 1);

                if (object->has_property(from)) {
                    JSValue element;
                    if (Completion c = get_index(evaluator, object, i, element); c.is_abrupt()) return c;

                    Completion stored = object->put(evaluator, to, element, true);
                    if (stored.is_abrupt()) return stored;
                } else {
                    object->delete_own_property(to);
                }
            }

            object->delete_own_property(std::to_string(length - 1));

            if (Completion c = put_length(evaluator, object, length - 1); c.is_abrupt()) return c;

            return Completion::normal(first);
        }

        Completion array_unshift(Evaluator &evaluator, const JSValue &this_value,
                                 const std::vector<JSValue> &args) {
            JSObject *object = nullptr;
            if (Completion c = this_object(evaluator, this_value, object); c.is_abrupt()) return c;

            uint32_t length = 0;
            if (Completion c = length_of(evaluator, object, length); c.is_abrupt()) return c;

            const uint32_t count = static_cast<uint32_t>(args.size());

            for (uint32_t i = length; i > 0; --i) {
                const std::string from = std::to_string(i - 1);
                const std::string to   = std::to_string(i - 1 + count);

                if (object->has_property(from)) {
                    JSValue element;
                    if (Completion c = get_index(evaluator, object, i - 1, element); c.is_abrupt()) return c;

                    Completion stored = object->put(evaluator, to, element, true);
                    if (stored.is_abrupt()) return stored;
                } else {
                    object->delete_own_property(to);
                }
            }

            for (uint32_t i = 0; i < count; ++i) {
                Completion stored = put_index(evaluator, object, i, args[i]);
                if (stored.is_abrupt()) return stored;
            }

            const double result = static_cast<double>(length) + count;
            if (Completion c = put_length(evaluator, object, result); c.is_abrupt()) return c;

            return Completion::normal(JSValue::number(result));
        }

        Completion array_reverse(Evaluator &evaluator, const JSValue &this_value, const std::vector<JSValue>&) {
            JSObject *object = nullptr;
            if (Completion c = this_object(evaluator, this_value, object); c.is_abrupt()) return c;

            uint32_t length = 0;
            if (Completion c = length_of(evaluator, object, length); c.is_abrupt()) return c;

            for (uint32_t lower = 0; lower < length / 2; ++lower) {
                const uint32_t upper = length - lower - 1;

                const bool lower_exists = object->has_property(std::to_string(lower));
                const bool upper_exists = object->has_property(std::to_string(upper));

                JSValue lower_value;
                JSValue upper_value;

                if (lower_exists) {
                    if (Completion c = get_index(evaluator, object, lower, lower_value); c.is_abrupt()) return c;
                }
                if (upper_exists) {
                    if (Completion c = get_index(evaluator, object, upper, upper_value); c.is_abrupt()) return c;
                }

                if (upper_exists) {
                    if (Completion c = put_index(evaluator, object, lower, upper_value); c.is_abrupt()) return c;
                } else {
                    object->delete_own_property(std::to_string(lower));
                }

                if (lower_exists) {
                    if (Completion c = put_index(evaluator, object, upper, lower_value); c.is_abrupt()) return c;
                } else {
                    object->delete_own_property(std::to_string(upper));
                }
            }

            return Completion::normal(JSValue::object(object));
        }

        Completion array_splice(Evaluator &evaluator, const JSValue &this_value,
                                const std::vector<JSValue> &args) {
            JSObject *object = nullptr;
            if (Completion c = this_object(evaluator, this_value, object); c.is_abrupt()) return c;

            uint32_t length = 0;
            if (Completion c = length_of(evaluator, object, length); c.is_abrupt()) return c;

            uint32_t start = 0;
            if (Completion c = to_relative(evaluator, argument_at(args, 0), length, start); c.is_abrupt()) return c;

            uint32_t remove = length - start;
            if (args.size() > 1) {
                Completion count = evaluator.to_number(args[1]);
                if (count.is_abrupt()) return count;

                const double raw = count.get_value_or_undefined().to_number();
                const double clamped = std::isnan(raw) ? 0 : std::max(0.0, std::trunc(raw));

                remove = static_cast<uint32_t>(std::min(clamped, static_cast<double>(length - start)));
            }

            JSObject *removed = new_array(evaluator);
            for (uint32_t i = 0; i < remove; ++i) {
                if (!object->has_property(std::to_string(start + i))) continue;

                JSValue element;
                if (Completion c = get_index(evaluator, object, start + i, element); c.is_abrupt()) return c;

                removed->define_own_property(std::to_string(i),
                    PropertyDescriptor::data(element, true, true, true));
            }
            if (Completion c = put_length(evaluator, removed, remove); c.is_abrupt()) return c;

            const uint32_t insert = static_cast<uint32_t>(args.size() > 2 ? args.size() - 2 : 0);

            if (insert < remove) {
                for (uint32_t i = start; i < length - remove; ++i) {
                    const std::string from = std::to_string(i + remove);
                    const std::string to   = std::to_string(i + insert);

                    if (object->has_property(from)) {
                        JSValue element;
                        if (Completion c = get_index(evaluator, object, i + remove, element); c.is_abrupt()) return c;

                        Completion stored = object->put(evaluator, to, element, true);
                        if (stored.is_abrupt()) return stored;
                    } else {
                        object->delete_own_property(to);
                    }
                }

                for (uint32_t i = length; i > length - remove + insert; --i) {
                    object->delete_own_property(std::to_string(i - 1));
                }
            } else if (insert > remove) {
                for (uint32_t i = length - remove; i > start; --i) {
                    const std::string from = std::to_string(i + remove - 1);
                    const std::string to   = std::to_string(i + insert - 1);

                    if (object->has_property(from)) {
                        JSValue element;
                        if (Completion c = get_index(evaluator, object, i + remove - 1, element); c.is_abrupt()) return c;

                        Completion stored = object->put(evaluator, to, element, true);
                        if (stored.is_abrupt()) return stored;
                    } else {
                        object->delete_own_property(to);
                    }
                }
            }

            for (uint32_t i = 0; i < insert; ++i) {
                if (Completion c = put_index(evaluator, object, start + i, args[i + 2]); c.is_abrupt()) return c;
            }

            const double result_length = static_cast<double>(length) - remove + insert;
            if (Completion c = put_length(evaluator, object, result_length); c.is_abrupt()) return c;

            return Completion::normal(JSValue::object(removed));
        }

        Completion array_index_of(Evaluator &evaluator, const JSValue &this_value,
                                  const std::vector<JSValue> &args) {
            JSObject *object = nullptr;
            if (Completion c = this_object(evaluator, this_value, object); c.is_abrupt()) return c;

            uint32_t length = 0;
            if (Completion c = length_of(evaluator, object, length); c.is_abrupt()) return c;

            if (length == 0) return Completion::normal(JSValue::number(-1));

            uint32_t start = 0;
            if (args.size() > 1) {
                if (Completion c = to_relative(evaluator, args[1], length, start); c.is_abrupt()) return c;
            }

            const JSValue target = argument_at(args, 0);

            for (uint32_t i = start; i < length; ++i) {
                if (!object->has_property(std::to_string(i))) continue;

                JSValue element;
                if (Completion c = get_index(evaluator, object, i, element); c.is_abrupt()) return c;

                if (JSValue::is_strictly_equal(element, target)) {
                    return Completion::normal(JSValue::number(i));
                }
            }

            return Completion::normal(JSValue::number(-1));
        }

        Completion array_last_index_of(Evaluator &evaluator, const JSValue &this_value,
                                       const std::vector<JSValue> &args) {
            JSObject *object = nullptr;
            if (Completion c = this_object(evaluator, this_value, object); c.is_abrupt()) return c;

            uint32_t length = 0;
            if (Completion c = length_of(evaluator, object, length); c.is_abrupt()) return c;

            if (length == 0) return Completion::normal(JSValue::number(-1));

            const JSValue target = argument_at(args, 0);

            for (uint32_t i = length; i > 0; --i) {
                if (!object->has_property(std::to_string(i - 1))) continue;

                JSValue element;
                if (Completion c = get_index(evaluator, object, i - 1, element); c.is_abrupt()) return c;

                if (JSValue::is_strictly_equal(element, target)) {
                    return Completion::normal(JSValue::number(i - 1));
                }
            }

            return Completion::normal(JSValue::number(-1));
        }

        enum class Iteration { ForEach, Map, Filter, Every, Some };

        Completion iterate(Evaluator &evaluator, Iteration kind, const JSValue &this_value,
                           const std::vector<JSValue> &args) {
            JSObject *object = nullptr;
            if (Completion c = this_object(evaluator, this_value, object); c.is_abrupt()) return c;

            uint32_t length = 0;
            if (Completion c = length_of(evaluator, object, length); c.is_abrupt()) return c;

            const JSValue callback = argument_at(args, 0);
            if (!callable(callback)) {
                return evaluator.throw_error(evaluator.type_error_prototype, "Callback is not a function");
            }

            const JSValue callback_this = argument_at(args, 1);

            JSObject *result = (kind == Iteration::Map || kind == Iteration::Filter)
                             ? new_array(evaluator) : nullptr;
            uint32_t kept = 0;

            for (uint32_t i = 0; i < length; ++i) {
                if (!object->has_property(std::to_string(i))) continue;

                JSValue element;
                if (Completion c = get_index(evaluator, object, i, element); c.is_abrupt()) return c;

                MarkedVector call_args(evaluator.heap);
                call_args.push_back(element);
                call_args.push_back(JSValue::number(i));
                call_args.push_back(JSValue::object(object));

                Completion outcome = callback.as_object()->call(evaluator, callback_this, call_args);
                if (outcome.is_abrupt()) return outcome;

                const JSValue value = outcome.get_value_or_undefined();

                switch (kind) {
                    case Iteration::ForEach: break;
                    case Iteration::Map:
                        result->define_own_property(std::to_string(i),
                            PropertyDescriptor::data(value, true, true, true));
                        break;
                    case Iteration::Filter:
                        if (value.to_boolean()) {
                            result->define_own_property(std::to_string(kept++),
                                PropertyDescriptor::data(element, true, true, true));
                        }
                        break;
                    case Iteration::Every:
                        if (!value.to_boolean()) return Completion::normal(JSValue::boolean(false));
                        break;
                    case Iteration::Some:
                        if (value.to_boolean()) return Completion::normal(JSValue::boolean(true));
                        break;
                }
            }

            switch (kind) {
                case Iteration::ForEach: return Completion::normal(JSValue::undefined());
                case Iteration::Every:   return Completion::normal(JSValue::boolean(true));
                case Iteration::Some:    return Completion::normal(JSValue::boolean(false));
                case Iteration::Map: {
                    if (Completion c = put_length(evaluator, result, length); c.is_abrupt()) return c;
                    return Completion::normal(JSValue::object(result));
                }
                case Iteration::Filter: {
                    if (Completion c = put_length(evaluator, result, kept); c.is_abrupt()) return c;
                    return Completion::normal(JSValue::object(result));
                }
            }

            return Completion::normal(JSValue::undefined());
        }

        Completion array_for_each(Evaluator &e, const JSValue &t, const std::vector<JSValue> &a) {
            return iterate(e, Iteration::ForEach, t, a);
        }
        Completion array_map(Evaluator &e, const JSValue &t, const std::vector<JSValue> &a) {
            return iterate(e, Iteration::Map, t, a);
        }
        Completion array_filter(Evaluator &e, const JSValue &t, const std::vector<JSValue> &a) {
            return iterate(e, Iteration::Filter, t, a);
        }
        Completion array_every(Evaluator &e, const JSValue &t, const std::vector<JSValue> &a) {
            return iterate(e, Iteration::Every, t, a);
        }
        Completion array_some(Evaluator &e, const JSValue &t, const std::vector<JSValue> &a) {
            return iterate(e, Iteration::Some, t, a);
        }

        Completion reduce(Evaluator &evaluator, bool from_right, const JSValue &this_value,
                          const std::vector<JSValue> &args) {
            JSObject *object = nullptr;
            if (Completion c = this_object(evaluator, this_value, object); c.is_abrupt()) return c;

            uint32_t length = 0;
            if (Completion c = length_of(evaluator, object, length); c.is_abrupt()) return c;

            const JSValue callback = argument_at(args, 0);
            if (!callable(callback)) {
                return evaluator.throw_error(evaluator.type_error_prototype, "Callback is not a function");
            }

            JSValue accumulator;
            bool has_accumulator = args.size() > 1;
            if (has_accumulator) accumulator = args[1];

            for (uint32_t step = 0; step < length; ++step) {
                const uint32_t i = from_right ? length - step - 1 : step;
                if (!object->has_property(std::to_string(i))) continue;

                JSValue element;
                if (Completion c = get_index(evaluator, object, i, element); c.is_abrupt()) return c;

                if (!has_accumulator) {
                    accumulator = element;
                    has_accumulator = true;
                    continue;
                }

                MarkedVector call_args(evaluator.heap);
                call_args.push_back(accumulator);
                call_args.push_back(element);
                call_args.push_back(JSValue::number(i));
                call_args.push_back(JSValue::object(object));

                Completion outcome = callback.as_object()->call(evaluator, JSValue::undefined(), call_args);
                if (outcome.is_abrupt()) return outcome;

                accumulator = outcome.get_value_or_undefined();
            }

            if (!has_accumulator) {
                return evaluator.throw_error(evaluator.type_error_prototype,
                    "Reduce of empty array with no initial value");
            }

            return Completion::normal(accumulator);
        }

        Completion array_reduce(Evaluator &e, const JSValue &t, const std::vector<JSValue> &a) {
            return reduce(e, false, t, a);
        }
        Completion array_reduce_right(Evaluator &e, const JSValue &t, const std::vector<JSValue> &a) {
            return reduce(e, true, t, a);
        }

        Completion merge_sort(Evaluator &evaluator, std::vector<JSValue> &values,
                              const JSValue &comparator) {
            if (values.size() < 2) return Completion::empty();

            const auto compare = [&](const JSValue &left, const JSValue &right, bool &less_or_equal) -> Completion {
                const bool left_undefined  = left.type() == JSValueType::Undefined;
                const bool right_undefined = right.type() == JSValueType::Undefined;

                if (left_undefined || right_undefined) {
                    less_or_equal = !left_undefined;
                    return Completion::empty();
                }

                if (callable(comparator)) {
                    MarkedVector call_args(evaluator.heap);
                    call_args.push_back(left);
                    call_args.push_back(right);

                    Completion outcome = comparator.as_object()->call(evaluator, JSValue::undefined(), call_args);
                    if (outcome.is_abrupt()) return outcome;

                    Completion number = evaluator.to_number(outcome.get_value_or_undefined());
                    if (number.is_abrupt()) return number;

                    less_or_equal = number.get_value_or_undefined().to_number() <= 0;
                    return Completion::empty();
                }

                Completion left_text = evaluator.to_string(left);
                if (left_text.is_abrupt()) return left_text;

                Completion right_text = evaluator.to_string(right);
                if (right_text.is_abrupt()) return right_text;

                less_or_equal = left_text.get_value_or_undefined().to_u16string()
                             <= right_text.get_value_or_undefined().to_u16string();
                return Completion::empty();
            };

            const std::size_t middle = values.size() / 2;
            std::vector<JSValue> left(values.begin(), values.begin() + middle);
            std::vector<JSValue> right(values.begin() + middle, values.end());

            if (Completion c = merge_sort(evaluator, left, comparator);  c.is_abrupt()) return c;
            if (Completion c = merge_sort(evaluator, right, comparator); c.is_abrupt()) return c;

            std::size_t l = 0;
            std::size_t r = 0;
            std::size_t out = 0;

            while (l < left.size() && r < right.size()) {
                bool take_left = true;
                if (Completion c = compare(left[l], right[r], take_left); c.is_abrupt()) return c;

                values[out++] = take_left ? left[l++] : right[r++];
            }

            while (l < left.size())  values[out++] = left[l++];
            while (r < right.size()) values[out++] = right[r++];

            return Completion::empty();
        }

        Completion array_sort(Evaluator &evaluator, const JSValue &this_value,
                              const std::vector<JSValue> &args) {
            JSObject *object = nullptr;
            if (Completion c = this_object(evaluator, this_value, object); c.is_abrupt()) return c;

            uint32_t length = 0;
            if (Completion c = length_of(evaluator, object, length); c.is_abrupt()) return c;

            const JSValue comparator = argument_at(args, 0);
            if (comparator.type() != JSValueType::Undefined && !callable(comparator)) {
                return evaluator.throw_error(evaluator.type_error_prototype, "Comparator is not a function");
            }

            MarkedVector present(evaluator.heap);
            uint32_t holes = 0;

            for (uint32_t i = 0; i < length; ++i) {
                if (!object->has_property(std::to_string(i))) { ++holes; continue; }

                JSValue element;
                if (Completion c = get_index(evaluator, object, i, element); c.is_abrupt()) return c;

                present.push_back(element);
            }

            if (Completion c = merge_sort(evaluator, present, comparator); c.is_abrupt()) return c;

            for (std::size_t i = 0; i < present.size(); ++i) {
                if (Completion c = put_index(evaluator, object, static_cast<uint32_t>(i), present[i]); c.is_abrupt()) return c;
            }

            for (uint32_t i = 0; i < holes; ++i) {
                object->delete_own_property(std::to_string(length - holes + i));
            }

            return Completion::normal(JSValue::object(object));
        }

        Completion array_to_locale_string(Evaluator &evaluator, const JSValue &this_value,
                                          const std::vector<JSValue>&) {
            JSObject *object = nullptr;
            if (Completion c = this_object(evaluator, this_value, object); c.is_abrupt()) return c;

            uint32_t length = 0;
            if (Completion c = length_of(evaluator, object, length); c.is_abrupt()) return c;

            std::u16string result;

            for (uint32_t i = 0; i < length; ++i) {
                if (i > 0) result += u',';

                JSValue element;
                if (Completion c = get_index(evaluator, object, i, element); c.is_abrupt()) return c;

                if (element.type() == JSValueType::Undefined
                    || element.type() == JSValueType::Null) {
                    continue;
                }

                Completion self = evaluator.to_object(element);
                if (self.is_abrupt()) return self;

                Completion method = self.get_value_or_undefined().as_object()
                    ->get(evaluator, "toLocaleString");
                if (method.is_abrupt()) return method;

                if (!callable(method.get_value_or_undefined())) {
                    return evaluator.throw_error(evaluator.type_error_prototype,
                        "toLocaleString is not a function");
                }

                Completion produced = method.get_value_or_undefined().as_object()
                    ->call(evaluator, element, {});
                if (produced.is_abrupt()) return produced;

                Completion text = evaluator.to_string(produced.get_value_or_undefined());
                if (text.is_abrupt()) return text;

                result += text.get_value_or_undefined().to_u16string();
            }

            return Completion::normal(JSValue::string(result));
        }
    }

    void install_array(const Builtins &b) {
        JSObject *prototype = b.evaluator.array_prototype;

        JSObject *array_constructor = b.constructor("Array", native_array, prototype);
        b.method(array_constructor, "isArray", array_is_array, 1);

        prototype->define_own_property("length",
            PropertyDescriptor::data(JSValue::number(0), true, false, false));

        b.method(prototype, "toString",       array_to_string);
        b.method(prototype, "toLocaleString", array_to_locale_string);
        b.method(prototype, "join",           array_join, 1);
        b.method(prototype, "concat",         array_concat, 1);
        b.method(prototype, "slice",          array_slice, 2);
        b.method(prototype, "push",           array_push, 1);
        b.method(prototype, "pop",            array_pop);
        b.method(prototype, "shift",          array_shift);
        b.method(prototype, "unshift",        array_unshift, 1);
        b.method(prototype, "reverse",        array_reverse);
        b.method(prototype, "splice",         array_splice, 2);
        b.method(prototype, "sort",           array_sort, 1);
        b.method(prototype, "indexOf",        array_index_of, 1);
        b.method(prototype, "lastIndexOf",    array_last_index_of, 1);
        b.method(prototype, "forEach",        array_for_each, 1);
        b.method(prototype, "map",            array_map, 1);
        b.method(prototype, "filter",         array_filter, 1);
        b.method(prototype, "every",          array_every, 1);
        b.method(prototype, "some",           array_some, 1);
        b.method(prototype, "reduce",         array_reduce, 1);
        b.method(prototype, "reduceRight",    array_reduce_right, 1);
    }
}