#include <algorithm>

#include "evaluator.h"

#include <stdexcept>

#include "lexer/token.h"
#include "memory/js_array.h"
#include "memory/js_function.h"
#include "parser/depth_guard.h"
#include "memory/js_regexp.h"
#include "memory/js_arguments.h"
#include "parser.h"
#include "lexer/lexer.h"
#include "memory/js_string.h"
#include "memory/js_wrapper.h"

#define TRY_VALUE(dest, expr)                              \
    do {                                                   \
        Completion _c = (expr);                            \
        if (_c.is_abrupt()) return _c;                     \
        dest = _c.get_value_or_undefined();                \
    } while (0)

namespace {
    bool loop_continues(const perunejs::Completion& completion, const perunejs::LabelSet& labels) {
        if (completion.type == perunejs::COMPLETION_TYPE::NORMAL) return true;
        if (completion.type != perunejs::COMPLETION_TYPE::CONTINUE) return false;
        if (completion.target.empty()) return true;

        return std::find(labels.begin(), labels.end(), completion.target) != labels.end();
    }
}

namespace perunejs {
    Completion Evaluator::evaluate(Environment *env, const Node& node) {
        if (stack_exhausted()) {
            return throw_error(range_error_prototype, "Maximum call stack size exceeded");
        }

        switch (node.kind) {
            case NodeKind::NumberLiteral:
                return eval_number(static_cast<const NumberLiteral&>(node));
            case NodeKind::BooleanLiteral:
                return eval_boolean(static_cast<const BooleanLiteral&>(node));
            case NodeKind::BinaryExpression:
                return eval_binary(env, static_cast<const BinaryExpression&>(node));
            case NodeKind::StringLiteral:
                return eval_string(static_cast<const StringLiteral&>(node));
            case NodeKind::RegExpLiteral:
                return eval_regexp(static_cast<const RegExpLiteral&>(node));
            case NodeKind::NullLiteral:
                return eval_null(static_cast<const NullLiteral&>(node));
            case NodeKind::Identifier:
                return eval_identifier(env, static_cast<const IdentifierLiteral&>(node));
            case NodeKind::UnaryExpression:
                return eval_unary(env, static_cast<const UnaryExpression&>(node));
            case NodeKind::AssignExpression:
                return eval_assign(env, static_cast<const AssignExpression&>(node));
            case NodeKind::ConditionalExpression:
                return eval_conditional(env, static_cast<const ConditionalExpression&>(node));
            case NodeKind::SequenceExpression:
                return eval_sequence(env, static_cast<const SequenceExpression&>(node));
            case NodeKind::UpdateExpression:
                return eval_update(env, static_cast<const UpdateExpression&>(node));
            case NodeKind::FunctionExpression:
                return eval_function(env, static_cast<const FunctionExpression&>(node));
            case NodeKind::CallExpression:
                return eval_call(env, static_cast<const CallExpression&>(node));
            case NodeKind::MemberExpression: {
                Reference ref;
                Completion resolved = evaluate_reference(env, node, ref);
                if (resolved.is_abrupt()) return resolved;
                return get_value(ref);
            }
            case NodeKind::ObjectExpression:
                return eval_object(env, static_cast<const ObjectExpression&>(node));
            case NodeKind::ArrayExpression:
                return eval_array(env, static_cast<const ArrayExpression&>(node));
            case NodeKind::NewExpression:
                return eval_new(env, static_cast<const NewExpression&>(node));
            case NodeKind::ThisExpression:
                return eval_this(env, static_cast<const ThisExpression&>(node));
            default:
                break;
        }

        throw std::logic_error("Evaluator: node kind not handled");
    }

    Completion Evaluator::eval_boolean(const BooleanLiteral& node) {
        return Completion::normal(JSValue::boolean(node.value));
    }

    Completion Evaluator::eval_number(const NumberLiteral& node) {
        return Completion::normal(JSValue::number(node.value));
    }

    Completion Evaluator::eval_string(const StringLiteral &node) {
        return Completion::normal(JSValue::string(node.value));
    }

    Completion Evaluator::eval_regexp(const RegExpLiteral &node) {
        return make_regexp(utf8_to_utf16(node.pattern), node.flags);
    }

    Completion Evaluator::make_regexp(const std::u16string &pattern, const std::string &flags) {
        std::string seen;
        for (const char flag : flags) {
            if (std::string("gim").find(flag) == std::string::npos
                || seen.find(flag) != std::string::npos) {
                return throw_error(syntax_error_prototype, "Invalid regular expression flags");
            }

            seen += flag;
        }

        const bool ignore_case = seen.find('i') != std::string::npos;
        const bool multiline = seen.find('m') != std::string::npos;

        std::shared_ptr<const RegExpEngine> engine;

        try {
            engine = std::make_shared<RegExpEngine>(pattern, ignore_case, multiline);
        } catch (const std::exception &error) {
            return throw_error(syntax_error_prototype, error.what());
        }

        const std::string source = pattern.empty() ? "(?:)" : utf16_to_utf8(pattern);

        auto *regexp = heap.allocate<JSRegExp>(source, flags);
        regexp->prototype = regexp_prototype;
        regexp->engine = std::move(engine);

        regexp->define_own_property("source",
            PropertyDescriptor::data(JSValue::string(source), false, false, false));
        regexp->define_own_property("global",
            PropertyDescriptor::data(JSValue::boolean(seen.find('g') != std::string::npos),
                                     false, false, false));
        regexp->define_own_property("ignoreCase",
            PropertyDescriptor::data(JSValue::boolean(ignore_case), false, false, false));
        regexp->define_own_property("multiline",
            PropertyDescriptor::data(JSValue::boolean(multiline), false, false, false));
        regexp->define_own_property("lastIndex",
            PropertyDescriptor::data(JSValue::number(0), true, false, false));

        return Completion::normal(JSValue::object(regexp));
    }

    Completion Evaluator::eval_null(const NullLiteral&) {
        return Completion::normal(JSValue::null());
    }

    Completion Evaluator::eval_identifier(Environment *env, const IdentifierLiteral& node) {
        Reference ref;
        Completion result = evaluate_reference(env, node, ref);
        if (result.is_abrupt()) return result;

        return get_value(ref);
    }

