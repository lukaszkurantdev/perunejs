#include "runtime/builtins.h"

#include "memory/js_array.h"
#include "memory/js_string.h"
#include "memory/js_wrapper.h"
#include "memory/marked_vector.h"
#include "utils/native_stack.h"
#include "utils/utf.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace perunejs {
    namespace {
        class JSJson : public JSObject {
        public:
            const char *class_name() override { return "JSON"; }
        };

        bool is_array(const JSValue &value) {
            return value.type() == JSValueType::Object
                && dynamic_cast<JSArray*>(value.as_object()) != nullptr;
        }

        bool callable(const JSValue &value) {
            return value.type() == JSValueType::Object && value.as_object()->is_callable();
        }

        struct JsonParser {
            const std::u16string &text;
            Evaluator &evaluator;
            std::size_t at = 0;
            std::string error;

            char16_t peek() const { return at < text.size() ? text[at] : u'\0'; }
            bool done() const { return at >= text.size(); }

            bool fail(const char *message) {
                if (error.empty()) error = message;
                return false;
            }

            void skip_whitespace() {
                while (at < text.size()) {
                    const char16_t unit = text[at];
                    if (unit != u' ' && unit != u'\t' && unit != u'\n' && unit != u'\r') break;

                    ++at;
                }
            }

            bool take_word(const char16_t *word) {
                std::size_t length = 0;
                while (word[length] != u'\0') ++length;

                if (text.compare(at, length, word) != 0) return false;

                at += length;
                return true;
            }

            bool parse_string(std::u16string &out) {
                if (peek() != u'"') return fail("expected string");

                ++at;
                out.clear();

                while (true) {
                    if (done()) return fail("unterminated string");

                    const char16_t unit = text[at];

                    if (unit == u'"') { ++at; return true; }

                    if (unit < 0x20) return fail("control character in string");

                    if (unit != u'\\') { out += unit; ++at; continue; }

                    ++at;
                    if (done()) return fail("unterminated escape");

                    const char16_t escape = text[at++];

                    switch (escape) {
                        case u'"':  out += u'"';  break;
                        case u'\\': out += u'\\'; break;
                        case u'/':  out += u'/';  break;
                        case u'b':  out += u'\b'; break;
                        case u'f':  out += u'\f'; break;
                        case u'n':  out += u'\n'; break;
                        case u'r':  out += u'\r'; break;
                        case u't':  out += u'\t'; break;
                        case u'u': {
                            if (at + 4 > text.size()) return fail("bad unicode escape");

                            char16_t code = 0;
                            for (int i = 0; i < 4; ++i) {
                                const char16_t digit = text[at + i];
                                int value;

                                if (digit >= u'0' && digit <= u'9')      value = digit - u'0';
                                else if (digit >= u'a' && digit <= u'f') value = digit - u'a' + 10;
                                else if (digit >= u'A' && digit <= u'F') value = digit - u'A' + 10;
                                else return fail("bad unicode escape");

                                code = static_cast<char16_t>(code * 16 + value);
                            }

                            at += 4;
                            out += code;
                        } break;
                        default: return fail("invalid escape");
                    }
                }
            }

            bool parse_number(double &out) {
                const std::size_t start = at;

                if (peek() == u'-') ++at;

                if (peek() == u'0') {
                    ++at;
                } else if (peek() >= u'1' && peek() <= u'9') {
                    while (peek() >= u'0' && peek() <= u'9') ++at;
                } else {
                    return fail("expected number");
                }

                if (peek() == u'.') {
                    ++at;
                    if (peek() < u'0' || peek() > u'9') return fail("expected digit after '.'");

                    while (peek() >= u'0' && peek() <= u'9') ++at;
                }

                if (peek() == u'e' || peek() == u'E') {
                    ++at;
                    if (peek() == u'+' || peek() == u'-') ++at;
                    if (peek() < u'0' || peek() > u'9') return fail("expected digit in exponent");

                    while (peek() >= u'0' && peek() <= u'9') ++at;
                }

                std::string digits;
                digits.reserve(at - start);
                for (std::size_t i = start; i < at; ++i) digits += static_cast<char>(text[i]);

                out = std::strtod(digits.c_str(), nullptr);
                return true;
            }

            bool parse_value(JSValue &out) {
                skip_whitespace();

                if (done()) return fail("unexpected end of input");

                switch (peek()) {
                    case u'n':
                        if (!take_word(u"null")) return fail("unexpected token");
                        out = JSValue::null();
                        return true;

                    case u't':
                        if (!take_word(u"true")) return fail("unexpected token");
                        out = JSValue::boolean(true);
                        return true;

                    case u'f':
                        if (!take_word(u"false")) return fail("unexpected token");
                        out = JSValue::boolean(false);
                        return true;

                    case u'"': {
                        std::u16string value;
                        if (!parse_string(value)) return false;

                        out = JSValue::string(value);
                        return true;
                    }

                    case u'[': return parse_array(out);
                    case u'{': return parse_object(out);

                    default: {
                        double value = 0;
                        if (!parse_number(value)) return false;

                        out = JSValue::number(value);
                        return true;
                    }
                }
            }

            bool parse_array(JSValue &out) {
                ++at;

                auto *array = evaluator.heap.allocate<JSArray>();
                array->prototype = evaluator.array_prototype;
                array->define_own_property("length",
                    PropertyDescriptor::data(JSValue::number(0), true, false, false));

                out = JSValue::object(array);

                skip_whitespace();
                if (peek() == u']') { ++at; return true; }

                uint32_t index = 0;
                while (true) {
                    JSValue element;
                    if (!parse_value(element)) return false;

                    array->define_own_property(std::to_string(index++),
                        PropertyDescriptor::data(element, true, true, true));

                    skip_whitespace();

                    if (peek() == u',') { ++at; continue; }
                    if (peek() == u']') { ++at; return true; }

                    return fail("expected ',' or ']'");
                }
            }

            bool parse_object(JSValue &out) {
                ++at;

                auto *object = evaluator.heap.allocate<JSObject>();
                object->prototype = evaluator.object_prototype;

                out = JSValue::object(object);

                skip_whitespace();
                if (peek() == u'}') { ++at; return true; }

                while (true) {
                    skip_whitespace();

                    std::u16string key;
                    if (!parse_string(key)) return false;

                    skip_whitespace();
                    if (peek() != u':') return fail("expected ':'");
                    ++at;

                    JSValue value;
                    if (!parse_value(value)) return false;

                    object->define_own_property(utf16_to_utf8(key),
                        PropertyDescriptor::data(value, true, true, true));

                    skip_whitespace();

                    if (peek() == u',') { ++at; continue; }
                    if (peek() == u'}') { ++at; return true; }

                    return fail("expected ',' or '}'");
                }
            }
        };

        Completion walk(Evaluator &evaluator, const JSValue &reviver,
                        JSObject *holder, const std::string &name, JSValue &out) {
            if (native_stack_pointer() < native_stack_limit()) {
                return evaluator.throw_error(evaluator.range_error_prototype,
                    "Maximum call stack size exceeded");
            }

            Completion held = holder->get(evaluator, name);
            if (held.is_abrupt()) return held;

            const JSValue value = held.get_value_or_undefined();

            if (value.type() == JSValueType::Object) {
                JSObject *object = value.as_object();

                const std::vector<std::string> keys = object->own_keys();

                for (const std::string &key : keys) {
                    const PropertyDescriptor *descriptor = object->get_own_property(key);
                    if (descriptor == nullptr || !descriptor->is_enumerable()) continue;

                    JSValue replaced;
                    if (Completion c = walk(evaluator, reviver, object, key, replaced); c.is_abrupt()) return c;

                    if (replaced.type() == JSValueType::Undefined) {
                        object->delete_own_property(key);
                    } else {
                        object->define_own_property(key,
                            PropertyDescriptor::data(replaced, true, true, true));
                    }
                }
            }

            MarkedVector args(evaluator.heap);
            args.push_back(JSValue::string(name));
            args.push_back(value);

            Completion result = reviver.as_object()->call(evaluator, JSValue::object(holder), args);
            if (result.is_abrupt()) return result;

            out = result.get_value_or_undefined();
            return Completion::empty();
        }

        Completion json_parse(Evaluator &evaluator, const JSValue&, const std::vector<JSValue> &args) {
            Completion text = evaluator.to_string(argument_at(args, 0));
            if (text.is_abrupt()) return text;

            const std::u16string source = text.get_value_or_undefined().to_u16string();

            JsonParser parser{source, evaluator};

            JSValue value;
            if (!parser.parse_value(value)) {
                return evaluator.throw_error(evaluator.syntax_error_prototype,
                    "Unexpected token in JSON: " + parser.error);
            }

            parser.skip_whitespace();
            if (!parser.done()) {
                return evaluator.throw_error(evaluator.syntax_error_prototype,
                    "Unexpected token in JSON");
            }

            const JSValue reviver = argument_at(args, 1);
            if (!callable(reviver)) return Completion::normal(value);

            auto *holder = evaluator.heap.allocate<JSObject>();
            holder->prototype = evaluator.object_prototype;
            holder->define_own_property("", PropertyDescriptor::data(value, true, true, true));

            JSValue revived;
            if (Completion c = walk(evaluator, reviver, holder, "", revived); c.is_abrupt()) return c;

            return Completion::normal(revived);
        }

        std::u16string quote(const std::u16string &value) {
            std::u16string result = u"\"";

            for (const char16_t unit : value) {
                switch (unit) {
                    case u'"':  result += u"\\\""; break;
                    case u'\\': result += u"\\\\"; break;
                    case u'\b': result += u"\\b";  break;
                    case u'\f': result += u"\\f";  break;
                    case u'\n': result += u"\\n";  break;
                    case u'\r': result += u"\\r";  break;
                    case u'\t': result += u"\\t";  break;
                    default:
                        if (unit < 0x20) {
                            char buffer[8];
                            std::snprintf(buffer, sizeof buffer, "\\u%04x", unit);

                            for (const char *at = buffer; *at != '\0'; ++at) result += *at;
                        } else {
                            result += unit;
                        }
                }
            }

            return result + u"\"";
        }

        struct Stringifier {
            Evaluator &evaluator;
            JSValue replacer;
            std::vector<std::string> allowed;
            bool has_allowed = false;
            std::u16string gap;
            std::u16string indent;
            std::vector<JSObject*> stack;

            Completion str(const std::string &key, JSObject *holder,
                           std::u16string &out, bool &skipped) {
                skipped = false;

                if (native_stack_pointer() < native_stack_limit()) {
                    return evaluator.throw_error(evaluator.range_error_prototype,
                        "Maximum call stack size exceeded");
                }

                Completion held = holder->get(evaluator, key);
                if (held.is_abrupt()) return held;

                JSValue value = held.get_value_or_undefined();

                if (value.type() == JSValueType::Object) {
                    Completion method = value.as_object()->get(evaluator, "toJSON");
                    if (method.is_abrupt()) return method;

                    if (callable(method.get_value_or_undefined())) {
                        MarkedVector args(evaluator.heap);
                        args.push_back(JSValue::string(key));

                        Completion produced = method.get_value_or_undefined().as_object()
                            ->call(evaluator, value, args);
                        if (produced.is_abrupt()) return produced;

                        value = produced.get_value_or_undefined();
                    }
                }

                if (callable(replacer)) {
                    MarkedVector args(evaluator.heap);
                    args.push_back(JSValue::string(key));
                    args.push_back(value);

                    Completion produced = replacer.as_object()
                        ->call(evaluator, JSValue::object(holder), args);
                    if (produced.is_abrupt()) return produced;

                    value = produced.get_value_or_undefined();
                }

                if (value.type() == JSValueType::Object) {
                    JSObject *object = value.as_object();
                    const char *cls = object->class_name();

                    if (std::strcmp(cls, "Number") == 0) {
                        Completion number = evaluator.to_number(value);
                        if (number.is_abrupt()) return number;

                        value = number.get_value_or_undefined();
                    } else if (std::strcmp(cls, "String") == 0) {
                        Completion text = evaluator.to_string(value);
                        if (text.is_abrupt()) return text;

                        value = text.get_value_or_undefined();
                    } else if (auto *wrapper = dynamic_cast<JSPrimitiveWrapper*>(object)) {
                        if (std::strcmp(wrapper->label, "Boolean") == 0) value = wrapper->primitive;
                    }
                }

                switch (value.type()) {
                    case JSValueType::Null:
                        out = u"null";
                        return Completion::empty();

                    case JSValueType::Boolean:
                        out = value.to_boolean() ? u"true" : u"false";
                        return Completion::empty();

                    case JSValueType::String:
                        out = quote(value.to_u16string());
                        return Completion::empty();

                    case JSValueType::Number:
                        out = std::isfinite(value.to_number())
                            ? evaluator.to_string(value).get_value_or_undefined().to_u16string()
                            : u"null";
                        return Completion::empty();

                    case JSValueType::Object:
                        if (!value.as_object()->is_callable()) {
                            return is_array(value) ? array(value.as_object(), out)
                                                   : object(value.as_object(), out);
                        }
                        break;

                    default:
                        break;
                }

                skipped = true;
                return Completion::empty();
            }

            Completion enter(JSObject *value) {
                for (JSObject *seen : stack) {
                    if (seen == value) {
                        return evaluator.throw_error(evaluator.type_error_prototype,
                            "Converting circular structure to JSON");
                    }
                }

                stack.push_back(value);
                return Completion::empty();
            }

            std::u16string assemble(const std::vector<std::u16string> &parts,
                                    const std::u16string &stepback,
                                    const char16_t open, const char16_t close) const {
                if (parts.empty()) return std::u16string{open} + close;

                std::u16string joined;

                if (gap.empty()) {
                    for (std::size_t i = 0; i < parts.size(); ++i) {
                        if (i > 0) joined += u',';
                        joined += parts[i];
                    }

                    return std::u16string{open} + joined + close;
                }

                const std::u16string separator = u",\n" + indent;

                for (std::size_t i = 0; i < parts.size(); ++i) {
                    if (i > 0) joined += separator;
                    joined += parts[i];
                }

                return std::u16string{open} + u"\n" + indent + joined + u"\n" + stepback + close;
            }

            Completion array(JSObject *value, std::u16string &out) {
                if (Completion c = enter(value); c.is_abrupt()) return c;

                const std::u16string stepback = indent;
                indent += gap;

                Completion length_value = value->get(evaluator, "length");
                if (length_value.is_abrupt()) return length_value;

                Completion length_number = evaluator.to_number(length_value.get_value_or_undefined());
                if (length_number.is_abrupt()) return length_number;

                const uint32_t length = length_number.get_value_or_undefined().to_uint32();

                std::vector<std::u16string> parts;

                for (uint32_t i = 0; i < length; ++i) {
                    std::u16string element;
                    bool skipped = false;

                    if (Completion c = str(std::to_string(i), value, element, skipped); c.is_abrupt()) return c;

                    parts.push_back(skipped ? u"null" : element);
                }

                out = assemble(parts, stepback, u'[', u']');

                stack.pop_back();
                indent = stepback;

                return Completion::empty();
            }

            Completion object(JSObject *value, std::u16string &out) {
                if (Completion c = enter(value); c.is_abrupt()) return c;

                const std::u16string stepback = indent;
                indent += gap;

                std::vector<std::string> keys;

                if (has_allowed) {
                    keys = allowed;
                } else {
                    for (const std::string &key : value->own_keys()) {
                        const PropertyDescriptor *descriptor = value->get_own_property(key);
                        if (descriptor != nullptr && descriptor->is_enumerable()) keys.push_back(key);
                    }
                }

                std::vector<std::u16string> parts;

                for (const std::string &key : keys) {
                    std::u16string member;
                    bool skipped = false;

                    if (Completion c = str(key, value, member, skipped); c.is_abrupt()) return c;
                    if (skipped) continue;

                    parts.push_back(quote(utf8_to_utf16(key)) + u":"
                                    + (gap.empty() ? u"" : u" ") + member);
                }

                out = assemble(parts, stepback, u'{', u'}');

                stack.pop_back();
                indent = stepback;

                return Completion::empty();
            }
        };

        Completion json_stringify(Evaluator &evaluator, const JSValue&,
                                  const std::vector<JSValue> &args) {
            Stringifier writer{evaluator};

            const JSValue replacer = argument_at(args, 1);

            if (callable(replacer)) {
                writer.replacer = replacer;
            } else if (is_array(replacer)) {
                JSObject *list = replacer.as_object();

                Completion length_value = list->get(evaluator, "length");
                if (length_value.is_abrupt()) return length_value;

                Completion length_number = evaluator.to_number(length_value.get_value_or_undefined());
                if (length_number.is_abrupt()) return length_number;

                const uint32_t length = length_number.get_value_or_undefined().to_uint32();
                writer.has_allowed = true;

                for (uint32_t i = 0; i < length; ++i) {
                    Completion element = list->get(evaluator, std::to_string(i));
                    if (element.is_abrupt()) return element;

                    const JSValue value = element.get_value_or_undefined();
                    const char *cls = value.type() == JSValueType::Object
                                    ? value.as_object()->class_name() : "";

                    if (value.type() != JSValueType::String && value.type() != JSValueType::Number
                        && std::strcmp(cls, "String") != 0 && std::strcmp(cls, "Number") != 0) {
                        continue;
                    }

                    Completion text = evaluator.to_string(value);
                    if (text.is_abrupt()) return text;

                    const std::string key = text.get_value_or_undefined().to_string();

                    if (std::find(writer.allowed.begin(), writer.allowed.end(), key)
                        == writer.allowed.end()) {
                        writer.allowed.push_back(key);
                    }
                }
            }

            JSValue space = argument_at(args, 2);

            if (space.type() == JSValueType::Object) {
                const char *cls = space.as_object()->class_name();

                if (std::strcmp(cls, "Number") == 0) {
                    Completion number = evaluator.to_number(space);
                    if (number.is_abrupt()) return number;

                    space = number.get_value_or_undefined();
                } else if (std::strcmp(cls, "String") == 0) {
                    Completion text = evaluator.to_string(space);
                    if (text.is_abrupt()) return text;

                    space = text.get_value_or_undefined();
                }
            }

            if (space.type() == JSValueType::Number) {
                const double raw = space.to_number();
                const int count = std::isnan(raw) ? 0
                                : static_cast<int>(std::min(10.0, std::max(0.0, std::trunc(raw))));

                writer.gap.assign(count, u' ');
            } else if (space.type() == JSValueType::String) {
                writer.gap = space.to_u16string();
                if (writer.gap.size() > 10) writer.gap.resize(10);
            }

            auto *holder = evaluator.heap.allocate<JSObject>();
            holder->prototype = evaluator.object_prototype;
            holder->define_own_property("",
                PropertyDescriptor::data(argument_at(args, 0), true, true, true));

            std::u16string result;
            bool skipped = false;

            if (Completion c = writer.str("", holder, result, skipped); c.is_abrupt()) return c;

            if (skipped) return Completion::normal(JSValue::undefined());

            return Completion::normal(JSValue::string(result));
        }
    }

    void install_json(const Builtins &b) {
        auto *json = b.heap.allocate<JSJson>();
        json->prototype = b.evaluator.object_prototype;

        b.global_function("JSON", JSValue::object(json));

        b.method(json, "parse",     json_parse, 2);
        b.method(json, "stringify", json_stringify, 3);
    }
}