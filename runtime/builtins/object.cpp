#include "runtime/builtins.h"

namespace perunejs {
    namespace {
        JSObject *object_argument(Evaluator &evaluator, const std::vector<JSValue> &args,
                                  const char *name, Completion &failure) {
            const JSValue value = argument_at(args, 0);

            if (value.type() != JSValueType::Object) {
                failure = evaluator.throw_error(evaluator.type_error_prototype,
                    std::string("Object.") + name + " called on non-object");
                return nullptr;
            }

            return value.as_object();
        }

        Completion to_property_descriptor(Evaluator &evaluator, const JSValue &value,
                                          PropertyDescriptor &out) {
            if (value.type() != JSValueType::Object) {
                return evaluator.throw_error(evaluator.type_error_prototype,
                    "Property description must be an object");
            }

            JSObject *source = value.as_object();

            const auto read = [&](const char *name, JSValue &slot, bool &present) -> Completion {
                present = source->has_property(name);
                if (!present) return Completion::empty();

                Completion field = source->get(evaluator, name);
                if (field.is_abrupt()) return field;

                slot = field.get_value_or_undefined();
                return Completion::empty();
            };

            JSValue field;
            bool present = false;

            if (Completion c = read("enumerable", field, present); c.is_abrupt()) return c;
            if (present) out.enumerable = field.to_boolean();

            if (Completion c = read("configurable", field, present); c.is_abrupt()) return c;
            if (present) out.configurable = field.to_boolean();

            if (Completion c = read("value", field, present); c.is_abrupt()) return c;
            if (present) out.value = field;

            if (Completion c = read("writable", field, present); c.is_abrupt()) return c;
            if (present) out.writable = field.to_boolean();

            if (Completion c = read("get", field, present); c.is_abrupt()) return c;
            if (present) {
                const bool callable = field.type() == JSValueType::Object && field.as_object()->is_callable();

                if (!callable && field.type() != JSValueType::Undefined) {
                    return evaluator.throw_error(evaluator.type_error_prototype, "Getter must be a function");
                }
                out.get = callable ? field.as_object() : nullptr;
            }

            if (Completion c = read("set", field, present); c.is_abrupt()) return c;
            if (present) {
                const bool callable = field.type() == JSValueType::Object && field.as_object()->is_callable();

                if (!callable && field.type() != JSValueType::Undefined) {
                    return evaluator.throw_error(evaluator.type_error_prototype, "Setter must be a function");
                }
                out.set = callable ? field.as_object() : nullptr;
            }

            if (out.is_accessor() && (out.value.has_value() || out.writable.has_value())) {
                return evaluator.throw_error(evaluator.type_error_prototype,
                    "Invalid property descriptor: cannot both specify accessors and a value or writable");
            }

            return Completion::empty();
        }

        JSValue from_property_descriptor(Evaluator &evaluator, const PropertyDescriptor &descriptor) {
            auto *result = evaluator.heap.allocate<JSObject>();
            result->prototype = evaluator.object_prototype;

            const auto put = [&](const char *name, const JSValue &value) {
                result->define_own_property(name, PropertyDescriptor::data(value, true, true, true));
            };

            if (descriptor.is_accessor()) {
                JSObject *getter = descriptor.get.value_or(nullptr);
                JSObject *setter = descriptor.set.value_or(nullptr);

                put("get", getter != nullptr ? JSValue::object(getter) : JSValue::undefined());
                put("set", setter != nullptr ? JSValue::object(setter) : JSValue::undefined());
            } else {
                put("value", descriptor.value.value_or(JSValue::undefined()));
                put("writable", JSValue::boolean(descriptor.is_writable()));
            }

            put("enumerable", JSValue::boolean(descriptor.is_enumerable()));
            put("configurable", JSValue::boolean(descriptor.is_configurable()));

            return JSValue::object(result);
        }

        Completion native_object(Evaluator &evaluator, const JSValue&, const std::vector<JSValue> &args) {
            const JSValue value = argument_at(args, 0);

            if (value.type() == JSValueType::Undefined || value.type() == JSValueType::Null) {
                auto *object = evaluator.heap.allocate<JSObject>();
                object->prototype = evaluator.object_prototype;

                return Completion::normal(JSValue::object(object));
            }

            return evaluator.to_object(value);
        }

