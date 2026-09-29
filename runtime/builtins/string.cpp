#include "runtime/builtins.h"

#include "memory/js_array.h"
#include "memory/js_regexp.h"
#include "memory/js_string.h"
#include "utils/unicode_case.h"
#include "utils/utf.h"
#include <cmath>
#include <optional>

namespace perunejs {
    namespace {
        constexpr std::size_t NOT_FOUND = std::u16string::npos;

        JSObject *make_value_array(Evaluator &evaluator, const std::vector<JSValue> &values) {
            auto *array = evaluator.heap.allocate<JSArray>();
            array->prototype = evaluator.array_prototype;

            array->define_own_property("length",
                PropertyDescriptor::data(JSValue::number(0), true, false, false));

            for (std::size_t i = 0; i < values.size(); ++i) {
                array->define_own_property(std::to_string(i),
                    PropertyDescriptor::data(values[i], true, true, true));
            }

            return array;
        }

        JSObject *make_array(Evaluator &evaluator, const std::vector<std::u16string> &parts) {
            auto *array = evaluator.heap.allocate<JSArray>();
            array->prototype = evaluator.array_prototype;

            array->define_own_property("length",
                PropertyDescriptor::data(JSValue::number(0), true, false, false));

            for (std::size_t i = 0; i < parts.size(); ++i) {
                array->define_own_property(std::to_string(i),
                    PropertyDescriptor::data(JSValue::string(parts[i]), true, true, true));
            }

            return array;
        }

        Completion this_string(Evaluator &evaluator, const JSValue &this_value, std::u16string &out) {
            if (this_value.type() == JSValueType::Undefined || this_value.type() == JSValueType::Null) {
                return evaluator.throw_error(evaluator.type_error_prototype,
                    "String.prototype method called on null or undefined");
            }

            Completion text = evaluator.to_string(this_value);
            if (text.is_abrupt()) return text;

            out = text.get_value_or_undefined().to_u16string();
            return Completion::empty();
        }

        Completion to_u16(Evaluator &evaluator, const JSValue &value, std::u16string &out) {
            Completion text = evaluator.to_string(value);
            if (text.is_abrupt()) return text;

            out = text.get_value_or_undefined().to_u16string();
            return Completion::empty();
        }

        Completion to_integer(Evaluator &evaluator, const JSValue &value, double &out) {
            Completion number = evaluator.to_number(value);
            if (number.is_abrupt()) return number;

            const double raw = number.get_value_or_undefined().to_number();
            out = std::isnan(raw) ? 0 : std::trunc(raw);

            return Completion::empty();
        }

        std::size_t clamp_index(double position, std::size_t length) {
            if (position < 0) return 0;
            if (position > static_cast<double>(length)) return length;

            return static_cast<std::size_t>(position);
        }

        std::size_t relative_index(double position, std::size_t length) {
            if (position < 0) {
                const double from_end = static_cast<double>(length) + position;
                return from_end < 0 ? 0 : static_cast<std::size_t>(from_end);
            }

            return clamp_index(position, length);
        }

        Completion native_string(Evaluator &evaluator, const JSValue&, const std::vector<JSValue> &args) {
            if (args.empty()) return Completion::normal(JSValue::string(std::u16string()));

            return evaluator.to_string(args[0]);
        }

        Completion construct_string(Evaluator &evaluator, const JSValue&, const std::vector<JSValue> &args) {
            std::u16string value;
            if (!args.empty()) {
                if (Completion c = to_u16(evaluator, args[0], value); c.is_abrupt()) return c;
            }

            auto *wrapper = evaluator.heap.allocate<JSString>(value);
            wrapper->prototype = evaluator.string_prototype;

            return Completion::normal(JSValue::object(wrapper));
        }

        Completion string_from_char_code(Evaluator &evaluator, const JSValue&,
                                         const std::vector<JSValue> &args) {
            std::u16string result;
            result.reserve(args.size());

            for (const JSValue &argument : args) {
                Completion number = evaluator.to_number(argument);
                if (number.is_abrupt()) return number;

                result.push_back(static_cast<char16_t>(number.get_value_or_undefined().to_uint32()));
            }

            return Completion::normal(JSValue::string(result));
        }

