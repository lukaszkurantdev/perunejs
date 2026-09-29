#include "runtime/builtins.h"

#include "memory/js_array.h"
#include "memory/js_promise.h"

namespace perunejs {
    namespace {
        void trigger_reactions(Evaluator &evaluator, std::vector<PromiseReaction> &reactions,
                               const JSValue &argument) {
            for (const PromiseReaction &reaction : reactions) {
                evaluator.enqueue_microtask(
                    evaluator.heap.allocate<JSPromiseReactionJob>(reaction, argument));
            }

            reactions.clear();
        }

        Completion require_promise(Evaluator &evaluator, const JSValue &value, JSPromise *&out) {
            if (value.type() == JSValueType::Object) {
                if (auto *promise = dynamic_cast<JSPromise *>(value.as_object()); promise != nullptr) {
                    out = promise;
                    return Completion::empty();
                }
            }

            return evaluator.throw_error(evaluator.type_error_prototype,
                "Method Promise.prototype.then called on incompatible receiver");
        }
        
        JSPromise *perform_then(Evaluator &evaluator, JSPromise *promise,
                                const JSValue &on_fulfilled, const JSValue &on_rejected) {
            auto *result = make_promise(evaluator);

            const auto handler_of = [](const JSValue &value) -> JSObject * {
                return value.type() == JSValueType::Object && value.as_object()->is_callable()
                    ? value.as_object()
                    : nullptr;
            };

            const PromiseReaction on_success{handler_of(on_fulfilled), result, false};
            const PromiseReaction on_failure{handler_of(on_rejected), result, true};

            switch (promise->state) {
                case PromiseState::Pending:
                    promise->fulfill_reactions.push_back(on_success);
                    promise->reject_reactions.push_back(on_failure);
                    break;
                case PromiseState::Fulfilled:
                    evaluator.enqueue_microtask(
                        evaluator.heap.allocate<JSPromiseReactionJob>(on_success, promise->result));
                    break;
                case PromiseState::Rejected:
                    evaluator.enqueue_microtask(
                        evaluator.heap.allocate<JSPromiseReactionJob>(on_failure, promise->result));
                    break;
            }

            return result;
        }

        std::pair<JSObject *, JSObject *> resolving_functions(Evaluator &evaluator, JSPromise *promise) {
            auto shared = std::make_shared<bool>(false);

            return {
                evaluator.heap.allocate<JSResolvingFunction>(promise, false, shared),
                evaluator.heap.allocate<JSResolvingFunction>(promise, true, shared)
            };
        }

        // ---- powierzchnia widoczna z JS ----

        Completion call_promise(Evaluator &e, const JSValue&, const std::vector<JSValue>&) {
            return e.throw_error(e.type_error_prototype, "Constructor Promise requires 'new'");
        }

        Completion construct_promise(Evaluator &e, const JSValue&, const std::vector<JSValue> &args) {
            const JSValue executor = argument_at(args, 0);

            if (executor.type() != JSValueType::Object || !executor.as_object()->is_callable()) {
                return e.throw_error(e.type_error_prototype, "Promise resolver is not a function");
            }

            JSPromise *promise = make_promise(e);
            const auto [resolve, reject] = resolving_functions(e, promise);

            MarkedVector call_args(e.heap);
            call_args.push_back(JSValue::object(resolve));
            call_args.push_back(JSValue::object(reject));

            // 25.4.3.1 krok 10: wyjątek z executora odrzuca promesę,
            // a nie wychodzi na zewnątrz konstruktora.
            if (Completion ran = executor.as_object()->call(e, JSValue::undefined(), call_args);
                ran.type == COMPLETION_TYPE::THROW) {
                reject_promise(e, promise, ran.get_value_or_undefined());
            }

            return Completion::normal(JSValue::object(promise));
        }

        Completion promise_then(Evaluator &e, const JSValue &self, const std::vector<JSValue> &args) {
            JSPromise *promise = nullptr;
            if (Completion c = require_promise(e, self, promise); c.is_abrupt()) return c;

            return Completion::normal(JSValue::object(
                perform_then(e, promise, argument_at(args, 0), argument_at(args, 1))));
        }

        Completion promise_catch(Evaluator &e, const JSValue &self, const std::vector<JSValue> &args) {
            JSPromise *promise = nullptr;
            if (Completion c = require_promise(e, self, promise); c.is_abrupt()) return c;

            return Completion::normal(JSValue::object(
                perform_then(e, promise, JSValue::undefined(), argument_at(args, 0))));
        }

        Completion promise_resolve(Evaluator &e, const JSValue&, const std::vector<JSValue> &args) {
            const JSValue value = argument_at(args, 0);

            // 25.4.4.5 krok 3: gotowa promesa wraca bez opakowywania.
            if (value.type() == JSValueType::Object) {
                if (dynamic_cast<JSPromise *>(value.as_object()) != nullptr) {
                    return Completion::normal(value);
                }
            }

            JSPromise *promise = make_promise(e);
            resolve_promise(e, promise, value);

            return Completion::normal(JSValue::object(promise));
        }