    Completion Evaluator::eval_unary(Environment *env, const UnaryExpression& node) {
        if (node.op == UnaryOperator::Delete) {
            Reference ref;
            Completion resolved = evaluate_reference(env, *node.operand, ref);

            if (resolved.is_abrupt()) return Completion::normal(JSValue::boolean(true));

            if (ref.kind == ReferenceKind::Unresolvable) {
                return Completion::normal(JSValue::boolean(true));
            }

            if (ref.kind == ReferenceKind::Property) {
                if (ref.value.type() != JSValueType::Object) {
                    return Completion::normal(JSValue::boolean(true));
                }

                const bool deleted = ref.value.as_object()->delete_own_property(ref.name);

                if (!deleted && strict_code) {
                    return throw_error(type_error_prototype, "Cannot delete property '" + ref.name + "'");
                }

                return Completion::normal(JSValue::boolean(deleted));
            }

            return Completion::normal(JSValue::boolean(ref.env->delete_binding(ref.name)));
        }

        if (node.op == UnaryOperator::Typeof && node.operand->kind == NodeKind::Identifier) {
            Reference ref;
            Completion result = evaluate_reference(env, *node.operand, ref);
            if (result.is_abrupt()) return result;

            if (ref.is_unresolvable()) {
                return Completion::normal(JSValue::string("undefined"));
            }

            JSValue operand;
            TRY_VALUE(operand, get_value(ref));
            return Completion::normal(JSValue::string(operand.get_type()));
        }
        JSValue operand;
        TRY_VALUE(operand, evaluate(env, *node.operand));

        switch (node.op) {
            case UnaryOperator::Plus: {
                JSValue number;
                TRY_VALUE(number, to_number(operand));
                return Completion::normal(number);
            }
            case UnaryOperator::Minus: {
                JSValue number;
                TRY_VALUE(number, to_number(operand));
                return Completion::normal(JSValue::number(-number.to_number()));
            }
            case UnaryOperator::BitNot: {
                JSValue number;
                TRY_VALUE(number, to_number(operand));
                return Completion::normal(JSValue::number(~number.to_int32()));
            }
            case UnaryOperator::Not:
                return Completion::normal(JSValue::boolean(!operand.to_boolean()));
            case UnaryOperator::Typeof:
                return Completion::normal(JSValue::string(operand.get_type()));
            case UnaryOperator::Void:
                return Completion::normal(JSValue::undefined());
            case UnaryOperator::Delete:
                break;
        }

        throw std::logic_error("Evaluator: unary operator not handled");
    }

    Completion Evaluator::eval_binary(Environment *env, const BinaryExpression& node) {
        if (node.op == BinaryOperator::LogicalAnd) {
            JSValue left;
            TRY_VALUE(left, evaluate(env, *node.left));

            if (left.to_boolean()) {
                JSValue right;
                TRY_VALUE(right, evaluate(env, *node.right));

                return Completion::normal(right);
            }

            return Completion::normal(left);
        }

        if (node.op == BinaryOperator::LogicalOr) {
            JSValue left;
            TRY_VALUE(left, evaluate(env, *node.left));

            if (!left.to_boolean()) {
                JSValue right;
                TRY_VALUE(right, evaluate(env, *node.right));

                return Completion::normal(right);
            }

            return Completion::normal(left);
        }

        JSValue left;
        TRY_VALUE(left, evaluate(env, *node.left));
        JSValue right;
        TRY_VALUE(right, evaluate(env, *node.right));

        switch (node.op) {
            case BinaryOperator::Eq: {
                JSValue equal;
                TRY_VALUE(equal, loosely_equals(left, right));
                return Completion::normal(equal);
            }
            case BinaryOperator::NotEq: {
                JSValue equal;
                TRY_VALUE(equal, loosely_equals(left, right));
                return Completion::normal(JSValue::boolean(!equal.to_boolean()));
            }
            case BinaryOperator::StrictEq:
                return Completion::normal(JSValue::boolean(JSValue::is_strictly_equal(left, right)));
            case BinaryOperator::StrictNotEq:
                return Completion::normal(JSValue::boolean(!JSValue::is_strictly_equal(left, right)));
            case BinaryOperator::Add:
            case BinaryOperator::Sub:
            case BinaryOperator::Mul:
            case BinaryOperator::Div:
            case BinaryOperator::Mod:
            case BinaryOperator::BitAnd:
            case BinaryOperator::BitOr:
            case BinaryOperator::BitXor:
            case BinaryOperator::Sar:
            case BinaryOperator::Shr:
            case BinaryOperator::Shl:
                return apply_binary_operator(left, node.op, right);
            case BinaryOperator::Lt: {
                JSValue result;
                TRY_VALUE(result, less_than(left, right, true));
                return Completion::normal(JSValue::boolean(
                    result.type() == JSValueType::Undefined ? false : result.to_boolean()));
            }
            case BinaryOperator::Gt: {
                JSValue result;
                TRY_VALUE(result, less_than(right, left, false));
                return Completion::normal(JSValue::boolean(
                    result.type() == JSValueType::Undefined ? false : result.to_boolean()));
            }
            case BinaryOperator::LtEq: {
                JSValue result;
                TRY_VALUE(result, less_than(right, left, false));
                return Completion::normal(JSValue::boolean(
                    result.type() == JSValueType::Undefined ? false : !result.to_boolean()));
            }
            case BinaryOperator::GtEq: {
                JSValue result;
                TRY_VALUE(result, less_than(left, right, true));
                return Completion::normal(JSValue::boolean(
                    result.type() == JSValueType::Undefined ? false : !result.to_boolean()));
            }
            case BinaryOperator::In: {
                if (right.type() != JSValueType::Object) {
                    return throw_error(type_error_prototype, "'in' requires an object on the right side");
                }

                Completion key = to_string(left);
                if (key.is_abrupt()) return key;

                return Completion::normal(JSValue::boolean(
                    right.as_object()->has_property(key.get_value_or_undefined().to_string())));
            }
            case BinaryOperator::InstanceOf: {
                if (right.type() != JSValueType::Object) {
                    return throw_error(type_error_prototype, "right-hand side of 'instanceof' is not an object");
                }
                return right.as_object()->has_instance(*this, left);
            }

            default:
                throw std::logic_error("Evaluator: binary operator not handled");
        }
    }

    Completion Evaluator::eval_assign(Environment *env, const AssignExpression &node) {
        Reference ref;
        Completion resolved = evaluate_reference(env, *node.target, ref);

        if (resolved.is_abrupt()) return resolved;

        if (node.op == AssignOperator::Assign) {
            JSValue value;
            TRY_VALUE(value, evaluate(env, *node.value));

            Completion stored = put_value(ref, value);
            if (stored.is_abrupt()) return stored;

            return Completion::normal(value);
        }

        JSValue left;
        TRY_VALUE(left, get_value(ref));

        JSValue right;
        TRY_VALUE(right, evaluate(env, *node.value));

        BinaryOperator op;
        switch (node.op) {
            case AssignOperator::AddAssign:    op = BinaryOperator::Add;    break;
            case AssignOperator::SubAssign:    op = BinaryOperator::Sub;    break;
            case AssignOperator::MulAssign:    op = BinaryOperator::Mul;    break;
            case AssignOperator::DivAssign:    op = BinaryOperator::Div;    break;
            case AssignOperator::ModAssign:    op = BinaryOperator::Mod;    break;
            case AssignOperator::BitAndAssign: op = BinaryOperator::BitAnd; break;
            case AssignOperator::BitOrAssign:  op = BinaryOperator::BitOr;  break;
            case AssignOperator::BitXorAssign: op = BinaryOperator::BitXor; break;
            case AssignOperator::SarAssign:    op = BinaryOperator::Sar;    break;
            case AssignOperator::ShrAssign:    op = BinaryOperator::Shr;    break;
            case AssignOperator::ShlAssign:    op = BinaryOperator::Shl;    break;
            default:
                throw std::logic_error("Evaluator: assignment operator not handled");
        }

        Completion applied = apply_binary_operator(left, op, right);
        if (applied.is_abrupt()) return applied;

        const JSValue result = applied.get_value_or_undefined();

        Completion stored = put_value(ref, result);
        if (stored.is_abrupt()) return stored;

        return Completion::normal(result);
    }

