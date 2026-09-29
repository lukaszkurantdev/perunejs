#ifndef PERUNEJS_JS_OBJECT_H
#define PERUNEJS_JS_OBJECT_H
#include <cstdint>
#include <map>

#include "cell.h"
#include "js_value.h"
#include "completion.h"

namespace perunejs {
    class JSObject;
    class Evaluator;

    enum class Hint : uint8_t { Default, Number, String };

    struct PropertyDescriptor {
        std::optional<JSValue> value;
        std::optional<bool> writable;
        std::optional<JSObject*> get;
        std::optional<JSObject*> set;
        std::optional<bool> enumerable;
        std::optional<bool> configurable;

        bool is_accessor() const { return get.has_value() || set.has_value(); }
        bool is_data()     const { return value.has_value() || writable.has_value(); }
        bool is_generic()  const { return !is_accessor() && !is_data(); }

        static PropertyDescriptor data(JSValue value, bool writable,
                                       bool enumerable, bool configurable) {
            PropertyDescriptor descriptor;
            descriptor.value = std::move(value);
            descriptor.writable = writable;
            descriptor.enumerable = enumerable;
            descriptor.configurable = configurable;
            return descriptor;
        }

        static PropertyDescriptor accessor(JSObject* getter, JSObject* setter, bool enumerable, bool configurable) {
            PropertyDescriptor descriptor;
            descriptor.get = getter;
            descriptor.set = setter;
            descriptor.enumerable = enumerable;
            descriptor.configurable = configurable;
            return descriptor;
        }

        static PropertyDescriptor only_value(JSValue v) {
            PropertyDescriptor descriptor;
            descriptor.value = std::move(v);
            return descriptor;
        }

        void complete() {
            if (is_accessor()) {
                if (!get.has_value()) get = nullptr;
                if (!set.has_value()) set = nullptr;
            } else {
                if (!value.has_value())    value    = JSValue::undefined();
                if (!writable.has_value()) writable = false;
            }
            if (!enumerable.has_value())   enumerable   = false;
            if (!configurable.has_value()) configurable = false;
        }

        JSValue get_value() const { return value.value_or(JSValue::undefined()); }
        bool is_writable() const { return writable.value_or(false); }
        bool is_enumerable() const { return enumerable.value_or(false); }
        bool is_configurable() const { return configurable.value_or(false); }
        JSObject* getter() const { return get.value_or(nullptr); }
        JSObject* setter()  const { return set.value_or(nullptr); }

        bool is_empty() const {
            return !value.has_value()      && !writable.has_value()
                && !get.has_value()        && !set.has_value()
                && !enumerable.has_value() && !configurable.has_value();
        }

        void trace(CellVisitor &visitor) const;
    };

    inline bool is_array_index(const std::string &name, uint32_t &index) {
        if (name.empty() || name.size() > 10) return false;
        if (name[0] == '0' && name.size() > 1) return false;

        uint64_t value = 0;
        for (const char c : name) {
            if (c < '0' || c > '9') return false;
            value = value * 10 + static_cast<uint64_t>(c - '0');
        }

        if (value >= 4294967295u) return false;

        index = static_cast<uint32_t>(value);
        return true;
    }

    class JSObject : public Cell {
    protected:
        std::unordered_map<std::string, PropertyDescriptor> properties;
        std::vector<std::string> insertion_order;

    public:
        JSObject() = default;
        JSObject *prototype = nullptr; // TODO: tutaj jak będę podpinał prototypy
        bool extensible = true;

        virtual const PropertyDescriptor *get_own_property(const std::string &name) const {
            auto property = properties.find(name);

            if (property == properties.end()) {
                return nullptr;
            }

            return &property->second;
        }

