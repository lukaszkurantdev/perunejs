 #ifndef PERUNEJS_JS_FUNCTION_H
#define PERUNEJS_JS_FUNCTION_H
#include "completion.h"
#include "environment.h"
#include "js_object.h"
#include <memory>

namespace perunejs {
    class JSFunction : public JSObject {
    public:
        const std::vector<std::unique_ptr<IdentifierLiteral>>* params = nullptr;
        const BlockStatement* body = nullptr;
        Environment* closure = nullptr;
        std::string name;
        bool strict = false;
        std::shared_ptr<Program> script;

        bool is_callable() const override { return true; }

        const char* class_name() override {
            return "Function";
        }

        Completion call(Evaluator& evaluator, const JSValue& this_value, const std::vector<JSValue>& args) override;

        Completion construct(Evaluator &evaluator, const std::vector<JSValue> &args) override;

        void trace(CellVisitor &visitor) const override {
            JSObject::trace(visitor);

            if (closure != nullptr) {
                visitor.visit(closure);
            }
        };
    };
}

#endif //PERUNEJS_JS_FUNCTION_H
