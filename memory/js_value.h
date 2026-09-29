
#ifndef PERUNEJS_JS_VALUE_H
#define PERUNEJS_JS_VALUE_H
#include <string>
#include "js_symbol.h"
#include "utils/utf.h"
#include "nodes.h"

namespace perunejs {
    struct Undefined {};
    struct Null {};

    enum class JSValueType : uint8_t {
        Undefined,
        Null,
        Boolean,
        Number,
        String,
        Object,
        Symbol      // musi być ostatni: type() to indeks w wariancie poniżej
        // SYMBOL_TYPE,
        // BIG_INTEGER_TYPE,
        // itd.
    };

    class JSObject;

    class JSValue {
        std::variant<Undefined, Null, bool, double, std::u16string, JSObject*, const JSSymbolData*> value;
    
    public:
        JSValue(): value(Undefined()) {}
        JSValue(std::variant<Undefined, Null, bool, double, std::u16string, JSObject*, const JSSymbolData*> value): value(value) {};

        JSValueType type() const { return static_cast<JSValueType>(value.index()); }

        static JSValue undefined() ;
        static JSValue null();
        static JSValue boolean(bool);
        static JSValue number(double);
        static JSValue string(std::u16string);
        static JSValue string(std::string_view utf8);
        static JSValue string(const char* utf8) { return string(std::string_view(utf8)); }
        static JSValue object(JSObject *);
        static JSValue symbol(const JSSymbolData *);

        bool to_boolean() const;
        double to_number() const;
        std::string to_string() const;
        std::u16string to_u16string() const;
        JSValue to_numeric() const;
        int32_t to_int32() const;
        uint32_t to_uint32() const;
        std::string get_type() const;


        JSObject *as_object() const;
        const JSSymbolData *as_symbol() const;

        static bool is_loosely_equal(const JSValue &x, const JSValue &y);
        static bool is_strictly_equal(const JSValue &x, const JSValue &y);
        static bool same_value(const JSValue& x, const JSValue& y);
        static std::optional<bool> is_less_than(const JSValue &x, const JSValue &y);
        static JSValue apply_string_or_numeric_binary_operator(const JSValue &lval, BinaryOperator op, const JSValue &rval);
    };
}


#endif //PERUNEJS_JS_VALUE_H
