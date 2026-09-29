
#include "embed/host_object.h"

#include "embed/error.h"
#include "embed/runtime.h"

namespace perunejs::embed {
    JSValue HostObject::get(Runtime &, const std::string &) { return JSValue::undefined(); }

    void HostObject::set(Runtime &, const std::string &, const JSValue &) {}

    std::vector<std::string> HostObject::property_names(Runtime &) { return {}; }

    const PropertyDescriptor *JSHostObject::materialize(const std::string &name,
                                                        const PropertyDescriptor &descriptor) const {
        auto *self = const_cast<JSHostObject *>(this);
        self->store_property(name, descriptor);

        return self->JSObject::get_own_property(name);
    }

    bool JSHostObject::host_provides(const std::string &name) const {
        for (const std::string &known : host->property_names(*runtime)) {
            if (known == name) return true;
        }

        return false;
    }

    const PropertyDescriptor *JSHostObject::get_own_property(const std::string &name) const {
        if (!host_provides(name)) return nullptr;

        JSValue value;
        try {
            value = host->get(*runtime, name);
        } catch (const std::exception &) {
            return nullptr;
        }

        return materialize(name, PropertyDescriptor::data(value, true, true, true));
    }

    Completion JSHostObject::get(Evaluator &evaluator, const std::string &name) {
        try {
            return Completion::normal(host->get(*runtime, name));
        } catch (const JSError &error) {
            const JSValue thrown = error.value();

            if (thrown.type() != JSValueType::Undefined) return Completion::throw_value(thrown);

            return evaluator.throw_error(evaluator.error_prototype, error.what());
        } catch (const std::exception &error) {
            return evaluator.throw_error(evaluator.error_prototype,
                std::string("Host object failed: ") + error.what());
        }
    }

    Completion JSHostObject::put(Evaluator &evaluator, const std::string &name,
                                 const JSValue &value, const bool) {
        try {
            host->set(*runtime, name, value);

            return Completion::empty();
        } catch (const JSError &error) {
            const JSValue thrown = error.value();

            if (thrown.type() != JSValueType::Undefined) return Completion::throw_value(thrown);

            return evaluator.throw_error(evaluator.error_prototype, error.what());
        } catch (const std::exception &error) {
            return evaluator.throw_error(evaluator.error_prototype,
                std::string("Host object failed: ") + error.what());
        }
    }

    std::vector<std::string> JSHostObject::own_keys() const {
        return host->property_names(*runtime);
    }
}
