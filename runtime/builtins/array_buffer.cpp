#include "runtime/builtins.h"
#include "memory/js_array_buffer.h"

namespace perunejs {
    namespace {
        Completion to_length(Evaluator &e, const JSValue &value, std::size_t &out) {
            Completion number = e.to_number(value);
            if (number.is_abrupt()) return number;

            const double raw = number.get_value_or_undefined().to_number();

            if (std::isnan(raw) || raw < 0) {
                return e.throw_error(e.range_error_prototype, "Invalid length");
            }

            out = static_cast<std::size_t>(raw);

            return Completion::empty();
        }

        Completion this_buffer(Evaluator &e, const JSValue &value, JSArrayBuffer *&out) {
            if (value.type() == JSValueType::Object) {
                if (auto *buffer = dynamic_cast<JSArrayBuffer *>(value.as_object()); buffer != nullptr) {
                    out = buffer;
                    return Completion::empty();
                }
            }

            return e.throw_error(e.type_error_prototype, "Receiver is not an ArrayBuffer");
        }

        Completion this_typed(Evaluator &e, const JSValue &value, JSTypedArray *&out) {
            if (value.type() == JSValueType::Object) {
                if (auto *array = dynamic_cast<JSTypedArray *>(value.as_object()); array != nullptr) {
                    out = array;
                    return Completion::empty();
                }
            }

            return e.throw_error(e.type_error_prototype, "Receiver is not a typed array");
        }

        Completion call_array_buffer(Evaluator &e, const JSValue&, const std::vector<JSValue>&) {
            return e.throw_error(e.type_error_prototype, "Constructor ArrayBuffer requires 'new'");
        }

        Completion construct_array_buffer(Evaluator &e, const JSValue&, const std::vector<JSValue> &args) {
            std::size_t length = 0;
            if (Completion c = to_length(e, argument_at(args, 0), length); c.is_abrupt()) return c;

            auto *buffer = e.heap.allocate<JSArrayBuffer>(length);
            buffer->prototype = e.array_buffer_prototype;

            return Completion::normal(JSValue::object(buffer));
        }

        Completion buffer_byte_length(Evaluator &e, const JSValue &self, const std::vector<JSValue>&) {
            JSArrayBuffer *buffer = nullptr;
            if (Completion c = this_buffer(e, self, buffer); c.is_abrupt()) return c;

            return Completion::normal(JSValue::number(static_cast<double>(buffer->byte_length())));
        }

        Completion buffer_slice(Evaluator &e, const JSValue &self, const std::vector<JSValue> &args) {
            JSArrayBuffer *buffer = nullptr;
            if (Completion c = this_buffer(e, self, buffer); c.is_abrupt()) return c;

            const auto total = static_cast<double>(buffer->byte_length());

            const auto resolve = [&](const JSValue &value, const double fallback, double &out) -> Completion {
                if (value.type() == JSValueType::Undefined) { out = fallback; return Completion::empty(); }

                Completion number = e.to_number(value);
                if (number.is_abrupt()) return number;

                double at = number.get_value_or_undefined().to_number();
                if (std::isnan(at)) at = 0;
                if (at < 0) at += total;

                out = at < 0 ? 0 : (at > total ? total : at);

                return Completion::empty();
            };

            double from = 0;
            double to = total;
            if (Completion c = resolve(argument_at(args, 0), 0, from); c.is_abrupt()) return c;
            if (Completion c = resolve(argument_at(args, 1), total, to); c.is_abrupt()) return c;

            const std::size_t size = to > from ? static_cast<std::size_t>(to - from) : 0;

            auto *copy = e.heap.allocate<JSArrayBuffer>(size);
            copy->prototype = e.array_buffer_prototype;

            for (std::size_t i = 0; i < size; ++i) {
                copy->bytes[i] = buffer->bytes[static_cast<std::size_t>(from) + i];
            }

            return Completion::normal(JSValue::object(copy));
        }

