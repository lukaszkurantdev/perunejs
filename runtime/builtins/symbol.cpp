#include "runtime/builtins.h"

#include "memory/js_array.h"
#include "memory/js_symbol.h"
#include "memory/js_wrapper.h"

#include <map>

namespace perunejs {
    namespace {
        std::map<std::string, const JSSymbolData *> &registry() {
            static std::map<std::string, const JSSymbolData *> known;
            return known;
        }

        Completion this_symbol(Evaluator &evaluator, const JSValue &value, const JSSymbolData *&out) {
            if (const JSSymbolData *direct = value.as_symbol(); direct != nullptr) {
                out = direct;
                return Completion::empty();
            }

            if (value.type() == JSValueType::Object) {
                if (auto *wrapper = dynamic_cast<JSPrimitiveWrapper *>(value.as_object());
                    wrapper != nullptr && wrapper->primitive.as_symbol() != nullptr) {
                    out = wrapper->primitive.as_symbol();
                    return Completion::empty();
                }
            }

            return evaluator.throw_error(evaluator.type_error_prototype,
                "Symbol.prototype method called on incompatible receiver");
        }

        Completion native_symbol(Evaluator &evaluator, const JSValue&, const std::vector<JSValue> &args) {
            std::string description;

            if (const JSValue given = argument_at(args, 0); given.type() != JSValueType::Undefined) {
                Completion text = evaluator.to_string(given);
                if (text.is_abrupt()) return text;

                description = text.get_value_or_undefined().to_string();
            }

            return Completion::normal(JSValue::symbol(SymbolTable::instance().create(description)));
        }

        Completion symbol_for(Evaluator &evaluator, const JSValue&, const std::vector<JSValue> &args) {
            Completion key = evaluator.to_string(argument_at(args, 0));
            if (key.is_abrupt()) return key;

            const std::string name = key.get_value_or_undefined().to_string();

            if (const auto found = registry().find(name); found != registry().end()) {
                return Completion::normal(JSValue::symbol(found->second));
            }

            const JSSymbolData *created = SymbolTable::instance().registered(name);
            registry().emplace(name, created);

            return Completion::normal(JSValue::symbol(created));
        }

        Completion symbol_key_for(Evaluator &evaluator, const JSValue&, const std::vector<JSValue> &args) {
            const JSValue value = argument_at(args, 0);

            if (value.as_symbol() == nullptr) {
                return evaluator.throw_error(evaluator.type_error_prototype,
                    "Symbol.keyFor requires a symbol");
            }

            for (const auto &[name, symbol] : registry()) {
                if (symbol == value.as_symbol()) return Completion::normal(JSValue::string(name));
            }

            return Completion::normal(JSValue::undefined());
        }

        Completion symbol_to_string(Evaluator &evaluator, const JSValue &self, const std::vector<JSValue>&) {
            const JSSymbolData *symbol = nullptr;
            if (Completion c = this_symbol(evaluator, self, symbol); c.is_abrupt()) return c;

            return Completion::normal(JSValue::string("Symbol(" + symbol->description + ")"));
        }

        Completion symbol_value_of(Evaluator &evaluator, const JSValue &self, const std::vector<JSValue>&) {
            const JSSymbolData *symbol = nullptr;
            if (Completion c = this_symbol(evaluator, self, symbol); c.is_abrupt()) return c;

            return Completion::normal(JSValue::symbol(symbol));
        }

        Completion symbol_description(Evaluator &evaluator, const JSValue &self, const std::vector<JSValue>&) {
            const JSSymbolData *symbol = nullptr;
            if (Completion c = this_symbol(evaluator, self, symbol); c.is_abrupt()) return c;

            return Completion::normal(JSValue::string(symbol->description));
        }

        bool is_symbol_key(const std::string &name) {
            return name.rfind("@@", 0) == 0;
        }

        Completion object_get_own_property_symbols(Evaluator &evaluator, const JSValue&,
                                                   const std::vector<JSValue> &args) {
            Completion object = evaluator.to_object(argument_at(args, 0));
            if (object.is_abrupt()) return object;

            auto *result = evaluator.heap.allocate<JSArray>();
            result->prototype = evaluator.array_prototype;
            result->define_own_property("length",
                PropertyDescriptor::data(JSValue::number(0), true, false, false));

            uint32_t written = 0;

            for (const std::string &key : object.get_value_or_undefined().as_object()->own_keys()) {
                if (!is_symbol_key(key)) continue;

                result->define_own_property(std::to_string(written++),
                    PropertyDescriptor::data(JSValue::string(key), true, true, true));
            }

            return Completion::normal(JSValue::object(result));
        }
    }

    void install_symbol(const Builtins &b) {
        Evaluator &evaluator = b.evaluator;

        JSObject *prototype = evaluator.symbol_prototype;
        b.method(prototype, "toString", symbol_to_string, 0);
        b.method(prototype, "valueOf",  symbol_value_of, 0);

        prototype->define_own_property("description",
            PropertyDescriptor::accessor(b.function("description", symbol_description, 0),
                                         nullptr, false, true));

        JSObject *symbol = b.function("Symbol", native_symbol, 0);

        symbol->define_own_property("prototype",
            PropertyDescriptor::data(JSValue::object(prototype), false, false, false));
        b.data_property(prototype, "constructor", JSValue::object(symbol));

        b.method(symbol, "for",    symbol_for, 1);
        b.method(symbol, "keyFor", symbol_key_for, 1);

        for (const char *name : {
                "asyncIterator", "hasInstance", "isConcatSpreadable", "iterator",
                "match", "replace", "search", "species", "split",
                "toPrimitive", "toStringTag", "unscopables"}) {
            b.constant(symbol, name, JSValue::symbol(SymbolTable::instance().well_known(name)));
        }

        b.global_function("Symbol", JSValue::object(symbol));

        Completion object_constructor = evaluator.global_object->get(evaluator, "Object");
        if (object_constructor.type == COMPLETION_TYPE::NORMAL
            && object_constructor.get_value_or_undefined().type() == JSValueType::Object) {
            b.method(object_constructor.get_value_or_undefined().as_object(),
                     "getOwnPropertySymbols", object_get_own_property_symbols, 1);
        }
    }
}