        static bool describes_same(const PropertyDescriptor &current,
                              const PropertyDescriptor &desc) {
            if (desc.value.has_value()) {
                if (!current.is_data()) return false;
                if (!JSValue::same_value(*desc.value, current.get_value())) return false;
            }
            if (desc.writable.has_value()) {
                if (!current.is_data()) return false;
                if (*desc.writable != current.is_writable()) return false;
            }
            if (desc.get.has_value()) {
                if (!current.is_accessor()) return false;
                if (*desc.get != current.getter()) return false;
            }
            if (desc.set.has_value()) {
                if (!current.is_accessor()) return false;
                if (*desc.set != current.setter()) return false;
            }
            if (desc.enumerable.has_value() &&
                *desc.enumerable != current.is_enumerable()) return false;

            if (desc.configurable.has_value() &&
                *desc.configurable != current.is_configurable()) return false;

            return true;
        }

        virtual Completion coerce_defined_value(Evaluator &evaluator, const std::string &name,
                                                PropertyDescriptor &desc);

        virtual bool rejects_with_range_error(const std::string &name,
                                              const PropertyDescriptor &desc) const {
            (void) name;
            (void) desc;
            return false;
        }

        virtual bool define_own_property(const std::string &name,
                                         const PropertyDescriptor &desc) {
            const PropertyDescriptor *found = get_own_property(name);

            if (found == nullptr) {
                if (!extensible) return false;

                PropertyDescriptor created = desc;
                created.complete();
                store_property(name, created);
                return true;
            }

            PropertyDescriptor current = *found;
            if (desc.is_empty()) return true;

            if (describes_same(current, desc)) return true;

            if (!current.is_configurable()) {
                if (desc.configurable.has_value() && *desc.configurable) return false;

                if (desc.enumerable.has_value() &&
                    *desc.enumerable != current.is_enumerable()) return false;
            }

            if (desc.is_generic()) {
// ?
            } else if (current.is_data() != desc.is_data()) {
                if (!current.is_configurable()) return false;

                PropertyDescriptor converted;
                converted.enumerable   = current.enumerable;
                converted.configurable = current.configurable;

                if (current.is_data()) {
                    converted.get = nullptr;
                    converted.set = nullptr;
                } else {
                    converted.value    = JSValue::undefined();
                    converted.writable = false;
                }
                current = converted;

            } else if (current.is_data()) {
                if (!current.is_configurable() && !current.is_writable()) {
                    if (desc.writable.has_value() && *desc.writable) return false;

                    if (desc.value.has_value() &&
                        !JSValue::same_value(*desc.value, current.get_value())) return false;
                }

            } else {
                if (!current.is_configurable()) {
                    if (desc.set.has_value() && *desc.set != current.setter()) return false;
                    if (desc.get.has_value() && *desc.get != current.getter()) return false;
                }
            }

            if (desc.value.has_value())        current.value        = desc.value;
            if (desc.writable.has_value())     current.writable     = desc.writable;
            if (desc.get.has_value())          current.get          = desc.get;
            if (desc.set.has_value())          current.set          = desc.set;
            if (desc.enumerable.has_value())   current.enumerable   = desc.enumerable;
            if (desc.configurable.has_value()) current.configurable = desc.configurable;

            store_property(name, current);
            return true;
        }

        virtual bool delete_own_property(const std::string &name) {
            auto property = properties.find(name);

            if (property == properties.end()) {
                return true;
            }

            if (!property->second.is_configurable()) {
                return false;
            }

            properties.erase(property);
            insertion_order.erase(
        std::remove(insertion_order.begin(), insertion_order.end(), name),
        insertion_order.end());

            return true;
        }

        virtual bool is_callable() const { return false; }

        virtual Completion call(Evaluator& evaluator, const JSValue& this_value,
                                    const std::vector<JSValue>& args);

        virtual Completion construct(Evaluator& evaluator, const std::vector<JSValue>& args);

        virtual Completion put(Evaluator& evaluator, const std::string& name,
                               const JSValue& value, bool is_throw);

        virtual Completion default_value(Evaluator& evaluator, Hint hint);

