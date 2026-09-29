#include "js_function.h"
#include "evaluator/evaluator.h"

namespace perunejs {
    Completion JSFunction::call(Evaluator &evaluator, const JSValue& this_value, const std::vector<JSValue> &args) {
        return evaluator.call_function(this, this_value, args);
    }

    Completion JSFunction::construct(Evaluator &evaluator, const std::vector<JSValue> &args) {
        JSObject *instance = evaluator.heap.allocate<JSObject>();

        Completion prototype_value = get(evaluator, "prototype");
        if (prototype_value.is_abrupt()) return prototype_value;

        const JSValue prototype = prototype_value.get_value_or_undefined();
        instance->prototype = prototype.type() == JSValueType::Object
                            ? prototype.as_object()
                            : evaluator.object_prototype;

        Completion result = call(evaluator, JSValue::object(instance), args);
        if (result.is_abrupt()) return result;

        const JSValue returned = result.get_value_or_undefined();

        if (returned.type() == JSValueType::Object) return Completion::normal(returned);

        return Completion::normal(JSValue::object(instance));
    }
}
