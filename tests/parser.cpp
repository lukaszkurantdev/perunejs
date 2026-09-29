#include <doctest/doctest.h>

#include "lexer/lexer.h"
#include "parser.h"
#include "utils/utf.h"

#include <sstream>
#include <string>

using namespace perunejs;

// ---------------------------------------------------------------------------
// Helpery
//
// shape() serializuje AST do s-wyrażeń, żeby test opisywał KSZTAŁT drzewa,
// a nie tylko fakt, że parser nie rzucił. Celowo nie używamy AstPrinter —
// testy nie powinny padać przez zmianę formatu debugowego wypisywania.
//
// Konwencja: operator unarny dostaje prefiks "u", żeby "-" prefiksowe
// odróżnić od "-" binarnego.
// ---------------------------------------------------------------------------

static std::string shape(const Node& node);

static std::string shape_or_nil(const Node* node) {
    return node ? shape(*node) : "nil";
}

static std::string number_to_text(double value) {
    std::ostringstream out;
    out << value;
    return out.str();
}

static std::string shape(const Node& node) {
    std::ostringstream out;

    switch (node.kind) {
        case NodeKind::NumberLiteral:
            return number_to_text(static_cast<const NumberLiteral&>(node).value);
        case NodeKind::StringLiteral:
            return "\"" + utf16_to_utf8(static_cast<const StringLiteral&>(node).value) + "\"";
        case NodeKind::BooleanLiteral:
            return static_cast<const BooleanLiteral&>(node).value ? "true" : "false";
        case NodeKind::NullLiteral:
            return "null";
        case NodeKind::Identifier:
            return static_cast<const IdentifierLiteral&>(node).name;

        case NodeKind::UnaryExpression: {
            const auto& n = static_cast<const UnaryExpression&>(node);
            return "(u" + std::string(to_string(n.op)) + " " + shape(*n.operand) + ")";
        }
        case NodeKind::BinaryExpression: {
            const auto& n = static_cast<const BinaryExpression&>(node);
            return "(" + std::string(to_string(n.op)) + " "
                 + shape(*n.left) + " " + shape(*n.right) + ")";
        }
        case NodeKind::UpdateExpression: {
            const auto& n = static_cast<const UpdateExpression&>(node);
            const std::string op = n.op == UpdateOperator::Inc ? "++" : "--";
            // prefiks: (++a), postfiks: (a++)
            return n.prefix ? "(" + op + shape(*n.operand) + ")"
                            : "(" + shape(*n.operand) + op + ")";
        }
        case NodeKind::SequenceExpression: {
            out << "(seq";
            for (const auto& e : static_cast<const SequenceExpression&>(node).expressions)
                out << " " << shape(*e);
            out << ")";
            return out.str();
        }
        case NodeKind::ConditionalExpression: {
            const auto& n = static_cast<const ConditionalExpression&>(node);
            return "(?: " + shape(*n.test) + " " + shape(*n.consequent)
                 + " " + shape(*n.alternate) + ")";
        }
        case NodeKind::AssignExpression: {
            const auto& n = static_cast<const AssignExpression&>(node);
            return "(" + std::string(to_string(n.op)) + " "
                 + shape(*n.target) + " " + shape(*n.value) + ")";
        }

        case NodeKind::ExpressionStatement:
            return shape(*static_cast<const ExpressionStatement&>(node).expr);
        case NodeKind::EmptyStatement:
            return "(empty)";

        case NodeKind::VariableDeclaration: {
            const auto& n = static_cast<const VariableDeclaration&>(node);
            out << "(" << to_string(n.decl);
            for (const auto& d : n.declarators) {
                out << " (" << d.id->name;
                if (d.init) out << " " << shape(*d.init);
                out << ")";
            }
            out << ")";
            return out.str();
        }

        case NodeKind::CallExpression: {
            const auto& n = static_cast<const CallExpression&>(node);
            out << "(call " << shape(*n.callee);
            for (const auto& a : n.args) out << " " << shape(*a);
            out << ")";
            return out.str();
        }
        case NodeKind::ReturnStatement: {
            const auto& n = static_cast<const ReturnStatement&>(node);
            return n.argument ? "(return " + shape(*n.argument) + ")" : "(return)";
        }
        // deklaracja: (function nazwa (parametry) ciało)
        // wyrażenie:  (fn nazwa-albo-nil (parametry) ciało)
        case NodeKind::FunctionDeclaration: {
            const auto& n = static_cast<const FunctionDeclaration&>(node);
            out << "(function " << (n.name.empty() ? "nil" : n.name) << " (";
            for (std::size_t i = 0; i < n.params.size(); ++i) {
                if (i > 0) out << " ";
                out << n.params[i]->name;
            }
            out << ") " << shape(*n.body) << ")";
            return out.str();
        }
        case NodeKind::FunctionExpression: {
            const auto& n = static_cast<const FunctionExpression&>(node);
            out << "(fn " << (n.name.empty() ? "nil" : n.name) << " (";
            for (std::size_t i = 0; i < n.params.size(); ++i) {
                if (i > 0) out << " ";
                out << n.params[i]->name;
            }
            out << ") " << shape(*n.body) << ")";
            return out.str();
        }

        case NodeKind::Program: {
            out << "(program";
            for (const auto& s : static_cast<const Program&>(node).body) out << " " << shape(*s);
            out << ")";
            return out.str();
        }
        case NodeKind::BlockStatement: {
            out << "(block";
            for (const auto& s : static_cast<const BlockStatement&>(node).body) out << " " << shape(*s);
            out << ")";
            return out.str();
        }

        case NodeKind::IfStatement: {
            const auto& n = static_cast<const IfStatement&>(node);
            return "(if " + shape(*n.test) + " " + shape(*n.consequent)
                 + " " + shape_or_nil(n.alternate.get()) + ")";
        }
        case NodeKind::WhileStatement: {
            const auto& n = static_cast<const WhileStatement&>(node);
            return "(while " + shape(*n.test) + " " + shape(*n.body) + ")";
        }
        case NodeKind::DoWhileStatement: {
            const auto& n = static_cast<const DoWhileStatement&>(node);
            return "(do " + shape(*n.body) + " " + shape(*n.test) + ")";
        }
        case NodeKind::ForStatement: {
            const auto& n = static_cast<const ForStatement&>(node);
            return "(for " + shape_or_nil(n.init.get()) + " " + shape_or_nil(n.test.get())
                 + " " + shape_or_nil(n.update.get()) + " " + shape(*n.body) + ")";
        }

        case NodeKind::BreakStatement: {
            const auto& n = static_cast<const BreakStatement&>(node);
            return n.label.empty() ? "(break)" : "(break " + n.label + ")";
        }
        case NodeKind::ContinueStatement: {
            const auto& n = static_cast<const ContinueStatement&>(node);
            return n.label.empty() ? "(continue)" : "(continue " + n.label + ")";
        }
        case NodeKind::LabeledStatement: {
            const auto& n = static_cast<const LabeledStatement&>(node);
            return "(label " + n.label + " " + shape(*n.body) + ")";
        }

        case NodeKind::SwitchStatement: {
            const auto& n = static_cast<const SwitchStatement&>(node);
            out << "(switch " << shape(*n.discriminant);
            for (const auto& c : n.cases) {
                out << (c.test ? " (case " + shape(*c.test) : " (default");
                for (const auto& s : c.body) out << " " << shape(*s);
                out << ")";
            }
            out << ")";
            return out.str();
        }

        // Literał regexp zapisujemy tak, jak wygląda w źródle: /wzorzec/flagi
        case NodeKind::RegExpLiteral: {
            const auto& n = static_cast<const RegExpLiteral&>(node);
            return "/" + n.pattern + "/" + n.flags;
        }
        case NodeKind::DebuggerStatement:
            return "(debugger)";

        case NodeKind::WithStatement: {
            const auto& n = static_cast<const WithStatement&>(node);
            return "(with " + shape(*n.object) + " " + shape(*n.body) + ")";
        }
        default: break;
    }

    return "<?>";
}