    Completion Evaluator::eval_statement(Environment *env, const Statement &node, const LabelSet &labels) {
        if (stack_exhausted()) {
            return throw_error(range_error_prototype, "Maximum call stack size exceeded");
        }

        switch (node.kind) {
            case NodeKind::ExpressionStatement: {
                const auto &expr = static_cast<const ExpressionStatement&>(node);
                return evaluate(env, *expr.expr);
            }
            case NodeKind::EmptyStatement:
                return {COMPLETION_TYPE::NORMAL, std::monostate()};
            case NodeKind::VariableDeclaration: {
                const auto &var = static_cast<const VariableDeclaration&>(node);

                for (auto &declarator : var.declarators) {
                    if (declarator.init == nullptr) continue;

                    JSValue value;
                    TRY_VALUE(value, evaluate(env, *declarator.init));

                    const std::string &name = declarator.id->name;
                    Environment *target = env->find_environment(name);

                    const Reference ref = target != nullptr
                        ? Reference::Variable(target, name, strict_code)
                        : Reference::Unresolvable(name, strict_code);

                    Completion stored = put_value(ref, value);
                    if (stored.is_abrupt()) return stored;
                }

                return {COMPLETION_TYPE::NORMAL, std::monostate()};
            }
            case NodeKind::BlockStatement: {
                const auto &block = static_cast<const BlockStatement&>(node);
                return eval_statement_list(env, block.body);
            }
            case NodeKind::IfStatement: {
                const auto &condition = static_cast<const IfStatement&>(node);

                auto completion = evaluate(env, *condition.test);
                if (completion.is_abrupt()) return completion;

                if (completion.has_value() && completion.get_value_or_undefined().to_boolean()) {
                    return eval_statement(env, *condition.consequent);
                }

                if (condition.alternate == nullptr) {
                    return {COMPLETION_TYPE::NORMAL, std::monostate()};
                }

                return eval_statement(env, *condition.alternate);
            }
            case NodeKind::WhileStatement: {
                const auto &loop = static_cast<const WhileStatement&>(node);

                std::variant<std::monostate, JSValue> value = std::monostate();

                while (true) {
                    Completion completion = evaluate(env, *loop.test);
                    if (completion.is_abrupt()) return completion;

                    if (!completion.has_value() || !completion.get_value_or_undefined().to_boolean()) {
                        return Completion(COMPLETION_TYPE::NORMAL, value);
                    }

                    auto result = eval_statement(env, *loop.body);

                    if (std::holds_alternative<JSValue>(result.value)) {
                        value = result.value;
                    }

                    if (!loop_continues(result, labels)) {
                        if (result.type == COMPLETION_TYPE::BREAK && result.target.empty()) {
                            return Completion(COMPLETION_TYPE::NORMAL, value);
                        }
                        return result;
                    }
                };
            }
            case NodeKind::BreakStatement: {
                const auto &brk = static_cast<const BreakStatement&>(node);
                return Completion(COMPLETION_TYPE::BREAK, std::monostate(), brk.label);
            }
            case NodeKind::DoWhileStatement: {
                const auto &loop = static_cast<const DoWhileStatement&>(node);

                std::variant<std::monostate, JSValue> value = std::monostate();

                while (true) {
                    auto result = eval_statement(env, *loop.body);

                    if (std::holds_alternative<JSValue>(result.value)) {
                        value = result.value;
                    }

                    if (!loop_continues(result, labels)) {
                        if (result.type == COMPLETION_TYPE::BREAK && result.target.empty()) {
                            return Completion(COMPLETION_TYPE::NORMAL, value);
                        }
                        return result;
                    }

                    Completion completion = evaluate(env, *loop.test);
                    if (completion.is_abrupt()) return completion;

                    if (!completion.has_value() || !completion.get_value_or_undefined().to_boolean()) {
                        return Completion(COMPLETION_TYPE::NORMAL, value);
                    }
                }
            }
            case NodeKind::ForStatement: {
                const auto &loop = static_cast<const ForStatement&>(node);

                if (loop.init) {
                    Completion init = eval_statement(env, *loop.init);
                    if (init.type == COMPLETION_TYPE::THROW) return init;
                }

                std::variant<std::monostate, JSValue> value = std::monostate();

                while (true) {
                    if (loop.test) {
                        Completion completion = evaluate(env, *loop.test);
                        if (completion.is_abrupt()) return completion;

                        if (!completion.has_value() || !completion.get_value_or_undefined().to_boolean()) {
                            return Completion(COMPLETION_TYPE::NORMAL, value);
                        }
                    }

                    auto result = eval_statement(env, *loop.body);

                    if (std::holds_alternative<JSValue>(result.value)) {
                        value = result.value;
                    }

                    if (!loop_continues(result, labels)) {
                        if (result.type == COMPLETION_TYPE::BREAK && result.target.empty()) {
                            return Completion(COMPLETION_TYPE::NORMAL, value);
                        }
                        return result;
                    }

                    if (loop.update) { Completion update = evaluate(env, *loop.update);
                        if (update.is_abrupt()) return update;
                    }
                }
            }
            case NodeKind::SwitchStatement: {
                const auto &sw = static_cast<const SwitchStatement&>(node);

                JSValue input;
                TRY_VALUE(input, evaluate(env, *sw.discriminant));

                std::variant<std::monostate, JSValue> value = std::monostate();

                std::size_t start = sw.cases.size();
                for (std::size_t i = 0; i < sw.cases.size(); ++i) {
                    if (!sw.cases[i].test) continue;

                    JSValue test_value;
                    TRY_VALUE(test_value, evaluate(env, *sw.cases[i].test));
                    if (JSValue::is_strictly_equal(input, test_value)) {
                        start = i;
                        break;
                    }
                }

                if (start == sw.cases.size()) {
                    for (std::size_t i = 0; i < sw.cases.size(); ++i) {
                        if (!sw.cases[i].test) {
                            start = i;
                            break;
                        }
                    }
                }

                if (start == sw.cases.size()) {
                    return Completion(COMPLETION_TYPE::NORMAL, value);
                }

                for (std::size_t i = start; i < sw.cases.size(); ++i) {
                    auto result = eval_statement_list(env, sw.cases[i].body);

                    if (std::holds_alternative<JSValue>(result.value)) {
                        value = result.value;
                    }

                    if (result.type != COMPLETION_TYPE::NORMAL) {
                        if (result.type == COMPLETION_TYPE::BREAK && result.target.empty()) {
                            return Completion(COMPLETION_TYPE::NORMAL, value);
                        }
                        return Completion(result.type, value, result.target);
                    }
                }

                return Completion(COMPLETION_TYPE::NORMAL, value);
            }
            case NodeKind::ContinueStatement: {
                const auto &cont = static_cast<const ContinueStatement&>(node);
                return Completion(COMPLETION_TYPE::CONTINUE, std::monostate(), cont.label);
            }
            case NodeKind::LabeledStatement: {
                const auto &labeled = static_cast<const LabeledStatement&>(node);
                return eval_labeled(env, labeled);
            }
            case NodeKind::FunctionDeclaration: {
                return {COMPLETION_TYPE::NORMAL, std::monostate()};
            }
            case NodeKind::ReturnStatement: {
                const auto &statement = static_cast<const ReturnStatement&>(node);

                if (statement.argument != nullptr) {
                    Completion value = evaluate(env, *statement.argument);
                    if (value.is_abrupt()) return value;
                    return Completion::ret(value.get_value_or_undefined());
                }
                return Completion::ret(JSValue::undefined());
            }
            case NodeKind::ThrowStatement: {
                const auto &statement = static_cast<const ThrowStatement&>(node);

                JSValue value;
                TRY_VALUE(value, evaluate(env, *statement.argument));

                return Completion::throw_value(value);
            }
            case NodeKind::TryStatement:
                return eval_try(env, static_cast<const TryStatement&>(node));
            case NodeKind::ForInStatement:
                return eval_for_in(env, static_cast<const ForInStatement&>(node), labels);
            case NodeKind::DebuggerStatement:
                return Completion::empty();
            case NodeKind::WithStatement: {
                const auto &with = static_cast<const WithStatement&>(node);

                JSValue value;
                TRY_VALUE(value, evaluate(env, *with.object));

                JSValue object;
                TRY_VALUE(object, to_object(value));

                auto *with_env = heap.allocate<ObjectEnvironment>(object.as_object(), env, true);

                return eval_statement(with_env, *with.body);
            }
            default:
                throw std::logic_error("Evaluator: statement kind not handled");
        }
    }

