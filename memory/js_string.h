#ifndef PERUNEJS_JS_STRING_H
#define PERUNEJS_JS_STRING_H

#include <string>
#include <vector>

#include "js_object.h"
#include "js_value.h"

namespace perunejs {
    class JSString : public JSObject {
    public:
        std::u16string value;

        explicit JSString(std::u16string value) : value(std::move(value)) {}

        const char *class_name() override { return "String"; }

        const PropertyDescriptor *get_own_property(const std::string &name) const override {
            if (const PropertyDescriptor *own = JSObject::get_own_property(name)) return own;


            if (name == "length") {
                return materialize(name, PropertyDescriptor::data(
                    JSValue::number(static_cast<double>(value.size())), false, false, false));
            }

            uint32_t index = 0;
            if (is_array_index(name, index) && index < value.size()) {
                return materialize(name, PropertyDescriptor::data(
                    JSValue::string(std::u16string(1, value[index])), false, true, false));
            }

            return nullptr;
        }

        bool define_own_property(const std::string &name, const PropertyDescriptor &desc) override {
            get_own_property(name);
            return JSObject::define_own_property(name, desc);
        }

        bool delete_own_property(const std::string &name) override {
            if (get_own_property(name) == nullptr) return true;
            return JSObject::delete_own_property(name);
        }

        std::vector<std::string> own_keys() const override {
            std::vector<std::string> keys;
            keys.reserve(value.size());

            for (std::size_t i = 0; i < value.size(); ++i) {
                keys.push_back(std::to_string(i));
            }

            for (const std::string &name : JSObject::own_keys()) {
                uint32_t index = 0;
                if (name == "length") continue;
                if (is_array_index(name, index) && index < value.size()) continue;

                keys.push_back(name);
            }

            return keys;
        }

    private:

        const PropertyDescriptor *materialize(const std::string &name,
                                              const PropertyDescriptor &descriptor) const {
            auto *self = const_cast<JSString *>(this);
            self->store_property(name, descriptor);
            return self->JSObject::get_own_property(name);
        }
    };
}


#endif //PERUNEJS_JS_STRING_H