// Parsuje źródło i zwraca kształt całego programu.
static std::string parse(const std::string& source) {
    Lexer lexer(source);
    Parser parser(lexer.scan_tokens());
    return shape(*parser.parse());
}

// ---------------------------------------------------------------------------
// Wyrażenia — priorytety i łączność (11.5, 11.6)
// ---------------------------------------------------------------------------

TEST_CASE("literały i identyfikatory") {
    CHECK(parse("1;")       == "(program 1)");
    CHECK(parse("2.5;")     == "(program 2.5)");
    CHECK(parse("abc;")     == "(program abc)");
    CHECK(parse("\"txt\";")  == "(program \"txt\")");
    CHECK(parse("\"\";")      == "(program \"\")");
    CHECK(parse("true;")    == "(program true)");
    CHECK(parse("false;")   == "(program false)");
    CHECK(parse("null;")    == "(program null)");
}

// 15.1.1.3 — "undefined" nie jest literałem, tylko właściwością obiektu
// globalnego, więc parser ma widzieć zwykły identyfikator.
TEST_CASE("undefined jest identyfikatorem, nie literałem") {
    CHECK(parse("undefined;") == "(program undefined)");
}

TEST_CASE("literały w wyrażeniach złożonych") {
    CHECK(parse("!true;")        == "(program (u! true))");
    CHECK(parse("\"a\" + \"b\";") == "(program (+ \"a\" \"b\"))");
    CHECK(parse("null + 1;")     == "(program (+ null 1))");
}

TEST_CASE("mnożenie wiąże mocniej niż dodawanie") {
    CHECK(parse("1 + 2 * 3;") == "(program (+ 1 (* 2 3)))");
    CHECK(parse("1 * 2 + 3;") == "(program (+ (* 1 2) 3))");
    CHECK(parse("1 + 2 % 3;") == "(program (+ 1 (% 2 3)))");
}

TEST_CASE("operatory binarne są lewostronnie łączne") {
    CHECK(parse("1 - 2 - 3;") == "(program (- (- 1 2) 3))");
    CHECK(parse("1 / 2 / 3;") == "(program (/ (/ 1 2) 3))");
}

TEST_CASE("nawiasy zmieniają priorytet") {
    CHECK(parse("(1 + 2) * 3;") == "(program (* (+ 1 2) 3))");
    CHECK(parse("((((1))));")   == "(program 1)");
}

TEST_CASE("operatory unarne (11.4)") {
    CHECK(parse("-1;")     == "(program (u- 1))");
    CHECK(parse("+x;")     == "(program (u+ x))");
    CHECK(parse("!x;")     == "(program (u! x))");
    // uwaga: "--" to token DECREMENT (maximal munch, 7.7), więc podwójna
    // negacja wymaga spacji — "--1" jest w JS błędem składni.
    CHECK(parse("- -1;")   == "(program (u- (u- 1)))");
    CHECK(parse("-1 + 2;") == "(program (+ (u- 1) 2))");
    CHECK(parse("-(1 + 2);") == "(program (u- (+ 1 2)))");
}

// 11.13 — przypisanie jest prawostronnie łączne
TEST_CASE("przypisanie jest prawostronnie łączne") {
    CHECK(parse("a = b = 1;") == "(program (= a (= b 1)))");
    CHECK(parse("a = 1 + 2;") == "(program (= a (+ 1 2)))");
}

TEST_CASE("przypisania złożone") {
    CHECK(parse("a += 1;") == "(program (+= a 1))");
    CHECK(parse("a -= 1;") == "(program (-= a 1))");
    CHECK(parse("a *= 1;") == "(program (*= a 1))");
    CHECK(parse("a /= 1;") == "(program (/= a 1))");
}

// ---------------------------------------------------------------------------
// Statementy (12.x)
// ---------------------------------------------------------------------------

TEST_CASE("program pusty i lista statementów") {
    CHECK(parse("")                  == "(program)");
    CHECK(parse(";")                 == "(program (empty))");
    CHECK(parse(";;;")               == "(program (empty) (empty) (empty))");
    CHECK(parse("a = 1; b = 2;")     == "(program (= a 1) (= b 2))");
}

TEST_CASE("blok (12.1)") {
    CHECK(parse("{}")            == "(program (block))");
    CHECK(parse("{ a = 1; }")    == "(program (block (= a 1)))");
    CHECK(parse("{ { a = 1; } }") == "(program (block (block (= a 1))))");
}

TEST_CASE("deklaracja zmiennych (12.2)") {
    CHECK(parse("var a;")            == "(program (var (a)))");
    CHECK(parse("var a = 1;")        == "(program (var (a 1)))");
    CHECK(parse("var a = 1, b;")     == "(program (var (a 1) (b)))");
    CHECK(parse("var a = 1, b = 2;") == "(program (var (a 1) (b 2)))");
}

TEST_CASE("if / else (12.5)") {
    CHECK(parse("if (a) b = 1;")            == "(program (if a (= b 1) nil))");
    CHECK(parse("if (a) b = 1; else c = 2;") == "(program (if a (= b 1) (= c 2)))");
}

// 12.5 — "else" wiąże się z najbliższym niedopasowanym "if"
TEST_CASE("dangling else wiąże się z bliższym if") {
    CHECK(parse("if (a) if (b) c = 1; else d = 2;")
          == "(program (if a (if b (= c 1) (= d 2)) nil))");
}

TEST_CASE("while i do-while (12.6)") {
    CHECK(parse("while (a) b = 1;")     == "(program (while a (= b 1)))");
    CHECK(parse("while (a) {}")         == "(program (while a (block)))");
    CHECK(parse("do b = 1; while (a);") == "(program (do (= b 1) a))");
    CHECK(parse("do b = 1; while (a)")  == "(program (do (= b 1) a))");  // ; opcjonalny
}

TEST_CASE("for — wszystkie części opcjonalne (12.6.3)") {
    CHECK(parse("for (;;) a = 1;")          == "(program (for nil nil nil (= a 1)))");
    CHECK(parse("for (var i = 0;;) a = 1;") == "(program (for (var (i 0)) nil nil (= a 1)))");
    CHECK(parse("for (; a ;) b = 1;")       == "(program (for nil a nil (= b 1)))");
    CHECK(parse("for (;; a = 1) b = 2;")    == "(program (for nil nil (= a 1) (= b 2)))");
    CHECK(parse("for (var i = 0; i; i = i - 1) {}")
          == "(program (for (var (i 0)) i (= i (- i 1)) (block)))");
}