    Completion Evaluator::eval_conditional(Environment *env, const ConditionalExpression &node) {
        auto value = evaluate(env, *node.test);
        if (value.is_abrupt()) return value;

        if (value.get_value_or_undefined().to_boolean()) {
            return evaluate(env, *node.consequent);
        }

        return evaluate(env, *node.alternate);
    }

    Completion Evaluator::eval_sequence(Environment *env, const SequenceExpression &node) {
        if (node.expressions.empty()) {
            throw std::logic_error("Evaluator: empty sequence expression");
        }

        JSValue value;

        for (auto &expression : node.expressions) {
            auto result = evaluate(env, *expression);
            if (result.is_abrupt()) return result;
            TRY_VALUE(value, result);
        }

        return Completion::normal(value);
    }

    Completion Evaluator::eval_block(Environment *env, const BlockStatement &node) {
        return  eval_statement_list(env, node.body);
    }

    Completion Evaluator::eval_labeled(Environment *env, const LabeledStatement &node, LabelSet labels) {
        labels.push_back(node.label);

        Completion result = node.body->kind == NodeKind::LabeledStatement
            ? eval_labeled(env, static_cast<const LabeledStatement&>(*node.body), labels)
            : eval_statement(env, *node.body, labels);

        if (result.type == COMPLETION_TYPE::BREAK && result.target == node.label) {
            return Completion(COMPLETION_TYPE::NORMAL, result.value);
        }
        return result;
    }

    Completion Evaluator::eval_update(Environment *env, const UpdateExpression &node) {
        Reference ref;
        Completion resolved = evaluate_reference(env, *node.operand, ref);

        if (resolved.is_abrupt()) return resolved;

        JSValue current;
        TRY_VALUE(current, get_value(ref));

        JSValue old_value;
        TRY_VALUE(old_value, to_number(current));

        const JSValue new_value = JSValue::number(
            node.op == UpdateOperator::Inc ? old_value.to_number() + 1
                                           : old_value.to_number() - 1);

        Completion stored = put_value(ref, new_value);
        if (stored.is_abrupt()) return stored;

        return Completion::normal(node.prefix ? new_value : old_value);
    }

    Completion Evaluator::eval_function(Environment *env, const FunctionExpression &node) {
        if (node.name.empty()) {
            return Completion::normal(JSValue::object(
                make_function(&node.params, node.body.get(), env, node.name, node.strict)));
        }

        auto *function_env = heap.allocate<DeclarativeEnvironment>(env);
        function_env->create_immutable_binding(node.name);

        JSFunction *function = make_function(&node.params, node.body.get(), function_env, node.name, node.strict);
        function_env->initialize_immutable_binding(node.name, JSValue::object(function));

        return Completion::normal(JSValue::object(function));
    }

    Completion Evaluator::eval_call(Environment *env, const CallExpression &node) {
        JSValue this_value = JSValue::undefined();
        JSValue callee_value;
        bool direct_eval = false;

        if (node.callee->kind == NodeKind::MemberExpression || node.callee->kind == NodeKind::Identifier) {
            Reference ref;
            Completion resolved = evaluate_reference(env, *node.callee, ref);
            if (resolved.is_abrupt()) return resolved;

            if (ref.is_property()) {
                this_value = ref.value;
            } else if (ref.kind == ReferenceKind::Variable) {
                this_value = ref.env->implicit_this_value();
            }

            TRY_VALUE(callee_value, get_value(ref));

            direct_eval = ref.kind == ReferenceKind::Variable
                       && ref.name == "eval"
                       && callee_value.type() == JSValueType::Object
                    && callee_value.as_object() == eval_builtin;
        } else {
            TRY_VALUE(callee_value, evaluate(env, *node.callee));
        }

        MarkedVector args(heap);
        args.reserve(node.args.size());

        for (const auto &arg : node.args) {
            JSValue argument;
            TRY_VALUE(argument, evaluate(env, *arg));
            args.push_back(argument);
        }


        current_position = node.start;

        if (callee_value.type() != JSValueType::Object || !callee_value.as_object()->is_callable()) {
            return throw_error(type_error_prototype, "expression is not a function");
        }

        if (direct_eval) {
            return eval_code(args.empty() ? JSValue::undefined() : args[0], env, true);
        }

        return callee_value.as_object()->call(*this, this_value, args);
    }

    Completion Evaluator::eval_catch(Environment *env, const TryStatement &node,
                                 const JSValue &thrown) {
        auto *catch_env = heap.allocate<DeclarativeEnvironment>(env);

        const std::string &name = node.catch_param->name;

        Completion created = catch_env->create_mutable_binding(*this, name, false);
        if (created.is_abrupt()) return created;

        Completion stored = catch_env->set_binding(*this, name, thrown, false);
        if (stored.is_abrupt()) return stored;

        return eval_statement_list(catch_env, node.catch_block->body);
    }

    Completion Evaluator::evaluate_reference(Environment *env, const Node &node, Reference &out) {
        if (node.kind == NodeKind::Identifier) {
            const auto &identifier = static_cast<const IdentifierLiteral&>(node);

            Environment *v_env = env->find_environment(identifier.name);

            if (v_env != nullptr) {
                out = Reference::Variable(v_env, identifier.name, strict_code);
            } else {
                out = Reference::Unresolvable(identifier.name, strict_code);
            }

            return Completion::empty();
        } else if (node.kind == NodeKind::MemberExpression) {
            const auto &member = static_cast<const MemberExpression&>(node);

            JSValue base;
            TRY_VALUE(base, evaluate(env, *member.object));

            std::string name;

            if (member.computed) {
                JSValue key;
                TRY_VALUE(key, evaluate(env, *member.property));

                Completion text = to_string(key);
                if (text.is_abrupt()) return text;

                name = text.get_value_or_undefined().to_string();
            } else {
                name = static_cast<const IdentifierLiteral&>(*member.property).name;
            }

            if (base.type() == JSValueType::Undefined || base.type() == JSValueType::Null) {
                current_position = node.start;

                return throw_error(type_error_prototype,
                    "Cannot read property '" + name + "' of "
                    + (base.type() == JSValueType::Null ? "null" : "undefined"));
            }

            out = Reference::Property(base, std::move(name), strict_code);
            return Completion::empty();
        }

        throw std::logic_error("Evaluator: expression is not a reference");
    }

