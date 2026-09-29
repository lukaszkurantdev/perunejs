#include "runtime/regexp_engine.h"

#include "utils/native_stack.h"
#include "utils/unicode_case.h"

#include <algorithm>
#include <functional>
#include <optional>
#include <stdexcept>

namespace perunejs {
    namespace {
        constexpr int INFINITE = -1;

        struct State {
            const std::u16string *input = nullptr;
            int index = 0;
            std::vector<int> starts;
            std::vector<int> ends;

            int length() const { return static_cast<int>(input->size()); }
            char16_t at(const int position) const { return (*input)[position]; }
        };

        using Result = std::optional<State>;
        using Continuation = std::function<Result(const State&)>;
        using Matcher = std::function<Result(const State&, const Continuation&)>;

        struct StackGuard {
            static bool exhausted() { return native_stack_pointer() < limit(); }

            static std::uintptr_t &limit() {
                static thread_local std::uintptr_t value = 0;
                return value;
            }
        };

        [[noreturn]] void overflow() {
            throw std::runtime_error("Maximum call stack size exceeded");
        }

        char16_t canonicalize(const char16_t unit, const bool ignore_case) {
            if (!ignore_case) return unit;

            const char16_t upper = to_upper_unit(unit);

            return (unit >= 0x80 && upper < 0x80) ? unit : upper;
        }

        bool is_digit(const char16_t unit) { return unit >= u'0' && unit <= u'9'; }

        bool is_line_terminator(const char16_t unit) {
            return unit == u'\n' || unit == u'\r' || unit == 0x2028 || unit == 0x2029;
        }

        bool is_word(const char16_t unit) {
            return (unit >= u'a' && unit <= u'z') || (unit >= u'A' && unit <= u'Z')
                || is_digit(unit) || unit == u'_';
        }

        struct CharSet {
            std::vector<std::pair<char16_t, char16_t>> ranges;
            bool negated = false;

            void add(const char16_t from, const char16_t to) { ranges.emplace_back(from, to); }
            void add(const char16_t unit) { add(unit, unit); }

            void add_all(const std::vector<std::pair<char16_t, char16_t>> &other) {
                ranges.insert(ranges.end(), other.begin(), other.end());
            }

            bool holds(const char16_t unit) const {
                for (const auto &range : ranges) {
                    if (unit >= range.first && unit <= range.second) return true;
                }

                return false;
            }

            bool matches(const char16_t unit, const bool ignore_case) const {
                bool found = holds(unit);

                if (!found && ignore_case) {
                    found = holds(to_upper_unit(unit)) || holds(to_lower_unit(unit));
                }

                return found != negated;
            }

            std::vector<std::pair<char16_t, char16_t>> complement() const {
                std::vector<std::pair<char16_t, char16_t>> sorted = ranges;
                std::sort(sorted.begin(), sorted.end());

                std::vector<std::pair<char16_t, char16_t>> result;
                int next = 0;

                for (const auto &range : sorted) {
                    if (range.first > next) {
                        result.emplace_back(static_cast<char16_t>(next),
                                            static_cast<char16_t>(range.first - 1));
                    }

                    next = std::max(next, static_cast<int>(range.second) + 1);
                }

                if (next <= 0xFFFF) result.emplace_back(static_cast<char16_t>(next), 0xFFFF);

                return result;
            }
        };

        CharSet class_escape(const char16_t letter) {
            CharSet set;

            switch (letter) {
                case u'd': case u'D':
                    set.add(u'0', u'9');
                    set.negated = letter == u'D';
                    break;

                case u's': case u'S':
                    for (const char16_t unit : {0x0009, 0x000A, 0x000B, 0x000C, 0x000D, 0x0020,
                                                0x00A0, 0x1680, 0x180E, 0x2028, 0x2029,
                                                0x202F, 0x205F, 0x3000, 0xFEFF}) {
                        set.add(unit);
                    }
                    set.add(0x2000, 0x200A);
                    set.negated = letter == u'S';
                    break;

                default:
                    set.add(u'a', u'z');
                    set.add(u'A', u'Z');
                    set.add(u'0', u'9');
                    set.add(u'_');
                    set.negated = letter == u'W';
                    break;
            }

            return set;
        }

        Matcher match_empty() {
            return [](const State &x, const Continuation &c) -> Result { return c(x); };
        }

        Matcher match_set(CharSet set, const bool ignore_case) {
            return [set, ignore_case](const State &x, const Continuation &c) -> Result {
                if (StackGuard::exhausted()) overflow();

                if (x.index >= x.length()) return std::nullopt;
                if (!set.matches(x.at(x.index), ignore_case)) return std::nullopt;

                State y = x;
                y.index = x.index + 1;

                return c(y);
            };
        }