// 12.6.3 + 11.8.7 — ograniczenie "NoIn" obowiazuje tylko w naglowku `for`.
// Wejscie w cialo funkcji otwiera nowa produkcje Expression, wiec `in` jest
// tam w pelni legalne. Regresja znaleziona na produkcyjnym bundlu React
// Native (modul core-js od RegExp: `for (var S = function () { ...
// 'dotAll' in l ... }, ...)`).
TEST_CASE("NoIn z naglowka for nie przecieka do ciala zagniezdzonej funkcji (12.6.3)") {
    CHECK_NOTHROW(parse("for (var f = function () { 'a' in b; };;) ;"));
    CHECK_NOTHROW(parse("for (var f = function () { return 'a' in b; };;) ;"));
    CHECK_NOTHROW(parse("for (f = function () { 'a' in b; };;) ;"));
    CHECK_NOTHROW(parse("for (var f = function () { for (var k in b) ; };;) ;"));

    // nawiasy rowniez otwieraja zwykle Expression
    CHECK_NOTHROW(parse("for (var x = ('a' in b);;) ;"));

    // ...ale samo ograniczenie w naglowku nadal obowiazuje
    CHECK_THROWS(parse("for (var x = 'a' in b;;) ;"));
}

TEST_CASE("break i etykiety (12.8, 12.12)") {
    CHECK(parse("while (a) { break; }")     == "(program (while a (block (break))))");
    CHECK(parse("foo: a = 1;")              == "(program (label foo (= a 1)))");
    CHECK(parse("a: b: c: x = 1;")          == "(program (label a (label b (label c (= x 1)))))");
    CHECK(parse("foo: while (a) { break foo; }")
          == "(program (label foo (while a (block (break foo)))))");
}

TEST_CASE("switch (12.11)") {
    CHECK(parse("switch (a) {}") == "(program (switch a))");
    CHECK(parse("switch (a) { case 1: b = 1; }")
          == "(program (switch a (case 1 (= b 1))))");
    CHECK(parse("switch (a) { case 1: default: b = 1; }")
          == "(program (switch a (case 1) (default (= b 1))))");
    CHECK(parse("switch (a) { case 1: case 2: b = 1; }")
          == "(program (switch a (case 1) (case 2 (= b 1))))");
}

// ---------------------------------------------------------------------------
// Odporność na wejście ucięte / niepoprawne
//
// Wymagamy błędu składni (runtime_error), a NIE std::out_of_range — ten
// ostatni oznaczałby wyjście kursora poza wektor tokenów. Testy pilnują też,
// że parser się nie zapętla: advance() zatrzymuje się na END_OF_FILE, więc
// każda pętla musi sama sprawdzać koniec wejścia.
// ---------------------------------------------------------------------------

static void check_syntax_error(const std::string& source) {
    INFO("źródło: " << source);
    CHECK_THROWS_AS(parse(source), std::runtime_error);
    CHECK_THROWS_AS(parse(source), std::exception);
    CHECK_NOTHROW([&] {
        try { parse(source); } catch (const std::out_of_range&) { FAIL("kursor wyszedł poza tokeny"); }
        catch (const std::runtime_error&) {}
    }());
}

TEST_CASE("wejście ucięte w połowie kończy się błędem składni") {
    check_syntax_error("{ a = 1;");
    check_syntax_error("switch (a) {");
    check_syntax_error("switch (a) { case 1:");
    check_syntax_error("var");
    check_syntax_error("var a =");
    check_syntax_error("if (");
    check_syntax_error("if (a)");
    check_syntax_error("while (a");
    check_syntax_error("for (;;");
    check_syntax_error("do a = 1;");
    check_syntax_error("(1 + 2");
    check_syntax_error("foo:");
}

TEST_CASE("niepoprawne wejście kończy się błędem składni") {
    check_syntax_error("1 + 2 )");
    check_syntax_error("}");
    check_syntax_error(")");
    check_syntax_error("*");
    check_syntax_error("1 +");
    check_syntax_error("switch (a) { case 1: default: default: }");  // 12.11: jeden default
}

// ---------------------------------------------------------------------------
// Automatic Semicolon Insertion (7.9)
//
// Reguła 1: średnik przed tokenem, którego gramatyka nie dopuszcza, jeśli
//           token jest oddzielony nową linią albo jest to "}".
// Reguła 2: średnik na końcu strumienia.
// Reguła 3: restricted productions - patrz testy niżej.
// ---------------------------------------------------------------------------

TEST_CASE("ASI na końcu strumienia (reguła 2)") {
    CHECK(parse("a = 1")      == "(program (= a 1))");
    CHECK(parse("var a = 1")  == "(program (var (a 1)))");
    CHECK(parse("while (a) break") == "(program (while a (break)))");
    CHECK(parse("1 + 2")      == "(program (+ 1 2))");
}

TEST_CASE("ASI przed zamykającym nawiasem klamrowym (reguła 1)") {
    CHECK(parse("{ a = 1 }")            == "(program (block (= a 1)))");
    CHECK(parse("while (a) { b = 1 }")  == "(program (while a (block (= b 1))))");
    CHECK(parse("{ a = 1; b = 2 }")     == "(program (block (= a 1) (= b 2)))");
}

TEST_CASE("ASI przed nową linią (reguła 1)") {
    CHECK(parse("a = 1\nb = 2;")  == "(program (= a 1) (= b 2))");
    CHECK(parse("a = 1\nb = 2")   == "(program (= a 1) (= b 2))");
    CHECK(parse("var a = 1\nvar b = 2\na + b;")
          == "(program (var (a 1)) (var (b 2)) (+ a b))");
}

// ASI działa tylko wtedy, gdy parser UTKNIE - sama nowa linia nie wystarczy,
// jeśli następny token jest gramatycznie dopuszczalny.
TEST_CASE("nowa linia nie przerywa wyrażenia, które może być kontynuowane") {
    CHECK(parse("a = b\n+c;")   == "(program (= a (+ b c)))");
    CHECK(parse("a = b\n*c;")   == "(program (= a (* b c)))");
    CHECK(parse("a\n== b;")     == "(program (== a b))");
}

TEST_CASE("bez nowej linii brakujący średnik nadal jest błędem") {
    check_syntax_error("a = 1 b = 2");
    check_syntax_error("var a = 1 var b = 2");
    check_syntax_error("1 + 2 )");
}

// Reguła 3 - restricted productions. Nowa linia rozdziela nawet wtedy,
// gdy następny token byłby gramatycznie dopuszczalny.
TEST_CASE("nowa linia rozdziela przy restricted productions (7.9.1)") {
    // "break foo" kontra "break; foo;"
    CHECK(parse("foo: while (a) { break foo; }")
          == "(program (label foo (while a (block (break foo)))))");
    CHECK(parse("while (a) { break\nfoo; }")
          == "(program (while a (block (break) foo)))");

    CHECK(parse("while (a) { continue\nfoo; }")
          == "(program (while a (block (continue) foo)))");

    // "a++" kontra "a; ++b"
    CHECK(parse("a++;")      == "(program (a++))");
    CHECK(parse("a\n++b;")  == "(program a (++b))");
}

// ---------------------------------------------------------------------------
// Operatory bitowe i przesunięcia (11.7, 11.10, 11.4.8)
//
// Priorytety, od najsłabiej wiążącego:
//     |   ^   &   << >> >>>   + -   * / %
// Każdy z |, ^, & to OSOBNE piętro - "a | b & c" znaczy "a | (b & c)".
// ---------------------------------------------------------------------------

TEST_CASE("operatory bitowe i przesunięcia parsują się") {
    CHECK(parse("1 | 2;")   == "(program (| 1 2))");
    CHECK(parse("1 ^ 2;")   == "(program (^ 1 2))");
    CHECK(parse("1 & 2;")   == "(program (& 1 2))");
    CHECK(parse("1 << 2;")  == "(program (<< 1 2))");
    CHECK(parse("1 >> 2;")  == "(program (>> 1 2))");
    CHECK(parse("1 >>> 2;") == "(program (>>> 1 2))");
    CHECK(parse("~1;")      == "(program (u~ 1))");
}

