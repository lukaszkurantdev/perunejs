
#ifndef PERUNEJS_EMBED_HOST_FUNCTION_H
#define PERUNEJS_EMBED_HOST_FUNCTION_H

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "memory/js_object.h"
#include "memory/js_value.h"

namespace perunejs::embed {
    class Runtime;

    using HostFunctionCallback = std::function<JSValue(Runtime &runtime,
                                                       const JSValue &this_value,
                                                       const std::vector<JSValue> &args)>;

    class JSHostFunction final : public JSObject {
        Runtime *runtime;
        HostFunctionCallback callback;

    public:
        std::string name;
        std::shared_ptr<void> host_data;

        JSHostFunction(Runtime &runtime, std::string name, HostFunctionCallback callback)
            : runtime(&runtime), callback(std::move(callback)), name(std::move(name)) {}

        bool is_callable() const override { return true; }
        const char *class_name() override { return "Function"; }

        Completion call(Evaluator &evaluator, const JSValue &this_value,
                        const std::vector<JSValue> &args) override;
    };
}

#endif //PERUNEJS_EMBED_HOST_FUNCTION_H