        Completion string_value_of(Evaluator &evaluator, const JSValue &this_value,
                                   const std::vector<JSValue>&) {
            if (this_value.type() == JSValueType::String) {
                return Completion::normal(this_value);
            }

            if (this_value.type() == JSValueType::Object) {
                if (auto *wrapper = dynamic_cast<JSString*>(this_value.as_object())) {
                    return Completion::normal(JSValue::string(wrapper->value));
                }
            }

            return evaluator.throw_error(evaluator.type_error_prototype,
                "String.prototype.valueOf called on incompatible receiver");
        }

        Completion string_char_at(Evaluator &evaluator, const JSValue &this_value,
                                  const std::vector<JSValue> &args) {
            std::u16string self;
            if (Completion c = this_string(evaluator, this_value, self); c.is_abrupt()) return c;

            double position = 0;
            if (Completion c = to_integer(evaluator, argument_at(args, 0), position); c.is_abrupt()) return c;

            if (position < 0 || position >= static_cast<double>(self.size())) {
                return Completion::normal(JSValue::string(std::u16string()));
            }

            return Completion::normal(JSValue::string(
                std::u16string(1, self[static_cast<std::size_t>(position)])));
        }

        Completion string_char_code_at(Evaluator &evaluator, const JSValue &this_value,
                                       const std::vector<JSValue> &args) {
            std::u16string self;
            if (Completion c = this_string(evaluator, this_value, self); c.is_abrupt()) return c;

            double position = 0;
            if (Completion c = to_integer(evaluator, argument_at(args, 0), position); c.is_abrupt()) return c;

            if (position < 0 || position >= static_cast<double>(self.size())) {
                return Completion::normal(JSValue::number(std::nan("")));
            }

            return Completion::normal(JSValue::number(
                static_cast<double>(self[static_cast<std::size_t>(position)])));
        }

        Completion string_index_of(Evaluator &evaluator, const JSValue &this_value,
                                   const std::vector<JSValue> &args) {
            std::u16string self;
            if (Completion c = this_string(evaluator, this_value, self); c.is_abrupt()) return c;

            std::u16string needle;
            if (Completion c = to_u16(evaluator, argument_at(args, 0), needle); c.is_abrupt()) return c;

            double position = 0;
            if (Completion c = to_integer(evaluator, argument_at(args, 1), position); c.is_abrupt()) return c;

            const std::size_t found = self.find(needle, clamp_index(position, self.size()));

            return Completion::normal(JSValue::number(
                found == NOT_FOUND ? -1 : static_cast<double>(found)));
        }

        Completion string_last_index_of(Evaluator &evaluator, const JSValue &this_value,
                                        const std::vector<JSValue> &args) {
            std::u16string self;
            if (Completion c = this_string(evaluator, this_value, self); c.is_abrupt()) return c;

            std::u16string needle;
            if (Completion c = to_u16(evaluator, argument_at(args, 0), needle); c.is_abrupt()) return c;

            Completion number = evaluator.to_number(argument_at(args, 1));
            if (number.is_abrupt()) return number;

            const double raw = number.get_value_or_undefined().to_number();
            const std::size_t start = std::isnan(raw) ? self.size()
                                                      : clamp_index(std::trunc(raw), self.size());

            const std::size_t found = self.rfind(needle, start);

            return Completion::normal(JSValue::number(
                found == NOT_FOUND ? -1 : static_cast<double>(found)));
        }

        Completion string_locale_compare(Evaluator &evaluator, const JSValue &this_value,
                                         const std::vector<JSValue> &args) {
            std::u16string self;
            if (Completion c = this_string(evaluator, this_value, self); c.is_abrupt()) return c;

            std::u16string other;
            if (Completion c = to_u16(evaluator, argument_at(args, 0), other); c.is_abrupt()) return c;

            const int comparison = self.compare(other);

            return Completion::normal(JSValue::number(comparison < 0 ? -1 : (comparison > 0 ? 1 : 0)));
        }

        Completion string_concat(Evaluator &evaluator, const JSValue &this_value,
                                 const std::vector<JSValue> &args) {
            std::u16string result;
            if (Completion c = this_string(evaluator, this_value, result); c.is_abrupt()) return c;

            for (const JSValue &argument : args) {
                std::u16string part;
                if (Completion c = to_u16(evaluator, argument, part); c.is_abrupt()) return c;

                result += part;
            }

            return Completion::normal(JSValue::string(result));
        }