        Completion object_get_prototype_of(Evaluator &evaluator, const JSValue&,
                                           const std::vector<JSValue> &args) {
            Completion failure = Completion::empty();
            JSObject *object = object_argument(evaluator, args, "getPrototypeOf", failure);
            if (object == nullptr) return failure;

            return Completion::normal(object->prototype != nullptr
                ? JSValue::object(object->prototype) : JSValue::null());
        }

        Completion object_get_own_property_descriptor(Evaluator &evaluator, const JSValue&,
                                                      const std::vector<JSValue> &args) {
            Completion failure = Completion::empty();
            JSObject *object = object_argument(evaluator, args, "getOwnPropertyDescriptor", failure);
            if (object == nullptr) return failure;

            Completion key = evaluator.to_string(argument_at(args, 1));
            if (key.is_abrupt()) return key;

            const PropertyDescriptor *descriptor =
                object->get_own_property(key.get_value_or_undefined().to_string());

            if (descriptor == nullptr) return Completion::normal(JSValue::undefined());

            return Completion::normal(from_property_descriptor(evaluator, *descriptor));
        }

        Completion object_get_own_property_names(Evaluator &evaluator, const JSValue&,
                                                 const std::vector<JSValue> &args) {
            Completion failure = Completion::empty();
            JSObject *object = object_argument(evaluator, args, "getOwnPropertyNames", failure);
            if (object == nullptr) return failure;

            return Completion::normal(JSValue::object(make_string_array(evaluator, object->own_keys())));
        }

        Completion object_keys(Evaluator &evaluator, const JSValue&, const std::vector<JSValue> &args) {
            Completion failure = Completion::empty();
            JSObject *object = object_argument(evaluator, args, "keys", failure);
            if (object == nullptr) return failure;

            std::vector<std::string> names;
            for (const std::string &name : object->own_keys()) {
                const PropertyDescriptor *descriptor = object->get_own_property(name);
                if (descriptor != nullptr && descriptor->is_enumerable()) names.push_back(name);
            }

            return Completion::normal(JSValue::object(make_string_array(evaluator, names)));
        }

        Completion object_define_property(Evaluator &evaluator, const JSValue&,
                                          const std::vector<JSValue> &args) {
            Completion failure = Completion::empty();
            JSObject *object = object_argument(evaluator, args, "defineProperty", failure);
            if (object == nullptr) return failure;

            Completion key = evaluator.to_string(argument_at(args, 1));
            if (key.is_abrupt()) return key;

            PropertyDescriptor descriptor;
            Completion converted = to_property_descriptor(evaluator, argument_at(args, 2), descriptor);
            if (converted.is_abrupt()) return converted;

            const std::string name = key.get_value_or_undefined().to_string();

            if (Completion c = object->coerce_defined_value(evaluator, name, descriptor); c.is_abrupt()) {
                return c;
            }

            if (!object->define_own_property(name, descriptor)) {
                if (object->rejects_with_range_error(name, descriptor)) {
                    return evaluator.throw_error(evaluator.range_error_prototype,
                        "Invalid array length");
                }

                return evaluator.throw_error(evaluator.type_error_prototype,
                    "Cannot redefine property '" + name + "'");
            }

            return Completion::normal(JSValue::object(object));
        }

        Completion define_properties(Evaluator &evaluator, JSObject *object, const JSValue &source) {
            Completion holder = evaluator.to_object(source);
            if (holder.is_abrupt()) return holder;

            JSObject *properties = holder.get_value_or_undefined().as_object();

            for (const std::string &name : properties->own_keys()) {
                const PropertyDescriptor *own = properties->get_own_property(name);
                if (own == nullptr || !own->is_enumerable()) continue;

                Completion value = properties->get(evaluator, name);
                if (value.is_abrupt()) return value;

                PropertyDescriptor descriptor;
                Completion converted = to_property_descriptor(evaluator, value.get_value_or_undefined(), descriptor);
                if (converted.is_abrupt()) return converted;

                if (!object->define_own_property(name, descriptor)) {
                    return evaluator.throw_error(evaluator.type_error_prototype,
                        "Cannot redefine property '" + name + "'");
                }
            }

            return Completion::normal(JSValue::object(object));
        }

        Completion object_define_properties(Evaluator &evaluator, const JSValue&,
                                            const std::vector<JSValue> &args) {
            Completion failure = Completion::empty();
            JSObject *object = object_argument(evaluator, args, "defineProperties", failure);
            if (object == nullptr) return failure;

            return define_properties(evaluator, object, argument_at(args, 1));
        }

