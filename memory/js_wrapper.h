#ifndef PERUNEJS_JS_WRAPPER_H
#define PERUNEJS_JS_WRAPPER_H

#include "js_object.h"
#include "js_value.h"

namespace perunejs {
    class JSPrimitiveWrapper : public JSObject {
    public:
        JSValue primitive;
        const char *label;

        JSPrimitiveWrapper(JSValue primitive, const char *label)
            : primitive(std::move(primitive)), label(label) {}

        const char *class_name() override { return label; }
    };
}

#endif //PERUNEJS_JS_WRAPPER_H

