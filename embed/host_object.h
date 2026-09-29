

#ifndef PERUNEJS_EMBED_HOST_OBJECT_H
#define PERUNEJS_EMBED_HOST_OBJECT_H

#include <memory>
#include <string>
#include <vector>

#include "memory/js_object.h"
#include "memory/js_value.h"

namespace perunejs::embed {
    class Runtime;

    class HostObject {
    public:
        virtual ~HostObject() = default;

        virtual JSValue get(Runtime &runtime, const std::string &name);
        virtual void set(Runtime &runtime, const std::string &name, const JSValue &value);
        virtual std::vector<std::string> property_names(Runtime &runtime);
    };

    class JSHostObject final : public JSObject {
        Runtime *runtime;
        std::shared_ptr<HostObject> host;

    public:
        JSHostObject(Runtime &runtime, std::shared_ptr<HostObject> host)
            : runtime(&runtime), host(std::move(host)) {}

        const std::shared_ptr<HostObject> &target() const { return host; }

        const char *class_name() override { return "Object"; }

        bool intercepts_lookup() const override { return true; }

        const PropertyDescriptor *get_own_property(const std::string &name) const override;

        Completion get(Evaluator &evaluator, const std::string &name) override;
        Completion put(Evaluator &evaluator, const std::string &name,
                       const JSValue &value, bool is_throw) override;

        std::vector<std::string> own_keys() const override;

    private:
        bool host_provides(const std::string &name) const;

        const PropertyDescriptor *materialize(const std::string &name,
                                              const PropertyDescriptor &descriptor) const;
    };
}

#endif //PERUNEJS_EMBED_HOST_OBJECT_H