    Completion Evaluator::eval_object(Environment *env, const ObjectExpression &node) {
        JSObject *object = heap.allocate<JSObject>();
        object->prototype = object_prototype;

        for (const ObjectProperty &property : node.properties) {
            if (property.kind == ObjectProperty::Kind::Init) {
                JSValue value;
                TRY_VALUE(value, evaluate(env, *property.value));

                object->define_own_property(property.key, PropertyDescriptor::data(value, true, true, true));
            } else {
                Completion function = eval_function(env, *property.accessor);
                if (function.is_abrupt()) return function;

                JSObject *accessor = function.get_value_or_undefined().as_object();

                PropertyDescriptor descriptor;

                if (property.kind == ObjectProperty::Kind::Get) {
                    descriptor.get = accessor;
                } else {
                    descriptor.set = accessor;
                }

                descriptor.enumerable = true;
                descriptor.configurable = true;

                object->define_own_property(property.key, descriptor);
            }
        }

        return  Completion::normal(JSValue::object(object));
    }

    Completion Evaluator::eval_this(Environment *, const ThisExpression &) {
        if (this_stack.empty()) {
            return Completion::normal(global_object != nullptr
                ? JSValue::object(global_object)
                : JSValue::undefined());
        }

        return Completion::normal(this_stack.back());
    }

    Completion Evaluator::eval_array(Environment *env, const ArrayExpression &node) {
        auto *array = heap.allocate<JSArray>();
        array->prototype = array_prototype;

        array->define_own_property("length",
            PropertyDescriptor::data(JSValue::number(0), true, false, false));

        for (std::size_t i = 0; i < node.elements.size(); ++i) {
            if (node.elements[i] == nullptr) continue;

            JSValue value;
            TRY_VALUE(value, evaluate(env, *node.elements[i]));

            array->define_own_property(std::to_string(i),
                PropertyDescriptor::data(value, true, true, true));
        }

        if (array->length() < node.elements.size()) {
            PropertyDescriptor final_length;
            final_length.value = JSValue::number(static_cast<double>(node.elements.size()));
            array->define_own_property("length", final_length);
        }

        return Completion::normal(JSValue::object(array));
    }

    Completion Evaluator::eval_new(Environment *env, const NewExpression &node) {
        JSValue callee;
        TRY_VALUE(callee, evaluate(env, *node.callee));

        MarkedVector args(heap);
        args.reserve(node.args.size());
        for (const auto &arg : node.args) {
            JSValue argument;
            TRY_VALUE(argument, evaluate(env, *arg));
            args.push_back(argument);
        }

        current_position = node.start;

        if (callee.type() != JSValueType::Object) {
            return throw_error(type_error_prototype, "expression is not a constructor");
        }

        return callee.as_object()->construct(*this, args);
    }

    Completion Evaluator::eval_for_in(Environment *env, const ForInStatement &node, const LabelSet &labels) {
        JSValue source;
        TRY_VALUE(source, evaluate(env, *node.right));

        if (source.type() == JSValueType::Undefined || source.type() == JSValueType::Null) {
            return Completion(COMPLETION_TYPE::NORMAL, std::monostate());
        }

        JSValue source_object;
        TRY_VALUE(source_object, to_object(source));

        std::vector<std::string> visited;
        std::variant<std::monostate, JSValue> value = std::monostate();

        for (JSObject *object = source_object.as_object(); object != nullptr; object = object->prototype) {
            for (const std::string &key : object->own_keys()) {
                if (std::find(visited.begin(), visited.end(), key) != visited.end()) continue;
                visited.push_back(key);

                const PropertyDescriptor *descriptor = object->get_own_property(key);
                if (descriptor == nullptr || !descriptor->is_enumerable()) continue;

                Reference ref;
                if (node.declaration != nullptr) {
                    const auto &declaration = static_cast<const VariableDeclaration&>(*node.declaration);
                    const std::string &name = declaration.declarators[0].id->name;

                    Environment *found = env->find_environment(name);
                    ref = found != nullptr ? Reference::Variable(found, name,strict_code)
                                           : Reference::Unresolvable(name, strict_code);
                } else {
                    Completion resolved = evaluate_reference(env, *node.target, ref);
                    if (resolved.is_abrupt()) return resolved;
                }

                Completion stored = put_value(ref, JSValue::string(key));
                if (stored.is_abrupt()) return stored;

                Completion result = eval_statement(env, *node.body);

                if (std::holds_alternative<JSValue>(result.value)) value = result.value;

                if (!loop_continues(result, labels)) {
                    if (result.type == COMPLETION_TYPE::BREAK && result.target.empty()) {
                        return Completion(COMPLETION_TYPE::NORMAL, value);
                    }
                    return result;
                }
            }
        }

        return Completion(COMPLETION_TYPE::NORMAL, value);
    }

    Completion Evaluator::compile(const std::string &source, const bool strict,
                                  std::shared_ptr<Program> &out) {
        try {
            Lexer lexer(source);
            Parser parser(lexer.scan_tokens(), strict);
            out = parser.parse();
        } catch (const std::exception &error) {
            return throw_error(syntax_error_prototype, error.what());
        }

        out->source = source;
        out->line_starts.clear();
        out->line_starts.push_back(0);
        for (std::size_t i = 0; i < source.size(); ++i) {
            if (source[i] == '\n') out->line_starts.push_back(static_cast<uint32_t>(i + 1));
        }

        return Completion::empty();
    }

    Completion Evaluator::run_script(Environment *env, const std::string &source, const bool strict) {
        std::shared_ptr<Program> program;

        Completion compiled = compile(source, strict, program);
        if (compiled.is_abrupt()) return compiled;

        ScriptScope owner(*this, program);

        Completion result = eval_program(env, *program);

        const Completion drained = drain_microtasks();
        if (result.type != COMPLETION_TYPE::THROW && drained.type == COMPLETION_TYPE::THROW) {
            return drained;
        }

        return result;
    }


    Completion Evaluator::eval_code(const JSValue &code, Environment *env, bool keep_this) {
        if (code.type() != JSValueType::String) return Completion::normal(code);

        const bool inherit_strict = keep_this && strict_code;

        std::shared_ptr<Program> program;

        Completion compiled = compile(code.to_string(), inherit_strict, program);
        if (compiled.is_abrupt()) return compiled;

        ScriptScope owner(*this, program);
        const Program &script = *program;

        Environment *code_env = script.strict
            ? static_cast<Environment *>(heap.allocate<DeclarativeEnvironment>(env))
            : env;

        StrictScope strict(strict_code, script.strict);

        std::optional<ThisScope> this_scope;
        if (!keep_this) {
            this_scope.emplace(this_stack, JSValue::object(global_object));
        }

        for (auto &statement : script.body) {
            Completion hoisted = hoist_variables(code_env, *statement);
            if (hoisted.is_abrupt()) return hoisted;
        }

        Completion result = eval_statement_list(code_env, script.body);
        if (result.is_abrupt()) return result;

        return Completion::normal(result.get_value_or_undefined());
    }

    Completion Evaluator::get_value(const Reference &ref) {
        switch (ref.kind) {
            case ReferenceKind::Unresolvable:
                return throw_error(reference_error_prototype, ref.name + " is not defined");
            case ReferenceKind::Variable:
                return ref.env->get_binding_value(*this, ref.name, ref.is_strict);
            case ReferenceKind::Property: {
                const JSValue &base = ref.value;

                if (base.type() == JSValueType::Object) {
                    return base.as_object()->get(*this, ref.name);
                }

                JSValue wrapper;
                TRY_VALUE(wrapper, to_object(base));

                return wrapper.as_object()->get(*this, ref.name);
            }
        }
    }