// Każdy przypadek sprawdza JEDNĄ granicę między sąsiednimi piętrami —
// podpięcie piętra w złej kolejności zapala dokładnie ten wiersz.
TEST_CASE("priorytety operatorów bitowych względem sąsiednich pięter") {
    CHECK(parse("1 | 2 ^ 3;")  == "(program (| 1 (^ 2 3)))");
    CHECK(parse("1 ^ 2 & 3;")  == "(program (^ 1 (& 2 3)))");
    CHECK(parse("1 & 2 << 3;") == "(program (& 1 (<< 2 3)))");
    CHECK(parse("1 << 2 + 3;") == "(program (<< 1 (+ 2 3)))");
    CHECK(parse("1 + 2 << 3;") == "(program (<< (+ 1 2) 3))");
}

TEST_CASE("operatory bitowe są lewostronnie łączne") {
    CHECK(parse("1 | 2 | 3;")    == "(program (| (| 1 2) 3))");
    CHECK(parse("1 ^ 2 ^ 3;")    == "(program (^ (^ 1 2) 3))");
    CHECK(parse("1 & 2 & 3;")    == "(program (& (& 1 2) 3))");
    CHECK(parse("1 << 2 << 3;")  == "(program (<< (<< 1 2) 3))");
    CHECK(parse("1 >> 2 >>> 3;") == "(program (>>> (>> 1 2) 3))");
}

TEST_CASE("nawiasy przełamują priorytety bitowe") {
    CHECK(parse("(1 | 2) ^ 3;")  == "(program (^ (| 1 2) 3))");
    CHECK(parse("(1 & 2) << 3;") == "(program (<< (& 1 2) 3))");
}

// 11.4.8 — "~" wiąże mocniej niż każdy operator binarny
TEST_CASE("~ jest operatorem unarnym i wiąże najmocniej") {
    CHECK(parse("~1 & 2;")   == "(program (& (u~ 1) 2))");
    CHECK(parse("~~1;")      == "(program (u~ (u~ 1)))");
    CHECK(parse("~(1 & 2);") == "(program (u~ (& 1 2)))");
    CHECK(parse("-~1;")      == "(program (u- (u~ 1)))");
}

// Wszystkie 12 form przypisania z ES5.1
TEST_CASE("przypisania złożone dla operatorów bitowych") {
    CHECK(parse("a %= 1;")   == "(program (%= a 1))");
    CHECK(parse("a &= 1;")   == "(program (&= a 1))");
    CHECK(parse("a |= 1;")   == "(program (|= a 1))");
    CHECK(parse("a ^= 1;")   == "(program (^= a 1))");
    CHECK(parse("a <<= 1;")  == "(program (<<= a 1))");
    CHECK(parse("a >>= 1;")  == "(program (>>= a 1))");
    CHECK(parse("a >>>= 1;") == "(program (>>>= a 1))");
}

TEST_CASE("przypisania bitowe są prawostronnie łączne") {
    CHECK(parse("a |= b |= 1;") == "(program (|= a (|= b 1)))");
}

// ---------------------------------------------------------------------------
// Operatory logiczne, równość i operator warunkowy (11.9, 11.11, 11.12)
// ---------------------------------------------------------------------------

TEST_CASE("operatory logiczne i równości parsują się") {
    CHECK(parse("a || b;")  == "(program (|| a b))");
    CHECK(parse("a && b;")  == "(program (&& a b))");
    CHECK(parse("a == b;")  == "(program (== a b))");
    CHECK(parse("a != b;")  == "(program (!= a b))");
    CHECK(parse("a === b;") == "(program (=== a b))");
    CHECK(parse("a !== b;") == "(program (!== a b))");
}

TEST_CASE("|| jest słabsze niż &&") {
    CHECK(parse("a || b && c;") == "(program (|| a (&& b c)))");
    CHECK(parse("a && b || c;") == "(program (|| (&& a b) c))");
}

TEST_CASE("operatory logiczne są lewostronnie łączne") {
    CHECK(parse("a || b || c;") == "(program (|| (|| a b) c))");
    CHECK(parse("a && b && c;") == "(program (&& (&& a b) c))");
}

// 11.9 wiąże MOCNIEJ niż 11.10 — klasyczna pułapka JS:
// "a & b == c" znaczy "a & (b == c)", a nie "(a & b) == c".
TEST_CASE("równość wiąże mocniej niż operatory bitowe") {
    CHECK(parse("a & b == c;")  == "(program (& a (== b c)))");
    CHECK(parse("a | b == c;")  == "(program (| a (== b c)))");
    CHECK(parse("a && b | c;")  == "(program (&& a (| b c)))");
}

TEST_CASE("równość wiąże słabiej niż przesunięcia i arytmetyka") {
    CHECK(parse("a == b << c;") == "(program (== a (<< b c)))");
    CHECK(parse("a == b + c;")  == "(program (== a (+ b c)))");
}

// 11.12 — gałęzie to AssignmentExpression, więc "?:" wychodzi
// prawostronnie łączny bez osobnej reguły.
TEST_CASE("operator warunkowy") {
    CHECK(parse("a ? b : c;")         == "(program (?: a b c))");
    CHECK(parse("a ? b : c ? d : e;") == "(program (?: a b (?: c d e)))");
    CHECK(parse("a ? b ? c : d : e;") == "(program (?: a (?: b c d) e))");
}

TEST_CASE("operator warunkowy wiąże najsłabiej z operatorów") {
    CHECK(parse("a || b ? c : d;") == "(program (?: (|| a b) c d))");
    CHECK(parse("a == b ? c : d;") == "(program (?: (== a b) c d))");
    CHECK(parse("a ? b : c;")      == "(program (?: a b c))");
}

// Gałąź "consequent" to pełne AssignmentExpression (11.12)
TEST_CASE("w gałęziach operatora warunkowego mieści się przypisanie") {
    CHECK(parse("a ? b = 1 : c = 2;") == "(program (?: a (= b 1) (= c 2)))");
}

TEST_CASE("operator warunkowy wiąże słabiej niż przypisanie po prawej") {
    CHECK(parse("a = b ? c : d;") == "(program (= a (?: b c d)))");
}

TEST_CASE("operatory relacyjne (11.8)") {
    CHECK(parse("a < b;")  == "(program (< a b))");
    CHECK(parse("a > b;")  == "(program (> a b))");
    CHECK(parse("a <= b;") == "(program (<= a b))");
    CHECK(parse("a >= b;") == "(program (>= a b))");
}

// Relacje wiążą MOCNIEJ niż równość, a SŁABIEJ niż przesunięcia
TEST_CASE("priorytety operatorów relacyjnych") {
    CHECK(parse("a == b < c;")  == "(program (== a (< b c)))");
    CHECK(parse("a < b << c;")  == "(program (< a (<< b c)))");
    CHECK(parse("a < b + c;")   == "(program (< a (+ b c)))");
    CHECK(parse("a & b < c;")   == "(program (& a (< b c)))");
}

TEST_CASE("relacje są lewostronnie łączne") {
    CHECK(parse("a < b < c;") == "(program (< (< a b) c))");
}

