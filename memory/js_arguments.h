#ifndef PERUNEJS_JS_ARGUMENTS_H
#define PERUNEJS_JS_ARGUMENTS_H

#include <string>
#include <unordered_map>

#include "environment.h"
#include "js_object.h"

namespace perunejs {
    class JSArguments : public JSObject {
    public:
        DeclarativeEnvironment *env = nullptr;
        std::unordered_map<std::string, std::string> mapped;

        const char *class_name() override { return "Arguments"; }

        const PropertyDescriptor *get_own_property(const std::string &name) const override {
            sync_from_binding(name);
            return JSObject::get_own_property(name);
        }

        bool define_own_property(const std::string &name, const PropertyDescriptor &desc) override {
            if (!JSObject::define_own_property(name, desc)) return false;

            const auto entry = mapped.find(name);
            if (entry == mapped.end()) return true;

            if (desc.value.has_value()) {
                env->raw_set_binding(entry->second, *desc.value);
            }

            if (desc.is_accessor() || (desc.writable.has_value() && !*desc.writable)) {
                mapped.erase(entry);
            }

            return true;
        }

        bool delete_own_property(const std::string &name) override {
            const bool deleted = JSObject::delete_own_property(name);
            if (deleted) mapped.erase(name);
            return deleted;
        }

        void trace(CellVisitor &visitor) const override {
            JSObject::trace(visitor);
            visitor.visit(env);
        }

    private:
        void sync_from_binding(const std::string &name) const {
            const auto entry = mapped.find(name);
            if (entry == mapped.end()) return;

            const PropertyDescriptor *current = JSObject::get_own_property(name);
            if (current == nullptr) return;

            PropertyDescriptor updated = *current;
            updated.value = env->raw_binding_value(entry->second);

            const_cast<JSArguments *>(this)->store_property(name, updated);
        }
    };
}


#endif //PERUNEJS_JS_ARGUMENTS_H