        Completion string_slice(Evaluator &evaluator, const JSValue &this_value,
                                const std::vector<JSValue> &args) {
            std::u16string self;
            if (Completion c = this_string(evaluator, this_value, self); c.is_abrupt()) return c;

            double start = 0;
            if (Completion c = to_integer(evaluator, argument_at(args, 0), start); c.is_abrupt()) return c;

            double end = static_cast<double>(self.size());
            if (argument_at(args, 1).type() != JSValueType::Undefined) {
                if (Completion c = to_integer(evaluator, args[1], end); c.is_abrupt()) return c;
            }

            const std::size_t from = relative_index(start, self.size());
            const std::size_t to   = relative_index(end, self.size());

            return Completion::normal(JSValue::string(
                from < to ? self.substr(from, to - from) : std::u16string()));
        }

        Completion string_substring(Evaluator &evaluator, const JSValue &this_value,
                                    const std::vector<JSValue> &args) {
            std::u16string self;
            if (Completion c = this_string(evaluator, this_value, self); c.is_abrupt()) return c;

            double start = 0;
            if (Completion c = to_integer(evaluator, argument_at(args, 0), start); c.is_abrupt()) return c;

            double end = static_cast<double>(self.size());
            if (argument_at(args, 1).type() != JSValueType::Undefined) {
                if (Completion c = to_integer(evaluator, args[1], end); c.is_abrupt()) return c;
            }

            std::size_t from = clamp_index(start, self.size());
            std::size_t to   = clamp_index(end, self.size());

            if (from > to) std::swap(from, to);

            return Completion::normal(JSValue::string(self.substr(from, to - from)));
        }

        Completion string_substr(Evaluator &evaluator, const JSValue &this_value,
                                 const std::vector<JSValue> &args) {
            std::u16string self;
            if (Completion c = this_string(evaluator, this_value, self); c.is_abrupt()) return c;

            double start = 0;
            if (Completion c = to_integer(evaluator, argument_at(args, 0), start); c.is_abrupt()) return c;

            double count = static_cast<double>(self.size());
            if (argument_at(args, 1).type() != JSValueType::Undefined) {
                if (Completion c = to_integer(evaluator, args[1], count); c.is_abrupt()) return c;
            }

            const std::size_t from = relative_index(start, self.size());
            if (count <= 0) return Completion::normal(JSValue::string(std::u16string()));

            const std::size_t available = self.size() - from;
            const std::size_t taken = count > static_cast<double>(available)
                                    ? available : static_cast<std::size_t>(count);

            return Completion::normal(JSValue::string(self.substr(from, taken)));
        }

        Completion change_case(Evaluator &evaluator, bool upper, const JSValue &this_value) {
            std::u16string self;
            if (Completion c = this_string(evaluator, this_value, self); c.is_abrupt()) return c;

            return Completion::normal(JSValue::string(change_case_full(self, upper)));
        }

        Completion string_to_lower_case(Evaluator &e, const JSValue &t, const std::vector<JSValue>&) {
            return change_case(e, false, t);
        }
        Completion string_to_upper_case(Evaluator &e, const JSValue &t, const std::vector<JSValue>&) {
            return change_case(e, true, t);
        }

        Completion string_trim(Evaluator &evaluator, const JSValue &this_value, const std::vector<JSValue>&) {
            std::u16string self;
            if (Completion c = this_string(evaluator, this_value, self); c.is_abrupt()) return c;

            std::size_t from = 0;
            std::size_t to = self.size();

            while (from < to && is_js_whitespace(self[from])) ++from;
            while (to > from && is_js_whitespace(self[to - 1])) --to;

            return Completion::normal(JSValue::string(self.substr(from, to - from)));
        }

