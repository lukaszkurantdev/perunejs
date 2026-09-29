#ifndef PERUNEJS_EVALUATOR_H
#define PERUNEJS_EVALUATOR_H

#include <iostream>

#include "memory/completion.h"
#include "memory/environment.h"
#include "memory/heap.h"
#include "memory/js_function.h"
#include "memory/js_value.h"
#include "memory/reference.h"
#include "memory/marked_vector.h"
#include "utils/native_stack.h"

namespace perunejs {
    class ThisScope {
        std::vector<JSValue>& stack_;
    public:
        ThisScope(std::vector<JSValue>& stack, const JSValue& value) : stack_(stack) { stack_.push_back(value); }
        ~ThisScope() { stack_.pop_back(); }
        ThisScope(const ThisScope&) = delete;
        ThisScope& operator=(const ThisScope&) = delete;
    };

    class StrictScope {
        bool& flag_;
        bool  saved_;
    public:
        StrictScope(bool& flag, bool value) : flag_(flag), saved_(flag) { flag_ = value; }
        ~StrictScope() { flag_ = saved_; }
        StrictScope(const StrictScope&) = delete;
        StrictScope& operator=(const StrictScope&) = delete;
    };

    class RootEnvironmentScope {
        std::vector<Environment*>& roots_;
    public:
        RootEnvironmentScope(std::vector<Environment*>& roots, Environment* env) : roots_(roots) { roots_.push_back(env); }
        ~RootEnvironmentScope() { roots_.pop_back(); }
        RootEnvironmentScope(const RootEnvironmentScope&) = delete;
        RootEnvironmentScope& operator=(const RootEnvironmentScope&) = delete;
    };

    // Ramka stosu wywołań — utrzymywana wyłącznie po to, żeby błąd potrafił
    // powiedzieć, gdzie powstał. `name` wskazuje na nazwę wywoływanej funkcji
    // (obiekt żyje przez cały czas wywołania), `offset` to miejsce wywołania
    // w `script`, czyli w skrypcie WOŁAJĄCYM.
    struct CallFrame {
        const std::string *name;
        uint32_t offset;
        const Program *script;
    };

    class CallFrameScope {
        std::vector<CallFrame> &frames_;
    public:
        CallFrameScope(std::vector<CallFrame> &frames, const std::string &name,
                       uint32_t offset, const Program *script) : frames_(frames) {
            frames_.push_back(CallFrame{&name, offset, script});
        }
        ~CallFrameScope() { frames_.pop_back(); }
        CallFrameScope(const CallFrameScope &) = delete;
        CallFrameScope &operator=(const CallFrameScope &) = delete;
    };

    using LabelSet = std::vector<std::string>;

    class Evaluator : public RootSource {
        uint32_t call_depth = 0;
        static constexpr uint32_t MAX_CALL_DEPTH = 10000; // TODO: to ma być możliwe do ustawienia przy konfiguracji

        std::vector<JSValue> this_stack;
        std::vector<CallFrame> call_stack;
        std::vector<JSObject *> microtasks;
        bool strict_code = false;

        // Ile ramek najwyżej trafia do Error.prototype.stack.
        static constexpr std::size_t MAX_STACK_FRAMES = 32;

        // Bezpiecznik na kolejkę, która sama siebie karmi bez końca.
        static constexpr std::size_t MAX_MICROTASKS = 1000000;
        bool draining_microtasks = false;
        std::uintptr_t stack_limit = native_stack_limit();

        bool stack_exhausted() const { return native_stack_pointer() < stack_limit; }

    public:
        Heap &heap;

        JSObject* global_object = nullptr;
        Environment* global_env = nullptr;

        JSObject* object_prototype   = nullptr;
        JSObject* function_prototype = nullptr;
        JSObject* array_prototype    = nullptr;
        JSObject* regexp_prototype   = nullptr;
        JSObject* string_prototype   = nullptr;
        JSObject* number_prototype   = nullptr;
        JSObject* boolean_prototype  = nullptr;
        JSObject* date_prototype     = nullptr;

        JSObject* error_prototype           = nullptr;
        JSObject* type_error_prototype      = nullptr;
        JSObject* reference_error_prototype = nullptr;
        JSObject* range_error_prototype     = nullptr;
        JSObject* syntax_error_prototype    = nullptr;
        JSObject* uri_error_prototype       = nullptr;
        JSObject* eval_error_prototype      = nullptr;

        JSObject* promise_prototype  = nullptr;
        JSObject* symbol_prototype   = nullptr;
        JSObject* array_buffer_prototype = nullptr;
        JSObject* typed_array_prototypes[9] = {};
        JSObject* map_prototype      = nullptr;
        JSObject* set_prototype      = nullptr;
        JSObject* weak_map_prototype = nullptr;
        JSObject* weak_set_prototype = nullptr;