// ---------------------------------------------------------------------------
// Operator przecinka (11.14)
//
// Najsłabszy operator w języku - słabszy nawet od przypisania.
// Uwaga: przecinek oddzielający deklaratory ("var a = 1, b = 2") to NIE
// ten operator, tylko separator; dlatego deklaratory wołają
// parse_assignment, a nie parse_expression.
// ---------------------------------------------------------------------------

TEST_CASE("operator przecinka tworzy sekwencję") {
    CHECK(parse("1, 2;")    == "(program (seq 1 2))");
    CHECK(parse("1, 2, 3;") == "(program (seq 1 2 3))");
}

// Pojedyncze wyrażenie NIE jest opakowywane w sekwencję
TEST_CASE("bez przecinka nie powstaje węzeł sekwencji") {
    CHECK(parse("1;")     == "(program 1)");
    CHECK(parse("1 + 2;") == "(program (+ 1 2))");
}

TEST_CASE("przecinek wiąże słabiej niż przypisanie") {
    CHECK(parse("a = 1, 2;")     == "(program (seq (= a 1) 2))");
    CHECK(parse("a = 1, b = 2;") == "(program (seq (= a 1) (= b 2)))");
}

TEST_CASE("przecinek w deklaracji zmiennych to separator, nie operator") {
    CHECK(parse("var a = 1, b = 2;") == "(program (var (a 1) (b 2)))");
    CHECK(parse("var a = (1, 2);")   == "(program (var (a (seq 1 2))))");
}

TEST_CASE("nawiasy pozwalają użyć przecinka w podwyrażeniu") {
    CHECK(parse("(1, 2) + 3;")  == "(program (+ (seq 1 2) 3))");
    CHECK(parse("1 + (2, 3);")  == "(program (+ 1 (seq 2 3)))");
}

TEST_CASE("przecinek działa w nagłówku for i w warunkach") {
    CHECK(parse("for (i = 0, j = 1; ; ) {}")
          == "(program (for (seq (= i 0) (= j 1)) nil nil (block)))");
    CHECK(parse("for (;; i = 1, j = 2) {}")
          == "(program (for nil nil (seq (= i 1) (= j 2)) (block)))");
    CHECK(parse("if (a, b) c;") == "(program (if (seq a b) c nil))");
}

// Gałęzie "?:" to AssignmentExpression, więc przecinka tam nie ma
TEST_CASE("przecinek nie wchodzi w gałęzie operatora warunkowego") {
    CHECK(parse("a ? b : c, d;") == "(program (seq (?: a b c) d))");
}

// ---------------------------------------------------------------------------
// return (12.9)
//
// Pozostałe przypadki — `return;`, `return 1;`, `return 1, 2;`, ASI po
// `return` i `return` tuż przed `}` — wymagają ciała funkcji, więc dojdą
// razem z parsowaniem deklaracji funkcji.
// ---------------------------------------------------------------------------

TEST_CASE("return poza ciałem funkcji jest błędem składni") {
    check_syntax_error("return;");
    check_syntax_error("return 1;");
    check_syntax_error("{ return 1; }");
    check_syntax_error("if (a) return 1;");
    check_syntax_error("while (a) { return 1; }");
    check_syntax_error("for (;;) { return; }");
}

// ---------------------------------------------------------------------------
// Funkcje (13) i wywołania (11.2)
// ---------------------------------------------------------------------------

TEST_CASE("deklaracja funkcji: nazwa, parametry, ciało") {
    CHECK(parse("function f() {}")          == "(program (function f () (block)))");
    CHECK(parse("function f(a) {}")         == "(program (function f (a) (block)))");
    CHECK(parse("function f(a, b, c) {}")   == "(program (function f (a b c) (block)))");
    CHECK(parse("function f(a) { a; }")     == "(program (function f (a) (block a)))");
}

// 13 — deklaracja bez nazwy nie należy do gramatyki
TEST_CASE("deklaracja funkcji musi mieć nazwę") {
    check_syntax_error("function () {}");
    check_syntax_error("function (a) {}");
}

// 13.1 — FunctionBody to zawsze blok
TEST_CASE("ciałem funkcji może być tylko blok") {
    check_syntax_error("function f() 1;");
    check_syntax_error("function f();");
}

// W ES5.1 wiszący przecinek w liście parametrów jest błędem (dopuszcza go dopiero ES2017)
TEST_CASE("lista parametrów nie przyjmuje wiszącego przecinka ani śmieci") {
    check_syntax_error("function f(a,) {}");
    check_syntax_error("function f(,) {}");
    check_syntax_error("function f(a b) {}");
    check_syntax_error("function f(1) {}");
    check_syntax_error("function f(a {}");
}

TEST_CASE("wyrażenie funkcyjne bywa anonimowe i nazwane") {
    CHECK(parse("var g = function () {};")  == "(program (var (g (fn nil () (block)))))");
    CHECK(parse("var g = function h() {};") == "(program (var (g (fn h () (block)))))");
    CHECK(parse("(function (a) {});")       == "(program (fn nil (a) (block)))");
}

// ---------- wywołania (11.2.3, lista argumentów 11.2.4) ----------

TEST_CASE("wywołanie z pustą i niepustą listą argumentów") {
    CHECK(parse("f();")         == "(program (call f))");
    CHECK(parse("f(1);")        == "(program (call f 1))");
    CHECK(parse("f(1, 2, 3);")  == "(program (call f 1 2 3))");
}

// Przecinek w liście argumentów to separator, a nie operator sekwencji (11.14).
// Sekwencję da się przekazać tylko w nawiasach.
TEST_CASE("przecinek w argumentach rozdziela argumenty") {
    CHECK(parse("f(a, b);")     == "(program (call f a b))");
    CHECK(parse("f((a, b));")   == "(program (call f (seq a b)))");
}

// Argument to AssignmentExpression, więc przypisanie i "?:" są w nim legalne
TEST_CASE("argument jest wyrażeniem przypisania") {
    CHECK(parse("f(a = 1);")        == "(program (call f (= a 1)))");
    CHECK(parse("f(a ? b : c, d);") == "(program (call f (?: a b c) d))");
}

TEST_CASE("wywołania składają się w lewo") {
    CHECK(parse("f()();")       == "(program (call (call f)))");
    CHECK(parse("f(1)(2);")     == "(program (call (call f 1) 2))");
    CHECK(parse("f()()();")     == "(program (call (call (call f))))");
}

TEST_CASE("natychmiastowe wywołanie wyrażenia funkcyjnego") {
    CHECK(parse("(function () {})();")      == "(program (call (fn nil () (block))))");
    CHECK(parse("(function (a) {})(1);")    == "(program (call (fn nil (a) (block)) 1))");
}

TEST_CASE("wywołanie wiąże mocniej niż operatory") {
    CHECK(parse("f() + 1;")     == "(program (+ (call f) 1))");
    CHECK(parse("-f();")        == "(program (u- (call f)))");
    CHECK(parse("typeof f();")  == "(program (utypeof (call f)))");
    CHECK(parse("f() * g();")   == "(program (* (call f) (call g)))");
    CHECK(parse("a = f(1);")    == "(program (= a (call f 1)))");
}

// 11.3.1 — postfiks stosuje się do LeftHandSideExpression, więc musi widzieć wywołanie
TEST_CASE("postfiks stosuje się do wyrażenia lewostronnego") {
    CHECK(parse("f()++;")       == "(program ((call f)++))");
}

TEST_CASE("niedomknięta lista argumentów jest błędem") {
    check_syntax_error("f(1;");
    check_syntax_error("f(1,);");
    check_syntax_error("f(,);");
}

