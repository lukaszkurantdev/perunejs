#include "runtime/builtins.h"

#include "lexer/lexer.h"
#include "memory/js_bound_function.h"
#include "memory/js_function.h"
#include "memory/marked_vector.h"
#include "parser.h"

namespace perunejs {
    namespace {
        bool is_callable_value(const JSValue &value) {
            return value.type() == JSValueType::Object && value.as_object()->is_callable();
        }

        Completion function_call(Evaluator &evaluator, const JSValue &this_value,
                                 const std::vector<JSValue> &args) {
            if (!is_callable_value(this_value)) {
                return evaluator.throw_error(evaluator.type_error_prototype,
                    "Function.prototype.call called on non-function");
            }

            MarkedVector rest(evaluator.heap);
            rest.reserve(args.size());
            for (std::size_t i = 1; i < args.size(); ++i) rest.push_back(args[i]);

            return this_value.as_object()->call(evaluator, argument_at(args, 0), rest);
        }

        Completion function_apply(Evaluator &evaluator, const JSValue &this_value,
                                  const std::vector<JSValue> &args) {
            if (!is_callable_value(this_value)) {
                return evaluator.throw_error(evaluator.type_error_prototype,
                    "Function.prototype.apply called on non-function");
            }

            const JSValue target_this = argument_at(args, 0);
            const JSValue array       = argument_at(args, 1);

            MarkedVector list(evaluator.heap);

            if (array.type() != JSValueType::Undefined && array.type() != JSValueType::Null) {
                if (array.type() != JSValueType::Object) {
                    return evaluator.throw_error(evaluator.type_error_prototype,
                        "Function.prototype.apply: second argument is not an array-like object");
                }

                JSObject *source = array.as_object();

                Completion length = source->get(evaluator, "length");
                if (length.is_abrupt()) return length;

                Completion count = evaluator.to_number(length.get_value_or_undefined());
                if (count.is_abrupt()) return count;

                const uint32_t total = count.get_value_or_undefined().to_uint32();
                list.reserve(total);

                for (uint32_t i = 0; i < total; ++i) {
                    Completion element = source->get(evaluator, std::to_string(i));
                    if (element.is_abrupt()) return element;

                    list.push_back(element.get_value_or_undefined());
                }
            }

            return this_value.as_object()->call(evaluator, target_this, list);
        }

        Completion function_bind(Evaluator &evaluator, const JSValue &this_value,
                                 const std::vector<JSValue> &args) {
            if (!is_callable_value(this_value)) {
                return evaluator.throw_error(evaluator.type_error_prototype,
                    "Function.prototype.bind called on non-function");
            }

            auto *bound = evaluator.heap.allocate<JSBoundFunction>();
            bound->prototype  = evaluator.function_prototype;
            bound->target     = this_value.as_object();
            bound->bound_this = argument_at(args, 0);

            for (std::size_t i = 1; i < args.size(); ++i) bound->bound_args.push_back(args[i]);

            double length = 0;

            Completion target_length = bound->target->get(evaluator, "length");
            if (target_length.is_abrupt()) return target_length;

            if (target_length.get_value_or_undefined().type() == JSValueType::Number) {
                length = std::max(0.0, target_length.get_value_or_undefined().to_number()
                                       - static_cast<double>(bound->bound_args.size()));
            }

            bound->define_own_property("length",
                PropertyDescriptor::data(JSValue::number(length), false, false, false));

            const PropertyDescriptor poisoned = PropertyDescriptor::accessor(
                evaluator.type_error_thrower, evaluator.type_error_thrower, false, false);

            bound->define_own_property("caller", poisoned);
            bound->define_own_property("arguments", poisoned);

            return Completion::normal(JSValue::object(bound));
        }

        Completion function_to_string(Evaluator &evaluator, const JSValue &this_value,
                                      const std::vector<JSValue>&) {
            if (!is_callable_value(this_value)) {
                return evaluator.throw_error(evaluator.type_error_prototype,
                    "Function.prototype.toString called on non-function");
            }

            JSObject *function = this_value.as_object();
            std::string name;

            if (const auto *user = dynamic_cast<const JSFunction*>(function))             name = user->name;
            else if (const auto *native = dynamic_cast<const JSNativeFunction*>(function)) name = native->name;
            else if (dynamic_cast<const JSBoundFunction*>(function) != nullptr)            name = "bound";

            return Completion::normal(JSValue::string("function " + name + "() { [native code] }"));
        }

        Completion construct_function(Evaluator &evaluator, const JSValue&,
                                      const std::vector<JSValue> &args) {
            std::string parameters;
            std::string body;

            if (!args.empty()) {
                for (std::size_t i = 0; i + 1 < args.size(); ++i) {
                    Completion text = evaluator.to_string(args[i]);
                    if (text.is_abrupt()) return text;

                    if (i > 0) parameters += ",";
                    parameters += text.get_value_or_undefined().to_string();
                }

                Completion text = evaluator.to_string(args.back());
                if (text.is_abrupt()) return text;

                body = text.get_value_or_undefined().to_string();
            }

            const std::string source = "(function anonymous(" + parameters + "\n) {\n" + body + "\n})";

            std::shared_ptr<Program> program;

            Completion compiled = evaluator.compile(source, /*strict*/ false, program);
            if (compiled.is_abrupt()) return compiled;

            try {
                Lexer lexer(source);
                Parser parser(lexer.scan_tokens());
                program = parser.parse();
            } catch (const std::exception &error) {
                return evaluator.throw_error(evaluator.syntax_error_prototype, error.what());
            }

            const FunctionExpression *expression = nullptr;

            if (program->body.size() == 1
                && program->body[0]->kind == NodeKind::ExpressionStatement) {
                const auto &statement = static_cast<const ExpressionStatement&>(*program->body[0]);

                if (statement.expr->kind == NodeKind::FunctionExpression) {
                    expression = static_cast<const FunctionExpression*>(statement.expr.get());
                }
            }

            if (expression == nullptr) {
                return evaluator.throw_error(evaluator.syntax_error_prototype,
                    "Invalid function body");
            }

            const Evaluator::ScriptScope owner(evaluator, program);

            JSFunction *function = evaluator.make_function(
                &expression->params, expression->body.get(),
                evaluator.global_env, "anonymous", expression->strict);

            return Completion::normal(JSValue::object(function));
        }
    }

    void install_function(const Builtins &b) {
        JSObject *prototype = b.evaluator.function_prototype;

        b.method(prototype, "call",     function_call, 1);
        b.method(prototype, "apply",    function_apply, 2);
        b.method(prototype, "bind",     function_bind, 1);
        b.method(prototype, "toString", function_to_string);

        b.constructor("Function", construct_function, prototype, nullptr, 1);
    }
}