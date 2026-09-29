#ifndef PERUNEJS_JS_PROMISE_H
#define PERUNEJS_JS_PROMISE_H

#include <memory>
#include <vector>

#include "js_native_function.h"
#include "js_object.h"
#include "js_value.h"

namespace perunejs {
    class JSPromise;

    enum class PromiseState : uint8_t { Pending, Fulfilled, Rejected };

    struct PromiseReaction {
        JSObject *handler = nullptr;
        JSPromise *result = nullptr;
        bool rejects = false;
    };

    class JSPromise : public JSObject {
    public:
        PromiseState state = PromiseState::Pending;
        JSValue result;

        std::vector<PromiseReaction> fulfill_reactions;
        std::vector<PromiseReaction> reject_reactions;

        const char *class_name() override { return "Promise"; }

        void trace(CellVisitor &visitor) const override {
            JSObject::trace(visitor);

            if (JSObject *value = result.as_object()) visitor.visit(value);

            for (const std::vector<PromiseReaction> *list : {&fulfill_reactions, &reject_reactions}) {
                for (const PromiseReaction &reaction : *list) {
                    if (reaction.handler != nullptr) visitor.visit(reaction.handler);
                    if (reaction.result != nullptr) visitor.visit(reaction.result);
                }
            }
        }
    };

    class JSResolvingFunction : public JSObject {
    public:
        JSPromise *promise = nullptr;
        bool rejects = false;
        std::shared_ptr<bool> already_resolved;

        JSResolvingFunction(JSPromise *promise, bool rejects, std::shared_ptr<bool> already_resolved)
            : promise(promise), rejects(rejects), already_resolved(std::move(already_resolved)) {}

        bool is_callable() const override { return true; }
        const char *class_name() override { return "Function"; }

        Completion call(Evaluator &evaluator, const JSValue &this_value,
                        const std::vector<JSValue> &args) override;

        void trace(CellVisitor &visitor) const override {
            JSObject::trace(visitor);
            if (promise != nullptr) visitor.visit(promise);
        }
    };

    class JSPromiseReactionJob : public JSObject {
    public:
        PromiseReaction reaction;
        JSValue argument;

        JSPromiseReactionJob(PromiseReaction reaction, JSValue argument)
            : reaction(reaction), argument(std::move(argument)) {}

        bool is_callable() const override { return true; }
        const char *class_name() override { return "Function"; }

        Completion call(Evaluator &evaluator, const JSValue &this_value,
                        const std::vector<JSValue> &args) override;

        void trace(CellVisitor &visitor) const override {
            JSObject::trace(visitor);

            if (reaction.handler != nullptr) visitor.visit(reaction.handler);
            if (reaction.result != nullptr) visitor.visit(reaction.result);
            if (JSObject *value = argument.as_object()) visitor.visit(value);
        }
    };

    class JSPromiseThenableJob : public JSObject {
    public:
        JSPromise *promise = nullptr;
        JSValue thenable;
        JSObject *then = nullptr;

        JSPromiseThenableJob(JSPromise *promise, JSValue thenable, JSObject *then)
            : promise(promise), thenable(std::move(thenable)), then(then) {}

        bool is_callable() const override { return true; }
        const char *class_name() override { return "Function"; }

        Completion call(Evaluator &evaluator, const JSValue &this_value,
                        const std::vector<JSValue> &args) override;

        void trace(CellVisitor &visitor) const override {
            JSObject::trace(visitor);

            if (promise != nullptr) visitor.visit(promise);
            if (then != nullptr) visitor.visit(then);
            if (JSObject *value = thenable.as_object()) visitor.visit(value);
        }
    };

    class JSPromiseAllElement : public JSObject {
    public:
        JSObject *values = nullptr;
        uint32_t index = 0;
        std::shared_ptr<uint32_t> remaining;
        std::shared_ptr<bool> already_called;
        JSPromise *result = nullptr;

        JSPromiseAllElement(JSObject *values, uint32_t index, std::shared_ptr<uint32_t> remaining,
                            std::shared_ptr<bool> already_called, JSPromise *result)
            : values(values), index(index), remaining(std::move(remaining)),
              already_called(std::move(already_called)), result(result) {}

        bool is_callable() const override { return true; }
        const char *class_name() override { return "Function"; }

        Completion call(Evaluator &evaluator, const JSValue &this_value,
                        const std::vector<JSValue> &args) override;

        void trace(CellVisitor &visitor) const override {
            JSObject::trace(visitor);

            if (values != nullptr) visitor.visit(values);
            if (result != nullptr) visitor.visit(result);
        }
    };

    void fulfill_promise(Evaluator &evaluator, JSPromise *promise, const JSValue &value);
    void reject_promise(Evaluator &evaluator, JSPromise *promise, const JSValue &reason);
    void resolve_promise(Evaluator &evaluator, JSPromise *promise, const JSValue &resolution);
    JSPromise *make_promise(Evaluator &evaluator);
}

#endif //PERUNEJS_JS_PROMISE_H