        Completion object_create(Evaluator &evaluator, const JSValue&, const std::vector<JSValue> &args) {
            const JSValue prototype = argument_at(args, 0);

            if (prototype.type() != JSValueType::Object && prototype.type() != JSValueType::Null) {
                return evaluator.throw_error(evaluator.type_error_prototype,
                    "Object prototype may only be an Object or null");
            }

            auto *object = evaluator.heap.allocate<JSObject>();
            object->prototype = prototype.type() == JSValueType::Object ? prototype.as_object() : nullptr;

            if (args.size() > 1 && args[1].type() != JSValueType::Undefined) {
                return define_properties(evaluator, object, args[1]);
            }

            return Completion::normal(JSValue::object(object));
        }

        Completion set_integrity(Evaluator &evaluator, JSObject *object, bool freeze) {
            for (const std::string &name : object->own_keys()) {
                const PropertyDescriptor *current = object->get_own_property(name);
                if (current == nullptr) continue;

                PropertyDescriptor update;
                update.configurable = false;
                if (freeze && current->is_data()) update.writable = false;

                if (!object->define_own_property(name, update)) {
                    return evaluator.throw_error(evaluator.type_error_prototype,
                        "Cannot redefine property '" + name + "'");
                }
            }

            object->extensible = false;
            return Completion::normal(JSValue::object(object));
        }

        Completion object_seal(Evaluator &evaluator, const JSValue&, const std::vector<JSValue> &args) {
            Completion failure = Completion::empty();
            JSObject *object = object_argument(evaluator, args, "seal", failure);
            if (object == nullptr) return failure;

            return set_integrity(evaluator, object, false);
        }

        Completion object_freeze(Evaluator &evaluator, const JSValue&, const std::vector<JSValue> &args) {
            Completion failure = Completion::empty();
            JSObject *object = object_argument(evaluator, args, "freeze", failure);
            if (object == nullptr) return failure;

            return set_integrity(evaluator, object, true);
        }

        Completion object_prevent_extensions(Evaluator &evaluator, const JSValue&,
                                             const std::vector<JSValue> &args) {
            Completion failure = Completion::empty();
            JSObject *object = object_argument(evaluator, args, "preventExtensions", failure);
            if (object == nullptr) return failure;

            object->extensible = false;
            return Completion::normal(JSValue::object(object));
        }

        bool integrity_holds(JSObject *object, bool frozen) {
            for (const std::string &name : object->own_keys()) {
                const PropertyDescriptor *current = object->get_own_property(name);
                if (current == nullptr) continue;

                if (current->is_configurable()) return false;
                if (frozen && current->is_data() && current->is_writable()) return false;
            }

            return !object->extensible;
        }

        Completion object_is_sealed(Evaluator &evaluator, const JSValue&, const std::vector<JSValue> &args) {
            Completion failure = Completion::empty();
            JSObject *object = object_argument(evaluator, args, "isSealed", failure);
            if (object == nullptr) return failure;

            return Completion::normal(JSValue::boolean(integrity_holds(object, false)));
        }

        Completion object_is_frozen(Evaluator &evaluator, const JSValue&, const std::vector<JSValue> &args) {
            Completion failure = Completion::empty();
            JSObject *object = object_argument(evaluator, args, "isFrozen", failure);
            if (object == nullptr) return failure;

            return Completion::normal(JSValue::boolean(integrity_holds(object, true)));
        }

        Completion object_is_extensible(Evaluator &evaluator, const JSValue&, const std::vector<JSValue> &args) {
            Completion failure = Completion::empty();
            JSObject *object = object_argument(evaluator, args, "isExtensible", failure);
            if (object == nullptr) return failure;

            return Completion::normal(JSValue::boolean(object->extensible));
        }

        Completion object_to_string(Evaluator &evaluator, const JSValue &this_value,
                                    const std::vector<JSValue>&) {
            if (this_value.type() == JSValueType::Undefined) {
                return Completion::normal(JSValue::string("[object Undefined]"));
            }
            if (this_value.type() == JSValueType::Null) {
                return Completion::normal(JSValue::string("[object Null]"));
            }

            Completion self = evaluator.to_object(this_value);
            if (self.is_abrupt()) return self;

            const char *cls = self.get_value_or_undefined().as_object()->class_name();

            return Completion::normal(JSValue::string(std::string("[object ") + cls + "]"));
        }