                Completion string_split(Evaluator &evaluator, const JSValue &this_value,
                                const std::vector<JSValue> &args) {
            std::u16string self;
            if (Completion c = this_string(evaluator, this_value, self); c.is_abrupt()) return c;

            const JSValue separator_value = argument_at(args, 0);

            uint32_t limit = 0xFFFFFFFFu;
            if (argument_at(args, 1).type() != JSValueType::Undefined) {
                Completion number = evaluator.to_number(args[1]);
                if (number.is_abrupt()) return number;

                limit = number.get_value_or_undefined().to_uint32();
            }

            std::vector<std::u16string> parts;
            const auto push = [&](std::u16string part) { parts.push_back(std::move(part)); };

            if (limit == 0) return Completion::normal(JSValue::object(make_array(evaluator, {})));

            if (separator_value.type() == JSValueType::Undefined) {
                push(self);
                return Completion::normal(JSValue::object(make_array(evaluator, parts)));
            }

            if (JSRegExp *pattern = dynamic_cast<JSRegExp*>(
                    separator_value.type() == JSValueType::Object
                        ? separator_value.as_object() : nullptr)) {
                std::vector<JSValue> values;

                const auto add = [&](const std::u16string &piece) {
                    values.push_back(JSValue::string(piece));
                };

                if (self.empty()) {
                    bool found = false;
                    RegExpMatch match;

                    if (Completion c = regexp_match_at(evaluator, pattern, self, 0, found, match);
                        c.is_abrupt()) return c;

                    if (found) return Completion::normal(JSValue::object(make_value_array(evaluator, {})));

                    add(self);
                    return Completion::normal(JSValue::object(make_value_array(evaluator, values)));
                }

                std::size_t start = 0;
                std::size_t at = 0;

                while (at < self.size()) {
                    bool found = false;
                    RegExpMatch match;

                    if (Completion c = regexp_match_at(evaluator, pattern, self,
                                                       static_cast<int>(at), found, match);
                        c.is_abrupt()) return c;

                    if (!found || static_cast<std::size_t>(match.ends[0]) == start) { ++at; continue; }

                    add(self.substr(start, at - start));
                    if (values.size() == limit) {
                        return Completion::normal(JSValue::object(make_value_array(evaluator, values)));
                    }

                    for (std::size_t g = 1; g < match.starts.size(); ++g) {
                        if (match.starts[g] < 0) values.push_back(JSValue::undefined());
                        else add(self.substr(match.starts[g], match.ends[g] - match.starts[g]));

                        if (values.size() == limit) {
                            return Completion::normal(JSValue::object(make_value_array(evaluator, values)));
                        }
                    }

                    start = match.ends[0];
                    at = start;
                }

                add(self.substr(start));

                if (values.size() > limit) values.resize(limit);

                return Completion::normal(JSValue::object(make_value_array(evaluator, values)));
            }

            std::u16string separator;
            if (Completion c = to_u16(evaluator, separator_value, separator); c.is_abrupt()) return c;

            if (self.empty()) {
                if (separator.empty()) return Completion::normal(JSValue::object(make_array(evaluator, {})));

                push(self);
                return Completion::normal(JSValue::object(make_array(evaluator, parts)));
            }

            std::size_t start = 0;
            std::size_t at = 0;

            while (at != self.size()) {
                const bool matched = !separator.empty()
                                  && at + separator.size() <= self.size()
                                  && self.compare(at, separator.size(), separator) == 0;

                const std::size_t end = matched ? at + separator.size() : at;

                if ((!matched && !separator.empty()) || end == start) { ++at; continue; }

                push(self.substr(start, at - start));
                if (parts.size() == limit) return Completion::normal(JSValue::object(make_array(evaluator, parts)));

                start = end;
                at = start;
            }

            push(self.substr(start));

            if (parts.size() > limit) parts.resize(limit);

            return Completion::normal(JSValue::object(make_array(evaluator, parts)));
        }

