#ifndef PERUNEJS_JS_NATIVE_FUNCTION_H
#define PERUNEJS_JS_NATIVE_FUNCTION_H
#include <utility>
#include <vector>

#include "js_object.h"
#include "js_value.h"

namespace perunejs {
    class Evaluator;

    using NativeFunction = Completion (*)(Evaluator&, const JSValue&, const std::vector<JSValue>&);

    class JSNativeFunction : public JSObject {
    public:
        std::string name;
        NativeFunction function = nullptr;
        NativeFunction construct_function = nullptr;
        bool constructible = false;

        JSNativeFunction(std::string  name, NativeFunction function, bool constructible = false) :
            name(std::move(name)), function(function), constructible(constructible) {}

        Completion construct(Evaluator &evaluator, const std::vector<JSValue> &args) override;

        bool is_callable() const override { return true;}
        const char* class_name() override { return "Function";}

        Completion call(Evaluator& evaluator, const JSValue& value, const std::vector<JSValue>& args) override {
            return function(evaluator, value, args);
        };

        void trace(CellVisitor& visitor) const override {
            JSObject::trace(visitor);
        }
    };
}

#endif //PERUNEJS_JS_NATIVE_FUNCTION_H