        JSObject* type_error_thrower = nullptr;
        JSObject* eval_builtin = nullptr;
        const std::shared_ptr<Program> *current_script = nullptr;

        struct ScriptScope {
            Evaluator &evaluator;
            const std::shared_ptr<Program> *previous;

            ScriptScope(Evaluator &owner, const std::shared_ptr<Program> &script)
                : evaluator(owner), previous(owner.current_script) {
                evaluator.current_script = &script;
            }

            ~ScriptScope() { evaluator.current_script = previous; }

            ScriptScope(const ScriptScope &) = delete;
            ScriptScope &operator=(const ScriptScope &) = delete;
        };
        std::ostream &output;

        explicit Evaluator(Heap &heap, std::ostream &output = std::cout) : heap(heap), output(output) {
            heap.add_root_source(this);
        }

        ~Evaluator() override { heap.remove_root_source(this); }
        
        Evaluator(const Evaluator &) = delete;
        Evaluator &operator=(const Evaluator &) = delete;

        void trace_roots(CellVisitor &visitor) override;

        std::vector<Environment *> root_environments;

        Completion evaluate(Environment *env, const Node& node);
        Completion eval_program(Environment *env, const Program& node);
        Completion call_function(JSFunction* function, const JSValue &this_value, const std::vector<JSValue>& args);
        JSFunction* make_function(const std::vector<std::unique_ptr<IdentifierLiteral>>* params,
                                  const BlockStatement* body,
                                  Environment* closure,
                                  std::string name, bool strict);

        Completion to_primitive(const JSValue& value, Hint hint);
        Completion to_object(const JSValue& value);
        Completion to_number(const JSValue& value);
        Completion to_string(const JSValue& value);
        Completion loosely_equals(const JSValue& x, const JSValue& y);
        Completion less_than(const JSValue& x, const JSValue& y, bool left_first);
        Completion apply_binary_operator(const JSValue& lval, BinaryOperator op, const JSValue& rval);

        Completion make_error(JSObject* prototype, const std::vector<JSValue>& args);
        Completion throw_error(JSObject* prototype, const std::string& message);
        Heap& heap_ref() { return heap; }
        Completion eval_code(const JSValue& code, Environment* env, bool keep_this);
        Completion compile(const std::string& source, bool strict, std::shared_ptr<Program>& out);
        Completion run_script(Environment* env, const std::string& source, bool strict = false);
        Completion make_regexp(const std::u16string& pattern, const std::string& flags);

        uint32_t current_position = 0;

        std::string capture_stack(const std::string& header) const;

        void bind_to_current_thread(std::size_t margin_bytes = 256 * 1024) {
            stack_limit = native_stack_limit(margin_bytes);
        }

        void enqueue_microtask(JSObject* job);
        Completion drain_microtasks();
        std::size_t pending_microtasks() const { return microtasks.size(); }

    private:
        Completion eval_number(const NumberLiteral& node);
        Completion eval_string(const StringLiteral& node);
        Completion eval_regexp(const RegExpLiteral& node);
        Completion eval_boolean(const BooleanLiteral& node);
        Completion eval_null(const NullLiteral& node);

        Completion eval_identifier(Environment *env, const IdentifierLiteral& node);
        Completion eval_unary(Environment *env, const UnaryExpression& node);
        Completion eval_binary(Environment *env, const BinaryExpression& node);
        Completion eval_assign(Environment *env, const AssignExpression& node);
        Completion eval_statement(Environment *env, const Statement& node, const LabelSet& labels = {});
        Completion eval_conditional(Environment *env, const ConditionalExpression& node);
        Completion eval_sequence(Environment *env, const SequenceExpression& node);
        Completion eval_block(Environment *env, const BlockStatement& node);
        Completion eval_labeled(Environment *env, const LabeledStatement& node, LabelSet labels = {});
        Completion eval_update(Environment *env, const UpdateExpression& node);
        Completion eval_function(Environment *env, const FunctionExpression& node);
        Completion eval_call(Environment *env, const CallExpression& node);
        Completion eval_try(Environment* env, const TryStatement& node);
        Completion eval_catch(Environment* env, const TryStatement& node, const JSValue& thrown);
        Completion evaluate_reference(Environment* env, const Node& node, Reference& out);
        Completion eval_object(Environment *env, const ObjectExpression &node);
        Completion eval_this(Environment*, const ThisExpression&);
        Completion eval_array(Environment *env, const ArrayExpression &node);
        Completion eval_new(Environment *env, const NewExpression &node);
        Completion eval_for_in(Environment *env, const ForInStatement &node, const LabelSet& labels);


        Completion get_value(const Reference& ref);
        Completion put_value(const Reference& ref, const JSValue& value);

        Completion eval_statement_list(Environment*, const std::vector<StatementPtr>& body);
        Completion hoist_variables(Environment* env, const Statement& node);



    };
}


#endif //PERUNEJS_EVALUATOR_H
