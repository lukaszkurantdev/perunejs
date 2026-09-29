
#ifndef PERUNEJS_BINDING_H
#define PERUNEJS_BINDING_H
#include "js_value.h"


namespace perunejs {
    class Binding {
        public:
            JSValue value;
            bool is_mutable = false;
            bool is_deletable = false;
            bool is_initialized = false;
            Binding() : value(Undefined()) {}
            Binding(JSValue value) : value(value) {}
    };
}

#endif //PERUNEJS_BINDING_H
