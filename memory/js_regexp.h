#ifndef PERUNEJS_JS_REGEXP_H
#define PERUNEJS_JS_REGEXP_H

#include <memory>
#include <string>
#include <utility>

#include "js_object.h"
#include "runtime/regexp_engine.h"

namespace perunejs {
    class JSRegExp : public JSObject {
    public:
        std::string pattern;
        std::string flags;

        std::shared_ptr<const RegExpEngine> engine;

        JSRegExp(std::string pattern, std::string flags)
            : pattern(std::move(pattern)), flags(std::move(flags)) {}

        const char* class_name() override { return "RegExp"; }

        bool has_flag(const char flag) const { return flags.find(flag) != std::string::npos; }

        bool search(const std::u16string &input, const int from, RegExpMatch &out) const {
            if (engine == nullptr) return false;

            for (int at = from; at <= static_cast<int>(input.size()); ++at) {
                if (engine->match_at(input, at, out)) return true;
            }

            return false;
        }
    };
}

#endif //PERUNEJS_JS_REGEXP_H