        Completion buffer_is_view(Evaluator&, const JSValue&, const std::vector<JSValue> &args) {
            const JSValue value = argument_at(args, 0);

            const bool view = value.type() == JSValueType::Object
                && dynamic_cast<JSTypedArray *>(value.as_object()) != nullptr;

            return Completion::normal(JSValue::boolean(view));
        }

        Completion construct_typed(Evaluator &e, const ElementKind kind, const std::vector<JSValue> &args) {
            const JSValue first = argument_at(args, 0);
            const std::size_t width = element_size(kind);

            if (first.type() == JSValueType::Object) {
                if (auto *buffer = dynamic_cast<JSArrayBuffer *>(first.as_object()); buffer != nullptr) {
                    std::size_t offset = 0;
                    if (argument_at(args, 1).type() != JSValueType::Undefined) {
                        if (Completion c = to_length(e, args[1], offset); c.is_abrupt()) return c;
                    }

                    if (offset % width != 0 || offset > buffer->byte_length()) {
                        return e.throw_error(e.range_error_prototype, "Invalid typed array offset");
                    }

                    std::size_t count = (buffer->byte_length() - offset) / width;
                    if (argument_at(args, 2).type() != JSValueType::Undefined) {
                        if (Completion c = to_length(e, args[2], count); c.is_abrupt()) return c;

                        if (offset + count * width > buffer->byte_length()) {
                            return e.throw_error(e.range_error_prototype, "Invalid typed array length");
                        }
                    }

                    auto *array = e.heap.allocate<JSTypedArray>(buffer, kind, offset, count);
                    array->prototype = e.typed_array_prototypes[static_cast<std::size_t>(kind)];

                    return Completion::normal(JSValue::object(array));
                }

                JSObject *source = first.as_object();

                Completion raw = source->get(e, "length");
                if (raw.is_abrupt()) return raw;

                std::size_t count = 0;
                if (Completion c = to_length(e, raw.get_value_or_undefined(), count); c.is_abrupt()) return c;

                auto *buffer = e.heap.allocate<JSArrayBuffer>(count * width);
                buffer->prototype = e.array_buffer_prototype;

                auto *array = e.heap.allocate<JSTypedArray>(buffer, kind, 0, count);
                array->prototype = e.typed_array_prototypes[static_cast<std::size_t>(kind)];

                for (std::size_t i = 0; i < count; ++i) {
                    Completion element = source->get(e, std::to_string(i));
                    if (element.is_abrupt()) return element;

                    Completion number = e.to_number(element.get_value_or_undefined());
                    if (number.is_abrupt()) return number;

                    array->write(i, number.get_value_or_undefined().to_number());
                }

                return Completion::normal(JSValue::object(array));
            }

            std::size_t count = 0;
            if (first.type() != JSValueType::Undefined) {
                if (Completion c = to_length(e, first, count); c.is_abrupt()) return c;
            }

            auto *buffer = e.heap.allocate<JSArrayBuffer>(count * width);
            buffer->prototype = e.array_buffer_prototype;

            auto *array = e.heap.allocate<JSTypedArray>(buffer, kind, 0, count);
            array->prototype = e.typed_array_prototypes[static_cast<std::size_t>(kind)];

            return Completion::normal(JSValue::object(array));
        }

        template <ElementKind KIND>
        Completion construct_of(Evaluator &e, const JSValue&, const std::vector<JSValue> &args) {
            return construct_typed(e, KIND, args);
        }

        template <ElementKind KIND>
        Completion call_of(Evaluator &e, const JSValue&, const std::vector<JSValue>&) {
            return e.throw_error(e.type_error_prototype,
                std::string("Constructor ") + element_name(KIND) + " requires 'new'");
        }

        Completion typed_length(Evaluator &e, const JSValue &self, const std::vector<JSValue>&) {
            JSTypedArray *array = nullptr;
            if (Completion c = this_typed(e, self, array); c.is_abrupt()) return c;

            return Completion::normal(JSValue::number(static_cast<double>(array->length)));
        }

