
#include "runtime/builtins.h"

#include "memory/js_wrapper.h"

#include <cstring>

namespace perunejs {
    namespace {
        Completion this_boolean(Evaluator &evaluator, const JSValue &this_value, bool &out) {
            if (this_value.type() == JSValueType::Boolean) {
                out = this_value.to_boolean();
                return Completion::empty();
            }

            if (this_value.type() == JSValueType::Object) {
                auto *wrapper = dynamic_cast<JSPrimitiveWrapper*>(this_value.as_object());

                if (wrapper != nullptr && std::strcmp(wrapper->label, "Boolean") == 0) {
                    out = wrapper->primitive.to_boolean();
                    return Completion::empty();
                }
            }

            return evaluator.throw_error(evaluator.type_error_prototype,
                "Boolean.prototype method called on incompatible receiver");
        }

        Completion native_boolean(Evaluator&, const JSValue&, const std::vector<JSValue> &args) {
            return Completion::normal(JSValue::boolean(argument_at(args, 0).to_boolean()));
        }

        Completion construct_boolean(Evaluator &evaluator, const JSValue&,
                                     const std::vector<JSValue> &args) {
            auto *wrapper = evaluator.heap.allocate<JSPrimitiveWrapper>(
                JSValue::boolean(argument_at(args, 0).to_boolean()), "Boolean");
            wrapper->prototype = evaluator.boolean_prototype;

            return Completion::normal(JSValue::object(wrapper));
        }

        Completion boolean_to_string(Evaluator &evaluator, const JSValue &this_value,
                                     const std::vector<JSValue>&) {
            bool value = false;
            if (Completion c = this_boolean(evaluator, this_value, value); c.is_abrupt()) return c;

            return Completion::normal(JSValue::string(value ? "true" : "false"));
        }

        Completion boolean_value_of(Evaluator &evaluator, const JSValue &this_value,
                                    const std::vector<JSValue>&) {
            bool value = false;
            if (Completion c = this_boolean(evaluator, this_value, value); c.is_abrupt()) return c;

            return Completion::normal(JSValue::boolean(value));
        }
    }

    void install_boolean(const Builtins &b) {
        JSObject *prototype = b.evaluator.boolean_prototype;

        b.constructor("Boolean", native_boolean, prototype, construct_boolean);

        b.method(prototype, "toString", boolean_to_string);
        b.method(prototype, "valueOf",  boolean_value_of);
    }
}