// ---------- return w ciele funkcji (12.9) ----------

TEST_CASE("return z wyrażeniem i bez") {
    CHECK(parse("function f() { return; }")
          == "(program (function f () (block (return))))");
    CHECK(parse("function f() { return 1; }")
          == "(program (function f () (block (return 1))))");
    CHECK(parse("function f() { return a + b; }")
          == "(program (function f () (block (return (+ a b)))))");
}

// return przyjmuje pełne Expression, więc przecinek jest tu operatorem sekwencji
TEST_CASE("return przyjmuje wyrażenie z przecinkiem") {
    CHECK(parse("function f() { return 1, 2; }")
          == "(program (function f () (block (return (seq 1 2)))))");
}

TEST_CASE("return tuż przed klamrą zamykającą nie wymaga średnika") {
    CHECK(parse("function f() { return }")
          == "(program (function f () (block (return))))");
}

// 7.9.1 — restricted production: koniec linii po `return` kończy instrukcję
TEST_CASE("nowa linia po return odcina wyrażenie") {
    CHECK(parse("function f() { return\n1; }")
          == "(program (function f () (block (return) 1)))");
    // bez końca linii wyrażenie należy do return
    CHECK(parse("function f() { return 1\n; }")
          == "(program (function f () (block (return 1))))");
}

// 7.9.1 — po `return` z końcem linii średnik jest WSTAWIANY automatycznie
// w tym miejscu, więc jawny `;` z następnej linii należy już do kolejnej
// instrukcji, a nie do return.
TEST_CASE("jawny średnik po return z nową linią jest osobną instrukcją pustą") {
    CHECK(parse("function f() { return\n; }")
          == "(program (function f () (block (return) (empty))))");
}

TEST_CASE("return działa w zagnieżdżeniu wewnątrz funkcji") {
    CHECK(parse("function f() { if (a) { return 1; } }")
          == "(program (function f () (block (if a (block (return 1)) nil))))");
    CHECK(parse("function f() { while (a) return; }")
          == "(program (function f () (block (while a (return)))))");
}

TEST_CASE("funkcja zagnieżdżona w funkcji ma własne return") {
    CHECK(parse("function f() { function g() { return 1; } return 2; }")
          == "(program (function f () (block (function g () (block (return 1))) (return 2))))");
    CHECK(parse("function f() { var h = function () { return 1; }; }")
          == "(program (function f () (block (var (h (fn nil () (block (return 1))))))))");
}

// Licznik zagnieżdżenia musi wracać do zera po wyjściu z ciała funkcji
TEST_CASE("return po zamknięciu funkcji znowu jest błędem") {
    check_syntax_error("function f() { return; } return;");
    check_syntax_error("var g = function () { return; }; return;");
}

// ---------------------------------------------------------------------------
// continue (12.9)
//
// W pliku był dotąd tylko `break` — `continue` miał obsługę w shape(),
// ale ani jednego przypadku testowego.
// ---------------------------------------------------------------------------

TEST_CASE("continue bez etykiety i z etykietą") {
    CHECK(parse("while (a) continue;")     == "(program (while a (continue)))");
    CHECK(parse("while (a) { continue; }") == "(program (while a (block (continue))))");
    CHECK(parse("foo: while (a) continue foo;")
          == "(program (label foo (while a (continue foo))))");
    CHECK(parse("for (;;) continue;")      == "(program (for nil nil nil (continue)))");
}

TEST_CASE("etykieta continue nie przechodzi przez nową linię (7.9.1)") {
    // `continue \n foo;` to `continue;` a potem osobna instrukcja `foo;`
    CHECK(parse("while (a) { continue\nfoo; }")
          == "(program (while a (block (continue) foo)))");
}

TEST_CASE("continue i break w tej samej pętli") {
    CHECK(parse("while (a) { if (b) continue; break; }")
          == "(program (while a (block (if b (continue) nil) (break))))");
}

// ---------------------------------------------------------------------------
// Inkrementacja przedrostkowa (11.4.4, 11.4.5)
//
// Testowany był tylko postfiks (`f()++`). Prefiks to osobna produkcja
// gramatyki i osobna gałąź w parserze.
// ---------------------------------------------------------------------------

TEST_CASE("prefiks ++ i --") {
    CHECK(parse("++a;")  == "(program (++a))");
    CHECK(parse("--a;")  == "(program (--a))");
    CHECK(parse("++f();") == "(program (++(call f)))");
}

TEST_CASE("prefiks i postfiks w jednym wyrażeniu") {
    CHECK(parse("a++ + ++b;") == "(program (+ (a++) (++b)))");
    CHECK(parse("++a + b++;") == "(program (+ (++a) (b++)))");
    CHECK(parse("a-- - --b;") == "(program (- (a--) (--b)))");
}

TEST_CASE("prefiks wiąże się z operatorem unarnym") {
    CHECK(parse("-++a;") == "(program (u- (++a)))");
    CHECK(parse("!--a;") == "(program (u! (--a)))");
}

TEST_CASE("inkrementacja w nagłówku for") {
    CHECK(parse("for (i = 0; i < 10; i++) a;")
          == "(program (for (= i 0) (< i 10) (i++) a))");
    CHECK(parse("for (i = 0; i < 10; ++i) a;")
          == "(program (for (= i 0) (< i 10) (++i) a))");
}

// ---------------------------------------------------------------------------
// Odporność na głębokie zagnieżdżenie
//
// Parser zjeżdża rekurencyjnie przez ~17 poziomów priorytetów na każdy
// nawias, więc głębokie wejście najszybciej pokaże brak licznika głębokości.
// ---------------------------------------------------------------------------

TEST_CASE("głębokie zagnieżdżenie nawiasów nie wywraca parsera") {
    const int depth = 100;
    std::string source(static_cast<std::size_t>(depth), '(');
    source += "1";
    source.append(static_cast<std::size_t>(depth), ')');
    source += ";";

    CHECK(parse(source) == "(program 1)");   // nawiasy nie tworzą węzłów
}

TEST_CASE("długi łańcuch operatorów nie wywraca parsera") {
    std::string source = "1";
    for (int i = 0; i < 500; ++i) source += " + 1";
    source += ";";

    CHECK_NOTHROW(parse(source));
}

// ---------------------------------------------------------------------------
// Błędy wczesne (16) — mogą jeszcze nie być zaimplementowane
// ---------------------------------------------------------------------------

TEST_CASE("switch dopuszcza najwyżej jedną klauzulę default") {
    check_syntax_error("switch (a) { default: ; default: ; }");
}

TEST_CASE("break i continue poza pętlą są błędem") {
    check_syntax_error("break;");
    check_syntax_error("continue;");
    check_syntax_error("if (a) break;");
}

TEST_CASE("continue może wskazywać tylko etykietę pętli") {
    check_syntax_error("foo: { continue foo; }");
    check_syntax_error("while (a) continue nieistniejaca;");
}

// ---------------------------------------------------------------------------
// Pozycje w źródle
//
// start/end są potrzebne do komunikatów błędów, stack trace'ów i map źródeł.
// ---------------------------------------------------------------------------

static std::unique_ptr<Program> parse_ast(const std::string& source) {
    Lexer lexer(source);
    Parser parser(lexer.scan_tokens());
    return parser.parse();
}