        Completion promise_reject(Evaluator &e, const JSValue&, const std::vector<JSValue> &args) {
            JSPromise *promise = make_promise(e);
            reject_promise(e, promise, argument_at(args, 0));

            return Completion::normal(JSValue::object(promise));
        }

        // Wspólne dla all i race: chodzimy po obiekcie tablicopodobnym,
        // bo protokół iteracji wymagałby prawdziwych symboli.
        Completion each_element(Evaluator &e, const JSValue &source,
                                const std::function<Completion(uint32_t, const JSValue &)> &visit,
                                uint32_t &count) {
            Completion converted = e.to_object(source);
            if (converted.is_abrupt()) return converted;

            JSObject *object = converted.get_value_or_undefined().as_object();

            Completion raw = object->get(e, "length");
            if (raw.is_abrupt()) return raw;

            Completion number = e.to_number(raw.get_value_or_undefined());
            if (number.is_abrupt()) return number;

            const double size = number.get_value_or_undefined().to_number();
            count = static_cast<uint32_t>(std::isnan(size) || size < 0 ? 0 : size);

            for (uint32_t i = 0; i < count; ++i) {
                Completion element = object->get(e, std::to_string(i));
                if (element.is_abrupt()) return element;

                if (Completion visited = visit(i, element.get_value_or_undefined());
                    visited.is_abrupt()) return visited;
            }

            return Completion::empty();
        }

        Completion subscribe(Evaluator &e, const JSValue &value, JSObject *on_fulfilled, JSObject *on_rejected) {
            Completion wrapped = promise_resolve(e, JSValue::undefined(), {value});
            if (wrapped.is_abrupt()) return wrapped;

            auto *promise = static_cast<JSPromise *>(wrapped.get_value_or_undefined().as_object());

            perform_then(e, promise,
                         on_fulfilled != nullptr ? JSValue::object(on_fulfilled) : JSValue::undefined(),
                         on_rejected != nullptr ? JSValue::object(on_rejected) : JSValue::undefined());

            return Completion::empty();
        }

        Completion promise_all(Evaluator &e, const JSValue&, const std::vector<JSValue> &args) {
            JSPromise *result = make_promise(e);

            auto *values = e.heap.allocate<JSArray>();
            values->prototype = e.array_prototype;
            values->define_own_property("length",
                PropertyDescriptor::data(JSValue::number(0), true, false, false));

            auto remaining = std::make_shared<uint32_t>(1);
            const auto [ignored, reject] = resolving_functions(e, result);
            (void) ignored;

            uint32_t total = 0;

            const Completion walked = each_element(e, argument_at(args, 0),
                [&](uint32_t index, const JSValue &element) -> Completion {
                    ++*remaining;

                    auto *collector = e.heap.allocate<JSPromiseAllElement>(
                        values, index, remaining, std::make_shared<bool>(false), result);

                    return subscribe(e, element, collector, reject);
                }, total);

            if (walked.is_abrupt()) {
                reject_promise(e, result, walked.get_value_or_undefined());
                return Completion::normal(JSValue::object(result));
            }

            // Zdjęcie początkowej jedynki: jeśli wejście było puste albo
            // wszystko rozstrzygnęło się od razu, tu domykamy całość.
            if (--*remaining == 0) {
                resolve_promise(e, result, JSValue::object(values));
            }

            return Completion::normal(JSValue::object(result));
        }

        Completion promise_race(Evaluator &e, const JSValue&, const std::vector<JSValue> &args) {
            JSPromise *result = make_promise(e);
            const auto [resolve, reject] = resolving_functions(e, result);

            uint32_t total = 0;

            const Completion walked = each_element(e, argument_at(args, 0),
                [&](uint32_t, const JSValue &element) -> Completion {
                    return subscribe(e, element, resolve, reject);
                }, total);

            if (walked.is_abrupt()) {
                reject_promise(e, result, walked.get_value_or_undefined());
            }

            return Completion::normal(JSValue::object(result));
        }
    }

    // ---- operacje ze spec, wołane też z js_promise.cpp ----

    JSPromise *make_promise(Evaluator &evaluator) {
        auto *promise = evaluator.heap.allocate<JSPromise>();
        promise->prototype = evaluator.promise_prototype;

        return promise;
    }

    void fulfill_promise(Evaluator &evaluator, JSPromise *promise, const JSValue &value) {
        if (promise->state != PromiseState::Pending) return;

        promise->result = value;
        promise->state = PromiseState::Fulfilled;
        promise->reject_reactions.clear();

        trigger_reactions(evaluator, promise->fulfill_reactions, value);
    }

