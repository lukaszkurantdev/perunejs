#include <cmath>

#include "globals.h"

#include "builtins.h"
#include "memory/js_native_function.h"
#include "memory/js_regexp.h"
#include "memory/js_string.h"
#include "memory/js_wrapper.h"
#include "memory/js_array.h"
#include "memory/js_date.h"

namespace perunejs {
    namespace {
        Completion function_prototype_call(Evaluator&, const JSValue&, const std::vector<JSValue>&) {
            return Completion::normal(JSValue::undefined());
        }

        Completion throw_type_error(Evaluator &evaluator, const JSValue&, const std::vector<JSValue>&) {
            return evaluator.throw_error(evaluator.type_error_prototype,
                "'caller', 'callee' and 'arguments' are restricted in strict mode");
        }

        JSObject *plain_prototype(Heap &heap, JSObject *parent) {
            auto *prototype = heap.allocate<JSObject>();
            prototype->prototype = parent;

            return prototype;
        }
    }

    Environment *setup_globals(Heap &heap, Evaluator &evaluator) {
        DeferGC defer(heap);

        auto *object_prototype = heap.allocate<JSObject>();
        evaluator.object_prototype = object_prototype;

        auto *global_object = heap.allocate<JSObject>();
        global_object->prototype = object_prototype;
        evaluator.global_object = global_object;

        auto *global_env = heap.allocate<ObjectEnvironment>(global_object, nullptr, false);
        evaluator.global_env = global_env;

        auto *function_prototype = heap.allocate<JSNativeFunction>("", function_prototype_call);
        function_prototype->prototype = object_prototype;
        function_prototype->define_own_property("length",
            PropertyDescriptor::data(JSValue::number(0), false, false, false));

        evaluator.function_prototype = function_prototype;

        auto *thrower = heap.allocate<JSNativeFunction>("ThrowTypeError", throw_type_error);
        thrower->prototype = function_prototype;
        thrower->define_own_property("length",
            PropertyDescriptor::data(JSValue::number(0), false, false, false));
        thrower->extensible = false;
        evaluator.type_error_thrower = thrower;

        auto *array_prototype = heap.allocate<JSArray>();
        array_prototype->prototype = object_prototype;
        array_prototype->define_own_property("length",
            PropertyDescriptor::data(JSValue::number(0), true, false, false));
        evaluator.array_prototype = array_prototype;

        auto *string_prototype = heap.allocate<JSString>(std::u16string());
        string_prototype->prototype = object_prototype;
        evaluator.string_prototype = string_prototype;

        auto *number_prototype = heap.allocate<JSPrimitiveWrapper>(JSValue::number(0), "Number");
        number_prototype->prototype = object_prototype;
        evaluator.number_prototype = number_prototype;

        auto *boolean_prototype = heap.allocate<JSPrimitiveWrapper>(JSValue::boolean(false), "Boolean");
        boolean_prototype->prototype = object_prototype;
        evaluator.boolean_prototype = boolean_prototype;

        auto *date_prototype = heap.allocate<JSDate>(std::nan(""));
        date_prototype->prototype = object_prototype;
        evaluator.date_prototype = date_prototype;

        auto *regexp_prototype = heap.allocate<JSRegExp>("(?:)", "");
        regexp_prototype->prototype = object_prototype;
        evaluator.regexp_prototype = regexp_prototype;

        evaluator.promise_prototype  = plain_prototype(heap, object_prototype);
        evaluator.symbol_prototype   = plain_prototype(heap, object_prototype);

        evaluator.array_buffer_prototype = plain_prototype(heap, object_prototype);
        for (JSObject *&typed : evaluator.typed_array_prototypes) {
            typed = plain_prototype(heap, object_prototype);
        }
        evaluator.map_prototype      = plain_prototype(heap, object_prototype);
        evaluator.set_prototype      = plain_prototype(heap, object_prototype);
        evaluator.weak_map_prototype = plain_prototype(heap, object_prototype);
        evaluator.weak_set_prototype = plain_prototype(heap, object_prototype);

        evaluator.error_prototype           = plain_prototype(heap, object_prototype);
        evaluator.type_error_prototype      = plain_prototype(heap, evaluator.error_prototype);
        evaluator.reference_error_prototype = plain_prototype(heap, evaluator.error_prototype);
        evaluator.range_error_prototype     = plain_prototype(heap, evaluator.error_prototype);
        evaluator.syntax_error_prototype    = plain_prototype(heap, evaluator.error_prototype);
        evaluator.uri_error_prototype       = plain_prototype(heap, evaluator.error_prototype);
        evaluator.eval_error_prototype      = plain_prototype(heap, evaluator.error_prototype);

        const Builtins builtins{heap, evaluator, global_object};

        install_function(builtins);
        install_object(builtins);
        install_error(builtins);
        install_regexp(builtins);
        install_array(builtins);
        install_global_functions(builtins);
        install_string(builtins);
        install_number(builtins);
        install_boolean(builtins);
        install_math(builtins);
        install_uri(builtins);
        install_date(builtins);
        install_json(builtins);
        install_collections(builtins);
        install_es2015(builtins);
        install_symbol(builtins);
        install_promise(builtins);
        install_array_buffer(builtins);

        // globalThis (ES2020) — poza ES5.1, ale bundle React Native sięga po
        // nie w pierwszej instrukcji, jeszcze przed jakimkolwiek polyfillem.
        builtins.data_property(global_object, "globalThis", JSValue::object(global_object));

        return global_env;
    }
}