    Completion Evaluator::put_value(const Reference &ref, const JSValue &value) {
        switch (ref.kind) {
            case ReferenceKind::Unresolvable: {
                if (ref.is_strict || global_object == nullptr) {
                    return throw_error(reference_error_prototype, ref.name + " is not defined");
                }

                Completion stored = global_object->put(*this, ref.name, value, false);
                if (stored.is_abrupt()) return stored;

                return Completion::normal(value);
            }
            case ReferenceKind::Variable: {
                Completion stored = ref.env->set_binding(*this, ref.name, value, ref.is_strict);
                if (stored.is_abrupt()) return stored;

                return Completion::normal(value);
            }
            case ReferenceKind::Property: {
                const JSValue &base = ref.value;

                if (base.type() != JSValueType::Object) {
                    if (ref.is_strict) {
                        return throw_error(type_error_prototype,
                            "Cannot assign to property '" + ref.name + "' of " + base.to_string());
                    }
                    JSValue wrapper;
                    TRY_VALUE(wrapper, to_object(base));

                    Completion stored = wrapper.as_object()->put(*this, ref.name, value, false);
                    if (stored.is_abrupt()) return stored;

                    return Completion::normal(value);
                }

                Completion stored = base.as_object()->put(*this, ref.name, value, ref.is_strict);

                if (stored.is_abrupt()) return stored;

                return Completion::normal(value);
            }
        }
    }

    Completion Evaluator::to_primitive(const JSValue &value, Hint hint) {
        if (value.type() != JSValueType::Object) {
            return Completion::normal(value);
        }

        return value.as_object()->default_value(*this, hint);
    }

    Completion Evaluator::to_object(const JSValue &value) {
        switch (value.type()) {
            case JSValueType::Undefined:
            case JSValueType::Null:
                return throw_error(type_error_prototype, "Cannot convert undefined or null to object");

            case JSValueType::Object:
                return Completion::normal(value);

            case JSValueType::String: {
                auto *wrapper = heap.allocate<JSString>(value.to_u16string());
                wrapper->prototype = string_prototype;
                return Completion::normal(JSValue::object(wrapper));
            }
            case JSValueType::Number: {
                auto *wrapper = heap.allocate<JSPrimitiveWrapper>(value, "Number");
                wrapper->prototype = number_prototype;
                return Completion::normal(JSValue::object(wrapper));
            }
            case JSValueType::Symbol: {
                auto *wrapper = heap.allocate<JSPrimitiveWrapper>(value, "Symbol");
                wrapper->prototype = symbol_prototype;
                return Completion::normal(JSValue::object(wrapper));
            }
            case JSValueType::Boolean: {
                auto *wrapper = heap.allocate<JSPrimitiveWrapper>(value, "Boolean");
                wrapper->prototype = boolean_prototype;
                return Completion::normal(JSValue::object(wrapper));
            }
        }

        return Completion::normal(value);
    }

    Completion Evaluator::to_number(const JSValue &value) {
        Completion primitive = to_primitive(value, Hint::Number);

        if (primitive.is_abrupt()) return primitive;

        return Completion::normal(JSValue::number(primitive.get_value_or_undefined().to_number()));
    }

    Completion Evaluator::to_string(const JSValue &value) {
        Completion primitive = to_primitive(value, Hint::String);

        if (primitive.is_abrupt()) return primitive;

        return Completion::normal(JSValue::string(primitive.get_value_or_undefined().to_string()));
    }

    Completion Evaluator::loosely_equals(const JSValue &x, const JSValue &y) {
    const JSValueType xt = x.type();
        const JSValueType yt = y.type();

        if (yt == JSValueType::Object && (xt == JSValueType::String || xt == JSValueType::Number)) {
            JSValue primitive;
            TRY_VALUE(primitive, to_primitive(y, Hint::Default));
            return loosely_equals(x, primitive);
        }

        if (xt == JSValueType::Object && (yt == JSValueType::String || yt == JSValueType::Number)) {
            JSValue primitive;
            TRY_VALUE(primitive, to_primitive(x, Hint::Default));
            return loosely_equals(primitive, y);
        }

        if (xt == JSValueType::Boolean) {
            return loosely_equals(JSValue::number(x.to_number()), y);
        }

        if (yt == JSValueType::Boolean) {
            return loosely_equals(x, JSValue::number(y.to_number()));
        }

        return Completion::normal(JSValue::boolean(JSValue::is_loosely_equal(x, y)));
    }

    Completion Evaluator::less_than(const JSValue &x, const JSValue &y, bool left_first) {
        JSValue px;
        JSValue py;

        if (left_first) {
            TRY_VALUE(px, to_primitive(x, Hint::Number));
            TRY_VALUE(py, to_primitive(y, Hint::Number));
        } else {
            TRY_VALUE(py, to_primitive(y, Hint::Number));
            TRY_VALUE(px, to_primitive(x, Hint::Number));
        }

        const std::optional<bool> result = JSValue::is_less_than(px, py);

        return Completion::normal(result.has_value() ? JSValue::boolean(result.value()) : JSValue::undefined());
    }

    Completion Evaluator::apply_binary_operator(const JSValue &lval, BinaryOperator op, const JSValue &rval) {
        const Hint hint = (op == BinaryOperator::Add) ? Hint::Default : Hint::Number;

        Completion left = to_primitive(lval, hint);

        if (left.is_abrupt()) return left;

        Completion right = to_primitive(rval, hint);

        if (right.is_abrupt()) return right;

        return Completion::normal(JSValue::apply_string_or_numeric_binary_operator(
            left.get_value_or_undefined(), op, right.get_value_or_undefined()));
    }

    Completion Evaluator::make_error(JSObject *prototype, const std::vector<JSValue> &args) {
        JSObject *error = heap.allocate<JSObject>();
        error->prototype = prototype;

        if (!args.empty() && args[0].type() != JSValueType::Undefined) {
            Completion message = to_string(args[0]);
            if (message.is_abrupt()) return message;

            error->define_own_property("message",
                PropertyDescriptor::data(message.get_value_or_undefined(), true, false, true));
        }

        std::string header = "Error";
        if (prototype != nullptr) {
            if (const PropertyDescriptor *named = prototype->get_own_property("name");
                named != nullptr && named->is_data()
                && named->get_value().type() == JSValueType::String) {
                header = named->get_value().to_string();
            }
        }

        if (const PropertyDescriptor *described = error->get_own_property("message");
            described != nullptr && described->is_data()
            && described->get_value().type() == JSValueType::String) {
            const std::string message = described->get_value().to_string();
            if (!message.empty()) header += ": " + message;
        }

        error->define_own_property("stack",
            PropertyDescriptor::data(JSValue::string(capture_stack(header)), true, false, true));

        return Completion::normal(JSValue::object(error));
    }

    Completion Evaluator::throw_error(JSObject *prototype, const std::string &message) {
        Completion error = make_error(prototype, { JSValue::string(message) });
        if (error.is_abrupt()) return error;

        return Completion::throw_value(error.get_value_or_undefined());
    }

