#ifndef PERUNEJS_JS_ARRAY_BUFFER_H
#define PERUNEJS_JS_ARRAY_BUFFER_H

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "js_object.h"
#include "js_value.h"

namespace perunejs {
    class JSArrayBuffer : public JSObject {
    public:
        std::vector<uint8_t> bytes;
        bool detached = false;

        explicit JSArrayBuffer(std::size_t length) : bytes(length, 0) {}

        const char *class_name() override { return "ArrayBuffer"; }

        std::size_t byte_length() const { return detached ? 0 : bytes.size(); }

        uint8_t *data() { return bytes.empty() ? nullptr : bytes.data(); }
    };

    enum class ElementKind : uint8_t {
        Int8, Uint8, Uint8Clamped, Int16, Uint16, Int32, Uint32, Float32, Float64
    };

    inline std::size_t element_size(const ElementKind kind) {
        switch (kind) {
            case ElementKind::Int8:
            case ElementKind::Uint8:
            case ElementKind::Uint8Clamped: return 1;
            case ElementKind::Int16:
            case ElementKind::Uint16:       return 2;
            case ElementKind::Int32:
            case ElementKind::Uint32:
            case ElementKind::Float32:      return 4;
            case ElementKind::Float64:      return 8;
        }

        return 1;
    }

    inline const char *element_name(const ElementKind kind) {
        switch (kind) {
            case ElementKind::Int8:         return "Int8Array";
            case ElementKind::Uint8:        return "Uint8Array";
            case ElementKind::Uint8Clamped: return "Uint8ClampedArray";
            case ElementKind::Int16:        return "Int16Array";
            case ElementKind::Uint16:       return "Uint16Array";
            case ElementKind::Int32:        return "Int32Array";
            case ElementKind::Uint32:       return "Uint32Array";
            case ElementKind::Float32:      return "Float32Array";
            case ElementKind::Float64:      return "Float64Array";
        }

        return "TypedArray";
    }

    class JSTypedArray : public JSObject {
    public:
        JSArrayBuffer *buffer = nullptr;
        ElementKind kind = ElementKind::Uint8;
        std::size_t byte_offset = 0;
        std::size_t length = 0;   // liczba elementów, nie bajtów

        JSTypedArray(JSArrayBuffer *buffer, ElementKind kind, std::size_t byte_offset, std::size_t length)
            : buffer(buffer), kind(kind), byte_offset(byte_offset), length(length) {}

        const char *class_name() override { return element_name(kind); }

        std::size_t byte_length() const { return length * element_size(kind); }

        double read(const std::size_t at) const {
            if (buffer == nullptr || buffer->detached || at >= length) return 0;

            const uint8_t *from = buffer->bytes.data() + byte_offset + at * element_size(kind);

            switch (kind) {
                case ElementKind::Int8:   { int8_t v;   std::memcpy(&v, from, 1); return v; }
                case ElementKind::Uint8:
                case ElementKind::Uint8Clamped: { uint8_t v; std::memcpy(&v, from, 1); return v; }
                case ElementKind::Int16:  { int16_t v;  std::memcpy(&v, from, 2); return v; }
                case ElementKind::Uint16: { uint16_t v; std::memcpy(&v, from, 2); return v; }
                case ElementKind::Int32:  { int32_t v;  std::memcpy(&v, from, 4); return v; }
                case ElementKind::Uint32: { uint32_t v; std::memcpy(&v, from, 4); return v; }
                case ElementKind::Float32:{ float v;    std::memcpy(&v, from, 4); return v; }
                case ElementKind::Float64:{ double v;   std::memcpy(&v, from, 8); return v; }
            }

            return 0;
        }

        void write(const std::size_t at, const double value) {
            if (buffer == nullptr || buffer->detached || at >= length) return;

            uint8_t *to = buffer->bytes.data() + byte_offset + at * element_size(kind);

            switch (kind) {
                case ElementKind::Int8:   { const auto v = static_cast<int8_t>(to_integer(value));   std::memcpy(to, &v, 1); return; }
                case ElementKind::Uint8:  { const auto v = static_cast<uint8_t>(to_integer(value));  std::memcpy(to, &v, 1); return; }
                case ElementKind::Uint8Clamped: { const auto v = clamp_byte(value); std::memcpy(to, &v, 1); return; }
                case ElementKind::Int16:  { const auto v = static_cast<int16_t>(to_integer(value));  std::memcpy(to, &v, 2); return; }
                case ElementKind::Uint16: { const auto v = static_cast<uint16_t>(to_integer(value)); std::memcpy(to, &v, 2); return; }
                case ElementKind::Int32:  { const auto v = static_cast<int32_t>(to_integer(value));  std::memcpy(to, &v, 4); return; }
                case ElementKind::Uint32: { const auto v = static_cast<uint32_t>(to_integer(value)); std::memcpy(to, &v, 4); return; }
                case ElementKind::Float32:{ const auto v = static_cast<float>(value);  std::memcpy(to, &v, 4); return; }
                case ElementKind::Float64:{ const double v = value;                    std::memcpy(to, &v, 8); return; }
            }
        }

        // 7.1.5 ToInt32 / ToUint32 — modulo 2^32 z obcięciem w stronę zera.
        static int64_t to_integer(const double value) {
            if (std::isnan(value) || std::isinf(value)) return 0;

            const double truncated = std::trunc(value);
            const double wrapped = std::fmod(truncated, 4294967296.0);

            return static_cast<int64_t>(wrapped);
        }

        static uint8_t clamp_byte(const double value) {
            if (std::isnan(value) || value <= 0) return 0;
            if (value >= 255) return 255;

            const double floored = std::floor(value);
            const double rest = value - floored;

            if (rest < 0.5) return static_cast<uint8_t>(floored);
            if (rest > 0.5) return static_cast<uint8_t>(floored) + 1;

            return static_cast<uint8_t>(floored) % 2 == 0
                ? static_cast<uint8_t>(floored)
                : static_cast<uint8_t>(floored) + 1;
        }

        const PropertyDescriptor *get_own_property(const std::string &name) const override {
            uint32_t at = 0;
            if (is_array_index(name, at)) {
                if (at >= length) return nullptr;

                return materialize(name,
                    PropertyDescriptor::data(JSValue::number(read(at)), true, true, true));
            }

            return JSObject::get_own_property(name);
        }

        bool define_own_property(const std::string &name, const PropertyDescriptor &desc) override {
            uint32_t at = 0;
            if (is_array_index(name, at)) {
                if (at >= length) return false;
                if (desc.value.has_value()) write(at, desc.value->to_number());

                return true;
            }

            return JSObject::define_own_property(name, desc);
        }

        std::vector<std::string> own_keys() const override {
            std::vector<std::string> keys;
            keys.reserve(length);

            for (std::size_t i = 0; i < length; ++i) keys.push_back(std::to_string(i));

            for (const std::string &name : JSObject::own_keys()) {
                uint32_t at = 0;
                if (is_array_index(name, at) && at < length) continue;

                keys.push_back(name);
            }

            return keys;
        }

        void trace(CellVisitor &visitor) const override {
            JSObject::trace(visitor);
            if (buffer != nullptr) visitor.visit(buffer);
        }

    private:
        const PropertyDescriptor *materialize(const std::string &name,
                                              const PropertyDescriptor &descriptor) const {
            auto *self = const_cast<JSTypedArray *>(this);
            self->store_property(name, descriptor);

            return self->JSObject::get_own_property(name);
        }
    };
}

#endif //PERUNEJS_JS_ARRAY_BUFFER_H
