
#include "js_object.h"

#include "evaluator/evaluator.h"

namespace perunejs {
    Completion JSObject::call(Evaluator& evaluator, const JSValue&, const std::vector<JSValue>&) {
        return evaluator.throw_error(evaluator.type_error_prototype, "not a function");
    }

    Completion JSObject::construct(Evaluator &evaluator, const std::vector<JSValue>&) {
        return evaluator.throw_error(evaluator.type_error_prototype, "not a constructor");
    }

    Completion JSObject::coerce_defined_value(Evaluator &evaluator, const std::string &name,
                                              PropertyDescriptor &desc) {
        (void) evaluator;
        (void) name;
        (void) desc;

        return Completion::empty();
    }

    Completion JSObject::put(Evaluator &evaluator, const std::string &name,
                             const JSValue &value, bool is_throw) {
        if (!can_put(name)) {
            if (is_throw) {
                return evaluator.throw_error(evaluator.type_error_prototype, "Cannot assign to read only property");
            }
            return Completion::normal(JSValue::undefined());
        }

        const PropertyDescriptor *descriptor = get_own_property(name);

        if (descriptor != nullptr && descriptor->is_data()) {
            PropertyDescriptor replacement = PropertyDescriptor::only_value(value);

            if (Completion c = coerce_defined_value(evaluator, name, replacement); c.is_abrupt()) return c;

            if (!define_own_property(name, replacement)) {
                if (rejects_with_range_error(name, replacement)) {
                    return evaluator.throw_error(evaluator.range_error_prototype,
                                                 "Invalid array length");
                }

                if (is_throw) {
                    return evaluator.throw_error(evaluator.type_error_prototype,
                                                 "Cannot assign to read only property");
                }
            }

            return Completion::normal(JSValue::undefined());
        }

        const PropertyDescriptor *inherited = get_property(name);
        if (inherited != nullptr && inherited->is_accessor()) {
            JSObject *setter = inherited->setter();
            return setter->call(evaluator, JSValue::object(this), {value});
        }

        define_own_property(name, PropertyDescriptor::data(value, true, true, true));

        return Completion::normal(JSValue::undefined());
    }

    Completion JSObject::default_value(Evaluator &evaluator, Hint hint) {
        const bool string_first = (hint == Hint::String);
        const char *first = string_first ? "toString" : "valueOf";
        const char *second = string_first ? "valueOf" : "toString";

        for (const char* method_name : {first, second}) {
            Completion method = get(evaluator, method_name);
            if (method.is_abrupt()) return method;

            const JSValue candidate = method.get_value_or_undefined();

            if (candidate.type() != JSValueType::Object) continue;
            if (!candidate.as_object()->is_callable()) continue;

            Completion result = candidate.as_object()->call(evaluator, JSValue::object(this), {});
            if (result.is_abrupt()) return result;

            const JSValue produced = result.get_value_or_undefined();
            if (produced.type() != JSValueType::Object) {
                return Completion::normal(produced);
            }
        }

        return evaluator.throw_error(evaluator.type_error_prototype, "Cannot convert object to primitive value");
    }

    Completion JSObject::has_instance(Evaluator &evaluator, const JSValue &value) {
        if (!is_callable()) {
            return evaluator.throw_error(evaluator.type_error_prototype, "right-hand side of 'instanceof' is not callable");
        }
        if (value.type() != JSValueType::Object) {
            return Completion::normal(JSValue::boolean(false));
        }

        Completion prototype_value = get(evaluator, "prototype");
        if (prototype_value.is_abrupt()) return prototype_value;

        const JSValue prototype = prototype_value.get_value_or_undefined();
        if (prototype.type() != JSValueType::Object) {
            return evaluator.throw_error(evaluator.type_error_prototype, "prototype of the constructor is not an object");
        }

        JSObject *target = prototype.as_object();

        for (JSObject *object = value.as_object()->prototype;
             object != nullptr;
             object = object->prototype) {
            if (object == target) return Completion::normal(JSValue::boolean(true));
             }

        return Completion::normal(JSValue::boolean(false));
    }
}