    namespace {
        std::string describe_location(const Program *script, uint32_t offset) {
            if (script == nullptr || script->line_starts.empty()) return "?";

            const uint32_t safe = offset < script->source.size()
                ? offset
                : static_cast<uint32_t>(script->source.size());

            const auto after = std::upper_bound(script->line_starts.begin(),
                                                script->line_starts.end(), safe);
            const auto line = static_cast<std::size_t>(after - script->line_starts.begin());

            return std::to_string(line) + ":" + std::to_string(safe - script->line_starts[line - 1] + 1);
        }
    }

    std::string Evaluator::capture_stack(const std::string &header) const {
        std::string out = header;

        const std::size_t total = call_stack.size();
        const std::size_t shown = std::min(total, MAX_STACK_FRAMES);

        for (std::size_t i = 0; i < shown; ++i) {
            const std::size_t at = total - 1 - i; // od najgłębszej ramki
            const CallFrame &frame = call_stack[at];

            const CallFrame *inner = at + 1 < total ? &call_stack[at + 1] : nullptr;

            const uint32_t position = inner != nullptr ? inner->offset : current_position;
            const Program *script = inner != nullptr
                ? inner->script
                : (current_script != nullptr ? current_script->get() : frame.script);

            const std::string name = frame.name != nullptr && !frame.name->empty()
                ? *frame.name
                : "<anonymous>";

            out += "\n    at " + name + " (" + describe_location(script, position) + ")";
        }

        if (total > shown) {
            out += "\n    ... " + std::to_string(total - shown) + " dalszych ramek";
        } else if (total > 0) {
            const CallFrame &oldest = call_stack.front();
            out += "\n    at <global> (" + describe_location(oldest.script, oldest.offset) + ")";
        }

        return out;
    }

    void Evaluator::enqueue_microtask(JSObject *job) {
        if (job != nullptr) microtasks.push_back(job);
    }

    Completion Evaluator::drain_microtasks() {
        if (draining_microtasks) return Completion::empty();

        StrictScope guard(draining_microtasks, true);

        Completion first_error = Completion::empty();
        std::size_t cursor = 0;

        while (cursor < microtasks.size() && cursor < MAX_MICROTASKS) {
            JSObject *job = microtasks[cursor++];
            if (job == nullptr || !job->is_callable()) continue;

            const Completion result = job->call(*this, JSValue::undefined(), {});

            if (result.type == COMPLETION_TYPE::THROW && first_error.type != COMPLETION_TYPE::THROW) {
                first_error = result;
            }
        }

        microtasks.erase(microtasks.begin(),
                         microtasks.begin() + static_cast<std::ptrdiff_t>(cursor));

        return first_error;
    }

    Completion Evaluator::eval_try(Environment *env, const TryStatement &node) {
        Completion result = eval_statement_list(env, node.block->body);

        if (node.catch_block != nullptr && result.type == COMPLETION_TYPE::THROW) {
            result = eval_catch(env, node, result.get_value_or_undefined());
        }

        if (node.finally_block == nullptr) {
            return result;
        }

        Completion finalizer = eval_statement_list(env, node.finally_block->body);

        if (finalizer.type != COMPLETION_TYPE::NORMAL) {
            return finalizer;
        }

        return result;
    }

    Completion Evaluator::eval_statement_list(Environment *env, const std::vector<StatementPtr> &body) {
        JSValue value;
        bool is_value_present = false;

        for (auto &statement : body) {
            auto result = eval_statement(env, *statement);

            if (std::holds_alternative<JSValue>(result.value)) {
                is_value_present=true;
                value = get<JSValue>(result.value);
            }

            if (result.type != COMPLETION_TYPE::NORMAL) {
                if (is_value_present) {
                    return Completion(result.type, value, result.target);
                }

                return result;
            }
        }

        if (is_value_present) {
            return Completion(COMPLETION_TYPE::NORMAL, value);
        }

        return Completion(COMPLETION_TYPE::NORMAL, std::monostate());
    }

    Completion Evaluator::hoist_variables(Environment *env, const Statement &node) {
        switch (node.kind) {
            case NodeKind::VariableDeclaration: {
                const auto &var = static_cast<const VariableDeclaration&>(node);
                for (auto &declarator : var.declarators) {
                    const std::string &id = declarator.id->name;
                    if (env->has_binding(id)) continue;

                    Completion created = env->create_mutable_binding(*this, id, false);
                    if (created.is_abrupt()) return created;

                    Completion stored = env->set_binding(*this, id, JSValue::undefined(), false);
                    if (stored.is_abrupt()) return stored;
                }
            } break;
            case NodeKind::BlockStatement: {
                const auto &block = static_cast<const BlockStatement&>(node);
                for (auto &statement : block.body) {
                    Completion hoisted = hoist_variables(env, *statement);
                    if (hoisted.is_abrupt()) return hoisted;
                }
            } break;
            case NodeKind::IfStatement: {
                const auto &if_statement = static_cast<const IfStatement&>(node);

                Completion hoisted = hoist_variables(env, *if_statement.consequent);
                if (hoisted.is_abrupt()) return hoisted;

                if (if_statement.alternate != nullptr) {
                    Completion alternate = hoist_variables(env, *if_statement.alternate);
                    if (alternate.is_abrupt()) return alternate;
                }
            } break;
            case NodeKind::WhileStatement:
                return hoist_variables(env, *static_cast<const WhileStatement&>(node).body);
            case NodeKind::DoWhileStatement:
                return hoist_variables(env, *static_cast<const DoWhileStatement&>(node).body);
            case NodeKind::LabeledStatement:
                return hoist_variables(env, *static_cast<const LabeledStatement&>(node).body);
            case NodeKind::ForStatement: {
                const auto &for_statement = static_cast<const ForStatement&>(node);

                if (for_statement.init != nullptr) {
                    Completion init = hoist_variables(env, *for_statement.init);
                    if (init.is_abrupt()) return init;
                }

                return hoist_variables(env, *for_statement.body);
            }
            case NodeKind::SwitchStatement: {
                const auto &switch_statement = static_cast<const SwitchStatement&>(node);
                for (auto &switch_case : switch_statement.cases) {
                    for (auto &statement : switch_case.body) {
                        Completion hoisted = hoist_variables(env, *statement);
                        if (hoisted.is_abrupt()) return hoisted;
                    }
                }
            } break;
            case NodeKind::FunctionDeclaration: {
                const auto &declaration = static_cast<const FunctionDeclaration&>(node);

                JSFunction *function = make_function(&declaration.params, declaration.body.get(),
                                                     env, declaration.name, declaration.strict);

                Completion created = env->create_mutable_binding(*this, declaration.name, false);
                if (created.is_abrupt()) return created;

                Completion stored = env->set_binding(*this, declaration.name,
                                                     JSValue::object(function), false);
                if (stored.is_abrupt()) return stored;
            } break;
            case NodeKind::TryStatement: {
                const auto &statement = static_cast<const TryStatement&>(node);

                for (const auto &s : statement.block->body) {
                    Completion hoisted = hoist_variables(env, *s);
                    if (hoisted.is_abrupt()) return hoisted;
                }
                if (statement.catch_block != nullptr) {
                    for (const auto &s : statement.catch_block->body) {
                        Completion hoisted = hoist_variables(env, *s);
                        if (hoisted.is_abrupt()) return hoisted;
                    }
                }
                if (statement.finally_block != nullptr) {
                    for (const auto &s : statement.finally_block->body) {
                        Completion hoisted = hoist_variables(env, *s);
                        if (hoisted.is_abrupt()) return hoisted;
                    }
                }
            } break;
            case NodeKind::ForInStatement: {
                const auto &loop = static_cast<const ForInStatement&>(node);

                if (loop.declaration != nullptr) {
                    Completion declaration = hoist_variables(env, *loop.declaration);
                    if (declaration.is_abrupt()) return declaration;
                }

                return hoist_variables(env, *loop.body);
            }
            case NodeKind::WithStatement:
                return hoist_variables(env, *static_cast<const WithStatement&>(node).body);
            default: {}
        }

        return Completion::empty();
    }

