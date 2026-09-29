#include "runtime/builtins.h"

namespace perunejs {
    namespace {
        Completion error_to_string(Evaluator &evaluator, const JSValue &this_value,
                                   const std::vector<JSValue>&) {
            if (this_value.type() != JSValueType::Object) {
                return evaluator.throw_error(evaluator.type_error_prototype,
                    "Error.prototype.toString called on non-object");
            }

            JSObject *self = this_value.as_object();

            Completion name_value = self->get(evaluator, "name");
            if (name_value.is_abrupt()) return name_value;

            Completion message_value = self->get(evaluator, "message");
            if (message_value.is_abrupt()) return message_value;

            std::string name = "Error";
            if (name_value.get_value_or_undefined().type() != JSValueType::Undefined) {
                Completion text = evaluator.to_string(name_value.get_value_or_undefined());
                if (text.is_abrupt()) return text;

                name = text.get_value_or_undefined().to_string();
            }

            std::string message;
            if (message_value.get_value_or_undefined().type() != JSValueType::Undefined) {
                Completion text = evaluator.to_string(message_value.get_value_or_undefined());
                if (text.is_abrupt()) return text;

                message = text.get_value_or_undefined().to_string();
            }

            if (name.empty())    return Completion::normal(JSValue::string(message));
            if (message.empty()) return Completion::normal(JSValue::string(name));

            return Completion::normal(JSValue::string(name + ": " + message));
        }

        Completion native_error(Evaluator &e, const JSValue&, const std::vector<JSValue> &args) {
            return e.make_error(e.error_prototype, args);
        }
        Completion native_type_error(Evaluator &e, const JSValue&, const std::vector<JSValue> &args) {
            return e.make_error(e.type_error_prototype, args);
        }
        Completion native_reference_error(Evaluator &e, const JSValue&, const std::vector<JSValue> &args) {
            return e.make_error(e.reference_error_prototype, args);
        }
        Completion native_range_error(Evaluator &e, const JSValue&, const std::vector<JSValue> &args) {
            return e.make_error(e.range_error_prototype, args);
        }
        Completion native_syntax_error(Evaluator &e, const JSValue&, const std::vector<JSValue> &args) {
            return e.make_error(e.syntax_error_prototype, args);
        }
        Completion native_uri_error(Evaluator &e, const JSValue&, const std::vector<JSValue> &args) {
            return e.make_error(e.uri_error_prototype, args);
        }
        Completion native_eval_error(Evaluator &e, const JSValue&, const std::vector<JSValue> &args) {
            return e.make_error(e.eval_error_prototype, args);
        }

        void define_error(const Builtins &b, const char *name, NativeFunction fn, JSObject *prototype) {
            b.data_property(prototype, "name",    JSValue::string(name));
            b.data_property(prototype, "message", JSValue::string(""));

            b.constructor(name, fn, prototype);
        }
    }

    void install_error(const Builtins &b) {
        Evaluator &evaluator = b.evaluator;

        b.method(evaluator.error_prototype, "toString", error_to_string);

        define_error(b, "Error",          native_error,           evaluator.error_prototype);
        define_error(b, "TypeError",      native_type_error,      evaluator.type_error_prototype);
        define_error(b, "ReferenceError", native_reference_error, evaluator.reference_error_prototype);
        define_error(b, "RangeError",     native_range_error,     evaluator.range_error_prototype);
        define_error(b, "SyntaxError",    native_syntax_error,    evaluator.syntax_error_prototype);
        define_error(b, "URIError",      native_uri_error,       evaluator.uri_error_prototype);
        define_error(b, "EvalError",     native_eval_error,      evaluator.eval_error_prototype);
    }
}