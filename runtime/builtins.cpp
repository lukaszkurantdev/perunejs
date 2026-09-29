#include "builtins.h"
#include "memory/js_array.h"

namespace perunejs {
    void Builtins::data_property(JSObject *target, const std::string &name, const JSValue &value) const {
        target->define_own_property(name, PropertyDescriptor::data(value, true, false, true));
    }

    void Builtins::constant(JSObject *target, const std::string &name, const JSValue &value) const {
        target->define_own_property(name, PropertyDescriptor::data(value, false, false, false));
    }

    void Builtins::global_function(const char *name, const JSValue &value) const {
        data_property(global_object, name, value);
    }

    void Builtins::global_constant(const char *name, const JSValue &value) const {
        constant(global_object, name, value);
    }

    JSObject *Builtins::constructor(const char *name, NativeFunction fn, JSObject *prototype,
                                    NativeFunction construct_fn, int length) const {
        JSObject *created = function(name, fn, length, /*constructible*/ true);
        static_cast<JSNativeFunction*>(created)->construct_function = construct_fn;

        created->define_own_property("prototype",
            PropertyDescriptor::data(JSValue::object(prototype), false, false, false));
        data_property(prototype, "constructor", JSValue::object(created));

        global_function(name, JSValue::object(created));
        return created;
    }

    JSObject *make_string_array(Evaluator &evaluator, const std::vector<std::string> &values) {
        auto *array = evaluator.heap.allocate<JSArray>();
        array->prototype = evaluator.array_prototype;

        array->define_own_property("length",
            PropertyDescriptor::data(JSValue::number(0), true, false, false));

        for (std::size_t i = 0; i < values.size(); ++i) {
            array->define_own_property(std::to_string(i),
                PropertyDescriptor::data(JSValue::string(values[i]), true, true, true));
        }

        return array;
    }

    JSObject *Builtins::function(const char *name, NativeFunction fn, int length,
                                bool constructible) const {
        auto *created = heap.allocate<JSNativeFunction>(name, fn, constructible);
        created->prototype = evaluator.function_prototype;

        created->define_own_property("length",
            PropertyDescriptor::data(JSValue::number(length), false, false, false));

        return created;
    }

    JSObject *Builtins::method(JSObject *target, const char *name, NativeFunction fn, int length) const {
        JSObject *created = function(name, fn, length);
        data_property(target, name, JSValue::object(created));

        return created;
    }
}