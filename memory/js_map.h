#ifndef PERUNEJS_JS_MAP_H
#define PERUNEJS_JS_MAP_H

#include <cmath>
#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

#include "heap.h"
#include "js_object.h"
#include "js_value.h"

namespace perunejs {
    inline bool same_value_zero(const JSValue &x, const JSValue &y) {
        if (x.type() == JSValueType::Number && y.type() == JSValueType::Number) {
            const double left = x.to_number();
            const double right = y.to_number();

            if (std::isnan(left) && std::isnan(right)) return true;

            return left == right;   // +0 == -0 true
        }

        return JSValue::same_value(x, y);
    }

    inline std::size_t hash_js_value(const JSValue &value) {
        switch (value.type()) {
            case JSValueType::Undefined: return 0x9e37;
            case JSValueType::Null:      return 0x85eb;
            case JSValueType::Boolean:   return value.to_boolean() ? 0xc2b2u : 0x27d4u;
            case JSValueType::Number: {
                double number = value.to_number();
                if (std::isnan(number)) return 0x165667;
                if (number == 0) number = 0;   // zbija -0 do +0

                return std::hash<double>{}(number);
            }
            case JSValueType::String:
                return std::hash<std::u16string>{}(value.to_u16string());
            case JSValueType::Object:
                return std::hash<const void *>{}(static_cast<const void *>(value.as_object()));
            case JSValueType::Symbol:
                return std::hash<const void *>{}(static_cast<const void *>(value.as_symbol()));
        }

        return 0;
    }

    struct CollectionEntry {
        JSValue key;
        JSValue value;
        bool present = true;
    };

    class JSCollection : public JSObject, public WeakContainer {
        std::unordered_multimap<std::size_t, std::size_t> index;
        std::size_t live = 0;
        Heap *owner = nullptr;

        void rebuild_index() {
            index.clear();
            for (std::size_t i = 0; i < entries.size(); ++i) {
                if (entries[i].present) index.emplace(hash_js_value(entries[i].key), i);
            }
        }

    public:
        std::vector<CollectionEntry> entries;
        std::string kind;
        bool weak = false;

        JSCollection(Heap &heap, std::string kind)
            : owner(&heap), kind(std::move(kind)) {
            weak = this->kind.rfind("Weak", 0) == 0;
            if (weak) owner->add_weak_container(this);
        }

        ~JSCollection() override { if (weak && owner != nullptr) owner->remove_weak_container(this); }

        const char *class_name() override { return kind.c_str(); }

        std::size_t size() const { return live; }

        std::size_t find(const JSValue &key) const {
            const auto range = index.equal_range(hash_js_value(key));

            for (auto it = range.first; it != range.second; ++it) {
                const CollectionEntry &entry = entries[it->second];
                if (entry.present && same_value_zero(entry.key, key)) return it->second;
            }

            return entries.size();
        }

        bool has(const JSValue &key) const { return find(key) != entries.size(); }

        JSValue fetch(const JSValue &key) const {
            const std::size_t at = find(key);
            return at == entries.size() ? JSValue::undefined() : entries[at].value;
        }

        void set(const JSValue &key, const JSValue &value) {
            const std::size_t at = find(key);

            if (at != entries.size()) {
                entries[at].value = value;
                return;
            }

            entries.push_back(CollectionEntry{key, value, true});
            index.emplace(hash_js_value(key), entries.size() - 1);
            ++live;
        }

        bool remove(const JSValue &key) {
            const auto range = index.equal_range(hash_js_value(key));

            for (auto it = range.first; it != range.second; ++it) {
                CollectionEntry &entry = entries[it->second];
                if (!entry.present || !same_value_zero(entry.key, key)) continue;

                entry.present = false;
                entry.key = JSValue::undefined();
                entry.value = JSValue::undefined();
                index.erase(it);
                --live;

                return true;
            }

            return false;
        }

        void clear() {
            for (CollectionEntry &entry : entries) {
                entry.present = false;
                entry.key = JSValue::undefined();
                entry.value = JSValue::undefined();
            }

            index.clear();
            live = 0;
        }

        void trace(CellVisitor &visitor) const override {
            JSObject::trace(visitor);

            if (weak) return;

            for (const CollectionEntry &entry : entries) {
                if (!entry.present) continue;

                if (JSObject *key = entry.key.as_object()) visitor.visit(key);
                if (JSObject *value = entry.value.as_object()) visitor.visit(value);
            }
        }

        void mark_reachable_values(CellVisitor &visitor) override {
            for (const CollectionEntry &entry : entries) {
                if (!entry.present) continue;

                JSObject *key = entry.key.as_object();
                if (key != nullptr && !key->marked) continue;

                if (JSObject *value = entry.value.as_object()) visitor.visit(value);
            }
        }

        void purge_dead_entries() override {
            bool removed = false;

            for (CollectionEntry &entry : entries) {
                if (!entry.present) continue;

                JSObject *key = entry.key.as_object();
                if (key == nullptr || key->marked) continue;

                entry.present = false;
                entry.key = JSValue::undefined();
                entry.value = JSValue::undefined();
                --live;
                removed = true;
            }

            if (removed) rebuild_index();
        }
    };
}

#endif //PERUNEJS_JS_MAP_H