        std::u16string expand_dollars(const std::u16string &pattern, const std::u16string &subject,
                                       std::size_t position, const std::u16string &matched,
                                       const std::vector<std::optional<std::u16string>> &groups) {
            std::u16string result;

            for (std::size_t i = 0; i < pattern.size(); ++i) {
                if (pattern[i] != u'$' || i + 1 == pattern.size()) { result += pattern[i]; continue; }

                const char16_t next = pattern[i + 1];

                if (next >= u'0' && next <= u'9') {
                    std::size_t number = next - u'0';
                    std::size_t width = 1;

                    if (i + 2 < pattern.size() && pattern[i + 2] >= u'0' && pattern[i + 2] <= u'9') {
                        const std::size_t wider = number * 10 + (pattern[i + 2] - u'0');

                        if (wider >= 1 && wider <= groups.size()) { number = wider; width = 2; }
                    }

                    if (number >= 1 && number <= groups.size()) {
                        if (groups[number - 1].has_value()) result += *groups[number - 1];

                        i += width;
                        continue;
                    }

                    result += pattern[i];
                    continue;
                }

                switch (next) {
                    case u'$':  result += u'$';                            ++i; break;
                    case u'&':  result += matched;                         ++i; break;
                    case u'`':  result += subject.substr(0, position);     ++i; break;
                    case u'\'': result += subject.substr(position + matched.size()); ++i; break;
                    default:    result += pattern[i];                            break;
                }
            }

            return result;
        }

        std::vector<std::optional<std::u16string>> groups_of(const std::u16string &subject,
                                                             const RegExpMatch &match) {
            std::vector<std::optional<std::u16string>> groups;

            for (std::size_t i = 1; i < match.starts.size(); ++i) {
                if (match.starts[i] < 0) groups.emplace_back();
                else groups.emplace_back(subject.substr(match.starts[i],
                                                        match.ends[i] - match.starts[i]));
            }

            return groups;
        }

        Completion replacement_text(Evaluator &evaluator, const JSValue &replacement,
                                    const std::u16string &subject, const std::size_t position,
                                    const std::u16string &matched,
                                    const std::vector<std::optional<std::u16string>> &groups,
                                    std::u16string &out) {
            if (replacement.type() == JSValueType::Object && replacement.as_object()->is_callable()) {
                MarkedVector args(evaluator.heap);
                args.push_back(JSValue::string(matched));

                for (const auto &group : groups) {
                    args.push_back(group.has_value() ? JSValue::string(*group) : JSValue::undefined());
                }

                args.push_back(JSValue::number(static_cast<double>(position)));
                args.push_back(JSValue::string(subject));

                Completion produced = replacement.as_object()
                    ->call(evaluator, JSValue::undefined(), args);
                if (produced.is_abrupt()) return produced;

                return to_u16(evaluator, produced.get_value_or_undefined(), out);
            }

            std::u16string pattern;
            if (Completion c = to_u16(evaluator, replacement, pattern); c.is_abrupt()) return c;

            out = expand_dollars(pattern, subject, position, matched, groups);
            return Completion::empty();
        }

        Completion string_replace(Evaluator &evaluator, const JSValue &this_value,
                                  const std::vector<JSValue> &args) {
            std::u16string self;
            if (Completion c = this_string(evaluator, this_value, self); c.is_abrupt()) return c;

            const JSValue search_value = argument_at(args, 0);
            const JSValue replacement = argument_at(args, 1);

            if (JSRegExp *pattern = dynamic_cast<JSRegExp*>(
                    search_value.type() == JSValueType::Object ? search_value.as_object() : nullptr)) {
                const bool global = pattern->has_flag('g');

                if (global) {
                    Completion stored = pattern->put(evaluator, "lastIndex", JSValue::number(0), true);
                    if (stored.is_abrupt()) return stored;
                }

                std::u16string result;
                std::size_t copied = 0;
                int from = 0;

                while (from <= static_cast<int>(self.size())) {
                    bool found = false;
                    RegExpMatch match;

                    if (Completion c = regexp_search(evaluator, pattern, self, from, found, match);
                        c.is_abrupt()) return c;

                    if (!found) break;

                    const std::u16string matched =
                        self.substr(match.starts[0], match.ends[0] - match.starts[0]);

                    std::u16string inserted;
                    if (Completion c = replacement_text(evaluator, replacement, self,
                                                        match.starts[0], matched,
                                                        groups_of(self, match), inserted);
                        c.is_abrupt()) return c;

                    result += self.substr(copied, match.starts[0] - copied);
                    result += inserted;
                    copied = match.ends[0];

                    if (!global) break;


                    from = match.ends[0] > match.starts[0] ? match.ends[0] : match.ends[0] + 1;
                }

                result += self.substr(copied);

                return Completion::normal(JSValue::string(result));
            }

            std::u16string needle;
            if (Completion c = to_u16(evaluator, search_value, needle); c.is_abrupt()) return c;

            const std::size_t position = self.find(needle);
            if (position == NOT_FOUND) return Completion::normal(JSValue::string(self));

            std::u16string inserted;
            if (Completion c = replacement_text(evaluator, replacement, self, position,
                                                needle, {}, inserted); c.is_abrupt()) return c;

            return Completion::normal(JSValue::string(
                self.substr(0, position) + inserted + self.substr(position + needle.size())));
        }