TEST_CASE("węzły niosą zakres w źródle") {
    //            0123456789
    const auto p = parse_ast("  a + 1;");
    REQUIRE(p->body.size() == 1);

    const auto& stmt = static_cast<const ExpressionStatement&>(*p->body[0]);
    const Node& expr = *stmt.expr;

    CHECK(expr.start == 2);
    CHECK(expr.end   == 7);
}

// ---------------------------------------------------------------------------
// void (11.4.2)
// ---------------------------------------------------------------------------

TEST_CASE("void jest operatorem unarnym") {
    CHECK(parse("void 0;")        == "(program (uvoid 0))");
    CHECK(parse("void void 0;")   == "(program (uvoid (uvoid 0)))");
    CHECK(parse("typeof void 0;") == "(program (utypeof (uvoid 0)))");
    CHECK(parse("void f();")      == "(program (uvoid (call f)))");
}

TEST_CASE("void wiąże mocniej niż operatory binarne") {
    CHECK(parse("void 0 + 1;") == "(program (+ (uvoid 0) 1))");
    CHECK(parse("1 + void 0;") == "(program (+ 1 (uvoid 0)))");
}

TEST_CASE("void bez operandu to błąd składni") {
    check_syntax_error("void;");
}

// ---------------------------------------------------------------------------
// debugger (12.15)
// ---------------------------------------------------------------------------

TEST_CASE("debugger jest instrukcją") {
    CHECK(parse("debugger;")                == "(program (debugger))");
    CHECK(parse("if (a) debugger; else b;") == "(program (if a (debugger) b))");
    CHECK(parse("while (a) { debugger; }")  == "(program (while a (block (debugger))))");
}

TEST_CASE("debugger podlega ASI jak każda instrukcja (7.9.1)") {
    CHECK(parse("debugger\nfoo;") == "(program (debugger) foo)");
    CHECK(parse("{ debugger }")   == "(program (block (debugger)))");
    CHECK(parse("debugger")       == "(program (debugger))");
    check_syntax_error("debugger 1;");
}

TEST_CASE("debugger nie jest wyrażeniem") {
    check_syntax_error("x = debugger;");
}

// ---------------------------------------------------------------------------
// Literał RegExp (7.8.5)
//
// Lekser rozpoznaje regexp już wcześniej (tests/lexer.cpp); tu sprawdzamy,
// że parser przyjmuje go jako wyrażenie pierwotne.
// ---------------------------------------------------------------------------

TEST_CASE("literał regexp jest wyrażeniem pierwotnym") {
    CHECK(parse("/ab+c/;")       == "(program /ab+c/)");
    CHECK(parse("/ab+c/gi;")     == "(program /ab+c/gi)");
    CHECK(parse("/[/]/;")        == "(program /[/]/)");
    CHECK(parse("x = /a/;")      == "(program (= x /a/))");
    CHECK(parse("f(/a/, /b/g);") == "(program (call f /a/ /b/g))");
    CHECK(parse("typeof /x/;")   == "(program (utypeof /x/))");
}

TEST_CASE("ukośnik po operandzie nadal jest dzieleniem") {
    CHECK(parse("a / b / c;") == "(program (/ (/ a b) c))");
    CHECK(parse("(a) / 2;")   == "(program (/ a 2))");
}

TEST_CASE("literał regexp niesie zakres w źródle") {
    //                        01234567
    const auto p = parse_ast("  /ab/g;");
    REQUIRE(p->body.size() == 1);

    const Node& expr = *static_cast<const ExpressionStatement&>(*p->body[0]).expr;

    CHECK(expr.kind  == NodeKind::RegExpLiteral);
    CHECK(expr.start == 2);
    CHECK(expr.end   == 7);
}

// ---------------------------------------------------------------------------
// Granica rekurencji parsera
//
// parse_statement i parse_primary to wierzchołki dwóch rekurencji: instrukcji
// i wyrażeń. Bez sprawdzania stosu głębokie zagnieżdżenie kończyło się SIGSEGV.
// ---------------------------------------------------------------------------

static std::string nested(const std::string& open, const std::string& middle,
                          const std::string& close, std::size_t depth) {
    std::string out;
    for (std::size_t i = 0; i < depth; ++i) out += open;
    out += middle;
    for (std::size_t i = 0; i < depth; ++i) out += close;
    return out;
}

TEST_CASE("umiarkowane zagnieżdżenie parsuje się normalnie") {
    CHECK(parse(nested("(", "1", ")", 200) + ";") == "(program 1)");
    CHECK(parse(nested("[", "", "]", 3) + ";")    == "(program <?>)");   // ArrayExpression nie ma kształtu
    CHECK(parse(nested("{", "", "}", 100))        != "");
}

TEST_CASE("zbyt głębokie zagnieżdżenie daje błąd składni, a nie przepełnienie stosu") {
    check_syntax_error(nested("(", "1", ")", 100000) + ";");          // wyrażenia
    check_syntax_error(nested("[", "", "]", 100000) + ";");           // literały tablicowe
    check_syntax_error(nested("{", "", "}", 100000));                 // bloki instrukcji
    check_syntax_error("1" + nested("+(1", "", ")", 100000) + ";");   // drzewo operatorów
}

// ---------------------------------------------------------------------------
// with (12.10)
// ---------------------------------------------------------------------------

TEST_CASE("with przyjmuje wyrażenie i dowolną instrukcję") {
    CHECK(parse("with (o) { a; }") == "(program (with o (block a)))");
    CHECK(parse("with (o) a;")     == "(program (with o a))");
    CHECK(parse("with (a, b) c;")  == "(program (with (seq a b) c))");
    CHECK(parse("with (o) with (p) a;") == "(program (with o (with p a)))");
}

TEST_CASE("with wymaga nawiasów i ciała") {
    check_syntax_error("with o { }");
    check_syntax_error("with (o)");
    check_syntax_error("with () { }");
}

// ---------------------------------------------------------------------------
// Prolog dyrektyw i flaga strict (14.1)
//
// Na tym etapie flaga niczego jeszcze nie zmienia w wykonaniu — sprawdzamy
// samo rozpoznanie dyrektywy i jej dziedziczenie w głąb drzewa.
// ---------------------------------------------------------------------------

static const FunctionDeclaration& first_declaration(const Program& program) {
    for (const auto& statement : program.body) {
        if (statement->kind == NodeKind::FunctionDeclaration) {
            return static_cast<const FunctionDeclaration&>(*statement);
        }
    }
    FAIL("brak deklaracji funkcji w programie");
    throw std::logic_error("unreachable");
}

TEST_CASE("dyrektywa na początku programu włącza strict") {
    CHECK(parse_ast("\"use strict\"; var a;")->strict);
    CHECK(parse_ast("'use strict'; var a;")->strict);
    CHECK_FALSE(parse_ast("var a;")->strict);
}

// ASI kończy instrukcję tak samo jak średnik — to najczęstszy błąd
// w implementacjach prologu.
TEST_CASE("dyrektywa bez średnika też się liczy") {
    CHECK(parse_ast("\"use strict\"\nvar a;")->strict);
    CHECK(parse_ast("\"use strict\"")->strict);
}

TEST_CASE("prolog kończy się na pierwszej instrukcji, która nie jest literałem") {
    CHECK_FALSE(parse_ast("var a; \"use strict\";")->strict);
    CHECK_FALSE(parse_ast("\"use\" + \" strict\"; var a;")->strict);
    CHECK_FALSE(parse_ast("{ \"use strict\"; } var a;")->strict);   // blok to nie prolog
}

