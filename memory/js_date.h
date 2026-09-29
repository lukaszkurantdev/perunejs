#ifndef PERUNEJS_JS_DATE_H
#define PERUNEJS_JS_DATE_H

#include "js_object.h"
#include "js_value.h"

namespace perunejs {
    class JSDate : public JSObject {
    public:
        double primitive_value;

        explicit JSDate(const double primitive_value) : primitive_value(primitive_value) {}

        const char *class_name() override { return "Date"; }

        Completion default_value(Evaluator &evaluator, const Hint hint) override {
            return JSObject::default_value(evaluator, hint == Hint::Default ? Hint::String : hint);
        }
    };
}

#endif //PERUNEJS_JS_DATE_H