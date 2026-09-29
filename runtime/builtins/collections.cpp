#include "runtime/builtins.h"
#include "memory/js_map.h"

namespace perunejs {
    namespace {
        Completion this_collection(Evaluator &evaluator, const JSValue &this_value,
                                   const char *kind, JSCollection *&out) {
            if (this_value.type() == JSValueType::Object) {
                if (auto *collection = dynamic_cast<JSCollection *>(this_value.as_object());
                    collection != nullptr && collection->kind == kind) {
                    out = collection;
                    return Completion::empty();
                }
            }

            return evaluator.throw_error(evaluator.type_error_prototype,
                std::string("Method called on incompatible receiver, expected ") + kind);
        }

        Completion populate(Evaluator &evaluator, JSCollection *collection,
                            const JSValue &source, bool pairs) {
            if (source.type() == JSValueType::Undefined || source.type() == JSValueType::Null) {
                return Completion::empty();
            }

            if (source.type() != JSValueType::Object) {
                return evaluator.throw_error(evaluator.type_error_prototype,
                    "Collection constructor argument is not iterable");
            }

            JSObject *object = source.as_object();

            Completion length = object->get(evaluator, "length");
            if (length.is_abrupt()) return length;

            if (length.get_value_or_undefined().type() == JSValueType::Undefined) {
                return evaluator.throw_error(evaluator.type_error_prototype,
                    "Collection constructor argument is not iterable");
            }

            Completion counted = evaluator.to_number(length.get_value_or_undefined());
            if (counted.is_abrupt()) return counted;

            const auto total = static_cast<uint32_t>(counted.get_value_or_undefined().to_number());

            for (uint32_t i = 0; i < total; ++i) {
                Completion element = object->get(evaluator, std::to_string(i));
                if (element.is_abrupt()) return element;

                const JSValue value = element.get_value_or_undefined();

                if (!pairs) {
                    collection->set(value, value);
                    continue;
                }

                if (value.type() != JSValueType::Object) {
                    return evaluator.throw_error(evaluator.type_error_prototype,
                        "Iterator value is not an entry object");
                }

                Completion key = value.as_object()->get(evaluator, "0");
                if (key.is_abrupt()) return key;

                Completion stored = value.as_object()->get(evaluator, "1");
                if (stored.is_abrupt()) return stored;

                collection->set(key.get_value_or_undefined(), stored.get_value_or_undefined());
            }

            return Completion::empty();
        }

        Completion construct_collection(Evaluator &evaluator, JSObject *prototype,
                                        const char *kind, bool pairs,
                                        const std::vector<JSValue> &args) {
            auto *collection = evaluator.heap.allocate<JSCollection>(evaluator.heap, kind);
            collection->prototype = prototype;

            DeferGC defer(evaluator.heap);

            if (Completion filled = populate(evaluator, collection, argument_at(args, 0), pairs);
                filled.is_abrupt()) {
                return filled;
            }

            return Completion::normal(JSValue::object(collection));
        }

        Completion require_new(Evaluator &evaluator, const char *kind) {
            return evaluator.throw_error(evaluator.type_error_prototype,
                std::string("Constructor ") + kind + " requires 'new'");
        }

        Completion call_map(Evaluator &e, const JSValue&, const std::vector<JSValue>&) {
            return require_new(e, "Map");
        }
        Completion call_set(Evaluator &e, const JSValue&, const std::vector<JSValue>&) {
            return require_new(e, "Set");
        }
        Completion call_weak_map(Evaluator &e, const JSValue&, const std::vector<JSValue>&) {
            return require_new(e, "WeakMap");
        }
        Completion call_weak_set(Evaluator &e, const JSValue&, const std::vector<JSValue>&) {
            return require_new(e, "WeakSet");
        }

        Completion construct_map(Evaluator &e, const JSValue&, const std::vector<JSValue> &args) {
            return construct_collection(e, e.map_prototype, "Map", true, args);
        }
        Completion construct_set(Evaluator &e, const JSValue&, const std::vector<JSValue> &args) {
            return construct_collection(e, e.set_prototype, "Set", false, args);
        }
        Completion construct_weak_map(Evaluator &e, const JSValue&, const std::vector<JSValue> &args) {
            return construct_collection(e, e.weak_map_prototype, "WeakMap", true, args);
        }
        Completion construct_weak_set(Evaluator &e, const JSValue&, const std::vector<JSValue> &args) {
            return construct_collection(e, e.weak_set_prototype, "WeakSet", false, args);
        }

        template <const char *KIND>
        Completion collection_get(Evaluator &e, const JSValue &self, const std::vector<JSValue> &args) {
            JSCollection *collection = nullptr;
            if (Completion c = this_collection(e, self, KIND, collection); c.is_abrupt()) return c;

            return Completion::normal(collection->fetch(argument_at(args, 0)));
        }

        template <const char *KIND>
        Completion collection_set(Evaluator &e, const JSValue &self, const std::vector<JSValue> &args) {
            JSCollection *collection = nullptr;
            if (Completion c = this_collection(e, self, KIND, collection); c.is_abrupt()) return c;

            collection->set(argument_at(args, 0), argument_at(args, 1));

            return Completion::normal(self);
        }

        template <const char *KIND>
        Completion collection_add(Evaluator &e, const JSValue &self, const std::vector<JSValue> &args) {
            JSCollection *collection = nullptr;
            if (Completion c = this_collection(e, self, KIND, collection); c.is_abrupt()) return c;

            const JSValue value = argument_at(args, 0);
            collection->set(value, value);

            return Completion::normal(self);
        }