        Completion typed_byte_length(Evaluator &e, const JSValue &self, const std::vector<JSValue>&) {
            JSTypedArray *array = nullptr;
            if (Completion c = this_typed(e, self, array); c.is_abrupt()) return c;

            return Completion::normal(JSValue::number(static_cast<double>(array->byte_length())));
        }

        Completion typed_byte_offset(Evaluator &e, const JSValue &self, const std::vector<JSValue>&) {
            JSTypedArray *array = nullptr;
            if (Completion c = this_typed(e, self, array); c.is_abrupt()) return c;

            return Completion::normal(JSValue::number(static_cast<double>(array->byte_offset)));
        }

        Completion typed_buffer(Evaluator &e, const JSValue &self, const std::vector<JSValue>&) {
            JSTypedArray *array = nullptr;
            if (Completion c = this_typed(e, self, array); c.is_abrupt()) return c;

            return Completion::normal(array->buffer != nullptr
                ? JSValue::object(array->buffer)
                : JSValue::undefined());
        }

        Completion typed_set(Evaluator &e, const JSValue &self, const std::vector<JSValue> &args) {
            JSTypedArray *array = nullptr;
            if (Completion c = this_typed(e, self, array); c.is_abrupt()) return c;

            const JSValue source = argument_at(args, 0);
            if (source.type() != JSValueType::Object) {
                return e.throw_error(e.type_error_prototype, "set expects an array-like");
            }

            std::size_t at = 0;
            if (argument_at(args, 1).type() != JSValueType::Undefined) {
                if (Completion c = to_length(e, args[1], at); c.is_abrupt()) return c;
            }

            JSObject *from = source.as_object();

            Completion raw = from->get(e, "length");
            if (raw.is_abrupt()) return raw;

            std::size_t count = 0;
            if (Completion c = to_length(e, raw.get_value_or_undefined(), count); c.is_abrupt()) return c;

            if (at + count > array->length) {
                return e.throw_error(e.range_error_prototype, "Source is too large");
            }

            for (std::size_t i = 0; i < count; ++i) {
                Completion element = from->get(e, std::to_string(i));
                if (element.is_abrupt()) return element;

                Completion number = e.to_number(element.get_value_or_undefined());
                if (number.is_abrupt()) return number;

                array->write(at + i, number.get_value_or_undefined().to_number());
            }

            return Completion::normal(JSValue::undefined());
        }

        Completion typed_subarray(Evaluator &e, const JSValue &self, const std::vector<JSValue> &args) {
            JSTypedArray *array = nullptr;
            if (Completion c = this_typed(e, self, array); c.is_abrupt()) return c;

            const auto total = static_cast<double>(array->length);

            const auto resolve = [&](const JSValue &value, const double fallback, double &out) -> Completion {
                if (value.type() == JSValueType::Undefined) { out = fallback; return Completion::empty(); }

                Completion number = e.to_number(value);
                if (number.is_abrupt()) return number;

                double at = number.get_value_or_undefined().to_number();
                if (std::isnan(at)) at = 0;
                if (at < 0) at += total;

                out = at < 0 ? 0 : (at > total ? total : at);

                return Completion::empty();
            };

            double from = 0;
            double to = total;
            if (Completion c = resolve(argument_at(args, 0), 0, from); c.is_abrupt()) return c;
            if (Completion c = resolve(argument_at(args, 1), total, to); c.is_abrupt()) return c;

            const std::size_t count = to > from ? static_cast<std::size_t>(to - from) : 0;
            const std::size_t width = element_size(array->kind);

            auto *view = e.heap.allocate<JSTypedArray>(array->buffer, array->kind,
                array->byte_offset + static_cast<std::size_t>(from) * width, count);
            view->prototype = e.typed_array_prototypes[static_cast<std::size_t>(array->kind)];

            return Completion::normal(JSValue::object(view));
        }