TEST_CASE("prolog może zawierać kilka dyrektyw") {
    CHECK(parse_ast("\"use asm\"; \"use strict\"; var a;")->strict);
    CHECK_FALSE(parse_ast("\"use asm\"; var a;")->strict);
}

TEST_CASE("dyrektywa w ciele funkcji dotyczy tylko tej funkcji") {
    const auto program = parse_ast("function f() { \"use strict\"; }");

    CHECK_FALSE(program->strict);
    CHECK(first_declaration(*program).strict);
}

TEST_CASE("funkcja dziedziczy strict z kodu otaczającego") {
    const auto program = parse_ast("\"use strict\"; function f() {}");

    CHECK(program->strict);
    CHECK(first_declaration(*program).strict);
}

TEST_CASE("dziedziczenie sięga funkcji zagnieżdżonych") {
    const auto program = parse_ast("function f() { \"use strict\"; function g() {} }");

    const FunctionDeclaration& f = first_declaration(*program);
    REQUIRE(f.strict);

    const FunctionDeclaration& g = [&]() -> const FunctionDeclaration& {
        for (const auto& statement : f.body->body) {
            if (statement->kind == NodeKind::FunctionDeclaration) {
                return static_cast<const FunctionDeclaration&>(*statement);
            }
        }
        FAIL("brak funkcji zagnieżdżonej");
        throw std::logic_error("unreachable");
    }();

    CHECK(g.strict);
}

TEST_CASE("wyrażenie funkcyjne też niesie flagę") {
    const auto program = parse_ast("var f = function () { \"use strict\"; };");
    REQUIRE(program->body.size() == 1);

    const auto& declaration = static_cast<const VariableDeclaration&>(*program->body[0]);
    const auto& function = static_cast<const FunctionExpression&>(*declaration.declarators[0].init);

    CHECK_FALSE(program->strict);
    CHECK(function.strict);
}

TEST_CASE("funkcja bez dyrektywy w kodzie nieścisłym zostaje nieścisła") {
    const auto program = parse_ast("function f() { var a; }");
    CHECK_FALSE(first_declaration(*program).strict);
}

// ---------------------------------------------------------------------------
// Wczesne błędy trybu strict (Annex C)
//
// Każdy przypadek ma parę: wersja strict ma być błędem SKŁADNI, a ta sama
// konstrukcja poza strict musi nadal działać.
// ---------------------------------------------------------------------------

TEST_CASE("with jest zakazane w strict") {
    check_syntax_error("\"use strict\"; with ({}) {}");
    check_syntax_error("function f() { \"use strict\"; with ({}) {} }");

    CHECK(parse("with (o) a;") == "(program (with o a))");
}

// 13.1: o dyrektywie dowiadujemy się dopiero w ciele, więc duplikat parametru
// sprzed dyrektywy też musi zostać wyłapany.
TEST_CASE("powtórzone nazwy parametrów są zakazane w strict") {
    check_syntax_error("\"use strict\"; function f(a, a) {}");
    check_syntax_error("function f(a, a) { \"use strict\"; }");
    check_syntax_error("\"use strict\"; var f = function (a, b, a) {};");

    CHECK(parse("function f(a, a) { return a; }")
          == "(program (function f (a a) (block (return a))))");
}

TEST_CASE("eval i arguments nie mogą być nazwą wiązania w strict") {
    check_syntax_error("\"use strict\"; var eval = 1;");
    check_syntax_error("\"use strict\"; var arguments = 1;");
    check_syntax_error("\"use strict\"; function eval() {}");
    check_syntax_error("\"use strict\"; function arguments() {}");
    check_syntax_error("\"use strict\"; function f(eval) {}");
    check_syntax_error("function f(arguments) { \"use strict\"; }");
    check_syntax_error("\"use strict\"; try {} catch (eval) {}");

    CHECK(parse("var eval = 1;")          == "(program (var (eval 1)))");
    CHECK(parse("function f(arguments) {}") == "(program (function f (arguments) (block)))");
}

TEST_CASE("eval i arguments nie mogą być celem przypisania w strict") {
    check_syntax_error("\"use strict\"; eval = 1;");
    check_syntax_error("\"use strict\"; arguments = 1;");
    check_syntax_error("\"use strict\"; eval += 1;");
    check_syntax_error("\"use strict\"; arguments++;");
    check_syntax_error("\"use strict\"; --eval;");

    CHECK(parse("eval = 1;")    == "(program (= eval 1))");
    CHECK(parse("arguments++;") == "(program (arguments++))");

    // Właściwość o takiej nazwie jest w porządku — zakaz dotyczy samej nazwy.
    CHECK(parse("\"use strict\"; o.eval = 1;") == "(program \"use strict\" (= <?> 1))");
}

TEST_CASE("delete samej nazwy jest zakazane w strict") {
    check_syntax_error("\"use strict\"; var x = 1; delete x;");
    check_syntax_error("\"use strict\"; delete eval;");

    CHECK(parse("delete x;") == "(program (udelete x))");
    // delete właściwości jest dozwolone w obu trybach
    CHECK(parse("\"use strict\"; delete o.x;") == "(program \"use strict\" (udelete <?>))");
}

// 7.6.1.2. Uwaga: "let", "yield" i "const" są u nas słowami kluczowymi leksera
// (to ES6), więc nie da się ich użyć jako identyfikatora nawet poza strict —
// istniejące odchylenie od ES5.1, niezależne od trybu strict.
TEST_CASE("słowa zastrzeżone w strict nie mogą być identyfikatorem") {
    for (const char *word : {"implements", "interface", "package", "private",
                             "protected", "public", "static"}) {
        const std::string name = word;
        CAPTURE(name);

        check_syntax_error("\"use strict\"; var " + name + " = 1;");
        check_syntax_error("\"use strict\"; " + name + ";");

        CHECK(parse("var " + name + " = 1;") == "(program (var (" + name + " 1)))");
    }
}

// 11.1.5 — ES5.1. Uwaga: od ES2015 wszystkie te ograniczenia zniesiono,
// więc Node nie jest tu wyrocznią.
TEST_CASE("powtórzone właściwości w literale obiektowym") {
    // dane + dane: błąd tylko w strict
    check_syntax_error("\"use strict\"; var o = {a: 1, a: 2};");
    CHECK(parse("var o = {a: 1, a: 2};") != "");

    // dwa gettery albo dwa settery: błąd zawsze
    check_syntax_error("var o = {get a() {}, get a() {}};");
    check_syntax_error("var o = {set a(v) {}, set a(v) {}};");

    // dane + akcesor: błąd zawsze, w obie strony
    check_syntax_error("var o = {a: 1, get a() {}};");
    check_syntax_error("var o = {get a() {}, a: 1};");

    // para get + set to poprawny sposób na jedną właściwość
    CHECK(parse("var o = {get a() {}, set a(v) {}};") != "");
    CHECK(parse("var o = {a: 1, b: 2};") != "");
}

// Getter i setter w literale powstają z pominięciem parse_function_expression,
// więc flagę strict trzeba im ustawić osobno.
TEST_CASE("akcesory w literale obiektowym dziedziczą strict") {
    const auto program = parse_ast("\"use strict\"; var o = {get x() { return 1; }};");
    REQUIRE(program->strict);

    const auto& declaration = static_cast<const VariableDeclaration&>(*program->body[1]);
    const auto& object = static_cast<const ObjectExpression&>(*declaration.declarators[0].init);
    REQUIRE(object.properties.size() == 1);

    CHECK(object.properties[0].accessor->strict);
}