        Matcher match_edge(const bool at_start, const bool multiline) {
            return [at_start, multiline](const State &x, const Continuation &c) -> Result {
                const int edge = at_start ? 0 : x.length();

                if (x.index == edge) return c(x);
                if (!multiline) return std::nullopt;

                const char16_t neighbour = at_start ? x.at(x.index - 1) : x.at(x.index);

                return is_line_terminator(neighbour) ? c(x) : std::nullopt;
            };
        }

        Matcher match_word_boundary(const bool negated) {
            return [negated](const State &x, const Continuation &c) -> Result {
                const auto word_at = [&x](const int position) {
                    if (position < 0 || position >= x.length()) return false;

                    return is_word(x.at(position));
                };

                const bool boundary = word_at(x.index - 1) != word_at(x.index);

                return boundary != negated ? c(x) : std::nullopt;
            };
        }

        Matcher match_lookahead(Matcher inner, const bool negated) {
            return [inner, negated](const State &x, const Continuation &c) -> Result {
                const Continuation identity = [](const State &y) -> Result { return y; };

                Result found = inner(x, identity);

                if (negated) return found.has_value() ? std::nullopt : c(x);
                if (!found.has_value()) return std::nullopt;

                State y = x;
                y.starts = found->starts;
                y.ends = found->ends;

                return c(y);
            };
        }

        Matcher match_backreference(const int group, const bool ignore_case) {
            return [group, ignore_case](const State &x, const Continuation &c) -> Result {
                const int start = x.starts[group];

                if (start < 0) return c(x);

                const int length = x.ends[group] - start;
                if (x.index + length > x.length()) return std::nullopt;

                for (int i = 0; i < length; ++i) {
                    if (canonicalize(x.at(start + i), ignore_case)
                        != canonicalize(x.at(x.index + i), ignore_case)) {
                        return std::nullopt;
                    }
                }

                State y = x;
                y.index = x.index + length;

                return c(y);
            };
        }

        Matcher match_capture(const int group, Matcher inner) {
            return [group, inner](const State &x, const Continuation &c) -> Result {
                const Continuation close = [&](const State &y) -> Result {
                    State z = y;
                    z.starts[group] = x.index;
                    z.ends[group] = y.index;

                    return c(z);
                };

                return inner(x, close);
            };
        }

        Matcher match_sequence(Matcher first, Matcher second) {
            return [first, second](const State &x, const Continuation &c) -> Result {
                if (StackGuard::exhausted()) overflow();

                const Continuation rest = [&](const State &y) -> Result { return second(y, c); };

                return first(x, rest);
            };
        }

        Matcher match_alternative(Matcher first, Matcher second) {
            return [first, second](const State &x, const Continuation &c) -> Result {
                if (Result taken = first(x, c); taken.has_value()) return taken;

                return second(x, c);
            };
        }

        Matcher match_repeat(Matcher inner, const int min, const int max, const bool greedy,
                             const int paren_index, const int paren_count) {
            return [inner, min, max, greedy, paren_index, paren_count]
                   (const State &x, const Continuation &c) -> Result {
                if (StackGuard::exhausted()) overflow();

                if (max == 0) return c(x);

                const int start_index = x.index;

                const Continuation again = [&](const State &y) -> Result {
                    if (min == 0 && y.index == start_index) return std::nullopt;

                    const int next_min = min == 0 ? 0 : min - 1;
                    const int next_max = max == INFINITE ? INFINITE : max - 1;

                    return match_repeat(inner, next_min, next_max, greedy,
                                        paren_index, paren_count)(y, c);
                };

                State cleared = x;
                for (int i = paren_index + 1; i <= paren_index + paren_count; ++i) {
                    cleared.starts[i] = -1;
                    cleared.ends[i] = -1;
                }

                if (min != 0) return inner(cleared, again);

                if (!greedy) {
                    if (Result skipped = c(x); skipped.has_value()) return skipped;

                    return inner(cleared, again);
                }

                if (Result taken = inner(cleared, again); taken.has_value()) return taken;

                return c(x);
            };
        }
    }

    struct RegExpEngine::Impl {
        std::u16string source;
        bool ignore_case;
        bool multiline;
        int total_captures = 0;
        int next_capture = 0;
        std::size_t at = 0;
        Matcher root;

        struct Piece {
            Matcher matcher;
            int paren_index = 0;
            int paren_count = 0;
        };

        Impl(std::u16string pattern, const bool ignore_case, const bool multiline)
            : source(std::move(pattern)), ignore_case(ignore_case), multiline(multiline) {}

        [[noreturn]] void fail(const char *message) const {
            throw std::runtime_error(std::string("Invalid regular expression: ") + message);
        }

        bool done() const { return at >= source.size(); }

        char16_t peek(const std::size_t shift = 0) const {
            return at + shift < source.size() ? source[at + shift] : u'\0';
        }

        bool take(const char16_t expected) {
            if (done() || source[at] != expected) return false;

            ++at;
            return true;
        }

