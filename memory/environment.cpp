#include "environment.h"

#include <stdexcept>

#include "evaluator/evaluator.h"
#include "js_object.h"

namespace perunejs {
    Environment *Environment::find_environment(const std::string &name) {
        for (Environment *env = this; env != nullptr; env = env->outer) {
            if (env->has_binding(name)) return env;
        }

        return nullptr;
    }

    void Environment::trace(CellVisitor &visitor) const {
        visitor.visit(outer);
    }

    bool DeclarativeEnvironment::has_binding(const std::string &name) const {
        return bindings.contains(name);
    }

    Completion DeclarativeEnvironment::create_mutable_binding(Evaluator &, const std::string &name, bool is_deletable) {
        Binding binding(JSValue::undefined());
        binding.is_mutable = true;
        binding.is_deletable = is_deletable;
        binding.is_initialized = true;

        bindings.insert(std::make_pair(name, binding));

        return Completion::empty();
    }

    Completion DeclarativeEnvironment::set_binding(Evaluator &evaluator, const std::string &name,
                                                   const JSValue &value, bool is_strict) {
        auto binding = bindings.find(name);

        if (binding == bindings.end()) {
            return evaluator.throw_error(evaluator.reference_error_prototype, name + " is not defined");
        }

        if (!binding->second.is_mutable) {
            if (is_strict) {
                return evaluator.throw_error(evaluator.type_error_prototype,
                                             "Assignment to constant variable");
            }
            return Completion::empty();
        }

        binding->second.value = value;
        return Completion::empty();
    }

    Completion DeclarativeEnvironment::get_binding_value(Evaluator &evaluator, const std::string &name,
                                                         bool is_strict) {
        auto binding = bindings.find(name);

        if (binding == bindings.end()) {
            return evaluator.throw_error(evaluator.reference_error_prototype, name + " is not defined");
        }
        if (!binding->second.is_mutable && !binding->second.is_initialized) {
            if (is_strict) {
                return evaluator.throw_error(evaluator.reference_error_prototype,name + " is not initialized");
            }
            return Completion::normal(JSValue::undefined());
        }

        return Completion::normal(binding->second.value);
    }

    bool DeclarativeEnvironment::delete_binding(const std::string &name) {
        auto binding = bindings.find(name);

        if (binding == bindings.end()) return true;
        if (!binding->second.is_deletable) return false;

        bindings.erase(binding);
        return true;
    }

    void DeclarativeEnvironment::create_immutable_binding(const std::string &name) {
        Binding binding(JSValue::undefined());
        binding.is_mutable = false;
        binding.is_deletable = false;
        binding.is_initialized = false;

        bindings.insert(std::make_pair(name, binding));
    }

    JSValue DeclarativeEnvironment::raw_binding_value(const std::string &name) const {
        const auto binding = bindings.find(name);
        return binding == bindings.end() ? JSValue::undefined() : binding->second.value;
    }

    void DeclarativeEnvironment::raw_set_binding(const std::string &name, const JSValue &value) {
        const auto binding = bindings.find(name);
        if (binding != bindings.end() && binding->second.is_mutable) {
            binding->second.value = value;
        }
    }

    void DeclarativeEnvironment::initialize_immutable_binding(const std::string &name,
                                                              const JSValue &value) {
        auto binding = bindings.find(name);
        if (binding == bindings.end()) {
            throw std::logic_error("initialize_immutable_binding: brak bindingu " + name);
        }
        if (binding->second.is_mutable || binding->second.is_initialized) {
            throw std::logic_error("initialize_immutable_binding: binding już zainicjaplizowany " + name);
        }

        binding->second.value = value;
        binding->second.is_initialized = true;
    }

    void DeclarativeEnvironment::trace(CellVisitor &visitor) const {
        Environment::trace(visitor);

        for (const auto &binding : bindings) {
            if (JSObject *object = binding.second.value.as_object()) visitor.visit(object);
        }
    }

    bool ObjectEnvironment::has_binding(const std::string &name) const {
        return binding_object->has_property(name);
    }

    Completion ObjectEnvironment::create_mutable_binding(Evaluator &evaluator, const std::string &name, bool is_deletable) {
        const bool defined = binding_object->define_own_property(
            name, PropertyDescriptor::data(JSValue::undefined(), true, true, is_deletable));

        if (!defined) {
            return evaluator.throw_error(evaluator.type_error_prototype,"Cannot create property '" + name + "'");
        }

        return Completion::empty();
    }

    Completion ObjectEnvironment::set_binding(Evaluator &evaluator, const std::string &name,
                                              const JSValue &value, bool is_strict) {
        return binding_object->put(evaluator, name, value, is_strict);
    }

    Completion ObjectEnvironment::get_binding_value(Evaluator &evaluator, const std::string &name, bool is_strict) {
        if (!binding_object->has_property(name)) {
            if (!is_strict) return Completion::normal(JSValue::undefined());

            return evaluator.throw_error(evaluator.reference_error_prototype, name + " is not defined");
        }

        return binding_object->get(evaluator, name);
    }

    bool ObjectEnvironment::delete_binding(const std::string &name) {
        return binding_object->delete_own_property(name);
    }

    JSValue ObjectEnvironment::implicit_this_value() const {
        return provide_this ? JSValue::object(binding_object) : JSValue::undefined();
    }

    void ObjectEnvironment::trace(CellVisitor &visitor) const {
        Environment::trace(visitor);
        visitor.visit(binding_object);
    }
}
