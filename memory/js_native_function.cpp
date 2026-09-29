#include "memory/js_native_function.h"
#include "evaluator/evaluator.h"

namespace perunejs {

    Completion JSNativeFunction::construct(Evaluator &evaluator, const std::vector<JSValue> &args) {
        if (!constructible) {
            return evaluator.throw_error(evaluator.type_error_prototype, name + " is not a constructor");
        }

        if (construct_function != nullptr) {
            return construct_function(evaluator, JSValue::undefined(), args);
        }

        return call(evaluator, JSValue::undefined(), args);
    }

}