        void count_captures() {
            bool in_class = false;

            for (std::size_t i = 0; i < source.size(); ++i) {
                const char16_t unit = source[i];

                if (unit == u'\\') { ++i; continue; }
                if (in_class) { if (unit == u']') in_class = false; continue; }
                if (unit == u'[') { in_class = true; continue; }

                if (unit == u'(' && !(i + 1 < source.size() && source[i + 1] == u'?')) {
                    ++total_captures;
                }
            }
        }

        int read_decimal() {
            int value = 0;
            while (!done() && is_digit(peek())) {
                value = value * 10 + (source[at++] - u'0');

                if (value > 1000000) fail("number too large");
            }

            return value;
        }

        int read_hex(const int count) {
            int value = 0;

            for (int i = 0; i < count; ++i) {
                if (done()) return -1;

                const char16_t unit = peek();
                int digit;

                if (unit >= u'0' && unit <= u'9')      digit = unit - u'0';
                else if (unit >= u'a' && unit <= u'f') digit = unit - u'a' + 10;
                else if (unit >= u'A' && unit <= u'F') digit = unit - u'A' + 10;
                else return -1;

                value = value * 16 + digit;
                ++at;
            }

            return value;
        }

        char16_t character_escape() {
            if (done()) fail("\\ at end of pattern");

            const char16_t escape = source[at++];

            switch (escape) {
                case u'f': return u'\f';
                case u'n': return u'\n';
                case u'r': return u'\r';
                case u't': return u'\t';
                case u'v': return u'\v';

                case u'c': {
                    if (done()) return u'c';

                    const char16_t letter = peek();
                    if (!((letter >= u'a' && letter <= u'z') || (letter >= u'A' && letter <= u'Z'))) {
                        return u'c';
                    }

                    ++at;
                    return static_cast<char16_t>(letter % 32);
                }

                case u'x': {
                    const int value = read_hex(2);
                    return value < 0 ? u'x' : static_cast<char16_t>(value);
                }

                case u'u': {
                    const int value = read_hex(4);
                    return value < 0 ? u'u' : static_cast<char16_t>(value);
                }

                default: return escape;
            }
        }

        CharSet parse_class_ranges() {
            CharSet set;
            set.negated = take(u'^');

            std::vector<std::pair<bool, char16_t>> atoms;   // czy pojedynczy znak??
            std::vector<CharSet> sets;

            while (true) {
                if (done()) fail("unterminated character class");
                if (peek() == u']') { ++at; break; }

                if (peek() == u'\\'
                    && (peek(1) == u'd' || peek(1) == u'D' || peek(1) == u's'
                        || peek(1) == u'S' || peek(1) == u'w' || peek(1) == u'W')) {
                    const char16_t letter = peek(1);
                    at += 2;

                    atoms.emplace_back(false, static_cast<char16_t>(sets.size()));
                    sets.push_back(class_escape(letter));
                    continue;
                }

                char16_t single;

                if (take(u'\\')) {
                    if (!done() && peek() == u'b') { ++at; single = u'\b'; }
                    else single = character_escape();
                } else {
                    single = source[at++];
                }

                atoms.emplace_back(true, single);
            }

            for (std::size_t i = 0; i < atoms.size(); ++i) {
                const bool single = atoms[i].first;

                if (!single) {
                    const CharSet &nested = sets[atoms[i].second];

                    if (nested.negated) set.add_all(nested.complement());
                    else set.add_all(nested.ranges);

                    continue;
                }

                const bool is_range = i + 2 < atoms.size()
                                   && atoms[i + 1].first && atoms[i + 1].second == u'-'
                                   && atoms[i + 2].first;

                if (is_range) {
                    const char16_t from = atoms[i].second;
                    const char16_t to = atoms[i + 2].second;

                    if (from > to) fail("range out of order in character class");

                    set.add(from, to);
                    i += 2;
                    continue;
                }

                set.add(atoms[i].second);
            }

            return set;
        }

        Piece parse_atom() {
            Piece piece;
            piece.paren_index = next_capture;

            const char16_t unit = peek();

            if (unit == u'.') {
                ++at;

                CharSet set;
                for (const char16_t terminator : {u'\n', u'\r', static_cast<char16_t>(0x2028),
                                                  static_cast<char16_t>(0x2029)}) {
                    set.add(terminator);
                }
                set.negated = true;

                piece.matcher = match_set(set, ignore_case);
            } else if (unit == u'[') {
                ++at;
                piece.matcher = match_set(parse_class_ranges(), ignore_case);
            } else if (unit == u'(') {
                ++at;

                if (take(u'?')) {
                    if (take(u':')) {
                        piece.matcher = parse_disjunction();
                    } else if (take(u'=')) {
                        piece.matcher = match_lookahead(parse_disjunction(), false);
                    } else if (take(u'!')) {
                        piece.matcher = match_lookahead(parse_disjunction(), true);
                    } else {
                        fail("invalid group");
                    }
                } else {
                    const int group = ++next_capture;
                    piece.matcher = match_capture(group, parse_disjunction());
                }

                if (!take(u')')) fail("unterminated group");
            } else if (unit == u'\\') {
                ++at;
                piece.matcher = parse_atom_escape();
            } else {
                if (unit == u'*' || unit == u'+' || unit == u'?' || unit == u')' || unit == u'|') {
                    fail("nothing to repeat");
                }

                ++at;

                CharSet set;
                set.add(unit);
                piece.matcher = match_set(set, ignore_case);
            }

            piece.paren_count = next_capture - piece.paren_index;
            return piece;
        }