        template <const char *KIND>
        Completion collection_has(Evaluator &e, const JSValue &self, const std::vector<JSValue> &args) {
            JSCollection *collection = nullptr;
            if (Completion c = this_collection(e, self, KIND, collection); c.is_abrupt()) return c;

            return Completion::normal(JSValue::boolean(collection->has(argument_at(args, 0))));
        }

        template <const char *KIND>
        Completion collection_delete(Evaluator &e, const JSValue &self, const std::vector<JSValue> &args) {
            JSCollection *collection = nullptr;
            if (Completion c = this_collection(e, self, KIND, collection); c.is_abrupt()) return c;

            return Completion::normal(JSValue::boolean(collection->remove(argument_at(args, 0))));
        }

        template <const char *KIND>
        Completion collection_clear(Evaluator &e, const JSValue &self, const std::vector<JSValue>&) {
            JSCollection *collection = nullptr;
            if (Completion c = this_collection(e, self, KIND, collection); c.is_abrupt()) return c;

            collection->clear();

            return Completion::normal(JSValue::undefined());
        }

        template <const char *KIND>
        Completion collection_size(Evaluator &e, const JSValue &self, const std::vector<JSValue>&) {
            JSCollection *collection = nullptr;
            if (Completion c = this_collection(e, self, KIND, collection); c.is_abrupt()) return c;

            return Completion::normal(JSValue::number(static_cast<double>(collection->size())));
        }

        template <const char *KIND>
        Completion collection_for_each(Evaluator &e, const JSValue &self, const std::vector<JSValue> &args) {
            JSCollection *collection = nullptr;
            if (Completion c = this_collection(e, self, KIND, collection); c.is_abrupt()) return c;

            const JSValue callback = argument_at(args, 0);
            if (callback.type() != JSValueType::Object || !callback.as_object()->is_callable()) {
                return e.throw_error(e.type_error_prototype, "forEach callback is not a function");
            }

            const JSValue this_arg = argument_at(args, 1);

            for (std::size_t i = 0; i < collection->entries.size(); ++i) {
                const CollectionEntry entry = collection->entries[i];
                if (!entry.present) continue;

                MarkedVector call_args(e.heap);
                call_args.push_back(entry.value);
                call_args.push_back(entry.key);
                call_args.push_back(self);

                Completion result = callback.as_object()->call(e, this_arg, call_args);
                if (result.is_abrupt()) return result;
            }

            return Completion::normal(JSValue::undefined());
        }

        extern const char MAP_KIND[]      = "Map";
        extern const char SET_KIND[]      = "Set";
        extern const char WEAK_MAP_KIND[] = "WeakMap";
        extern const char WEAK_SET_KIND[] = "WeakSet";

        void define_size_getter(const Builtins &b, JSObject *prototype, NativeFunction getter) {
            JSObject *accessor = b.function("size", getter, 0);

            prototype->define_own_property("size",
                PropertyDescriptor::accessor(accessor, nullptr, false, true));
        }
    }

    void install_collections(const Builtins &b) {
        Evaluator &evaluator = b.evaluator;

        JSObject *map_prototype = evaluator.map_prototype;
        b.method(map_prototype, "get",      collection_get<MAP_KIND>, 1);
        b.method(map_prototype, "set",      collection_set<MAP_KIND>, 2);
        b.method(map_prototype, "has",      collection_has<MAP_KIND>, 1);
        b.method(map_prototype, "delete",   collection_delete<MAP_KIND>, 1);
        b.method(map_prototype, "clear",    collection_clear<MAP_KIND>, 0);
        b.method(map_prototype, "forEach",  collection_for_each<MAP_KIND>, 1);
        define_size_getter(b, map_prototype, collection_size<MAP_KIND>);
        b.constructor("Map", call_map, map_prototype, construct_map, 0);

        JSObject *set_prototype = evaluator.set_prototype;
        b.method(set_prototype, "add",      collection_add<SET_KIND>, 1);
        b.method(set_prototype, "has",      collection_has<SET_KIND>, 1);
        b.method(set_prototype, "delete",   collection_delete<SET_KIND>, 1);
        b.method(set_prototype, "clear",    collection_clear<SET_KIND>, 0);
        b.method(set_prototype, "forEach",  collection_for_each<SET_KIND>, 1);
        define_size_getter(b, set_prototype, collection_size<SET_KIND>);
        b.constructor("Set", call_set, set_prototype, construct_set, 0);

        JSObject *weak_map_prototype = evaluator.weak_map_prototype;
        b.method(weak_map_prototype, "get",    collection_get<WEAK_MAP_KIND>, 1);
        b.method(weak_map_prototype, "set",    collection_set<WEAK_MAP_KIND>, 2);
        b.method(weak_map_prototype, "has",    collection_has<WEAK_MAP_KIND>, 1);
        b.method(weak_map_prototype, "delete", collection_delete<WEAK_MAP_KIND>, 1);
        b.constructor("WeakMap", call_weak_map, weak_map_prototype, construct_weak_map, 0);

        JSObject *weak_set_prototype = evaluator.weak_set_prototype;
        b.method(weak_set_prototype, "add",    collection_add<WEAK_SET_KIND>, 1);
        b.method(weak_set_prototype, "has",    collection_has<WEAK_SET_KIND>, 1);
        b.method(weak_set_prototype, "delete", collection_delete<WEAK_SET_KIND>, 1);
        b.constructor("WeakSet", call_weak_set, weak_set_prototype, construct_weak_set, 0);
    }
}
