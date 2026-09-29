#ifndef PERUNEJS_COMPLETION_H
#define PERUNEJS_COMPLETION_H
#include <utility>
#include <variant>

#include "js_value.h"

namespace perunejs {
    enum class COMPLETION_TYPE : uint8_t {
        NORMAL,
        BREAK,
        CONTINUE,
        RETURN,
        THROW,
    };

    class Completion {
    public:
        COMPLETION_TYPE type;
        std::variant<std::monostate, JSValue> value;
        std::string target;

        Completion(COMPLETION_TYPE type,
                   std::variant<std::monostate, JSValue> value,
                   std::string target = "")
            : type(type), value(std::move(value)), target(std::move(target)) {};

        static Completion empty() {
            return {COMPLETION_TYPE::NORMAL, std::monostate()};
        }
        static Completion normal(JSValue value) {
            return {COMPLETION_TYPE::NORMAL, value};
        }
        static Completion throw_value(JSValue value) {
            return {COMPLETION_TYPE::THROW, value};
        }
        static Completion ret(JSValue value) {
            return {COMPLETION_TYPE::RETURN, value};
        }
        static Completion brk(std::string label) {
            return {COMPLETION_TYPE::BREAK, std::monostate(), std::move(label)};
        }
        static Completion cont(std::string label) {
            return {COMPLETION_TYPE::CONTINUE, std::monostate(), std::move(label)};
        }

        bool is_abrupt() const { return type != COMPLETION_TYPE::NORMAL; }
        bool has_value() const { return std::holds_alternative<JSValue>(value); }

        JSValue get_value_or_undefined() const {
            return has_value() ? get<JSValue>(value) : JSValue::undefined();
        }
    };
}

#endif //PERUNEJS_COMPLETION_H
