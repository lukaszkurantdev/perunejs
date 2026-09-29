#ifndef PERUNEJS_JS_ARRAY_H
#define PERUNEJS_JS_ARRAY_H
#include "js_object.h"

namespace perunejs {
    class JSArray : public JSObject {
    public:
        Completion coerce_defined_value(Evaluator &evaluator, const std::string &name,
                                        PropertyDescriptor &desc) override {
            if (name != "length" || !desc.value.has_value()) return Completion::empty();

            Completion number = evaluator.to_number(*desc.value);
            if (number.is_abrupt()) return number;

            desc.value = number.get_value_or_undefined();
            return Completion::empty();
        }

        bool rejects_with_range_error(const std::string &name,
                                      const PropertyDescriptor &desc) const override {
            if (name != "length" || !desc.value.has_value()) return false;

            const double raw = desc.value->to_number();

            return static_cast<double>(static_cast<uint32_t>(raw)) != raw;
        }

        const char* class_name() override { return "Array"; }

        bool define_own_property(const std::string &name,
                                 const PropertyDescriptor &desc) override {
            if (name == "length") {
                if (!desc.value.has_value()) return JSObject::define_own_property(name, desc);

                const double raw = desc.value->to_number();
                const uint32_t new_length = static_cast<uint32_t>(raw);

                if (static_cast<double>(new_length) != raw) return false;


                const uint32_t old_length = length();

                for (uint32_t i = old_length; i > new_length; --i) {
                    delete_own_property(std::to_string(i - 1));
                }

                PropertyDescriptor updated = desc;
                updated.value = JSValue::number(new_length);
                return JSObject::define_own_property("length", updated);
            }

            uint32_t index = 0;
            if (is_array_index(name, index)) {
                if (!JSObject::define_own_property(name, desc)) return false;

                if (index >= length()) {
                    PropertyDescriptor grown;
                    grown.value = JSValue::number(static_cast<double>(index) + 1);
                    JSObject::define_own_property("length", grown);
                }
                return true;
            }

            return JSObject::define_own_property(name, desc);
        }

        uint32_t length() const {
            const PropertyDescriptor *descriptor = get_own_property("length");
            return descriptor ? static_cast<uint32_t>(descriptor->get_value().to_number()) : 0;
        }
    };
}

#endif //PERUNEJS_JS_ARRAY_H
