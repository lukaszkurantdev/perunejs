#ifndef PERUNEJS_BUILTINS_H
#define PERUNEJS_BUILTINS_H


#include <string>
#include <vector>

#include "evaluator/evaluator.h"
#include "memory/js_native_function.h"
#include "memory/js_regexp.h"

namespace perunejs {
    struct Builtins {
        Heap &heap;
        Evaluator &evaluator;
        JSObject *global_object = nullptr;

        void data_property(JSObject *target, const std::string &name, const JSValue &value) const;
        void constant(JSObject *target, const std::string &name, const JSValue &value) const;
        void global_function(const char *name, const JSValue &value) const;
        void global_constant(const char *name, const JSValue &value) const;

        JSObject *function(const char *name, NativeFunction fn, int length = 0,
                               bool constructible = false) const;

        JSObject *method(JSObject *target, const char *name, NativeFunction fn, int length = 0) const;

        JSObject *constructor(const char *name, NativeFunction fn, JSObject *prototype,
                              NativeFunction construct_fn = nullptr, int length = 1) const;
    };

    JSObject *make_string_array(Evaluator &evaluator, const std::vector<std::string> &values);

    inline JSValue argument_at(const std::vector<JSValue> &args, std::size_t index) {
        return index < args.size() ? args[index] : JSValue::undefined();
    }

    Completion regexp_search(Evaluator &evaluator, JSRegExp *regexp, const std::u16string &input,
                             int from, bool &found, RegExpMatch &out);
    Completion regexp_match_at(Evaluator &evaluator, JSRegExp *regexp, const std::u16string &input,
                               int at, bool &found, RegExpMatch &out);
    JSObject *make_match_result(Evaluator &evaluator, const std::u16string &input,
                                const RegExpMatch &match);
    Completion to_regexp(Evaluator &evaluator, const JSValue &value, JSRegExp *&out);

    void install_function(const Builtins &b);
    void install_object(const Builtins &b);
    void install_error(const Builtins &b);
    void install_regexp(const Builtins &b);
    void install_array(const Builtins &b);
    void install_global_functions(const Builtins &b);
    void install_string(const Builtins &b);
    void install_number(const Builtins &b);
    void install_boolean(const Builtins &b);
    void install_math(const Builtins &b);
    void install_uri(const Builtins &b);
    void install_date(const Builtins &b);
    void install_json(const Builtins &b);
    void install_collections(const Builtins &b);
    void install_es2015(const Builtins &b);
    void install_symbol(const Builtins &b);
    void install_promise(const Builtins &b);
    void install_array_buffer(const Builtins &b);

    inline bool is_js_whitespace(char16_t unit) {
        switch (unit) {
            case 0x0009: case 0x000A: case 0x000B: case 0x000C: case 0x000D:
            case 0x0020: case 0x00A0: case 0x1680: case 0x180E: case 0x2028:
            case 0x2029: case 0x202F: case 0x205F: case 0x3000: case 0xFEFF:
                return true;
            default:
                return unit >= 0x2000 && unit <= 0x200A;
        }
    }

}

#endif //PERUNEJS_BUILTINS_H