        Completion object_to_locale_string(Evaluator &evaluator, const JSValue &this_value,
                                           const std::vector<JSValue>&) {
            Completion self = evaluator.to_object(this_value);
            if (self.is_abrupt()) return self;

            Completion method = self.get_value_or_undefined().as_object()->get(evaluator, "toString");
            if (method.is_abrupt()) return method;

            const JSValue function = method.get_value_or_undefined();
            if (function.type() != JSValueType::Object || !function.as_object()->is_callable()) {
                return evaluator.throw_error(evaluator.type_error_prototype, "toString is not a function");
            }

            return function.as_object()->call(evaluator, this_value, {});
        }

        Completion object_value_of(Evaluator &evaluator, const JSValue &this_value, const std::vector<JSValue>&) {
            return evaluator.to_object(this_value);
        }

        Completion object_has_own_property(Evaluator &evaluator, const JSValue &this_value,
                                           const std::vector<JSValue> &args) {
            Completion key = evaluator.to_string(argument_at(args, 0));
            if (key.is_abrupt()) return key;

            Completion object = evaluator.to_object(this_value);
            if (object.is_abrupt()) return object;

            const bool own = object.get_value_or_undefined().as_object()
                ->get_own_property(key.get_value_or_undefined().to_string()) != nullptr;

            return Completion::normal(JSValue::boolean(own));
        }

        Completion object_is_prototype_of(Evaluator &evaluator, const JSValue &this_value,
                                          const std::vector<JSValue> &args) {
            const JSValue value = argument_at(args, 0);
            if (value.type() != JSValueType::Object) return Completion::normal(JSValue::boolean(false));

            Completion self = evaluator.to_object(this_value);
            if (self.is_abrupt()) return self;

            const JSObject *target = self.get_value_or_undefined().as_object();

            for (const JSObject *object = value.as_object()->prototype;
                 object != nullptr; object = object->prototype) {
                if (object == target) return Completion::normal(JSValue::boolean(true));
            }

            return Completion::normal(JSValue::boolean(false));
        }

        Completion object_property_is_enumerable(Evaluator &evaluator, const JSValue &this_value,
                                                 const std::vector<JSValue> &args) {
            Completion key = evaluator.to_string(argument_at(args, 0));
            if (key.is_abrupt()) return key;

            Completion self = evaluator.to_object(this_value);
            if (self.is_abrupt()) return self;

            const PropertyDescriptor *descriptor = self.get_value_or_undefined().as_object()
                ->get_own_property(key.get_value_or_undefined().to_string());

            return Completion::normal(JSValue::boolean(descriptor != nullptr && descriptor->is_enumerable()));
        }
    }

    void install_object(const Builtins &b) {
        JSObject *prototype = b.evaluator.object_prototype;

        JSObject *object_constructor = b.constructor("Object", native_object, prototype);

        b.method(object_constructor, "getPrototypeOf",           object_get_prototype_of, 1);
        b.method(object_constructor, "getOwnPropertyDescriptor", object_get_own_property_descriptor, 2);
        b.method(object_constructor, "getOwnPropertyNames",      object_get_own_property_names, 1);
        b.method(object_constructor, "keys",                     object_keys, 1);
        b.method(object_constructor, "create",                   object_create, 2);
        b.method(object_constructor, "defineProperty",           object_define_property, 3);
        b.method(object_constructor, "defineProperties",         object_define_properties, 2);
        b.method(object_constructor, "seal",                     object_seal, 1);
        b.method(object_constructor, "freeze",                   object_freeze, 1);
        b.method(object_constructor, "preventExtensions",        object_prevent_extensions, 1);
        b.method(object_constructor, "isSealed",                 object_is_sealed, 1);
        b.method(object_constructor, "isFrozen",                 object_is_frozen, 1);
        b.method(object_constructor, "isExtensible",             object_is_extensible, 1);

        b.method(prototype, "toString",             object_to_string);
        b.method(prototype, "toLocaleString",       object_to_locale_string);
        b.method(prototype, "valueOf",              object_value_of);
        b.method(prototype, "hasOwnProperty",       object_has_own_property, 1);
        b.method(prototype, "isPrototypeOf",        object_is_prototype_of, 1);
        b.method(prototype, "propertyIsEnumerable", object_property_is_enumerable, 1);
    }
}