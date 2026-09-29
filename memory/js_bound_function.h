#ifndef PERUNEJS_JS_BOUND_FUNCTION_H
#define PERUNEJS_JS_BOUND_FUNCTION_H


#include <vector>

#include "js_object.h"
#include "js_value.h"

namespace perunejs {
    class JSBoundFunction : public JSObject {
    public:
        JSObject *target = nullptr;
        JSValue bound_this;
        std::vector<JSValue> bound_args;

        bool is_callable() const override { return true; }
        const char *class_name() override { return "Function"; }

        Completion call(Evaluator &evaluator, const JSValue &this_value,
                        const std::vector<JSValue> &args) override;

        Completion construct(Evaluator &evaluator, const std::vector<JSValue> &args) override;
        
        Completion has_instance(Evaluator &evaluator, const JSValue &value) override;

        void trace(CellVisitor &visitor) const override {
            JSObject::trace(visitor);

            visitor.visit(target);
            if (JSObject *object = bound_this.as_object()) visitor.visit(object);

            for (const JSValue &argument : bound_args) {
                if (JSObject *object = argument.as_object()) visitor.visit(object);
            }
        }
    };
}


#endif //PERUNEJS_JS_BOUND_FUNCTION_H