    Completion Evaluator::call_function(JSFunction *function, const JSValue &this_value, const std::vector<JSValue> &args) {
        if (stack_exhausted() || call_depth >= MAX_CALL_DEPTH) {
            return throw_error(range_error_prototype, "Maximum call stack size exceeded");
        }

        if (call_depth >= MAX_CALL_DEPTH) {
            return throw_error(range_error_prototype, "Maximum call stack size exceeded");
        }

        CallFrameScope frame(call_stack, function->name, current_position,
                             current_script != nullptr ? current_script->get() : nullptr);

        StrictScope strict(strict_code, function->strict);
        ScriptScope owner(*this, function->script);
        JSValue this_binding = this_value;

        if (!function->strict) {
            if (this_binding.type() == JSValueType::Undefined || this_binding.type() == JSValueType::Null) {
                if (global_object != nullptr) this_binding = JSValue::object(global_object);
            } else if (this_binding.type() != JSValueType::Object) {
                Completion boxed = to_object(this_binding);
                if (boxed.is_abrupt()) return boxed;

                this_binding = boxed.get_value_or_undefined();
            }
        }

        DepthGuard guard(call_depth);
        ThisScope this_scope(this_stack, this_binding);

        auto *env = heap.allocate<DeclarativeEnvironment>(function->closure);

        const auto &params = *function->params;
        for (std::size_t i = 0; i < params.size(); ++i) {
            const std::string &name = params[i]->name;

            if (!env->has_binding(name)) {
                Completion created = env->create_mutable_binding(*this, name, false);
                if (created.is_abrupt()) return created;
            }

            Completion stored = env->set_binding(*this, name,
                i < args.size() ? args[i] : JSValue::undefined(), false);
            if (stored.is_abrupt()) return stored;
        }

        auto *arguments = heap.allocate<JSArguments>();
        arguments->prototype = object_prototype;
        arguments->env = env;

        arguments->define_own_property("length",
            PropertyDescriptor::data(JSValue::number(static_cast<double>(args.size())), true, false, true));

        for (std::size_t i = 0; i < args.size(); ++i) {
            arguments->define_own_property(std::to_string(i),
                PropertyDescriptor::data(args[i], true, true, true));
        }

        if (function->strict) {
            const PropertyDescriptor poisoned = PropertyDescriptor::accessor(
                type_error_thrower, type_error_thrower, false, false);

            arguments->define_own_property("callee", poisoned);
            arguments->define_own_property("caller", poisoned);
        } else {
            arguments->define_own_property("callee",
                PropertyDescriptor::data(JSValue::object(function), true, false, true));

            for (std::size_t i = args.size(); i-- > 0;) {
                if (i >= params.size()) continue;

                const std::string &name = params[i]->name;

                bool already_mapped = false;
                for (const auto &entry : arguments->mapped) {
                    if (entry.second == name) { already_mapped = true; break; }
                }

                if (!already_mapped) arguments->mapped.emplace(std::to_string(i), name);
            }
        }

        if (!env->has_binding("arguments")) {
            Completion created = env->create_mutable_binding(*this, "arguments", false);
            if (created.is_abrupt()) return created;

            Completion stored = env->set_binding(*this, "arguments", JSValue::object(arguments), false);
            if (stored.is_abrupt()) return stored;
        }

        for (const auto &statement : function->body->body) {
            Completion hoisted = hoist_variables(env, *statement);
            if (hoisted.is_abrupt()) return hoisted;
        }

        Completion result = eval_statement_list(env, function->body->body);

        if (result.type == COMPLETION_TYPE::THROW) {
            return result;
        }

        if (result.type == COMPLETION_TYPE::RETURN) {
            return Completion::normal(result.get_value_or_undefined());
        }

        return Completion::normal(JSValue::undefined());
    }

    JSFunction* Evaluator::make_function(const std::vector<std::unique_ptr<IdentifierLiteral>> *params,
        const BlockStatement *body, Environment *closure, std::string name, bool strict) {

        auto *function = heap.allocate<JSFunction>();
        function->params  = params;
        function->body    = body;
        function->closure = closure;
        function->name    = std::move(name);
        function->strict  = strict;
        if (current_script != nullptr) function->script = *current_script;
        function->prototype = function_prototype;

        auto *instance_prototype = heap.allocate<JSObject>();
        instance_prototype->prototype = object_prototype;
        instance_prototype->define_own_property("constructor",
            PropertyDescriptor::data(JSValue::object(function), true, false, true));

        function->define_own_property("prototype",
            PropertyDescriptor::data(JSValue::object(instance_prototype), true, false, false));

        function->define_own_property("length",
            PropertyDescriptor::data(JSValue::number(static_cast<double>(params->size())),
                                     false, false, false));

        if (strict && type_error_thrower != nullptr) {
            const PropertyDescriptor poisoned = PropertyDescriptor::accessor(
                type_error_thrower, type_error_thrower, false, false);

            function->define_own_property("caller", poisoned);
            function->define_own_property("arguments", poisoned);
        }

        return function;
    }

    Completion Evaluator::eval_program(Environment *env, const Program &node) {
        RootEnvironmentScope root(root_environments, env);
        StrictScope strict(strict_code, node.strict);

        for (auto &statement : node.body) {
            Completion hoisted = hoist_variables(env, *statement);
            if (hoisted.is_abrupt()) return hoisted;
        }

        return eval_statement_list(env, node.body);
    }

    void Evaluator::trace_roots(CellVisitor &visitor) {
        visitor.visit(global_object);
        visitor.visit(global_env);
        visitor.visit(eval_builtin);
        visitor.visit(type_error_thrower);

        for (JSObject *intrinsic : {object_prototype, function_prototype, array_prototype, regexp_prototype,
                                    string_prototype, number_prototype, boolean_prototype, date_prototype,
                                  error_prototype, type_error_prototype, reference_error_prototype,
                                  range_error_prototype, syntax_error_prototype,
                                  uri_error_prototype, eval_error_prototype,
                                  promise_prototype, symbol_prototype, map_prototype, set_prototype,
                                  weak_map_prototype, weak_set_prototype, array_buffer_prototype}) {
            visitor.visit(intrinsic);
        }

        for (JSObject *typed : typed_array_prototypes) {
            if (typed != nullptr) visitor.visit(typed);
        }

        for (const JSValue &value : this_stack) {
            if (JSObject *object = value.as_object())
                visitor.visit(object);
        }

        for (JSObject *job : microtasks) {
            if (job != nullptr) visitor.visit(job);
        }

        for (Environment *env : root_environments) {
            visitor.visit(env);
        }
    }
}