        virtual Completion has_instance(Evaluator& evaluator, const JSValue& value);

        virtual const char* class_name();

        void trace(CellVisitor &visitor) const override {
            if (prototype != nullptr) visitor.visit(prototype);

            for (const auto &property : properties) {
                property.second.trace(visitor);
            }
        }

        virtual std::vector<std::string> own_keys() const {
            std::vector<std::pair<uint32_t, const std::string*>> indices;
            std::vector<const std::string*> rest;

            for (const std::string &name : insertion_order) {
                uint32_t index = 0;
                if (is_array_index(name, index)) {
                    indices.emplace_back(index, &name);
                } else {
                    rest.push_back(&name);
                }
            }
            std::sort(indices.begin(), indices.end(),
                      [](const auto &a, const auto &b) { return a.first < b.first; });

            std::vector<std::string> keys;
            keys.reserve(indices.size() + rest.size());

            for (const auto &entry : indices) {
                keys.push_back(*entry.second);
            }
            for (const std::string *name : rest) {
                keys.push_back(*name);
            }

            return keys;
        }

        virtual bool intercepts_lookup() const { return false; }

        const PropertyDescriptor* get_property(const std::string &name) const {
            for (const JSObject *object = this; object != nullptr; object = object->prototype) {
                if (object != this && object->intercepts_lookup()) continue;

                if (const PropertyDescriptor *descriptor = object->get_own_property(name)) {
                    return descriptor;
                }
            }
            return nullptr;
        }

        bool has_property(const std::string &name) const {
            return get_property(name) != nullptr;
        }

        bool can_put(const std::string &name) const {
            const PropertyDescriptor *descriptor = get_own_property(name);
            if (descriptor != nullptr) {
                if (descriptor->is_accessor()) {
                    return descriptor->setter() != nullptr;
                }

                return descriptor->is_writable();
            }

            if (prototype == nullptr) {
                return extensible;
            }

            const PropertyDescriptor *inherited = get_property(name);
            if (inherited == nullptr) {
                return extensible;
            }

            if (inherited->is_accessor()) {
                return inherited->setter() != nullptr;
            }

            if (!extensible) return false;

            return inherited->is_writable();
        }

        virtual Completion get(Evaluator& evaluator,const std::string &name) {
            const PropertyDescriptor *descriptor = nullptr;

            for (JSObject *object = this; object != nullptr; object = object->prototype) {
                if (object != this && object->intercepts_lookup()) {
                    Completion intercepted = object->get(evaluator, name);

                    if (intercepted.is_abrupt()
                        || intercepted.get_value_or_undefined().type() != JSValueType::Undefined) {
                        return intercepted;
                    }

                    continue;
                }

                descriptor = object->get_own_property(name);
                if (descriptor != nullptr) break;
            }

            if (descriptor == nullptr) {
                return Completion::normal(JSValue::undefined());
            }

            if (descriptor->is_data()) {
                return Completion::normal(descriptor->get_value());
            }

            JSObject *getter = descriptor->getter();

            if (getter == nullptr) {
                return Completion::normal(JSValue::undefined());
            }

            return getter->call(evaluator, JSValue::object(this), {});
        }

    protected:
        void store_property(const std::string &name, const PropertyDescriptor &descriptor) {
            const auto [entry, inserted] = properties.insert_or_assign(name, descriptor);
            if (inserted) insertion_order.push_back(name);
        }
    };

    inline void PropertyDescriptor::trace(CellVisitor &visitor) const {
        if (value.has_value()) {
            if (JSObject *object = value->as_object()) visitor.visit(object);
        }
        if (get.has_value() && *get != nullptr) visitor.visit(*get);
        if (set.has_value() && *set != nullptr) visitor.visit(*set);
    }

    inline const char* JSObject::class_name() {
        return "Object";
    }
}

#endif //PERUNEJS_JS_OBJECT_H