        Completion typed_fill(Evaluator &e, const JSValue &self, const std::vector<JSValue> &args) {
            JSTypedArray *array = nullptr;
            if (Completion c = this_typed(e, self, array); c.is_abrupt()) return c;

            Completion number = e.to_number(argument_at(args, 0));
            if (number.is_abrupt()) return number;

            const double value = number.get_value_or_undefined().to_number();
            for (std::size_t i = 0; i < array->length; ++i) array->write(i, value);

            return Completion::normal(self);
        }

        void install_typed(const Builtins &b, const ElementKind kind, NativeFunction call,
                           NativeFunction construct) {
            Evaluator &evaluator = b.evaluator;
            const auto slot = static_cast<std::size_t>(kind);

            JSObject *prototype = evaluator.typed_array_prototypes[slot];

            b.method(prototype, "set",      typed_set, 1);
            b.method(prototype, "subarray", typed_subarray, 2);
            b.method(prototype, "fill",     typed_fill, 1);

            prototype->define_own_property("length",
                PropertyDescriptor::accessor(b.function("length", typed_length, 0), nullptr, false, true));
            prototype->define_own_property("byteLength",
                PropertyDescriptor::accessor(b.function("byteLength", typed_byte_length, 0), nullptr, false, true));
            prototype->define_own_property("byteOffset",
                PropertyDescriptor::accessor(b.function("byteOffset", typed_byte_offset, 0), nullptr, false, true));
            prototype->define_own_property("buffer",
                PropertyDescriptor::accessor(b.function("buffer", typed_buffer, 0), nullptr, false, true));

            JSObject *constructor = b.constructor(element_name(kind), call, prototype, construct, 3);
            b.constant(constructor, "BYTES_PER_ELEMENT",
                JSValue::number(static_cast<double>(element_size(kind))));
            b.constant(prototype, "BYTES_PER_ELEMENT",
                JSValue::number(static_cast<double>(element_size(kind))));
        }
    }

    void install_array_buffer(const Builtins &b) {
        Evaluator &evaluator = b.evaluator;

        JSObject *prototype = evaluator.array_buffer_prototype;

        b.method(prototype, "slice", buffer_slice, 2);
        prototype->define_own_property("byteLength",
            PropertyDescriptor::accessor(b.function("byteLength", buffer_byte_length, 0), nullptr, false, true));

        JSObject *constructor = b.constructor("ArrayBuffer", call_array_buffer, prototype,
                                              construct_array_buffer, 1);
        b.method(constructor, "isView", buffer_is_view, 1);

        install_typed(b, ElementKind::Int8,         call_of<ElementKind::Int8>,         construct_of<ElementKind::Int8>);
        install_typed(b, ElementKind::Uint8,        call_of<ElementKind::Uint8>,        construct_of<ElementKind::Uint8>);
        install_typed(b, ElementKind::Uint8Clamped, call_of<ElementKind::Uint8Clamped>, construct_of<ElementKind::Uint8Clamped>);
        install_typed(b, ElementKind::Int16,        call_of<ElementKind::Int16>,        construct_of<ElementKind::Int16>);
        install_typed(b, ElementKind::Uint16,       call_of<ElementKind::Uint16>,       construct_of<ElementKind::Uint16>);
        install_typed(b, ElementKind::Int32,        call_of<ElementKind::Int32>,        construct_of<ElementKind::Int32>);
        install_typed(b, ElementKind::Uint32,       call_of<ElementKind::Uint32>,       construct_of<ElementKind::Uint32>);
        install_typed(b, ElementKind::Float32,      call_of<ElementKind::Float32>,      construct_of<ElementKind::Float32>);
        install_typed(b, ElementKind::Float64,      call_of<ElementKind::Float64>,      construct_of<ElementKind::Float64>);
    }
}