        Completion string_match(Evaluator &evaluator, const JSValue &this_value,
                                const std::vector<JSValue> &args) {
            std::u16string self;
            if (Completion c = this_string(evaluator, this_value, self); c.is_abrupt()) return c;

            JSRegExp *pattern = nullptr;
            if (Completion c = to_regexp(evaluator, argument_at(args, 0), pattern); c.is_abrupt()) return c;

            if (!pattern->has_flag('g')) {
                bool found = false;
                RegExpMatch match;

                if (Completion c = regexp_search(evaluator, pattern, self, 0, found, match);
                    c.is_abrupt()) return c;

                if (!found) return Completion::normal(JSValue::null());

                return Completion::normal(JSValue::object(make_match_result(evaluator, self, match)));
            }

            Completion stored = pattern->put(evaluator, "lastIndex", JSValue::number(0), true);
            if (stored.is_abrupt()) return stored;

            std::vector<std::u16string> found_texts;
            int from = 0;

            while (from <= static_cast<int>(self.size())) {
                bool found = false;
                RegExpMatch match;

                if (Completion c = regexp_search(evaluator, pattern, self, from, found, match);
                    c.is_abrupt()) return c;

                if (!found) break;

                found_texts.push_back(self.substr(match.starts[0], match.ends[0] - match.starts[0]));

                from = match.ends[0] > match.starts[0] ? match.ends[0] : match.ends[0] + 1;
            }

            if (found_texts.empty()) return Completion::normal(JSValue::null());

            return Completion::normal(JSValue::object(make_array(evaluator, found_texts)));
        }

        Completion string_search(Evaluator &evaluator, const JSValue &this_value,
                                 const std::vector<JSValue> &args) {
            std::u16string self;
            if (Completion c = this_string(evaluator, this_value, self); c.is_abrupt()) return c;

            JSRegExp *pattern = nullptr;
            if (Completion c = to_regexp(evaluator, argument_at(args, 0), pattern); c.is_abrupt()) return c;

            bool found = false;
            RegExpMatch match;

            if (Completion c = regexp_search(evaluator, pattern, self, 0, found, match);
                c.is_abrupt()) return c;

            return Completion::normal(JSValue::number(found ? match.starts[0] : -1));
        }
    }

    void install_string(const Builtins &b) {
        JSObject *prototype = b.evaluator.string_prototype;

        JSObject *string_constructor =
            b.constructor("String", native_string, prototype, construct_string);

        b.method(string_constructor, "fromCharCode", string_from_char_code, 1);

        b.method(prototype, "toString",            string_value_of);
        b.method(prototype, "valueOf",             string_value_of);
        b.method(prototype, "charAt",              string_char_at, 1);
        b.method(prototype, "charCodeAt",          string_char_code_at, 1);
        b.method(prototype, "indexOf",             string_index_of, 1);
        b.method(prototype, "lastIndexOf",         string_last_index_of, 1);
        b.method(prototype, "localeCompare",       string_locale_compare, 1);
        b.method(prototype, "concat",              string_concat, 1);
        b.method(prototype, "slice",               string_slice, 2);
        b.method(prototype, "substring",           string_substring, 2);
        b.method(prototype, "substr",              string_substr, 2);
        b.method(prototype, "toLowerCase",         string_to_lower_case);
        b.method(prototype, "toLocaleLowerCase",   string_to_lower_case);
        b.method(prototype, "toUpperCase",         string_to_upper_case);
        b.method(prototype, "toLocaleUpperCase",   string_to_upper_case);
        b.method(prototype, "trim",                string_trim);
        b.method(prototype, "split",               string_split, 2);
        b.method(prototype, "replace",             string_replace, 2);
        b.method(prototype, "match",               string_match, 1);
        b.method(prototype, "search",              string_search, 1);
    }
}