        Matcher parse_atom_escape() {
            if (done()) fail("\\ at end of pattern");

            const char16_t unit = peek();

            if (is_digit(unit)) {
                if (unit == u'0' && !is_digit(peek(1))) {
                    ++at;

                    CharSet set;
                    set.add(u'\0');
                    return match_set(set, ignore_case);
                }

                const int group = read_decimal();
                if (group == 0 || group > total_captures) fail("invalid backreference");

                return match_backreference(group, ignore_case);
            }

            if (unit == u'b' || unit == u'B') {
                ++at;
                return match_word_boundary(unit == u'B');
            }

            if (unit == u'd' || unit == u'D' || unit == u's'
                || unit == u'S' || unit == u'w' || unit == u'W') {
                ++at;
                return match_set(class_escape(unit), ignore_case);
            }

            CharSet set;
            set.add(character_escape());

            return match_set(set, ignore_case);
        }

        bool parse_quantifier(int &min, int &max, bool &greedy) {
            const std::size_t start = at;

            if (take(u'*'))      { min = 0; max = INFINITE; }
            else if (take(u'+')) { min = 1; max = INFINITE; }
            else if (take(u'?')) { min = 0; max = 1; }
            else if (peek() == u'{') {
                ++at;

                if (!is_digit(peek())) { at = start; return false; }

                min = read_decimal();
                max = min;

                if (take(u',')) {
                    if (peek() == u'}') max = INFINITE;
                    else if (is_digit(peek())) max = read_decimal();
                    else { at = start; return false; }
                }

                if (!take(u'}')) { at = start; return false; }
                if (max != INFINITE && min > max) fail("numbers out of order in quantifier");
            } else {
                return false;
            }

            greedy = !take(u'?');
            return true;
        }

        Matcher parse_term() {
            const char16_t unit = peek();

            if (unit == u'^') { ++at; return match_edge(true, multiline); }
            if (unit == u'$') { ++at; return match_edge(false, multiline); }

            if (unit == u'\\' && (peek(1) == u'b' || peek(1) == u'B')) {
                const bool negated = peek(1) == u'B';
                at += 2;

                return match_word_boundary(negated);
            }

            const Piece piece = parse_atom();

            int min = 0;
            int max = 0;
            bool greedy = true;

            if (!parse_quantifier(min, max, greedy)) return piece.matcher;

            return match_repeat(piece.matcher, min, max, greedy,
                                piece.paren_index, piece.paren_count);
        }

        Matcher parse_alternative() {
            Matcher result = match_empty();
            bool empty = true;

            while (!done() && peek() != u'|' && peek() != u')') {
                Matcher term = parse_term();

                result = empty ? term : match_sequence(result, term);
                empty = false;
            }

            return result;
        }

        Matcher parse_disjunction() {
            Matcher result = parse_alternative();

            while (take(u'|')) {
                result = match_alternative(result, parse_alternative());
            }

            return result;
        }

        void parse() {
            count_captures();

            root = parse_disjunction();

            if (!done()) fail("unmatched ')'");
        }
    };

    RegExpEngine::RegExpEngine(const std::u16string &pattern,
                               const bool ignore_case, const bool multiline)
        : impl(std::make_unique<Impl>(pattern, ignore_case, multiline)) {
        impl->parse();
        captures = impl->total_captures;
    }

    RegExpEngine::~RegExpEngine() = default;

    bool RegExpEngine::match_at(const std::u16string &input, const int at, RegExpMatch &out) const {
        if (at < 0 || at > static_cast<int>(input.size())) return false;

        StackGuard::limit() = native_stack_limit();

        State start;
        start.input = &input;
        start.index = at;
        start.starts.assign(captures + 1, -1);
        start.ends.assign(captures + 1, -1);

        const Continuation identity = [](const State &y) -> Result { return y; };

        Result found = impl->root(start, identity);
        if (!found.has_value()) return false;

        out.starts = found->starts;
        out.ends = found->ends;
        out.starts[0] = at;
        out.ends[0] = found->index;

        return true;
    }
}