    void reject_promise(Evaluator &evaluator, JSPromise *promise, const JSValue &reason) {
        if (promise->state != PromiseState::Pending) return;

        promise->result = reason;
        promise->state = PromiseState::Rejected;
        promise->fulfill_reactions.clear();

        trigger_reactions(evaluator, promise->reject_reactions, reason);
    }

    // 25.4.1.3.2 — rozstrzyganie z uwzględnieniem obiektów z metodą `then`.
    void resolve_promise(Evaluator &evaluator, JSPromise *promise, const JSValue &resolution) {
        if (resolution.type() == JSValueType::Object && resolution.as_object() == promise) {
            const Completion cycle = evaluator.make_error(evaluator.type_error_prototype,
                { JSValue::string("Chaining cycle detected for promise") });

            reject_promise(evaluator, promise, cycle.get_value_or_undefined());
            return;
        }

        if (resolution.type() != JSValueType::Object) {
            fulfill_promise(evaluator, promise, resolution);
            return;
        }

        Completion then = resolution.as_object()->get(evaluator, "then");
        if (then.type == COMPLETION_TYPE::THROW) {
            reject_promise(evaluator, promise, then.get_value_or_undefined());
            return;
        }

        const JSValue handler = then.get_value_or_undefined();

        if (handler.type() != JSValueType::Object || !handler.as_object()->is_callable()) {
            fulfill_promise(evaluator, promise, resolution);
            return;
        }

        evaluator.enqueue_microtask(evaluator.heap.allocate<JSPromiseThenableJob>(
            promise, resolution, handler.as_object()));
    }

    Completion JSResolvingFunction::call(Evaluator &evaluator, const JSValue&,
                                         const std::vector<JSValue> &args) {
        if (*already_resolved) return Completion::normal(JSValue::undefined());
        *already_resolved = true;

        const JSValue value = args.empty() ? JSValue::undefined() : args[0];

        if (rejects) reject_promise(evaluator, promise, value);
        else         resolve_promise(evaluator, promise, value);

        return Completion::normal(JSValue::undefined());
    }

    Completion JSPromiseReactionJob::call(Evaluator &evaluator, const JSValue&,
                                          const std::vector<JSValue>&) {
        // Brak uchwytu: wartość (albo powód) idzie dalej bez zmian.
        if (reaction.handler == nullptr) {
            if (reaction.rejects) reject_promise(evaluator, reaction.result, argument);
            else                  resolve_promise(evaluator, reaction.result, argument);

            return Completion::normal(JSValue::undefined());
        }

        MarkedVector call_args(evaluator.heap);
        call_args.push_back(argument);

        const Completion produced = reaction.handler->call(evaluator, JSValue::undefined(), call_args);

        if (produced.type == COMPLETION_TYPE::THROW) {
            reject_promise(evaluator, reaction.result, produced.get_value_or_undefined());
        } else {
            resolve_promise(evaluator, reaction.result, produced.get_value_or_undefined());
        }

        return Completion::normal(JSValue::undefined());
    }

    Completion JSPromiseThenableJob::call(Evaluator &evaluator, const JSValue&,
                                          const std::vector<JSValue>&) {
        auto shared = std::make_shared<bool>(false);

        auto *resolve = evaluator.heap.allocate<JSResolvingFunction>(promise, false, shared);
        auto *reject = evaluator.heap.allocate<JSResolvingFunction>(promise, true, shared);

        MarkedVector call_args(evaluator.heap);
        call_args.push_back(JSValue::object(resolve));
        call_args.push_back(JSValue::object(reject));

        if (const Completion ran = then->call(evaluator, thenable, call_args);
            ran.type == COMPLETION_TYPE::THROW && !*shared) {
            *shared = true;
            reject_promise(evaluator, promise, ran.get_value_or_undefined());
        }

        return Completion::normal(JSValue::undefined());
    }

    Completion JSPromiseAllElement::call(Evaluator &evaluator, const JSValue&,
                                         const std::vector<JSValue> &args) {
        if (*already_called) return Completion::normal(JSValue::undefined());
        *already_called = true;

        if (Completion stored = values->put(evaluator, std::to_string(index),
                                            args.empty() ? JSValue::undefined() : args[0], false);
            stored.is_abrupt()) return stored;

        if (--*remaining == 0) {
            resolve_promise(evaluator, result, JSValue::object(values));
        }

        return Completion::normal(JSValue::undefined());
    }

    void install_promise(const Builtins &b) {
        JSObject *prototype = b.evaluator.promise_prototype;

        b.method(prototype, "then",  promise_then, 2);
        b.method(prototype, "catch", promise_catch, 1);

        JSObject *constructor = b.constructor("Promise", call_promise, prototype, construct_promise, 1);

        b.method(constructor, "resolve", promise_resolve, 1);
        b.method(constructor, "reject",  promise_reject, 1);
        b.method(constructor, "all",     promise_all, 1);
        b.method(constructor, "race",    promise_race, 1);
    }
}
