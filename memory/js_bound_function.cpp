#include "js_bound_function.h"

#include "evaluator/evaluator.h"

namespace perunejs {
    namespace {
        void combine(MarkedVector &out, const std::vector<JSValue> &bound,
                    const std::vector<JSValue> &args) {
            out.reserve(bound.size() + args.size());

            for (const JSValue &value : bound) out.push_back(value);
            for (const JSValue &value : args)  out.push_back(value);
        }
    }

    Completion JSBoundFunction::call(Evaluator &evaluator, const JSValue&, const std::vector<JSValue> &args) {
        MarkedVector all(evaluator.heap);
        combine(all, bound_args, args);

        return target->call(evaluator, bound_this, all);
    }

    Completion JSBoundFunction::construct(Evaluator &evaluator, const std::vector<JSValue> &args) {
        MarkedVector all(evaluator.heap);
        combine(all, bound_args, args);

        return target->construct(evaluator, all);
    }

    Completion JSBoundFunction::has_instance(Evaluator &evaluator, const JSValue &value) {
        return target->has_instance(evaluator, value);
    }
}