
#include "embed/host_function.h"

#include "embed/error.h"
#include "embed/runtime.h"

namespace perunejs::embed {
    Completion JSHostFunction::call(Evaluator &evaluator, const JSValue &this_value,
                                    const std::vector<JSValue> &args) {
        if (callback == nullptr) return Completion::normal(JSValue::undefined());

        try {
            return Completion::normal(callback(*runtime, this_value, args));
        } catch (const JSError &error) {
            const JSValue thrown = error.value();

            if (thrown.type() != JSValueType::Undefined) {
                return Completion::throw_value(thrown);
            }

            return evaluator.throw_error(evaluator.error_prototype, error.what());
        } catch (const std::exception &error) {
            return evaluator.throw_error(evaluator.error_prototype,
                std::string("Host function failed: ") + error.what());
        }
    }
}
