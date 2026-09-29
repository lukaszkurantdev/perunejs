#include "runtime/builtins.h"

#include <cmath>
#include <random>

namespace perunejs {
    namespace {

        class JSMath : public JSObject {
        public:
            const char *class_name() override { return "Math"; }
        };

        Completion to_double(Evaluator &evaluator, const JSValue &value, double &out) {
            Completion number = evaluator.to_number(value);
            if (number.is_abrupt()) return number;

            out = number.get_value_or_undefined().to_number();
            return Completion::empty();
        }

        template <double (*Operation)(double)>
        Completion unary(Evaluator &evaluator, const JSValue&, const std::vector<JSValue> &args) {
            double value = 0;
            if (Completion c = to_double(evaluator, argument_at(args, 0), value); c.is_abrupt()) return c;

            return Completion::normal(JSValue::number(Operation(value)));
        }

        Completion math_round(Evaluator &evaluator, const JSValue&, const std::vector<JSValue> &args) {
            double value = 0;
            if (Completion c = to_double(evaluator, argument_at(args, 0), value); c.is_abrupt()) return c;

            if (std::isnan(value) || std::isinf(value) || value == 0) {
                return Completion::normal(JSValue::number(value));
            }

            if (std::abs(value) >= 4503599627370496.0) {
                return Completion::normal(JSValue::number(value));
            }

            const double lower = std::floor(value);

            const double result = (value - lower >= 0.5) ? lower + 1 : lower;

            if (result == 0 && value < 0) {
                return Completion::normal(JSValue::number(-0.0));
            }

            return Completion::normal(JSValue::number(result));
        }

        Completion math_pow(Evaluator &evaluator, const JSValue&, const std::vector<JSValue> &args) {
            double base = 0;
            if (Completion c = to_double(evaluator, argument_at(args, 0), base); c.is_abrupt()) return c;

            double exponent = 0;
            if (Completion c = to_double(evaluator, argument_at(args, 1), exponent); c.is_abrupt()) return c;

            if (std::abs(base) == 1 && std::isinf(exponent)) {
                return Completion::normal(JSValue::number(std::nan("")));
            }

            return Completion::normal(JSValue::number(std::pow(base, exponent)));
        }


        template <bool Largest>
        Completion extremum(Evaluator &evaluator, const JSValue&, const std::vector<JSValue> &args) {
            double result = Largest ? -std::numeric_limits<double>::infinity()
                                    :  std::numeric_limits<double>::infinity();
            bool saw_nan = false;

            for (const JSValue &argument : args) {
                double value = 0;
                if (Completion c = to_double(evaluator, argument, value); c.is_abrupt()) return c;

                if (std::isnan(value)) { saw_nan = true; continue; }
                if (saw_nan) continue;

                const bool replace = Largest
                    ? (value > result || (value == 0 && result == 0 && !std::signbit(value)))
                    : (value < result || (value == 0 && result == 0 &&  std::signbit(value)));

                if (replace) result = value;
            }

            if (saw_nan) return Completion::normal(JSValue::number(std::nan("")));

            return Completion::normal(JSValue::number(result));
        }

        Completion math_random(Evaluator&, const JSValue&, const std::vector<JSValue>&) {
            static std::mt19937_64 generator(std::random_device{}());
            static std::uniform_real_distribution<double> distribution(0.0, 1.0);

            return Completion::normal(JSValue::number(distribution(generator)));
        }

        Completion math_atan2(Evaluator &evaluator, const JSValue&, const std::vector<JSValue> &args) {
            double y = 0;
            if (Completion c = to_double(evaluator, argument_at(args, 0), y); c.is_abrupt()) return c;

            double x = 0;
            if (Completion c = to_double(evaluator, argument_at(args, 1), x); c.is_abrupt()) return c;

            return Completion::normal(JSValue::number(std::atan2(y, x)));
        }
    }

    void install_math(const Builtins &b) {
        auto *math = b.heap.allocate<JSMath>();
        math->prototype = b.evaluator.object_prototype;

        b.global_function("Math", JSValue::object(math));

        b.constant(math, "E",       JSValue::number(M_E));
        b.constant(math, "LN10",    JSValue::number(M_LN10));
        b.constant(math, "LN2",     JSValue::number(M_LN2));
        b.constant(math, "LOG2E",   JSValue::number(M_LOG2E));
        b.constant(math, "LOG10E",  JSValue::number(M_LOG10E));
        b.constant(math, "PI",      JSValue::number(M_PI));
        b.constant(math, "SQRT1_2", JSValue::number(M_SQRT1_2));
        b.constant(math, "SQRT2",   JSValue::number(M_SQRT2));

        b.method(math, "abs",    unary<std::fabs>, 1);
        b.method(math, "acos",   unary<std::acos>, 1);
        b.method(math, "asin",   unary<std::asin>, 1);
        b.method(math, "atan",   unary<std::atan>, 1);
        b.method(math, "ceil",   unary<std::ceil>, 1);
        b.method(math, "cos",    unary<std::cos>, 1);
        b.method(math, "exp",    unary<std::exp>, 1);
        b.method(math, "floor",  unary<std::floor>, 1);
        b.method(math, "log",    unary<std::log>, 1);
        b.method(math, "sin",    unary<std::sin>, 1);
        b.method(math, "sqrt",   unary<std::sqrt>, 1);
        b.method(math, "tan",    unary<std::tan>, 1);

        b.method(math, "atan2",  math_atan2, 2);
        b.method(math, "pow",    math_pow, 2);
        b.method(math, "round",  math_round, 1);
        b.method(math, "random", math_random, 0);

        b.method(math, "max",    extremum<true>, 2);
        b.method(math, "min",    extremum<false>, 2);
    }
}