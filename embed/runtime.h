
#ifndef PERUNEJS_EMBED_RUNTIME_H
#define PERUNEJS_EMBED_RUNTIME_H

#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "embed/error.h"
#include "embed/host_function.h"
#include "embed/host_object.h"
#include "evaluator/evaluator.h"
#include "memory/persistent.h"

namespace perunejs::embed {
    class Runtime {
        Heap owned_heap;
        std::ostream &output;
        Evaluator owned_evaluator;
        Environment *global_environment = nullptr;

        JSValue unwrap(const Completion &completion);

        JSObject *require_object(const JSValue &value, const char *what);

    public:
        explicit Runtime(std::ostream &output = std::cout);

        Runtime(const Runtime &) = delete;
        Runtime &operator=(const Runtime &) = delete;

        Heap &heap() { return owned_heap; }
        Evaluator &evaluator() { return owned_evaluator; }
        Environment *environment() const { return global_environment; }

        JSValue evaluate(const std::string &source);

        JSValue global();

        JSValue get_property(const JSValue &object, const std::string &name);
        void set_property(const JSValue &object, const std::string &name, const JSValue &value);
        bool has_property(const JSValue &object, const std::string &name);
        std::vector<std::string> property_names(const JSValue &object);


        JSValue create_object();
        JSValue create_object(std::shared_ptr<HostObject> host);
        JSValue create_array(std::size_t length);
        JSValue create_function(const std::string &name, unsigned arity, HostFunctionCallback callback);
        JSValue create_string(const std::string &text);
        JSValue create_array_buffer(std::size_t length);

        // Surowe bajty bufora!! odpowiednik jsi::Runtime::data.
        uint8_t *array_buffer_data(const JSValue &value);
        std::size_t array_buffer_size(const JSValue &value);

        JSValue call(const JSValue &function, const JSValue &this_value,
                     const std::vector<JSValue> &args = {});
        JSValue construct(const JSValue &constructor, const std::vector<JSValue> &args = {});

        bool is_callable(const JSValue &value);
        bool is_array(const JSValue &value);
        bool is_host_object(const JSValue &value);
        std::shared_ptr<HostObject> host_object(const JSValue &value);
        bool strict_equals(const JSValue &left, const JSValue &right);
        bool instance_of(const JSValue &value, const JSValue &constructor);

        std::string to_string(const JSValue &value);
        double to_number(const JSValue &value);

        PersistentValue persist(const JSValue &value);
        WeakHandle weak(const JSValue &value);

        void drain_microtasks();
    };
}

#endif //PERUNEJS_EMBED_RUNTIME_H
