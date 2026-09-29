

#include "embed/runtime.h"

#include "memory/js_array.h"
#include "memory/js_array_buffer.h"
#include "runtime/globals.h"

namespace perunejs::embed {
    Runtime::Runtime(std::ostream &output)
        : output(output), owned_evaluator(owned_heap, output) {
        global_environment = setup_globals(owned_heap, owned_evaluator);
    }

    JSValue Runtime::unwrap(const Completion &completion) {
        if (completion.type != COMPLETION_TYPE::THROW) {
            return completion.get_value_or_undefined();
        }

        const JSValue thrown = completion.get_value_or_undefined();

        std::string description = "Uncaught JS exception";
        if (const Completion text = owned_evaluator.to_string(thrown);
            text.type == COMPLETION_TYPE::NORMAL) {
            description = text.get_value_or_undefined().to_string();
        }

        if (thrown.type() == JSValueType::Object) {
            if (const Completion stack = thrown.as_object()->get(owned_evaluator, "stack");
                stack.type == COMPLETION_TYPE::NORMAL
                && stack.get_value_or_undefined().type() == JSValueType::String) {
                description = stack.get_value_or_undefined().to_string();
            }
        }

        throw JSError(owned_heap, thrown, description);
    }

    JSObject *Runtime::require_object(const JSValue &value, const char *what) {
        if (value.type() != JSValueType::Object) {
            throw JSError(std::string("Expected an object: ") + what);
        }

        return value.as_object();
    }

    JSValue Runtime::evaluate(const std::string &source) {
        return unwrap(owned_evaluator.run_script(global_environment, source));
    }

    JSValue Runtime::global() { return JSValue::object(owned_evaluator.global_object); }

    JSValue Runtime::get_property(const JSValue &object, const std::string &name) {
        return unwrap(require_object(object, name.c_str())->get(owned_evaluator, name));
    }

    void Runtime::set_property(const JSValue &object, const std::string &name, const JSValue &value) {
        unwrap(require_object(object, name.c_str())->put(owned_evaluator, name, value, true));
    }

    bool Runtime::has_property(const JSValue &object, const std::string &name) {
        return require_object(object, name.c_str())->has_property(name);
    }

    std::vector<std::string> Runtime::property_names(const JSValue &object) {
        return require_object(object, "property_names")->own_keys();
    }

    JSValue Runtime::create_object() {
        auto *created = owned_heap.allocate<JSObject>();
        created->prototype = owned_evaluator.object_prototype;

        return JSValue::object(created);
    }

    JSValue Runtime::create_object(std::shared_ptr<HostObject> host) {
        auto *created = owned_heap.allocate<JSHostObject>(*this, std::move(host));
        created->prototype = owned_evaluator.object_prototype;

        return JSValue::object(created);
    }

    JSValue Runtime::create_array(const std::size_t length) {
        auto *created = owned_heap.allocate<JSArray>();
        created->prototype = owned_evaluator.array_prototype;

        created->define_own_property("length",
            PropertyDescriptor::data(JSValue::number(static_cast<double>(length)), true, false, false));

        return JSValue::object(created);
    }

    JSValue Runtime::create_function(const std::string &name, const unsigned arity,
                                     HostFunctionCallback callback) {
        auto *created = owned_heap.allocate<JSHostFunction>(*this, name, std::move(callback));
        created->prototype = owned_evaluator.function_prototype;

        created->define_own_property("length",
            PropertyDescriptor::data(JSValue::number(arity), false, false, true));
        created->define_own_property("name",
            PropertyDescriptor::data(JSValue::string(name), false, false, true));

        return JSValue::object(created);
    }

    JSValue Runtime::create_string(const std::string &text) { return JSValue::string(text); }

    JSValue Runtime::create_array_buffer(const std::size_t length) {
        auto *created = owned_heap.allocate<JSArrayBuffer>(length);
        created->prototype = owned_evaluator.array_buffer_prototype;

        return JSValue::object(created);
    }

    uint8_t *Runtime::array_buffer_data(const JSValue &value) {
        if (value.type() != JSValueType::Object) return nullptr;

        auto *buffer = dynamic_cast<JSArrayBuffer *>(value.as_object());

        return buffer != nullptr ? buffer->data() : nullptr;
    }

    std::size_t Runtime::array_buffer_size(const JSValue &value) {
        if (value.type() != JSValueType::Object) return 0;

        auto *buffer = dynamic_cast<JSArrayBuffer *>(value.as_object());

        return buffer != nullptr ? buffer->byte_length() : 0;
    }

    JSValue Runtime::call(const JSValue &function, const JSValue &this_value,
                          const std::vector<JSValue> &args) {
        JSObject *callee = require_object(function, "call");

        if (!callee->is_callable()) throw JSError("Value is not callable");

        return unwrap(callee->call(owned_evaluator, this_value, args));
    }

    JSValue Runtime::construct(const JSValue &constructor, const std::vector<JSValue> &args) {
        return unwrap(require_object(constructor, "construct")->construct(owned_evaluator, args));
    }

    bool Runtime::is_callable(const JSValue &value) {
        return value.type() == JSValueType::Object && value.as_object()->is_callable();
    }

    bool Runtime::is_array(const JSValue &value) {
        return value.type() == JSValueType::Object
            && dynamic_cast<JSArray *>(value.as_object()) != nullptr;
    }

    bool Runtime::is_host_object(const JSValue &value) {
        return value.type() == JSValueType::Object
            && dynamic_cast<JSHostObject *>(value.as_object()) != nullptr;
    }

    std::shared_ptr<HostObject> Runtime::host_object(const JSValue &value) {
        if (value.type() != JSValueType::Object) return nullptr;

        auto *wrapper = dynamic_cast<JSHostObject *>(value.as_object());

        return wrapper != nullptr ? wrapper->target() : nullptr;
    }

    bool Runtime::strict_equals(const JSValue &left, const JSValue &right) {
        return JSValue::is_strictly_equal(left, right);
    }

    bool Runtime::instance_of(const JSValue &value, const JSValue &constructor) {
        const Completion verdict =
            require_object(constructor, "instance_of")->has_instance(owned_evaluator, value);

        return unwrap(verdict).to_boolean();
    }

    std::string Runtime::to_string(const JSValue &value) {
        return unwrap(owned_evaluator.to_string(value)).to_string();
    }

    double Runtime::to_number(const JSValue &value) {
        return unwrap(owned_evaluator.to_number(value)).to_number();
    }

    PersistentValue Runtime::persist(const JSValue &value) { return {owned_heap, value}; }

    WeakHandle Runtime::weak(const JSValue &value) {
        return value.type() == JSValueType::Object
            ? WeakHandle(owned_heap, value.as_object())
            : WeakHandle();
    }

    void Runtime::drain_microtasks() {
        const Completion drained = owned_evaluator.drain_microtasks();
        if (drained.type == COMPLETION_TYPE::THROW) unwrap(drained);
    }
}
