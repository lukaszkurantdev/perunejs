#include <doctest/doctest.h>

#include "lexer/lexer.h"
#include "parser.h"
#include "evaluator/evaluator.h"
#include "runtime/globals.h"

#include <cmath>
#include <cstdlib>
#include <ctime>
#include <string>
#include <variant>

using namespace perunejs;

// ---------------------------------------------------------------------------
// Helpery
// ---------------------------------------------------------------------------

static Completion run(const std::string& source) {
    Lexer lexer(source);
    Parser parser(lexer.scan_tokens());
    auto program = parser.parse();

    Heap heap;
    Evaluator evaluator(heap);

    // setup_globals tworzy obiekt globalny i zwraca środowisko obiektowe nad nim.
    // Bez tego {} nie ma Object.prototype, a undefined/NaN/Infinity
    // są niezadeklarowanymi nazwami.
    Environment *env = setup_globals(heap, evaluator);

    return evaluator.eval_program(env, *program);
}

// Czy program zakończył się rzuceniem? Od czasu przejścia na completion records
// błędy JavaScriptu (TypeError, ReferenceError, RangeError) NIE są wyjątkami C++,
// tylko zwykłym zakończeniem typu THROW.
static bool throws(const std::string& source) {
    return run(source).type == COMPLETION_TYPE::THROW;
}

// Treść rzuconej wartości - pozwala sprawdzić, KTÓRY to błąd.
//
// Od kiedy błędy są obiektami Error, nie wystarczy JSValue::to_string():
// ToString(obiekt) musi wywołać Error.prototype.toString, a to potrafi
// tylko ewaluator. Dlatego ten helper prowadzi cały przebieg sam —
// ewaluator (i sterta) muszą żyć dłużej niż rzucona wartość.
static std::string thrown(const std::string& source) {
    Lexer lexer(source);
    Parser parser(lexer.scan_tokens());
    auto program = parser.parse();

    Heap heap;
    Evaluator evaluator(heap);
    Environment *env = setup_globals(heap, evaluator);

    Completion c = evaluator.eval_program(env, *program);
    REQUIRE(c.type == COMPLETION_TYPE::THROW);

    const JSValue value = c.get_value_or_undefined();
    if (value.type() != JSValueType::Object) {
        return value.to_string();
    }

    Completion text = evaluator.to_string(value);
    REQUIRE(text.type == COMPLETION_TYPE::NORMAL);
    return text.get_value_or_undefined().to_string();
}

// Czy program w ogóle wyprodukował wartość (completion.value != empty)?
static bool has_value(const std::string& source) {
    return std::holds_alternative<JSValue>(run(source).value);
}

static JSValue value_of(const std::string& source) {
    Completion c = run(source);
    REQUIRE(std::holds_alternative<JSValue>(c.value));
    return std::get<JSValue>(c.value);
}

static double number_of(const std::string& source) {
    return value_of(source).to_number();
}

// ---------------------------------------------------------------------------
// Wartość programu — StatementList (12.1)
//
// Kluczowa reguła: completion o pustej wartości (empty) NIE nadpisuje
// dotychczasowej wartości programu, tylko ją zachowuje. Dlatego "1+2; ;"
// daje 3, a nie undefined. Uwaga: empty to trzeci stan, różny od undefined —
// undefined JEST wartością i normalnie by nadpisało.
// ---------------------------------------------------------------------------

TEST_CASE("wartością programu jest wartość ostatniego statementu") {
    CHECK(number_of("1 + 2;")        == 3);
    CHECK(number_of("1 + 2; 5 + 5;") == 10);
    CHECK(number_of("1; 2; 3;")      == 3);
}

TEST_CASE("pusty statement nie wymazuje wartości programu (12.1)") {
    CHECK(number_of("1 + 2; ;")   == 3);
    CHECK(number_of("1 + 2; ;;;") == 3);
    CHECK(number_of("; 1 + 2;")   == 3);
    CHECK(number_of("; 1 + 2; ;") == 3);
}

TEST_CASE("program bez żadnej wartości zwraca completion pusty, nie undefined") {
    CHECK(has_value("")     == false);
    CHECK(has_value(";")    == false);
    CHECK(has_value(";;;")  == false);
}

TEST_CASE("completion programu ma typ normal") {
    CHECK(run("1 + 2;").type == COMPLETION_TYPE::NORMAL);
    CHECK(run(";").type      == COMPLETION_TYPE::NORMAL);
    CHECK(run("").type       == COMPLETION_TYPE::NORMAL);
}

// ---------------------------------------------------------------------------
// Wyrażenia przez pełny pipeline (11.4-11.6)
// ---------------------------------------------------------------------------

TEST_CASE("arytmetyka") {
    CHECK(number_of("1 + 2;")     == 3);
    CHECK(number_of("5 - 3;")     == 2);
    CHECK(number_of("4 * 3;")     == 12);
    CHECK(number_of("10 / 4;")    == 2.5);
    CHECK(number_of("7 % 3;")     == 1);
    CHECK(number_of("1 + 2 * 3;") == 7);   // priorytet
    CHECK(number_of("(1 + 2) * 3;") == 9);
    CHECK(number_of("10 - 2 - 3;") == 5);  // łączność lewostronna
}

TEST_CASE("operatory unarne (11.4)") {
    CHECK(number_of("-5;")    == -5);
    CHECK(number_of("+5;")    == 5);
    CHECK(number_of("- -5;")  == 5);   // "--" byłoby tokenem DECREMENT (7.7)
    CHECK(number_of("-(2 + 3);") == -5);
}

TEST_CASE("dzielenie daje wartości zmiennoprzecinkowe wg IEEE-754") {
    CHECK(std::isinf(number_of("1 / 0;")));
    CHECK(number_of("1 / 0;") > 0);
    CHECK(std::isnan(number_of("0 / 0;")));
}

TEST_CASE("negacja logiczna zwraca boolean (11.4.9)") {
    CHECK(value_of("!0;").type()  == JSValueType::Boolean);
    CHECK(value_of("!0;").to_boolean()  == true);
    CHECK(value_of("!1;").to_boolean()  == false);
    CHECK(value_of("!!1;").to_boolean() == true);
}

// ---------------------------------------------------------------------------
// Operatory, których parser jeszcze nie zna — AST budowane ręcznie
//
// eval_binary obsługuje ==, !=, ===, !==, && i ||, ale parser nie ma
// jeszcze pięter 11.9-11.11, więc tych ścieżek nie da się osiągnąć
// przez parsowanie. Budujemy drzewo wprost, żeby ten kod nie był
// nieprzetestowany.
// ---------------------------------------------------------------------------

static ExpressionPtr num(double v) { return std::make_unique<NumberLiteral>(v); }

static JSValue eval_binary_of(BinaryOperator op, ExpressionPtr left, ExpressionPtr right) {
    BinaryExpression node(op, std::move(left), std::move(right));
    Heap heap;
    Evaluator evaluator(heap);
    DeclarativeEnvironment env;
    return evaluator.evaluate(&env, node).get_value_or_undefined();
}

TEST_CASE("równość luźna i ścisła (11.9)") {
    CHECK(eval_binary_of(BinaryOperator::Eq,          num(1), num(1)).to_boolean() == true);
    CHECK(eval_binary_of(BinaryOperator::Eq,          num(1), num(2)).to_boolean() == false);
    CHECK(eval_binary_of(BinaryOperator::NotEq,       num(1), num(2)).to_boolean() == true);
    CHECK(eval_binary_of(BinaryOperator::StrictEq,    num(1), num(1)).to_boolean() == true);
    CHECK(eval_binary_of(BinaryOperator::StrictNotEq, num(1), num(2)).to_boolean() == true);
}

TEST_CASE("&& i || zwracają operand, nie boolean (11.11)") {
    // W JS "a && b" nie daje true/false, tylko jeden z operandów.
    CHECK(eval_binary_of(BinaryOperator::LogicalAnd, num(1), num(5)).to_number() == 5);
    CHECK(eval_binary_of(BinaryOperator::LogicalAnd, num(0), num(5)).to_number() == 0);
    CHECK(eval_binary_of(BinaryOperator::LogicalOr,  num(1), num(5)).to_number() == 1);
    CHECK(eval_binary_of(BinaryOperator::LogicalOr,  num(0), num(5)).to_number() == 5);
}

TEST_CASE("&& i || nie ewaluują prawej strony, gdy nie muszą (11.11)") {
    // Prawa strona to dzielenie przez zero pod unary minus — gdyby została
    // policzona mimo skrótu, wynik przestałby być operandem lewym.
    CHECK(eval_binary_of(BinaryOperator::LogicalAnd, num(0), num(999)).to_number() == 0);
    CHECK(eval_binary_of(BinaryOperator::LogicalOr,  num(7), num(999)).to_number() == 7);
}

// ---------------------------------------------------------------------------
// Statementy jeszcze nieobsłużone przez evaluator
// ---------------------------------------------------------------------------

// Statementy, których evaluator jeszcze nie wykonuje. Gdy powstaną,
// te asercje trzeba przenieść do testów sprawdzających wynik.
// ---------------------------------------------------------------------------
// Zmienne: deklaracja, odczyt, przypisanie (12.2, 11.1.2, 11.13.1)
//
// Uwaga o typach wyjątków: spec mówi o ReferenceError, a odwzorowanie tego
// na konkretny typ C++ jest decyzją implementacji — dlatego testy sprawdzają
// tylko, że wyjątek leci, nie jaki dokładnie.
// ---------------------------------------------------------------------------

TEST_CASE("deklaracja z inicjalizatorem wiąże wartość") {
    CHECK(number_of("var a = 1; a;")        == 1);
    CHECK(number_of("var a = 1; a + 1;")    == 2);
    CHECK(number_of("var a = 1, b = 2; a + b;") == 3);
    CHECK(value_of("var s = \"txt\"; s;").to_string() == "txt");
}

TEST_CASE("deklaracja bez inicjalizatora daje undefined") {
    CHECK(value_of("var a; a;").type() == JSValueType::Undefined);
}

// 12.2 — deklarator bez inicjalizatora nie ma semantyki runtime, więc
// nie nadpisuje wartości, którą zmienna już ma.
TEST_CASE("powtórzona deklaracja bez inicjalizatora nie kasuje wartości") {
    CHECK(number_of("var a = 1; var a; a;") == 1);
    CHECK(number_of("var a = 1; var a = 2; a;") == 2);
}

// Binding musi powstać PRZED ewaluacją inicjalizatora, inaczej "var a = a"
// dałoby ReferenceError zamiast undefined.
TEST_CASE("var a = a daje undefined, nie błąd") {
    CHECK(value_of("var a = a; a;").type() == JSValueType::Undefined);
}

// 12.2 kończy się na "Return (normal, empty, empty)" — deklaracja nie wnosi
// wartości do programu, więc reguła 12.1 zachowuje poprzednią.
TEST_CASE("deklaracja nie wnosi wartości do programu") {
    CHECK(has_value("var a = 1;")       == false);
    CHECK(has_value("var a;")           == false);
    CHECK(number_of("1 + 2; var a = 5;") == 3);
}

// 11.13.1 krok 6: "Return rval" — wartością przypisania jest wartość
// przypisana, nie poprzednia.
TEST_CASE("przypisanie ma wartość równą przypisanej") {
    CHECK(number_of("var a; a = 5;")        == 5);
    CHECK(number_of("var a = 1; a = 5;")    == 5);
    CHECK(number_of("var a; var b; a = (b = 2); a;") == 2);
    CHECK(number_of("var a; var b; a = (b = 2); b;") == 2);
}

TEST_CASE("przypisanie zmienia wiązanie") {
    CHECK(number_of("var a = 1; a = 5; a;") == 5);
    CHECK(value_of("var a = 1; a = \"txt\"; a;").to_string() == "txt");
}

TEST_CASE("odczyt niezadeklarowanej nazwy jest błędem") {
    CHECK(throws("a;"));
    CHECK(throws("var a; a + b;"));
}

// 8.7.2: zapis pod nierozwiązywalną referencją tworzy niejawną zmienną
// globalną. Niejawna globalna powstaje TYLKO przy zapisie — sam odczyt
// (także ten w "a += 1") nadal rzuca ReferenceError.
TEST_CASE("przypisanie do niezadeklarowanej nazwy tworzy zmienną globalną") {
    CHECK(number_of("a = 1; a;") == 1);
    CHECK(number_of("(function () { zmienna = 3; })(); zmienna;") == 3);
    CHECK(number_of("zm = 5; this.zm;") == 5);
    CHECK(throws("a += 1;"));
}

TEST_CASE("zmienne nie przeciekają między uruchomieniami programu") {
    CHECK(number_of("var a = 1; a;") == 1);
    CHECK(throws("a;"));   // świeże środowisko
}

// ---------------------------------------------------------------------------
// Literały nieliczbowe i konwersje typów (rozdz. 9, 11.6.1)
// ---------------------------------------------------------------------------

TEST_CASE("literały string, boolean i null zachowują swój typ") {
    CHECK(value_of("\"abc\";").type()  == JSValueType::String);
    CHECK(value_of("\"abc\";").to_string() == "abc");
    CHECK(value_of("\"\";").to_string()    == "");

    CHECK(value_of("true;").type()  == JSValueType::Boolean);
    CHECK(value_of("true;").to_boolean()  == true);
    CHECK(value_of("false;").to_boolean() == false);

    CHECK(value_of("null;").type() == JSValueType::Null);
}

// 11.6.1 — "+" konkatenuje, gdy po ToPrimitive którykolwiek operand
// jest stringiem; w przeciwnym razie dodaje liczby.
TEST_CASE("operator + : konkatenacja kontra dodawanie") {
    CHECK(value_of("\"a\" + \"b\";").to_string() == "ab");
    CHECK(value_of("1 + \"a\";").to_string()     == "1a");
    CHECK(value_of("\"a\" + 1;").to_string()     == "a1");
    CHECK(value_of("\"a\" + \"b\";").type()      == JSValueType::String);

    CHECK(value_of("true + true;").type()     == JSValueType::Number);
    CHECK(number_of("true + true;")  == 2);    // ToNumber(true) == 1
    CHECK(number_of("null + 1;")     == 1);    // ToNumber(null) == 0
    CHECK(number_of("false + 5;")    == 5);
}

// 11.5 — operatory multiplikatywne zawsze konwertują przez ToNumber,
// więc stringi liczbowe są liczone, a nie sklejane.
TEST_CASE("operatory multiplikatywne zawsze konwertują na liczby") {
    CHECK(number_of("\"5\" * \"2\";") == 10);
    CHECK(number_of("\"6\" / 2;")     == 3);
    CHECK(number_of("true * 5;")      == 5);
    CHECK(std::isnan(number_of("\"abc\" * 2;")));
}

TEST_CASE("negacja logiczna literałów (11.4.9, ToBoolean)") {
    CHECK(value_of("!true;").to_boolean()    == false);
    CHECK(value_of("!false;").to_boolean()   == true);
    CHECK(value_of("!null;").to_boolean()    == true);
    CHECK(value_of("!\"\";").to_boolean()    == true);
    CHECK(value_of("!\"abc\";").to_boolean() == false);
    CHECK(value_of("!\"0\";").to_boolean()   == false);   // napis "0" jest prawdziwy
}

TEST_CASE("literały nieliczbowe podlegają regule 12.1 tak samo jak liczby") {
    CHECK(value_of("\"a\"; ;").to_string()      == "a");
    CHECK(value_of("null; ;").type()            == JSValueType::Null);
    CHECK(value_of("1; \"ostatni\";").to_string() == "ostatni");
}

// ---------------------------------------------------------------------------
// Przypisania złożone (11.13.2)
//
// "a op= b" liczy się jak "a = a op b", ale kolejność kroków jest inna niż
// przy zwykłym "=": stara wartość jest odczytywana PRZED ewaluacją prawej
// strony, więc błąd braku deklaracji leci wcześniej.
// ---------------------------------------------------------------------------

// Wykonuje program w podanym środowisku, połykając ewentualny wyjątek —
// pozwala sprawdzić, co zdążyło się wykonać, zanim poleciał błąd.
// Operacje na środowisku wymagają ewaluatora (rekord obiektowy może wołać
// getter/setter). Dla rekordu deklaratywnego wystarczy ewaluator tymczasowy.
static void bind(Environment& env, const std::string& name, const JSValue& value) {
    Heap heap;
    Evaluator evaluator(heap);
    env.create_mutable_binding(evaluator, name, false);
    env.set_binding(evaluator, name, value, false);
}

static double binding_number(Environment& env, const std::string& name) {
    Heap heap;
    Evaluator evaluator(heap);
    Completion value = env.get_binding_value(evaluator, name, false);
    REQUIRE(value.type == COMPLETION_TYPE::NORMAL);
    return value.get_value_or_undefined().to_number();
}

static void run_in(Environment& env, const std::string& source) {
    Lexer lexer(source);
    Parser parser(lexer.scan_tokens());
    auto program = parser.parse();
    Heap heap;
    Evaluator evaluator(heap);
    try { evaluator.eval_program(&env, *program); } catch (const std::exception&) {}
}

TEST_CASE("przypisania złożone liczą właściwą stronę we właściwej kolejności") {
    CHECK(number_of("var a = 1; a += 2;")  == 3);
    CHECK(number_of("var a = 10; a -= 3;") == 7);
    CHECK(number_of("var a = 2; a *= 3;")  == 6);
    CHECK(number_of("var a = 10; a /= 4;") == 2.5);
}

// Odejmowanie i dzielenie nie są przemienne — te przypadki wychwycą
// zamienione argumenty, których "+=" i "*=" nie pokażą.
TEST_CASE("przypisania złożone zapisują wynik do zmiennej") {
    CHECK(number_of("var a = 1; a += 2; a;")  == 3);
    CHECK(number_of("var a = 10; a -= 3; a;") == 7);
    CHECK(number_of("var a = 2; a *= 3; a;")  == 6);
    CHECK(number_of("var a = 10; a /= 4; a;") == 2.5);
}

// 11.13.2 krok 8: "Return r" — wartością wyrażenia jest wynik operacji,
// nie prawa strona.
TEST_CASE("wartością przypisania złożonego jest wynik operacji") {
    CHECK(number_of("var a = 1; var b; b = (a += 2); b;")  == 3);
    CHECK(number_of("var a = 10; var b; b = (a -= 3); b;") == 7);
}

// "+=" dziedziczy pełną semantykę "+" z 11.6.1, łącznie z konkatenacją.
TEST_CASE("+= konkatenuje stringi tak samo jak +") {
    CHECK(value_of("var a = \"x\"; a += \"y\";").to_string()   == "xy");
    CHECK(value_of("var a = \"x\"; a += \"y\"; a;").to_string() == "xy");
    CHECK(value_of("var a = \"x\"; a += 1;").to_string()        == "x1");
    CHECK(number_of("var a = \"5\"; a -= 2;")                   == 3);   // "-" zawsze ToNumber
}

TEST_CASE("przypisanie złożone do niezadeklarowanej nazwy jest błędem") {
    CHECK(throws("a += 1;"));
    CHECK(throws("a -= 1;"));
    CHECK(throws("a *= 1;"));
    CHECK(throws("a /= 1;"));
}

// Różnica kolejności między 11.13.1 a 11.13.2, widoczna tylko wtedy, gdy
// prawa strona ma efekt uboczny, a lewa nie istnieje:
//   "="  — sprawdzenie należy do PutValue, czyli PO ewaluacji prawej strony
//   "+=" — odczyt starej wartości jest krokiem 2, czyli PRZED prawą stroną
TEST_CASE("przy op= błąd wyprzedza ewaluację prawej strony") {
    DeclarativeEnvironment env_assign;
    run_in(env_assign, "var b = 0; a = (b = 5);");
    CHECK(binding_number(env_assign, "b") == 5);

    DeclarativeEnvironment env_compound;
    run_in(env_compound, "var b = 0; a += (b = 5);");
    CHECK(binding_number(env_compound, "b") == 0);
}

TEST_CASE("przypisanie złożone nie tworzy bindingu po cichu") {
    DeclarativeEnvironment env;
    run_in(env, "a += 1;");
    CHECK(env.has_binding("a") == false);
}

// ---------------------------------------------------------------------------
// Operatory bitowe i przesunięcia (11.7, 11.10, 11.4.8)
// ---------------------------------------------------------------------------

TEST_CASE("operatory bitowe") {
    CHECK(number_of("5 & 3;") == 1);
    CHECK(number_of("5 | 3;") == 7);
    CHECK(number_of("5 ^ 3;") == 6);
    CHECK(number_of("~5;")    == -6);
    CHECK(number_of("~0;")    == -1);
}

// 11.7.2 kontra 11.7.3 — ">>" bierze ToInt32 lewej strony i przesuwa
// arytmetycznie, ">>>" bierze ToUint32 i przesuwa logicznie. To jedyny
// operator zwracający wartość bez znaku.
TEST_CASE(">> i >>> różnią się traktowaniem znaku") {
    CHECK(number_of("-1 >> 0;")   == -1);
    CHECK(number_of("-1 >>> 0;")  == 4294967295.0);
    CHECK(number_of("-1 >> 1;")   == -1);
    CHECK(number_of("-1 >>> 1;")  == 2147483647.0);
    CHECK(number_of("-8 >> 2;")   == -2);
    CHECK(number_of("-8 >>> 28;") == 15);
    CHECK(number_of("8 >> 2;")    == 2);
    CHECK(number_of("8 >>> 2;")   == 2);   // dla dodatnich bez różnicy
}

// 11.7.1 krok 3: shiftCount = rnum & 0x1F — liczy się tylko 5 najniższych
// bitów prawej strony, więc "1 << 32" to "1 << 0".
TEST_CASE("licznik przesunięcia to 5 najniższych bitów") {
    CHECK(number_of("1 << 32;")   == 1);
    CHECK(number_of("1 << 33;")   == 2);
    CHECK(number_of("1 << 31;")   == -2147483648.0);
    CHECK(number_of("-1 >> 32;")  == -1);
    CHECK(number_of("1 >>> 32;")  == 1);
    CHECK(number_of("1 << 1;")    == 2);
}

// Operandy przechodzą przez ToInt32, więc ułamki są obcinane,
// a wartości spoza 32 bitów zawijane. "x | 0" to klasyczny idiom
// obcięcia liczby do inta.
TEST_CASE("operandy bitowe są konwertowane do 32 bitów") {
    CHECK(number_of("1.9 | 0;")          == 1);
    CHECK(number_of("-1.9 | 0;")         == -1);
    CHECK(number_of("4294967296 | 0;")   == 0);
    CHECK(number_of("4294967297 | 0;")   == 1);
    CHECK(number_of("\"5\" & 3;")        == 1);    // przez ToNumber
    CHECK(number_of("true | 0;")         == 1);
    CHECK(number_of("null | 0;")         == 0);
}

TEST_CASE("priorytety operatorów bitowych w wykonaniu") {
    CHECK(number_of("1 | 2 ^ 3;")  == 1);   // 1 | (2^3) == 1 | 1
    CHECK(number_of("1 ^ 2 & 3;")  == 3);   // 1 ^ (2&3) == 1 ^ 2
    CHECK(number_of("1 & 2 << 3;") == 0);   // 1 & (2<<3) == 1 & 16
    CHECK(number_of("1 << 2 + 3;") == 32);  // 1 << (2+3)
    CHECK(number_of("~1 & 3;")     == 2);   // (~1) & 3 == -2 & 3
}

TEST_CASE("przypisania złożone dla operatorów bitowych") {
    CHECK(number_of("var a = 5; a &= 3;")    == 1);
    CHECK(number_of("var a = 5; a |= 3;")    == 7);
    CHECK(number_of("var a = 5; a ^= 3;")    == 6);
    CHECK(number_of("var a = 1; a <<= 3;")   == 8);
    CHECK(number_of("var a = -8; a >>= 2;")  == -2);
    CHECK(number_of("var a = -1; a >>>= 0;") == 4294967295.0);
    CHECK(number_of("var a = 7; a %= 4;")    == 3);
}

TEST_CASE("przypisania bitowe zapisują wynik do zmiennej") {
    CHECK(number_of("var a = 5; a &= 3; a;")    == 1);
    CHECK(number_of("var a = 1; a <<= 3; a;")   == 8);
    CHECK(number_of("var a = -1; a >>>= 0; a;") == 4294967295.0);
    CHECK(number_of("var a = 7; a %= 4; a;")    == 3);
}

// ---------------------------------------------------------------------------
// Operatory logiczne i równość (11.9, 11.11)
// ---------------------------------------------------------------------------

// 11.11 — "&&" i "||" zwracają JEDEN Z OPERANDÓW, nie boolean.
TEST_CASE("&& i || zwracają operand, nie wartość logiczną") {
    CHECK(number_of("1 || 2;") == 1);
    CHECK(number_of("0 || 2;") == 2);
    CHECK(number_of("1 && 2;") == 2);
    CHECK(number_of("0 && 2;") == 0);
    CHECK(value_of("\"\" || \"x\";").to_string() == "x");
    CHECK(value_of("0 || null;").type() == JSValueType::Null);
}

TEST_CASE("&& i || pomijają prawą stronę, gdy wynik jest już znany") {
    // gdyby prawa strona była liczona, niezadeklarowane "brak" rzuciłoby
    CHECK(number_of("0 && brak;") == 0);
    CHECK(number_of("1 || brak;") == 1);
    CHECK(throws("1 && brak;"));
    CHECK(throws("0 || brak;"));
}

TEST_CASE("równość luźna konwertuje typy, ścisła nie (11.9)") {
    CHECK(value_of("1 == 1;").to_boolean()     == true);
    CHECK(value_of("1 != 2;").to_boolean()     == true);
    CHECK(value_of("\"1\" == 1;").to_boolean() == true);    // ToNumber
    CHECK(value_of("\"1\" === 1;").to_boolean() == false);  // różne typy
    CHECK(value_of("1 !== 2;").to_boolean()    == true);
    CHECK(value_of("null == null;").to_boolean() == true);
}

TEST_CASE("równość zwraca boolean niezależnie od typu operandów") {
    CHECK(value_of("1 == 1;").type()       == JSValueType::Boolean);
    CHECK(value_of("\"a\" == \"a\";").type() == JSValueType::Boolean);
}

TEST_CASE("priorytety operatorów logicznych w wykonaniu") {
    CHECK(number_of("0 || 1 && 2;") == 2);   // 0 || (1 && 2)
    CHECK(number_of("1 && 0 || 3;") == 3);   // (1 && 0) || 3
}

// ---------------------------------------------------------------------------
// Operator warunkowy (11.12)
// ---------------------------------------------------------------------------

TEST_CASE("operator warunkowy wybiera gałąź wg ToBoolean") {
    CHECK(number_of("1 ? 2 : 3;")     == 2);
    CHECK(number_of("0 ? 2 : 3;")     == 3);
    CHECK(number_of("\"\" ? 2 : 3;")  == 3);
    CHECK(number_of("\"x\" ? 2 : 3;") == 2);
    CHECK(number_of("null ? 2 : 3;")  == 3);
}

// 11.12 — liczona jest TYLKO wybrana gałąź. Niezadeklarowane "brak"
// rzuciłoby ReferenceError, gdyby druga gałąź też była ewaluowana.
TEST_CASE("operator warunkowy nie liczy niewybranej gałęzi") {
    CHECK(number_of("1 ? 2 : brak;") == 2);
    CHECK(number_of("0 ? brak : 3;") == 3);
    CHECK(throws("1 ? brak : 3;"));
    CHECK(throws("0 ? 2 : brak;"));
}

TEST_CASE("operator warunkowy zachowuje typ wybranej gałęzi") {
    CHECK(value_of("1 ? \"a\" : 2;").type() == JSValueType::String);
    CHECK(value_of("0 ? \"a\" : 2;").type() == JSValueType::Number);
    CHECK(value_of("1 ? null : 2;").type()  == JSValueType::Null);
}

TEST_CASE("zagnieżdżony operator warunkowy") {
    CHECK(number_of("1 ? 2 : 3 ? 4 : 5;") == 2);
    CHECK(number_of("0 ? 2 : 3 ? 4 : 5;") == 4);
    CHECK(number_of("0 ? 2 : 0 ? 4 : 5;") == 5);
}

TEST_CASE("przypisanie w gałęzi operatora warunkowego") {
    CHECK(number_of("var a; var b; 1 ? a = 1 : b = 2; a;") == 1);
    CHECK(number_of("var a = 0; var b = 0; 0 ? a = 1 : b = 2; b;") == 2);
    // niewybrana gałąź nie może wykonać przypisania
    CHECK(number_of("var a = 9; var b = 0; 0 ? a = 1 : b = 2; a;") == 9);
}

// ---------------------------------------------------------------------------
// Operatory relacyjne (11.8)
//
// Wszystkie cztery są zdefiniowane przez jedno porównanie:
//   x <  y   ->  less(x, y)          x >  y   ->  less(y, x)
//   x <= y   ->  !less(y, x)         x >= y   ->  !less(x, y)
// z zastrzeżeniem, że brak wyniku (NaN) daje false we WSZYSTKICH czterech.
// ---------------------------------------------------------------------------

TEST_CASE("operatory relacyjne na liczbach") {
    CHECK(value_of("1 < 2;").to_boolean()  == true);
    CHECK(value_of("2 < 1;").to_boolean()  == false);
    CHECK(value_of("2 > 1;").to_boolean()  == true);
    CHECK(value_of("1 > 2;").to_boolean()  == false);
    CHECK(value_of("1 <= 1;").to_boolean() == true);
    CHECK(value_of("1 <= 0;").to_boolean() == false);
    CHECK(value_of("1 >= 1;").to_boolean() == true);
    CHECK(value_of("0 >= 1;").to_boolean() == false);
}

// To jest test na trójstan: gdyby is_less_than zwracało zwykłe false,
// negacja w "<=" i ">=" dałaby tu true.
TEST_CASE("każde porównanie z NaN daje false") {
    CHECK(value_of("0/0 < 1;").to_boolean()  == false);
    CHECK(value_of("0/0 > 1;").to_boolean()  == false);
    CHECK(value_of("0/0 <= 1;").to_boolean() == false);
    CHECK(value_of("0/0 >= 1;").to_boolean() == false);
    CHECK(value_of("0/0 <= 0/0;").to_boolean() == false);
}

TEST_CASE("relacje zwracają boolean") {
    CHECK(value_of("1 < 2;").type() == JSValueType::Boolean);
    CHECK(value_of("\"a\" < \"b\";").type() == JSValueType::Boolean);
}

TEST_CASE("porównanie stringów jest leksykograficzne, mieszane idzie przez ToNumber") {
    CHECK(value_of("\"10\" < \"9\";").to_boolean() == true);   // oba stringi
    CHECK(value_of("\"10\" < 9;").to_boolean()     == false);  // przez ToNumber
    CHECK(value_of("\"a\" < \"b\";").to_boolean()  == true);
    CHECK(value_of("null < 1;").to_boolean()       == true);
    CHECK(value_of("true < 2;").to_boolean()       == true);
}

TEST_CASE("priorytety operatorów relacyjnych") {
    CHECK(value_of("1 + 1 < 3;").to_boolean()      == true);   // (1+1) < 3
    CHECK(value_of("1 < 2 == true;").to_boolean()  == true);   // (1<2) == true
    CHECK(value_of("1 < 2 && 2 < 3;").to_boolean() == true);
    // relacje są lewostronnie łączne, więc "a < b < c" NIE znaczy tego,
    // co w matematyce: (0<1) daje true, ToNumber(true) to 1, a 1 < 2
    CHECK(value_of("0 < 1 < 2;").to_boolean() == true);
    CHECK(value_of("3 > 2 > 1;").to_boolean() == false);       // (3>2)=true -> 1 > 1
}

// ---------------------------------------------------------------------------
// Operator przecinka (11.14)
// ---------------------------------------------------------------------------

TEST_CASE("wartością sekwencji jest ostatni element") {
    CHECK(number_of("1, 2;")       == 2);
    CHECK(number_of("1, 2, 3;")    == 3);
    CHECK(number_of("(1, 2) + 3;") == 5);
    CHECK(value_of("1, \"x\";").to_string() == "x");
}

// 11.14 wywołuje GetValue na lewym elemencie - jest on w pełni ewaluowany,
// odrzucany jest tylko jego wynik.
TEST_CASE("wcześniejsze elementy sekwencji są wykonywane") {
    CHECK(number_of("var a; (a = 5, 1); a;") == 5);
    CHECK(number_of("var a = 0; (a = 1, a = 2, 9); a;") == 2);
    CHECK(throws("brak, 1;"));
}

// Przecinek jest słabszy od przypisania: "a = 1, 2" to "(a = 1), 2"
TEST_CASE("przecinek wiąże słabiej niż przypisanie") {
    CHECK(number_of("var a; a = 1, 2;")     == 2);   // wartość sekwencji
    CHECK(number_of("var a; a = 1, 2; a;")  == 1);   // ale a dostało 1
    CHECK(number_of("var a; a = (1, 2); a;") == 2);  // nawiasy zmieniają wynik
}

TEST_CASE("przecinek w deklaracji nie jest operatorem") {
    CHECK(number_of("var a = 1, b = 2; a + b;") == 3);
    CHECK(number_of("var a = (1, 2); a;")       == 2);
}

// ---------------------------------------------------------------------------
// Blok i instrukcja warunkowa (12.1, 12.5)
// ---------------------------------------------------------------------------

TEST_CASE("blok wykonuje listę statementów i zwraca wartość ostatniego") {
    CHECK(number_of("{ 1 + 2; }")  == 3);
    CHECK(number_of("{ 1; 2; }")   == 2);
    CHECK(number_of("{ { 5; } }")  == 5);
    CHECK(has_value("{ }")         == false);
    CHECK(has_value("{ ; ; }")     == false);
}

// Reguła 12.1 działa też przez granicę bloku
TEST_CASE("pusty blok nie wymazuje wartości programu") {
    CHECK(number_of("1 + 2; { }")    == 3);
    CHECK(number_of("1 + 2; { ; }")  == 3);
    CHECK(number_of("{ 1; } { 2; }") == 2);
}

// W ES5.1 blok NIE tworzy zakresu - "var" należy do programu/funkcji
TEST_CASE("blok nie tworzy zakresu dla var") {
    CHECK(number_of("{ var a = 1; } a;")     == 1);
    CHECK(number_of("var a = 1; { a = 2; } a;") == 2);
}

TEST_CASE("instrukcja warunkowa wybiera gałąź") {
    CHECK(number_of("if (1) 2;")               == 2);
    CHECK(number_of("if (0) 2; else 3;")       == 3);
    CHECK(number_of("if (1) 2; else 3;")       == 2);
    CHECK(number_of("if (1) { 2; } else { 3; }") == 2);
    CHECK(number_of("if (\"x\") 2; else 3;")   == 2);   // przez ToBoolean
    CHECK(number_of("if (\"\") 2; else 3;")    == 3);
}

// 12.5: "if" bez gałęzi "else", gdy warunek jest fałszywy, zwraca completion
// BEZ wartości - a nie undefined. Dzięki temu nie wymazuje poprzedniej.
TEST_CASE("if bez else przy fałszywym warunku nie wnosi wartości") {
    CHECK(has_value("if (0) 2;")      == false);
    CHECK(number_of("1 + 2; if (0) 5;") == 3);
    CHECK(number_of("1 + 2; if (1) 5;") == 5);
}

TEST_CASE("niewybrana gałąź if nie jest wykonywana") {
    CHECK(number_of("var a = 9; if (0) { a = 1; } a;") == 9);
    CHECK(number_of("var a = 9; if (1) { a = 1; } a;") == 1);
    CHECK(number_of("if (1) 1; else brak;")            == 1);
}

TEST_CASE("zagnieżdżone if i dangling else") {
    // "else" należy do bliższego "if" (12.5)
    CHECK(number_of("var a = 0; if (1) if (0) a = 1; else a = 2; a;") == 2);
    CHECK(number_of("var a = 0; if (0) if (1) a = 1; else a = 2; a;") == 0);
}

// Hoisting działa na poziomie programu, więc nie zależy od tego,
// czy blok został w ogóle wykonany (10.5).
TEST_CASE("hoisting nie zależy od wykonania bloku") {
    CHECK(value_of("a; if (0) { var a = 1; }").type() == JSValueType::Undefined);
    CHECK(value_of("a; { var a = 1; }").type()        == JSValueType::Undefined);
    CHECK(number_of("if (0) { var a = 1; } var b = 2; b;") == 2);
}

// ---------------------------------------------------------------------------
// Pętle (12.6)
//
// Ciało pętli to JEDEN statement (może być blokiem). Wartość pętli to
// wartość ostatniej iteracji, która jakąś wniosła - reguła z 12.1
// zastosowana przez iteracje.
// ---------------------------------------------------------------------------

TEST_CASE("while wykonuje ciało dopóki warunek jest prawdziwy") {
    CHECK(number_of("var i = 0; while (i < 3) { i += 1; } i;") == 3);
    CHECK(number_of("var i = 0; while (i < 0) { i += 1; } i;") == 0);
    CHECK(number_of("var s = 0; var i = 0; while (i < 4) { s += i; i += 1; } s;") == 6);
}

TEST_CASE("while sprawdza warunek przed pierwszą iteracją") {
    CHECK(number_of("var a = 0; while (0) { a = 1; } a;") == 0);
    CHECK(has_value("while (0) 1;") == false);
    CHECK(number_of("1 + 2; while (0) 5;") == 3);
}

TEST_CASE("wartością pętli jest wartość ostatniej iteracji") {
    CHECK(number_of("var i = 0; while (i < 3) { i += 1; }") == 3);
    CHECK(number_of("var i = 0; do { i += 1; } while (i < 3);") == 3);
}

TEST_CASE("break przerywa pętlę natychmiast") {
    CHECK(number_of("var i = 0; while (1) { i += 1; break; } i;") == 1);
    CHECK(number_of("var i = 0; while (1) { break; i = 5; } i;") == 0);
    CHECK(number_of("var i = 0; while (1) { i += 1; if (i > 2) break; } i;") == 3);
}

// 12.6.1 - w do-while ciało wykonuje się PRZED warunkiem,
// więc zawsze co najmniej raz.
TEST_CASE("do-while wykonuje ciało co najmniej raz") {
    CHECK(number_of("var i = 0; do { i += 1; } while (0); i;") == 1);
    CHECK(number_of("var i = 0; do { i += 1; } while (i < 3); i;") == 3);
    CHECK(number_of("var a = 0; do { a = 1; } while (0); a;") == 1);
}

TEST_CASE("break w do-while") {
    CHECK(number_of("var i = 0; do { i += 1; break; } while (1); i;") == 1);
    CHECK(number_of("var i = 0; do { break; i = 5; } while (1); i;") == 0);
    CHECK(number_of("1 + 2; do ; while (0);") == 3);
}

TEST_CASE("for z pełnym nagłówkiem") {
    CHECK(number_of("var s = 0; for (var i = 0; i < 3; i += 1) { s += i; } s;") == 3);
    CHECK(number_of("var s = 0; for (var i = 0; i < 3; i += 1) s += i;") == 3);
    CHECK(number_of("var i = 0; for (; i < 3; i += 1) { } i;") == 3);
}

// 12.6.3 - każda część nagłówka jest opcjonalna; brak warunku
// znaczy "zawsze prawda".
TEST_CASE("for z pominiętymi częściami nagłówka") {
    CHECK(has_value("for (;;) break;") == false);
    CHECK(number_of("1 + 2; for (;;) break;") == 3);
    CHECK(number_of("var i = 0; for (;;) { i += 1; if (i > 2) break; } i;") == 3);
    CHECK(number_of("var i = 0; for (; 0; i += 1) { } i;") == 0);
}

// init wykonuje się dokładnie raz, przed pierwszym sprawdzeniem warunku
TEST_CASE("init pętli for wykonuje się raz") {
    CHECK(number_of("var i = 9; for (var i = 0; 0; ) {} i;") == 0);
    CHECK(number_of("var s = 0; for (var i = 0; i < 3; i += 1) { s += 1; } s;") == 3);
}

TEST_CASE("zagnieżdżone pętle") {
    CHECK(number_of("var s = 0; var i = 0;"
                    "while (i < 3) { var j = 0; while (j < 2) { s += 1; j += 1; } i += 1; } s;") == 6);
    // break przerywa tylko najbliższą pętlę
    CHECK(number_of("var s = 0;"
                    "for (var i = 0; i < 3; i += 1) { for (var j = 0; j < 3; j += 1) { break; } s += 1; } s;") == 3);
}

TEST_CASE("pętle nie tworzą zakresu dla var") {
    CHECK(number_of("for (var i = 0; i < 3; i += 1) { } i;") == 3);
    CHECK(number_of("while (0) { var a = 1; } var b = 2; b;") == 2);
    CHECK(value_of("while (0) { var a = 1; } a;").type() == JSValueType::Undefined);
}

// ---------------------------------------------------------------------------
// continue (12.7)
//
// "continue" NIE pomija warunku w do-while ani kroku update w for -
// przechodzi do nich, a nie na początek pętli.
// ---------------------------------------------------------------------------

TEST_CASE("continue pomija resztę ciała pętli") {
    CHECK(number_of("var s=0; for (var i=0; i<5; i+=1) { if (i==2) continue; s+=i; } s;") == 8);
    CHECK(number_of("var i=0; var n=0; while (i<3) { i+=1; continue; n+=1; } n;") == 0);
    CHECK(number_of("var i=0; var n=0; do { i+=1; continue; n+=1; } while (i<3); n;") == 0);
}

// Gdyby continue pomijało warunek albo update, te pętle by się nie kończyły.
TEST_CASE("continue nie pomija warunku ani kroku update") {
    CHECK(number_of("var i=0; do { i+=1; continue; } while (i<3); i;") == 3);
    CHECK(number_of("var i=0; for (; i<3; ) { i+=1; continue; } i;")   == 3);
    CHECK(number_of("var i=0; for (var j=0; j<3; j+=1) { continue; } j;") == 3);
}

// ---------------------------------------------------------------------------
// Etykiety (12.12)
//
// LabeledStatement łapie completion typu BREAK, którego target równa się
// jego etykiecie, i zamienia go na NORMAL. Wszystko inne przepuszcza.
// ---------------------------------------------------------------------------

TEST_CASE("etykieta bez break jest przezroczysta") {
    CHECK(number_of("foo: 1;")            == 1);
    CHECK(number_of("foo: { 1; 2; }")     == 2);
    CHECK(number_of("a: b: c: 5;")        == 5);
}

TEST_CASE("break z etykietą przerywa oznaczoną pętlę") {
    CHECK(number_of("var i=0; foo: while (1) { i+=1; break foo; } i;") == 1);
    CHECK(number_of("var i=0; foo: { i=1; break foo; i=5; } i;")       == 1);
}

// break z etykietą przelatuje przez WSZYSTKIE pętle po drodze
// (bo mają pusty zbiór etykiet) i zostaje złapany dopiero przez etykietę.
TEST_CASE("break z etykietą wychodzi z zagnieżdżonych pętli") {
    CHECK(number_of("var s=0;"
                    "foo: for (var i=0;i<3;i+=1) { for (var j=0;j<3;j+=1) { break foo; } s+=1; } s;") == 0);
    // dla porównania: gołe break przerywa tylko najbliższą pętlę
    CHECK(number_of("var s=0;"
                    "for (var i=0;i<3;i+=1) { for (var j=0;j<3;j+=1) { break; } s+=1; } s;") == 3);
}

// ---------------------------------------------------------------------------
// switch (12.11)
// ---------------------------------------------------------------------------

TEST_CASE("switch wybiera pasującą klauzulę") {
    CHECK(number_of("switch (1) { case 1: 10; }")         == 10);
    CHECK(has_value("switch (2) { case 1: 10; }")         == false);
    CHECK(has_value("switch (1) { }")                     == false);
    CHECK(number_of("switch (2) { case 1: 10; case 2: 20; break; }") == 20);
}

// 12.11 używa porównania ŚCISŁEGO (===), więc typy muszą się zgadzać
TEST_CASE("switch porównuje ściśle, bez konwersji typów") {
    CHECK(number_of("switch (\"1\") { case 1: 10; default: 30; }") == 30);
    CHECK(number_of("switch (1) { case \"1\": 10; default: 30; }") == 30);
    CHECK(number_of("switch (1) { case 1: 10; break; default: 30; }") == 10);
}

// Bez "break" wykonują się WSZYSTKIE kolejne klauzule, niezależnie od ich testów
TEST_CASE("switch bez break przechodzi do kolejnych klauzul") {
    CHECK(number_of("switch (1) { case 1: 10; case 2: 20; }")       == 20);
    CHECK(number_of("var s=0; switch(1){case 1: s=1; case 2: s=2;} s;") == 2);
    CHECK(number_of("var s=0; switch(1){case 1: s=1; break; case 2: s=2;} s;") == 1);
}

// "default" nie ma pierwszeństwa - pasujący "case" wygrywa nawet wtedy,
// gdy stoi za nim; a gdy nic nie pasuje, fall-through leci od "default" dalej.
TEST_CASE("default nie musi stać na końcu") {
    CHECK(number_of("switch (2) { default: 1; case 2: 2; }") == 2);
    CHECK(number_of("switch (9) { default: 1; case 2: 2; }") == 2);
    CHECK(number_of("switch (3) { case 1: 10; default: 30; }") == 30);
}

TEST_CASE("break w switchu nie przerywa otaczającej pętli") {
    CHECK(number_of("var i=0; while (1) { switch (1) { case 1: break; } i+=1; break; } i;") == 1);
    CHECK(number_of("var s=0; for (var i=0;i<3;i+=1) { switch (i) { case 1: break; } s+=1; } s;") == 3);
}

TEST_CASE("switch nie tworzy zakresu dla var") {
    CHECK(number_of("switch (1) { case 1: var a = 5; } a;") == 5);
    CHECK(value_of("a; switch (0) { case 1: var a = 5; }").type() == JSValueType::Undefined);
}

// ---------------------------------------------------------------------------
// typeof (11.4.3)
// ---------------------------------------------------------------------------

TEST_CASE("typeof zwraca nazwę typu") {
    CHECK(value_of("typeof 1;").to_string()       == "number");
    CHECK(value_of("typeof \"x\";").to_string()   == "string");
    CHECK(value_of("typeof true;").to_string()    == "boolean");
    CHECK(value_of("typeof (1+1);").to_string()   == "number");
}

// Tabela 20 w 11.4.3: typeof null to "object" - błąd projektowy
// zachowany w spec dla zgodności wstecznej.
TEST_CASE("typeof null zwraca \"object\", nie \"null\"") {
    CHECK(value_of("typeof null;").to_string() == "object");
}

// Krok 2.a: typeof sprawdza referencję PRZED GetValue, więc jako jedyny
// operator nie rzuca ReferenceError na nieznanej nazwie.
TEST_CASE("typeof niezadeklarowanej nazwy nie jest błędem") {
    CHECK(value_of("typeof brak;").to_string()   == "undefined");
    CHECK(value_of("typeof (brak);").to_string() == "undefined");
    CHECK(throws("brak;"));          // dla kontrastu
}

// "undefined" wychodzi dwiema różnymi drogami: z braku bindingu
// i z wartości istniejącej zmiennej.
TEST_CASE("typeof rozróżnia brak bindingu od wartości undefined") {
    CHECK(value_of("var a; typeof a;").to_string()   == "undefined");
    CHECK(value_of("typeof brak;").to_string()       == "undefined");
    CHECK(value_of("var a = 1; typeof a;").to_string() == "number");
}

TEST_CASE("typeof wiąże jak operator unarny") {
    CHECK(value_of("typeof brak + 1;").to_string() == "undefined1");  // (typeof brak) + 1
    CHECK(value_of("typeof 1 + 1;").to_string()    == "number1");
}

// ---------------------------------------------------------------------------
// Inkrementacja i dekrementacja (11.3, 11.4.4-5)
//
// Prefiks i postfiks modyfikują zmienną tak samo, różnią się WARTOŚCIĄ
// wyrażenia: prefiks daje nową, postfiks starą.
// ---------------------------------------------------------------------------

TEST_CASE("prefiks zwraca nową wartość, postfiks starą") {
    CHECK(number_of("var a = 1; ++a;") == 2);
    CHECK(number_of("var a = 1; a++;") == 1);
    CHECK(number_of("var a = 1; --a;") == 0);
    CHECK(number_of("var a = 1; a--;") == 1);
}

TEST_CASE("obie formy zmieniają zmienną tak samo") {
    CHECK(number_of("var a = 1; ++a; a;") == 2);
    CHECK(number_of("var a = 1; a++; a;") == 2);
    CHECK(number_of("var a = 1; --a; a;") == 0);
    CHECK(number_of("var a = 1; a--; a;") == 0);
}

// Krok 3 obu algorytmów to ToNumber(GetValue(...)) - konwersja jest
// częścią odczytu, więc TAKŻE zwracana wartość jest liczbą.
TEST_CASE("inkrementacja konwertuje operand przez ToNumber") {
    CHECK(value_of("var a = \"5\"; a++;").type() == JSValueType::Number);
    CHECK(number_of("var a = \"5\"; a++;")       == 5);      // nie "5"
    CHECK(number_of("var a = \"5\"; a++; a;")    == 6);      // nie "51"
    CHECK(number_of("var a = true; a++;")        == 1);
    CHECK(number_of("var a = true; a++; a;")     == 2);
    CHECK(std::isnan(number_of("var a = \"x\"; a++;")));
    CHECK(std::isnan(number_of("var a = \"x\"; a++; a;")));
}

// To odróżnia "a++" od "a += 1", gdzie "+" konkatenuje stringi
TEST_CASE("a++ różni się od a += 1 dla stringów") {
    CHECK(number_of("var a = \"5\"; a++; a;")     == 6);
    CHECK(value_of("var a = \"5\"; a += 1; a;").to_string() == "51");
}

TEST_CASE("postfiks zwraca starą wartość, choć zmienna jest już zmieniona") {
    CHECK(number_of("var a = 1; a++ + a;") == 3);   // 1 + 2
    CHECK(number_of("var a = 1; ++a + a;") == 4);   // 2 + 2
}

TEST_CASE("inkrementacja niezadeklarowanej nazwy jest błędem") {
    CHECK(throws("++brak;"));
    CHECK(throws("brak++;"));
}

TEST_CASE("inkrementacja w pętli") {
    CHECK(number_of("var s = 0; for (var i = 0; i < 4; i++) { s += i; } s;") == 6);
    CHECK(number_of("var i = 0; while (i++ < 3) { } i;") == 4);
}

// ---------------------------------------------------------------------------
// Rozstrzyganie identyfikatorów przez łańcuch środowisk (10.2.2.1)
//
// Evaluator nie tworzy jeszcze zagnieżdżonych środowisk sam (blok w ES5.1 nie
// zakłada nowego rekordu, funkcji jeszcze nie ma), więc łańcuch budujemy ręcznie
// i uruchamiamy program w środowisku wewnętrznym.
// ---------------------------------------------------------------------------

static Completion run_in_env(Environment& env, const std::string& source) {
    Lexer lexer(source);
    Parser parser(lexer.scan_tokens());
    auto program = parser.parse();
    Heap heap;
    Evaluator evaluator(heap);
    return evaluator.eval_program(&env, *program);
}

static JSValue value_of_in(Environment& env, const std::string& source) {
    Completion c = run_in_env(env, source);
    REQUIRE(std::holds_alternative<JSValue>(c.value));
    return std::get<JSValue>(c.value);
}

static bool throws_in(Environment& env, const std::string& source) {
    return run_in_env(env, source).type == COMPLETION_TYPE::THROW;
}

TEST_CASE("identyfikator rozstrzyga się w zewnętrznym środowisku") {
    DeclarativeEnvironment outer;
    bind(outer, "a", JSValue::number(1));

    DeclarativeEnvironment inner(&outer);

    CHECK(value_of_in(inner, "a;").to_number() == 1);
    CHECK(value_of_in(inner, "a + 1;").to_number() == 2);
}

TEST_CASE("przypisanie sięga do zewnętrznego środowiska zamiast tworzyć lokalny binding") {
    DeclarativeEnvironment outer;
    bind(outer, "a", JSValue::number(1));

    DeclarativeEnvironment inner(&outer);

    CHECK(value_of_in(inner, "a = 5;").to_number() == 5);
    CHECK(binding_number(outer, "a") == 5);
    CHECK(inner.has_binding("a") == false);
}

TEST_CASE("przypisanie złożone sięga do zewnętrznego środowiska") {
    DeclarativeEnvironment outer;
    bind(outer, "a", JSValue::number(10));

    DeclarativeEnvironment inner(&outer);

    CHECK(value_of_in(inner, "a += 2;").to_number() == 12);
    CHECK(binding_number(outer, "a") == 12);

    CHECK(value_of_in(inner, "a <<= 1;").to_number() == 24);
    CHECK(binding_number(outer, "a") == 24);
}

TEST_CASE("inkrementacja sięga do zewnętrznego środowiska") {
    DeclarativeEnvironment outer;
    bind(outer, "a", JSValue::number(1));

    DeclarativeEnvironment inner(&outer);

    CHECK(value_of_in(inner, "a++;").to_number() == 1);   // postfix zwraca starą wartość
    CHECK(binding_number(outer, "a") == 2);

    CHECK(value_of_in(inner, "++a;").to_number() == 3);
    CHECK(binding_number(outer, "a") == 3);
}

// 11.4.3 — typeof zwraca "undefined" tylko dla nazwy nierozstrzygalnej
// w całym łańcuchu, a nie dla każdej nazwy spoza bieżącego rekordu.
TEST_CASE("typeof rozstrzyga identyfikator przez cały łańcuch") {
    DeclarativeEnvironment outer;
    bind(outer, "a", JSValue::string("txt"));

    DeclarativeEnvironment inner(&outer);

    CHECK(value_of_in(inner, "typeof a;").to_string() == "string");
    CHECK(value_of_in(inner, "typeof ghost;").to_string() == "undefined");
}

// var musi założyć binding w bieżącym rekordzie także wtedy, gdy ta sama nazwa
// istnieje na zewnątrz (10.5 krok 8) — inaczej deklaracja lokalna nadpisywałaby
// zmienną zewnętrzną.
TEST_CASE("var przesłania nazwę z zewnętrznego środowiska, nie nadpisuje jej") {
    DeclarativeEnvironment outer;
    bind(outer, "a", JSValue::number(1));

    DeclarativeEnvironment inner(&outer);

    // sama deklaracja nie produkuje wartości (12.2), więc nie pytamy o nią
    run_in_env(inner, "var a = 2;");

    CHECK(inner.has_binding("a") == true);
    CHECK(binding_number(inner, "a") == 2);
    CHECK(binding_number(outer, "a") == 1);
}

TEST_CASE("nazwa nieobecna w całym łańcuchu nadal jest błędem") {
    DeclarativeEnvironment outer;
    DeclarativeEnvironment inner(&outer);

    CHECK(throws_in(inner, "ghost;"));
    CHECK(throws_in(inner, "ghost = 1;"));
    CHECK(throws_in(inner, "ghost++;"));
}

// ---------------------------------------------------------------------------
// Funkcje jako wartości (13.2)
//
// Krok A: obiekt funkcji powstaje i daje się rozpoznać. Samego wywołania
// jeszcze nie ma - te testy pilnują wyłącznie reprezentacji.
// ---------------------------------------------------------------------------

static std::size_t heap_size_after(const std::string& source) {
    Lexer lexer(source);
    Parser parser(lexer.scan_tokens());
    auto program = parser.parse();
    Heap heap;
    Evaluator evaluator(heap);
    DeclarativeEnvironment env;
    evaluator.eval_program(&env, *program);
    return heap.size();
}

TEST_CASE("deklaracja funkcji jako instrukcja nie przerywa programu") {
    // Regresja: pusta gałąź FunctionDeclaration w eval_statement przechodziła
    // przez fall-through do rzucającego default.
    CHECK(number_of("function f() {} 42;") == 42);
    CHECK(number_of("42; function f() {}") == 42);
}

TEST_CASE("typeof funkcji to \"function\" (tabela 20)") {
    CHECK(value_of("function f() {} typeof f;").to_string()      == "function");
    CHECK(value_of("var g = function () {}; typeof g;").to_string() == "function");
}

TEST_CASE("deklaracja funkcji jest wyniesiona razem z wartością (10.5)") {
    // var daje undefined przed inicjalizacją, funkcja - gotowy obiekt
    CHECK(value_of("typeof f; function f() {}").to_string() == "function");
    CHECK(value_of("var v; typeof v;").to_string()          == "undefined");
}

TEST_CASE("funkcja wygrywa z var o tej samej nazwie, niezależnie od kolejności") {
    CHECK(value_of("var f; function f() {} typeof f;").to_string() == "function");
    CHECK(value_of("function f() {} var f; typeof f;").to_string() == "function");
}

TEST_CASE("przypisanie w var nadpisuje wyniesioną funkcję dopiero przy wykonaniu") {
    CHECK(value_of("function f() {} var f = 1; typeof f;").to_string() == "number");
    CHECK(value_of("var f = 1; function f() {} typeof f;").to_string() == "number");
}

TEST_CASE("obiekt jest zawsze prawdziwy (9.2)") {
    CHECK(number_of("function f() {} if (f) 1; else 2;")        == 1);
    CHECK(number_of("var g = function () {}; if (g) 1; else 2;") == 1);
}

TEST_CASE("=== dla funkcji porównuje tożsamość, nie zawartość (11.9.6)") {
    CHECK(value_of("function f() {} f === f;").to_boolean()  == true);
    CHECK(value_of("function a() {} function b() {} a === b;").to_boolean() == false);

    // dwa osobne wyrażenia funkcyjne o identycznym ciele to różne obiekty
    CHECK(value_of("var x = function () {}; var y = function () {}; x === y;").to_boolean() == false);
    CHECK(value_of("var x = function () {}; var y = x; x === y;").to_boolean()  == true);
}

TEST_CASE("obiekty funkcji trafiają na stertę") {
    // Nie sprawdzamy dokładnej liczby komórek - jedna funkcja to dziś obiekt
    // funkcji PLUS obiekt jej właściwości "prototype", a przy kolejnych krokach
    // dojdą następne. Istotne jest, że alokacja idzie przez Heap i rośnie.
    CHECK(heap_size_after("1 + 2;") == 0);

    const std::size_t one = heap_size_after("function f() {}");
    const std::size_t two = heap_size_after("function f() {} function g() {}");

    CHECK(one > 0);
    CHECK(two > one);
    CHECK(heap_size_after("var h = function () {};") == one);   // wyrażenie kosztuje tyle samo
}

// ---------------------------------------------------------------------------
// Wywołanie funkcji (11.2.3, 13.2.1)
// ---------------------------------------------------------------------------

TEST_CASE("wartość zwracana") {
    CHECK(number_of("function f() { return 1; } f();") == 1);
    CHECK(value_of("function f() {} f();").to_string()          == "undefined");
    CHECK(value_of("function f() { return; } f();").to_string() == "undefined");
    CHECK(number_of("function f() { return 1 + 2; } f();") == 3);
}

TEST_CASE("return wychodzi z każdej konstrukcji sterującej") {
    CHECK(number_of("function f() { if (true) { return 1; } return 2; } f();") == 1);
    CHECK(number_of("function f() { while (true) { return 1; } } f();")        == 1);
    CHECK(number_of("function f() { for (;;) { return 1; } } f();")            == 1);
    CHECK(number_of("function f() { do { return 1; } while (true); } f();")    == 1);
    CHECK(number_of("function f() { switch (1) { case 1: return 1; } } f();")  == 1);
    CHECK(number_of("function f() { { { return 1; } } } f();")                 == 1);
}

TEST_CASE("return kończy funkcję, kolejne instrukcje się nie wykonują") {
    CHECK(number_of("var n = 0; function f() { return 1; n = 5; } f(); n;") == 0);
}

TEST_CASE("parametry: nadmiar ignorowany, brak daje undefined (10.5)") {
    CHECK(number_of("function f(a) { return a; } f(1);")        == 1);
    CHECK(number_of("function f(a) { return a; } f(1, 2, 3);")  == 1);
    CHECK(value_of("function f(a, b) { return b; } f(1);").to_string() == "undefined");
    CHECK(value_of("function f(a) { return a; } f();").to_string()     == "undefined");
}

TEST_CASE("kolejność wiązania: parametr, funkcja, var (10.5)") {
    // var NIE nadpisuje parametru
    CHECK(number_of("function f(a) { var a; return a; } f(1);") == 1);
    // deklaracja funkcji nadpisuje parametr
    CHECK(value_of("function f(a) { function a() {} return typeof a; } f(1);").to_string()
          == "function");
    // duplikat nazwy parametru: wygrywa ostatni
    CHECK(number_of("function f(a, a) { return a; } f(1, 2);") == 2);
}

TEST_CASE("argumenty liczone od lewej do prawej") {
    CHECK(number_of("var s = 0;"
                    "function bump(x) { s = s * 10 + x; return x; }"
                    "function f(a, b, c) { return 0; }"
                    "f(bump(1), bump(2), bump(3)); s;") == 123);
}

TEST_CASE("rekurencja") {
    CHECK(number_of("function fact(n) { if (n <= 1) return 1; return n * fact(n - 1); }"
                    "fact(10);") == 3628800);
    CHECK(number_of("function fib(n) { if (n < 2) return n; return fib(n-1) + fib(n-2); }"
                    "fib(15);") == 610);
}

// ---------------------------------------------------------------------------
// Domknięcia — outer nowego środowiska to DOMKNIĘCIE funkcji,
// a nie środowisko wywołującego. To odróżnia zasięg leksykalny od dynamicznego.
// ---------------------------------------------------------------------------

TEST_CASE("funkcja widzi zmienne z miejsca definicji") {
    CHECK(number_of("function outer() { var n = 5; function inner() { return n; }"
                    "return inner(); } outer();") == 5);
}

TEST_CASE("domknięcie przeżywa zakończenie wywołania") {
    CHECK(number_of("function counter() { var n = 0; return function () { return ++n; }; }"
                    "var c = counter(); c(); c();") == 2);
}

TEST_CASE("każde wywołanie tworzy niezależne domknięcie") {
    CHECK(number_of("function counter() { var n = 0; return function () { return ++n; }; }"
                    "var c = counter(); var d = counter();"
                    "c(); c(); c(); d();") == 1);
}

TEST_CASE("zasięg jest leksykalny, nie dynamiczny") {
    // gdyby outer nowego środowiska wskazywał na wywołującego,
    // inner zobaczyłoby n = 99 zamiast n = 1
    CHECK(number_of("var n = 1;"
                    "function inner() { return n; }"
                    "function outer() { var n = 99; return inner(); }"
                    "outer();") == 1);
}

TEST_CASE("zmienne lokalne nie wyciekają na zewnątrz") {
    CHECK(value_of("function f() { var local = 1; } f(); typeof local;").to_string()
          == "undefined");
    CHECK(value_of("function outer() { function inner() { var b = 1; } inner();"
                   "return typeof b; } outer();").to_string() == "undefined");
}

// ---------------------------------------------------------------------------
// Błędy
// ---------------------------------------------------------------------------

TEST_CASE("wywołanie czegoś, co nie jest funkcją, to błąd") {
    CHECK(throws("var x = 1; x();"));
    CHECK(throws("var x; x();"));
    CHECK(throws("(1)();"));
}

TEST_CASE("nieskończona rekurencja daje błąd, a nie przepełnienie stosu") {
    CHECK(throws("function f() { return f(); } f();"));
}

// ---------------------------------------------------------------------------
// Funkcje wbudowane i środowisko globalne
//
// print nie jest częścią ECMAScript - to rusztowanie do czasu powstania
// obiektów i prawdziwego console.log. Wyjście idzie przez wstrzykiwany
// strumień, żeby dało się je przechwycić bez łapania stdout procesu.
// ---------------------------------------------------------------------------

#include <sstream>

static std::string output_of(const std::string& source) {
    Lexer lexer(source);
    Parser parser(lexer.scan_tokens());
    auto program = parser.parse();

    Heap heap;

    std::ostringstream out;
    Evaluator evaluator(heap, out);

    Environment *global = setup_globals(heap, evaluator);

    evaluator.eval_program(global, *program);

    return out.str();
}

static bool throws_with_globals(const std::string& source) {
    Lexer lexer(source);
    Parser parser(lexer.scan_tokens());
    auto program = parser.parse();

    Heap heap;

    std::ostringstream out;
    Evaluator evaluator(heap, out);

    Environment *global = setup_globals(heap, evaluator);

    return evaluator.eval_program(global, *program).type == COMPLETION_TYPE::THROW;
}

TEST_CASE("print wypisuje przez ToString") {
    CHECK(output_of("print(1);")         == "1\n");
    CHECK(output_of("print(\"a\");")     == "a\n");
    CHECK(output_of("print(true);")      == "true\n");
    CHECK(output_of("print(null);")      == "null\n");
    CHECK(output_of("print(undefined);") == "undefined\n");
}

TEST_CASE("print skleja argumenty spacją") {
    CHECK(output_of("print(1, 2);")        == "1 2\n");
    CHECK(output_of("print(1, \"a\", 2);") == "1 a 2\n");
    CHECK(output_of("print();")            == "\n");
}

TEST_CASE("print używa Number::toString, nie operator<< na double") {
    CHECK(output_of("print(0.1 + 0.2);") == "0.30000000000000004\n");
    CHECK(output_of("print(1e21);")      == "1e+21\n");
    CHECK(output_of("print(1e20);")      == "100000000000000000000\n");
    CHECK(output_of("print(-0);")        == "0\n");
    CHECK(output_of("print(1 / 0);")     == "Infinity\n");
    CHECK(output_of("print(0 / 0);")     == "NaN\n");
}

TEST_CASE("print zwraca undefined") {
    CHECK(output_of("print(print(1));") == "1\nundefined\n");
}

TEST_CASE("NaN, Infinity i undefined to wiązania globalne, nie literały") {
    CHECK(output_of("print(NaN);")               == "NaN\n");
    CHECK(output_of("print(Infinity);")          == "Infinity\n");
    CHECK(output_of("print(-Infinity);")         == "-Infinity\n");
    CHECK(output_of("print(undefined);")         == "undefined\n");
    CHECK(output_of("print(typeof NaN);")        == "number\n");
    CHECK(output_of("print(typeof undefined);")  == "undefined\n");
    CHECK(output_of("print(NaN === NaN);")       == "false\n");
}

TEST_CASE("funkcja natywna jest wywoływalna jak zwykła") {
    CHECK(output_of("print(typeof print);")             == "function\n");
    CHECK(output_of("var p = print; p(1);")             == "1\n");
    CHECK(output_of("function f(g) { g(1); } f(print);") == "1\n");
}

TEST_CASE("wywołanie niewywoływalnego globala nadal jest błędem") {
    CHECK(throws_with_globals("NaN();"));
    CHECK(throws_with_globals("undefined();"));
}

// ---------------------------------------------------------------------------
// FizzBuzz - kryterium zamknięcia etapu 1.
// Wyjście sprawdzone jako identyczne z `node` dla zakresu 1..100.
// ---------------------------------------------------------------------------

TEST_CASE("FizzBuzz") {
    CHECK(output_of(
        "for (var i = 1; i <= 20; i++) {"
        "    if (i % 15 === 0)     print(\"FizzBuzz\");"
        "    else if (i % 3 === 0) print(\"Fizz\");"
        "    else if (i % 5 === 0) print(\"Buzz\");"
        "    else                  print(i);"
        "}")
        == "1\n2\nFizz\n4\nBuzz\nFizz\n7\n8\nFizz\nBuzz\n"
           "11\nFizz\n13\n14\nFizzBuzz\n16\n17\nFizz\n19\nBuzz\n");
}

TEST_CASE("FizzBuzz z funkcją i domknięciem daje ten sam wynik") {
    CHECK(output_of(
        "function fizzbuzz(n) {"
        "    if (n % 15 === 0) return \"FizzBuzz\";"
        "    if (n % 3 === 0)  return \"Fizz\";"
        "    if (n % 5 === 0)  return \"Buzz\";"
        "    return n;"
        "}"
        "for (var i = 1; i <= 15; i++) print(fizzbuzz(i));")
        == "1\n2\nFizz\n4\nBuzz\nFizz\n7\n8\nFizz\nBuzz\n11\nFizz\n13\n14\nFizzBuzz\n");
}

// ---------------------------------------------------------------------------
// Propagacja zakończeń nagłych przez warstwy
//
// Po przejściu na completion records każde miejsce konsumujące wynik
// evaluate() musi sprawdzić is_abrupt(). Pominięcie zamienia THROW
// w zwykłą wartość - błąd cichy, bo nic się nie wywala.
// ---------------------------------------------------------------------------

TEST_CASE("throw z argumentu wywołania nie ginie") {
    CHECK(throws("function f(a) { return a; } f(brak);"));
    CHECK(throws("function f() {} f(1, brak);"));
    CHECK(throws("function f() {} f(brak, 1);"));
    CHECK(throws_with_globals("print(brak);"));
}

TEST_CASE("throw z callee nie ginie") {
    CHECK(throws("brak(1);"));
    CHECK(throws("brak();"));
}

TEST_CASE("throw z głębi rekurencji dociera na wierzch") {
    CHECK(throws("function f() { return f(); } f();"));
    CHECK(throws("function f() { return g(); } function g() { return f(); } f();"));
}

TEST_CASE("throw z ciała funkcji nie zamienia się w undefined") {
    CHECK(throws("function f() { return brak; } f();"));
    CHECK(throws("function f() { brak; } f();"));
    CHECK(throws("function f() { if (true) { return brak; } } f();"));
    CHECK(throws("function f() { while (true) { return brak; } } f();"));
}

TEST_CASE("throw z warunku if") {
    CHECK(throws("if (brak) 1;"));
    CHECK(throws("if (brak) 1; else 2;"));
}

TEST_CASE("throw z warunku i kroku pętli") {
    CHECK(throws("while (brak) {}"));
    CHECK(throws("do {} while (brak);"));
    CHECK(throws("for (; brak; ) {}"));
    CHECK(throws("for (brak; ; ) {}"));
    CHECK(throws("var i = 0; for (; i < 1; brak) { i += 1; }"));
}

TEST_CASE("throw z ciała pętli nie jest mylony z break") {
    CHECK(throws("while (true) { brak; }"));
    CHECK(throws("do { brak; } while (true);"));
    CHECK(throws("for (;;) { brak; }"));
    CHECK(throws("for (var i = 0; i < 3; i += 1) { if (i == 1) brak; }"));
}

TEST_CASE("throw z wyrażeń switcha") {
    CHECK(throws("switch (brak) {}"));
    CHECK(throws("switch (1) { case brak: 1; }"));
    CHECK(throws("switch (1) { case 1: brak; }"));
}

TEST_CASE("throw z operatora warunkowego i sekwencji") {
    CHECK(throws("brak ? 1 : 2;"));
    CHECK(throws("1 ? brak : 2;"));
    CHECK(throws("0 ? 1 : brak;"));
    CHECK(throws("brak, 1;"));
    CHECK(throws("1, brak;"));
}

TEST_CASE("throw z inicjalizatora deklaracji i z instrukcji etykietowanej") {
    CHECK(throws("var x = brak;"));
    CHECK(throws("var a = 1, b = brak;"));
    CHECK(throws("foo: { brak; }"));
    CHECK(throws("foo: while (true) { brak; }"));
}

// ---------------------------------------------------------------------------
// Zakończenia nagłe adresowane do konstrukcji nadal działają
// (regresja: blanket is_abrupt() w eval_statement_list i eval_labeled
//  odsyłał break/continue w górę zamiast je obsłużyć)
// ---------------------------------------------------------------------------

TEST_CASE("break i continue nie uciekają z pętli") {
    CHECK(number_of("var i = 0; while (1) { i += 1; break; } i;")          == 1);
    CHECK(number_of("var i = 0; for (;;) { i += 1; if (i > 2) break; } i;") == 3);
    CHECK(number_of("var i = 0; do { i += 1; continue; } while (i < 3); i;") == 3);
}

TEST_CASE("break z etykietą jest przechwytywany przez instrukcję etykietowaną") {
    CHECK(number_of("var i = 0; foo: while (1) { i += 1; break foo; } i;") == 1);
    CHECK(number_of("var s = 0;"
                    "outer: for (var i = 0; i < 3; i += 1) {"
                    "    for (var j = 0; j < 3; j += 1) { if (j == 1) break outer; s += 1; }"
                    "} s;") == 1);

    // TODO: `continue` z etykietą nie jest jeszcze obsługiwany - wymaga, żeby
    // pętla znała własne etykiety. Dziś taki continue przelatuje przez pętlę
    // w górę i nie zostaje nigdzie przechwycony.
}

TEST_CASE("return nadal zatrzymuje się na wywołaniu funkcji") {
    // regresja: blanket is_abrupt() w call_function przepuszczał RETURN
    // na zewnątrz zamiast zamienić go na wartość wywołania
    Completion c = run("function f() { return 1; } f();");
    CHECK(c.type == COMPLETION_TYPE::NORMAL);
    CHECK(c.get_value_or_undefined().to_number() == 1);
}

// ---------------------------------------------------------------------------
// throw (12.13)
// ---------------------------------------------------------------------------

TEST_CASE("throw kończy program zakończeniem THROW") {
    CHECK(throws("throw 1;"));
    CHECK(throws("throw \"x\";"));
    CHECK(throws("throw true;"));
    CHECK(throws("throw undefined;"));
}

TEST_CASE("throw niesie rzuconą wartość") {
    CHECK(thrown("throw 1;")     == "1");
    CHECK(thrown("throw \"x\";") == "x");
    CHECK(thrown("throw 1 + 2;") == "3");
}

TEST_CASE("błąd w argumencie throw wygrywa z samym throw") {
    // Sprawdzamy RODZAJ błędu, nie dokładne brzmienie komunikatu.
    // Nierozwiązywalna referencja to ReferenceError, nie TypeError.
    CHECK(thrown("throw brak;").rfind("ReferenceError:", 0) == 0);
    CHECK(value_of("try { throw brak; } catch (e) { e instanceof ReferenceError; }").to_boolean());
}

TEST_CASE("throw przerywa wykonanie") {
    CHECK(thrown("var n = 0; throw 1; n = 5;") == "1");
    CHECK(throws("function f() { throw 1; } f();"));
    CHECK(throws("while (true) { throw 1; }"));
}

// ---------------------------------------------------------------------------
// try / catch (12.14)
// ---------------------------------------------------------------------------

TEST_CASE("catch łapie rzuconą wartość") {
    CHECK(number_of("try { throw 1; } catch (e) { e; }")        == 1);
    CHECK(value_of("try { throw \"x\"; } catch (e) { e; }").to_string() == "x");
    CHECK(number_of("try { throw 1 + 2; } catch (e) { e; }")    == 3);
}

TEST_CASE("catch nie wykonuje się, gdy nic nie rzucono") {
    CHECK(number_of("try { 1; } catch (e) { 2; }") == 1);
    CHECK(number_of("var n = 0; try { 1; } catch (e) { n = 5; } n;") == 0);
}

TEST_CASE("catch łapie także błędy wewnętrzne silnika") {
    CHECK(number_of("try { brak; } catch (e) { 1; }")           == 1);
    CHECK(number_of("try { var x = 1; x(); } catch (e) { 1; }") == 1);
    CHECK(number_of("try { function f() { return f(); } f(); } catch (e) { 1; }") == 1);
}

TEST_CASE("parametr catch ma własny zasięg blokowy") {
    CHECK(number_of("var e = 9; try { throw 1; } catch (e) { } e;") == 9);
    CHECK(number_of("var e = 9; try { throw 1; } catch (e) { e; } e;") == 9);
    // poza blokiem catch parametr nie istnieje
    CHECK(value_of("try { throw 1; } catch (e) { } typeof e;").to_string() == "undefined");
}

TEST_CASE("throw z wnętrza catch leci dalej") {
    CHECK(thrown("try { throw 1; } catch (e) { throw e + 1; }") == "2");
    CHECK(throws("try { throw 1; } catch (e) { brak; }"));
}

TEST_CASE("try zagnieżdżone") {
    CHECK(number_of("try { try { throw 1; } catch (e) { throw e + 1; } } catch (e) { e; }") == 2);
    CHECK(number_of("try { throw 1; } catch (e) { try { throw 2; } catch (e2) { e2; } }")   == 2);
}

// ---------------------------------------------------------------------------
// finally — nadpisuje wynik TYLKO gdy samo kończy się nagle
// ---------------------------------------------------------------------------

TEST_CASE("finally wykonuje się zawsze") {
    CHECK(number_of("var n = 0; try { 1; } finally { n = 5; } n;")                  == 5);
    CHECK(number_of("var n = 0; try { throw 1; } catch (e) { } finally { n = 5; } n;") == 5);
    CHECK(number_of("var n = 0; function f() { try { return 1; } finally { n = 5; } } f(); n;") == 5);
    CHECK(number_of("var n = 0; while (true) { try { break; } finally { n = 5; } } n;") == 5);
}

TEST_CASE("finally normalne NIE nadpisuje wyniku try") {
    CHECK(number_of("function f() { try { return 1; } finally { 2; } } f();")   == 1);
    CHECK(number_of("function f() { try { return 1; } finally { } } f();")      == 1);
    CHECK(number_of("try { throw 1; } catch (e) { e; } finally { 99; }")        == 1);
}

TEST_CASE("finally nagłe nadpisuje wynik try") {
    CHECK(number_of("function f() { try { return 1; } finally { return 2; } } f();") == 2);
    CHECK(number_of("function f() { try { throw 1; } finally { return 2; } } f();")  == 2);
    CHECK(thrown("try { return_nothing; } finally { throw 2; }") == "2");
}

TEST_CASE("try bez catch przepuszcza wyjątek, ale wykonuje finally") {
    CHECK(thrown("var n = 0; try { throw 1; } finally { n = 5; }") == "1");
    CHECK(throws("try { throw 1; } finally { }"));
}

TEST_CASE("finally nie gubi break ani continue") {
    CHECK(number_of("var i = 0; while (i < 3) { try { i += 1; continue; } finally { } } i;") == 3);
    CHECK(number_of("var i = 0; while (true) { try { i += 1; break; } finally { } } i;")     == 1);
}

// ---------------------------------------------------------------------------
// var wewnątrz try jest zasięgu funkcyjnego, parametr catch nie
// ---------------------------------------------------------------------------

TEST_CASE("var z try, catch i finally jest wynoszony") {
    CHECK(number_of("try { var x = 1; } catch (e) {} x;")              == 1);
    CHECK(number_of("try { throw 1; } catch (e) { var y = 2; } y;")    == 2);
    CHECK(number_of("try { 1; } finally { var z = 3; } z;")            == 3);
    CHECK(value_of("try { var q; } catch (e) {} typeof q;").to_string() == "undefined");
}

// ---------------------------------------------------------------------------
// Obiekty z poziomu JavaScriptu (11.1.5, 11.2.1, 8.12)
// ---------------------------------------------------------------------------

TEST_CASE("literał obiektu i dostęp przez kropkę") {
    CHECK(number_of("var o = {a: 1}; o.a;")            == 1);
    CHECK(number_of("var o = {a: 1, b: 2}; o.b;")      == 2);
    CHECK(value_of("var o = {}; o.x;").to_string()     == "undefined");
    CHECK(value_of("({a: 1}).a;").to_string()          == "1");
}

TEST_CASE("dostęp przez klucz obliczany") {
    CHECK(number_of("var o = {a: 1}; o[\"a\"];")             == 1);
    CHECK(number_of("var o = {\"a b\": 1}; o[\"a b\"];")     == 1);
    CHECK(number_of("var o = {a: 1}; var k = \"a\"; o[k];")  == 1);
    // klucz jest konwertowany przez ToString
    CHECK(number_of("var o = {}; o[1] = 5; o[\"1\"];")       == 5);
}

TEST_CASE("słowo kluczowe jest legalną nazwą właściwości") {
    CHECK(number_of("var o = {if: 1}; o.if;")           == 1);
    CHECK(number_of("var o = {default: 2}; o.default;") == 2);
    CHECK(number_of("var o = {new: 3}; o[\"new\"];")    == 3);
}

TEST_CASE("klucz liczbowy w literale przechodzi przez ToString") {
    CHECK(number_of("var o = {1: 'a'}; o[1] === o[\"1\"] ? 1 : 0;") == 1);
}

TEST_CASE("zapis do właściwości") {
    CHECK(number_of("var o = {}; o.x = 1; o.x;")           == 1);
    CHECK(number_of("var o = {a: 1}; o.a = 2; o.a;")       == 2);
    CHECK(number_of("var o = {}; o[\"y\"] = 3; o.y;")      == 3);
    CHECK(number_of("var o = {a: 1}; o.a += 5; o.a;")      == 6);
    CHECK(number_of("var o = {a: 1}; o.a++; o.a;")         == 2);
    CHECK(number_of("var o = {a: 1}; var v = o.a++; v;")   == 1);
}

TEST_CASE("null i undefined nie mają właściwości") {
    CHECK(throws("null.x;"));
    CHECK(throws("undefined.x;"));
    CHECK(throws("var o; o.x;"));
    CHECK(throws("null.x = 1;"));
}

TEST_CASE("getter i setter w literale obiektu") {
    CHECK(number_of("var o = {get x() { return 5; }}; o.x;") == 5);
    CHECK(number_of("var o = {v: 0, set x(n) { this.v = n; }}; o.x = 7; o.v;") == 7);
    CHECK(number_of("var o = {v: 3, get x() { return this.v; }}; o.x;") == 3);
}

TEST_CASE("typeof i delete na właściwościach") {
    CHECK(value_of("var o = {a: 1}; typeof o.a;").to_string()   == "number");
    CHECK(value_of("var o = {}; typeof o.brak;").to_string()    == "undefined");
    CHECK(value_of("var o = {a: 1}; delete o.a; o.a;").to_string() == "undefined");
    CHECK(value_of("var o = {a: 1}; delete o.a;").to_string()   == "true");
    CHECK(value_of("var o = {}; delete o.brak;").to_string()    == "true");
}

TEST_CASE("obiekt jest zawsze prawdziwy") {
    CHECK(number_of("if ({}) 1; else 2;")       == 1);
    CHECK(number_of("if ({a: 1}) 1; else 2;")   == 1);
}

TEST_CASE("=== dla obiektów porównuje tożsamość") {
    CHECK(value_of("var o = {}; o === o;").to_string()               == "true");
    CHECK(value_of("({}) === ({});").to_string()                     == "false");
    CHECK(value_of("var a = {}; var b = a; a === b;").to_string()    == "true");
}

// ---------------------------------------------------------------------------
// Tablice (11.1.4, 15.4)
// ---------------------------------------------------------------------------

TEST_CASE("literał tablicy") {
    CHECK(number_of("[1, 2, 3].length;")     == 3);
    CHECK(number_of("[1, 2, 3][0];")         == 1);
    CHECK(number_of("[1, 2, 3][2];")         == 3);
    CHECK(number_of("[].length;")            == 0);
    CHECK(value_of("[1][5];").to_string()    == "undefined");
}

TEST_CASE("dziury liczą się do length") {
    CHECK(number_of("[1, , 3].length;")      == 3);
    CHECK(value_of("[1, , 3][1];").to_string() == "undefined");
    CHECK(number_of("[,].length;")           == 1);
    CHECK(number_of("[1, 2, ].length;")      == 2);   // przecinek na końcu NIE dodaje dziury
}

TEST_CASE("zapis pod indeks podnosi length") {
    CHECK(number_of("var a = []; a[0] = 1; a.length;")   == 1);
    CHECK(number_of("var a = []; a[5] = 1; a.length;")   == 6);
    CHECK(number_of("var a = [1]; a[3] = 1; a.length;")  == 4);
    CHECK(number_of("var a = []; a[0] = 1; a[0];")       == 1);
}

TEST_CASE("skrócenie length usuwa elementy") {
    CHECK(value_of("var a = [1,2,3]; a.length = 1; a[1];").to_string() == "undefined");
    CHECK(number_of("var a = [1,2,3]; a.length = 1; a.length;")        == 1);
    CHECK(number_of("var a = [1,2,3]; a.length = 1; a[0];")            == 1);
}

TEST_CASE("klucz nieindeksowy nie rusza length") {
    CHECK(number_of("var a = [1]; a[\"x\"] = 5; a.length;")   == 1);
    CHECK(number_of("var a = [1]; a[\"x\"] = 5; a.x;")        == 5);
    CHECK(number_of("var a = []; a[\"01\"] = 5; a.length;")   == 0);   // "01" nie jest indeksem
}

TEST_CASE("typeof tablicy to object") {
    CHECK(value_of("typeof [];").to_string() == "object");
}

// ---------------------------------------------------------------------------
// this (10.4.3, 11.2.3)
// ---------------------------------------------------------------------------

TEST_CASE("this w wywołaniu metody to obiekt przed kropką") {
    CHECK(number_of("var o = {x: 1, f: function () { return this.x; }}; o.f();") == 1);
    CHECK(number_of("var o = {x: 5, f: function () { return this.x; }}; o[\"f\"]();") == 5);
}

TEST_CASE("this gubi się przy odpięciu metody od obiektu") {
    // g() to zwykłe wywołanie — this nie jest już obiektem o, tylko (10.4.3,
    // poza strict) obiektem globalnym. Odczyt nieistniejącej właściwości
    // globalu daje undefined, więc nic nie rzuca.
    CHECK(value_of("var o = {x: 1, f: function () { return this.x; }};"
                   "var g = o.f; g();").type() == JSValueType::Undefined);

    // A że to naprawdę obiekt globalny, widać po tym, że globalna zmienna
    // o tej samej nazwie zostaje "przechwycona" przez odpiętą metodę.
    CHECK(number_of("var x = 42;"
                    "var o = {x: 1, f: function () { return this.x; }};"
                    "var g = o.f; g();") == 42);
}

TEST_CASE("this z prototypu wskazuje na obiekt, przez który wołano") {
    CHECK(number_of("var base = {f: function () { return this.x; }};"
                    "var o = {x: 7}; o.f = base.f; o.f();") == 7);
}

TEST_CASE("metoda modyfikuje obiekt przez this") {
    CHECK(number_of("var o = {x: 0, bump: function () { this.x = this.x + 1; }};"
                    "o.bump(); o.bump(); o.x;") == 2);
}

// ---------------------------------------------------------------------------
// new i konstruktory (11.2.2, 13.2.2)
// ---------------------------------------------------------------------------

TEST_CASE("konstruktor ustawia właściwości przez this") {
    CHECK(number_of("function F() { this.x = 1; } new F().x;")          == 1);
    CHECK(number_of("function F(a) { this.x = a; } new F(9).x;")        == 9);
    CHECK(number_of("function F(a, b) { this.s = a + b; } new F(1,2).s;") == 3);
}

TEST_CASE("metody z prototypu konstruktora są widoczne w instancji") {
    CHECK(number_of("function F() {} F.prototype.f = function () { return 7; };"
                    "new F().f();") == 7);
    CHECK(number_of("function F() { this.x = 3; }"
                    "F.prototype.get = function () { return this.x; };"
                    "new F().get();") == 3);
}

TEST_CASE("każda instancja jest osobnym obiektem") {
    CHECK(value_of("function F() {} new F() === new F();").to_string() == "false");
    CHECK(number_of("function F() { this.x = 0; }"
                    "var a = new F(); var b = new F(); a.x = 5; b.x;") == 0);
}

TEST_CASE("konstruktor zwracający obiekt nadpisuje instancję (13.2.2 krok 9)") {
    CHECK(number_of("function F() { this.x = 1; return {x: 2}; } new F().x;") == 2);
    // zwrócenie prymitywu jest ignorowane
    CHECK(number_of("function F() { this.x = 1; return 5; } new F().x;")      == 1);
}

TEST_CASE("funkcja ma właściwość prototype z constructor") {
    CHECK(value_of("function F() {} typeof F.prototype;").to_string()   == "object");
    CHECK(value_of("function F() {} F.prototype.constructor === F;").to_string() == "true");
}

TEST_CASE("new na czymś, co nie jest konstruktorem") {
    CHECK(throws("new 1;"));
    CHECK(throws("var x = {}; new x;"));
}

TEST_CASE("length funkcji to liczba parametrów") {
    CHECK(number_of("function f() {} f.length;")           == 0);
    CHECK(number_of("function f(a, b, c) {} f.length;")    == 3);
}

// ---------------------------------------------------------------------------
// Operator in (11.8.7)
// ---------------------------------------------------------------------------

TEST_CASE("in sprawdza własne właściwości") {
    CHECK(value_of("var o = {a: 1}; \"a\" in o;").to_string() == "true");
    CHECK(value_of("var o = {a: 1}; \"b\" in o;").to_string() == "false");
    CHECK(value_of("\"a\" in {a: 1};").to_string()            == "true");
}

TEST_CASE("in widzi łańcuch prototypów") {
    CHECK(value_of("\"toString\" in {};").to_string()   == "true");
    CHECK(value_of("\"valueOf\" in {};").to_string()    == "true");
    CHECK(value_of("function F() {} F.prototype.p = 1;"
                   "\"p\" in new F();").to_string()     == "true");
}

TEST_CASE("klucz w in przechodzi przez ToString") {
    CHECK(value_of("0 in [1, 2];").to_string()   == "true");
    CHECK(value_of("2 in [1, 2];").to_string()   == "false");
    CHECK(value_of("var o = {}; o[1] = 5; 1 in o;").to_string() == "true");
}

TEST_CASE("length tablicy istnieje, ale nie jest enumerowalne") {
    CHECK(value_of("\"length\" in [];").to_string() == "true");
}

TEST_CASE("prawy operand in musi być obiektem") {
    CHECK(throws("\"a\" in 1;"));
    CHECK(throws("\"a\" in \"abc\";"));
    CHECK(throws("\"a\" in null;"));
    CHECK(throws("\"a\" in undefined;"));
}

TEST_CASE("in ma priorytet operatora relacyjnego") {
    // ("a" in o) === true, a nie "a" in (o === true)
    CHECK(value_of("var o = {a: 1}; \"a\" in o === true;").to_string() == "true");
}

TEST_CASE("in jest legalne w nagłówku for, gdy jest w nawiasach") {
    CHECK(number_of("var o = {a: 1}; var n = 0;"
                    "for (var i = (\"a\" in o); n < 1; n++) { }"
                    "n;") == 1);
}

// ---------------------------------------------------------------------------
// for ... in (12.6.4)
// ---------------------------------------------------------------------------

TEST_CASE("for-in po własnych właściwościach") {
    CHECK(value_of("var o = {a: 1, b: 2}; var s = '';"
                   "for (var k in o) s += k + ','; s;").to_string() == "a,b,");
    CHECK(value_of("var o = {}; var s = '';"
                   "for (var k in o) s += k + ','; s;").to_string() == "");
}

TEST_CASE("for-in daje wartości przez klucz") {
    CHECK(number_of("var o = {a: 1, b: 2}; var sum = 0;"
                    "for (var k in o) sum += o[k]; sum;") == 3);
}

TEST_CASE("for-in: indeksy rosnąco, potem reszta w kolejności wstawienia") {
    CHECK(value_of("var o = {}; o.b = 1; o[2] = 1; o.a = 1; o[10] = 1;"
                   "var s = ''; for (var k in o) s += k + ','; s;").to_string()
          == "2,10,b,a,");
}

TEST_CASE("for-in po tablicy daje indeksy jako napisy, bez length") {
    CHECK(value_of("var s = ''; for (var k in [7, 8]) s += k + ','; s;").to_string()
          == "0,1,");
    CHECK(value_of("var s = ''; for (var k in [7, 8]) s += typeof k; s;").to_string()
          == "stringstring");
}

TEST_CASE("for-in widzi właściwości z prototypu") {
    CHECK(value_of("function F() {} F.prototype.p = 1;"
                   "var o = new F(); o.own = 2;"
                   "var s = ''; for (var k in o) s += k + ','; s;").to_string()
          == "own,p,");
}

TEST_CASE("właściwość przesłonięta pojawia się raz") {
    CHECK(value_of("function F() {} F.prototype.x = 1;"
                   "var o = new F(); o.x = 2;"
                   "var s = ''; for (var k in o) s += k + ','; s;").to_string() == "x,");
    CHECK(number_of("function F() {} F.prototype.x = 1;"
                    "var o = new F(); o.x = 2;"
                    "var v = 0; for (var k in o) v = o[k]; v;") == 2);
}

TEST_CASE("właściwości nieenumerowalne są pomijane") {
    // constructor na F.prototype nie jest enumerowalne
    CHECK(value_of("function F() {} var o = new F();"
                   "var s = ''; for (var k in o) s += k + ','; s;").to_string() == "");
}

TEST_CASE("null i undefined po prawej: pętla się nie wykonuje") {
    CHECK(number_of("var n = 0; for (var k in null) n++; n;")      == 0);
    CHECK(number_of("var n = 0; for (var k in undefined) n++; n;") == 0);
}

TEST_CASE("for-in bez deklaracji, z celem będącym zmienną") {
    CHECK(value_of("var o = {a: 1, b: 2}; var k; var s = '';"
                   "for (k in o) s += k + ','; s;").to_string() == "a,b,");
}

TEST_CASE("for-in z celem będącym właściwością") {
    CHECK(value_of("var o = {a: 1, b: 2}; var box = {};"
                   "for (box.k in o) { } box.k;").to_string() == "b");
}

TEST_CASE("break i continue w for-in") {
    CHECK(value_of("var o = {a: 1, b: 2, c: 3}; var s = '';"
                   "for (var k in o) { if (k === 'b') break; s += k; } s;").to_string() == "a");
    CHECK(value_of("var o = {a: 1, b: 2, c: 3}; var s = '';"
                   "for (var k in o) { if (k === 'b') continue; s += k; } s;").to_string() == "ac");
}

TEST_CASE("return z wnętrza for-in") {
    CHECK(value_of("function f() { var o = {a: 1}; for (var k in o) return k; }"
                   "f();").to_string() == "a");
}

TEST_CASE("zmienna pętli przeżywa pętlę") {
    CHECK(value_of("var o = {a: 1, b: 2}; for (var k in o) { } k;").to_string() == "b");
}

TEST_CASE("zwykły for nadal działa obok for-in") {
    CHECK(number_of("var n = 0; for (var i = 0; i < 3; i++) n += i; n;") == 3);
    CHECK(number_of("var n = 0; for (;;) { n++; break; } n;")            == 1);
}

// ---------------------------------------------------------------------------
// ToPrimitive w ewaluatorze (9.1, 8.12.8)
//
// Konwersja obiektu woła valueOf/toString użytkownika, więc może mieć efekty
// uboczne i może rzucić. Te testy sprawdzają trzy rzeczy: że konwersja w ogóle
// zachodzi, że idzie z właściwym hintem i w kolejności ze źródła, oraz że
// rzucony wyjątek JS wraca jako completion THROW, a nie wyjątek C++.
// ---------------------------------------------------------------------------

TEST_CASE("unarne +, - i ~ wołają valueOf") {
    CHECK(number_of("+({valueOf: function () { return 5; }});") == 5);
    CHECK(number_of("-({valueOf: function () { return 5; }});") == -5);
    CHECK(number_of("~({valueOf: function () { return 5; }});") == -6);
}

TEST_CASE("unarny plus bez prymitywnego valueOf schodzi do toString") {
    // Object.prototype.valueOf zwraca sam obiekt, więc [[DefaultValue]] go
    // pomija i próbuje toString.
    CHECK(number_of("+({toString: function () { return \"7\"; }});") == 7);
    CHECK(std::isnan(number_of("+({});")));   // "[object Object]" -> NaN
}

TEST_CASE("++ i -- na obiekcie idą przez ToNumber") {
    CHECK(number_of("var o = {valueOf: function () { return 5; }}; o++; o;") == 6);
    CHECK(number_of("var o = {valueOf: function () { return 5; }}; --o;")    == 4);
    CHECK(std::isnan(number_of("var o = {}; o++; o;")));
}

TEST_CASE("postfiksowe ++ zwraca starą wartość PO ToNumber, nie sam obiekt") {
    CHECK(number_of("var o = {valueOf: function () { return 5; }}; o++;") == 5);
    CHECK(value_of("var o = {valueOf: function () { return 5; }}; typeof o++;").to_string() == "number");
}

TEST_CASE("== obiektu z prymitywem woła ToPrimitive w obie strony") {
    CHECK(value_of("({valueOf: function () { return 1; }}) == 1;").to_boolean());
    CHECK(value_of("1 == {valueOf: function () { return 1; }};").to_boolean());
    CHECK(value_of("\"[object Object]\" == {};").to_boolean());
    CHECK(value_of("({valueOf: function () { return 1; }}) != 2;").to_boolean());
}

TEST_CASE("== obiektu z booleanem: boolean najpierw staje się liczbą") {
    CHECK(value_of("true == {valueOf: function () { return 1; }};").to_boolean());
    CHECK(value_of("({valueOf: function () { return 0; }}) == false;").to_boolean());
}

TEST_CASE("== dwóch obiektów porównuje tożsamość i nie woła valueOf") {
    CHECK(value_of("var o = {}; o == o;").to_boolean());
    CHECK_FALSE(value_of("({}) == {};").to_boolean());
    CHECK(number_of("var n = 0; var o = {valueOf: function () { n = n + 1; return 1; }}; o == o; n;") == 0);
}

TEST_CASE("== obiektu z null i undefined jest false bez konwersji") {
    CHECK_FALSE(value_of("({}) == null;").to_boolean());
    CHECK_FALSE(value_of("undefined == {};").to_boolean());
    CHECK(number_of("var n = 0; var o = {valueOf: function () { n = n + 1; return 1; }}; o == null; n;") == 0);
}

TEST_CASE("relacje wołają ToPrimitive z hintem Number") {
    // Hint Number: valueOf przed toString. Przy odwrotnej kolejności "9" < 2
    // dałoby false.
    CHECK(value_of("({valueOf: function () { return 1; }, toString: function () { return \"9\"; }}) < 2;").to_boolean());
}

TEST_CASE("relacje konwertują operandy w kolejności ze źródła") {
    // 11.8.5 flaga LeftFirst: a > b to porównanie (b, a), ale a nadal
    // jest konwertowane jako pierwsze.
    const auto order = [](const std::string& op) {
        return value_of(
            "var log = \"\";"
            "var a = {valueOf: function () { log = log + \"a\"; return 1; }};"
            "var b = {valueOf: function () { log = log + \"b\"; return 2; }};"
            "a " + op + " b; log;").to_string();
    };

    CHECK(order("<")  == "ab");
    CHECK(order(">")  == "ab");
    CHECK(order("<=") == "ab");
    CHECK(order(">=") == "ab");
}

TEST_CASE("rzut z lewego operandu relacji zatrzymuje konwersję prawego") {
    const auto right_touched = [](const std::string& op) {
        return value_of(
            "var log = \"\";"
            "var a = {valueOf: function () { throw 1; }};"
            "var b = {valueOf: function () { log = \"b\"; return 2; }};"
            "try { a " + op + " b; } catch (e) {} log;").to_string();
    };

    CHECK(right_touched("<")  == "");
    CHECK(right_touched(">")  == "");
    CHECK(right_touched("<=") == "");
    CHECK(right_touched(">=") == "");
}

TEST_CASE("porównanie z NaN daje false dla wszystkich czterech relacji") {
    // is_less_than zwraca wtedy undefined; <= i >= NIE mogą go zanegować do true.
    CHECK_FALSE(value_of("NaN < 1;").to_boolean());
    CHECK_FALSE(value_of("NaN > 1;").to_boolean());
    CHECK_FALSE(value_of("NaN <= 1;").to_boolean());
    CHECK_FALSE(value_of("NaN >= 1;").to_boolean());
    CHECK_FALSE(value_of("1 <= NaN;").to_boolean());
    CHECK_FALSE(value_of("undefined <= undefined;").to_boolean());
    CHECK_FALSE(value_of("({}) <= 1;").to_boolean());   // "[object Object]" -> NaN
    CHECK_FALSE(value_of("({}) >= 1;").to_boolean());
}

TEST_CASE("wyjątek z valueOf wraca jako THROW, nie wyjątek C++") {
    const std::string o = "var o = {valueOf: function () { throw 7; }};";

    CHECK(thrown(o + "o == 1;") == "7");
    CHECK(thrown(o + "1 == o;") == "7");
    CHECK(thrown(o + "o < 1;")  == "7");
    CHECK(thrown(o + "1 > o;")  == "7");
    CHECK(thrown(o + "o <= 1;") == "7");
    CHECK(thrown(o + "+o;")     == "7");
    CHECK(thrown(o + "-o;")     == "7");
    CHECK(thrown(o + "~o;")     == "7");
    CHECK(thrown(o + "o++;")    == "7");
    CHECK(thrown(o + "o + 1;")  == "7");
    CHECK(thrown(o + "o & 1;")  == "7");
}

TEST_CASE("obiekt bez prymitywnej reprezentacji daje TypeError") {
    CHECK(value_of(
        "var o = {valueOf: function () { return {}; }, toString: function () { return {}; }};"
        "try { +o; } catch (e) { e instanceof TypeError; }").to_boolean());
}

TEST_CASE("print wypisuje obiekty przez toString") {
    CHECK(output_of("print({});") == "[object Object]\n");
    CHECK(output_of("print({toString: function () { return \"hej\"; }});") == "hej\n");
    CHECK(output_of("print(new Error(\"boom\"));") == "Error: boom\n");
    CHECK(thrown("print({toString: function () { throw 7; }});") == "7");
}

// ---------------------------------------------------------------------------
// Hierarchia Error (15.11)
// ---------------------------------------------------------------------------

// Czy wyjątek rzucony przez `code` jest instancją `cls`?
// Uwaga: wynikiem instrukcji try jest wartość jej BLOKU, gdy nic nie rzuci.
// Bez osobnej flagi test przechodziłby także wtedy, gdy kod nie rzucił,
// a jedynie zwrócił coś prawdziwego.
static bool caught_instance_of(const std::string& code, const std::string& cls) {
    return value_of("var caught = false;"
                    "try { " + code + " } catch (e) { caught = e instanceof " + cls + "; }"
                    "caught;").to_boolean();
}

static std::size_t occurrences(const std::string& text, const std::string& needle) {
    std::size_t count = 0;
    for (std::size_t at = text.find(needle); at != std::string::npos; at = text.find(needle, at + 1)) {
        ++count;
    }
    return count;
}

TEST_CASE("new Error ustawia własne message tylko gdy argument nie jest undefined") {
    CHECK(value_of("new Error(\"x\").message;").to_string() == "x");
    CHECK(value_of("new Error(\"x\").hasOwnProperty(\"message\");").to_boolean());
    CHECK_FALSE(value_of("new Error().hasOwnProperty(\"message\");").to_boolean());
    CHECK_FALSE(value_of("new Error(undefined).hasOwnProperty(\"message\");").to_boolean());
}

TEST_CASE("message jest konwertowane na string, a domyślne pochodzi z prototypu") {
    CHECK(value_of("typeof new Error(42).message;").to_string() == "string");
    CHECK(value_of("new Error(42).message;").to_string() == "42");
    CHECK(value_of("new Error().message;").to_string() == "");
}

TEST_CASE("wywołanie konstruktora Error bez new też tworzy błąd") {
    // 15.11.1: Error(...) jako funkcja działa tak samo jak new Error(...).
    CHECK(value_of("Error(\"x\") instanceof Error;").to_boolean());
    CHECK(value_of("TypeError(\"x\").message;").to_string() == "x");
}

TEST_CASE("podklasy błędów dziedziczą po Error i Object") {
    for (const char* raw : {"TypeError", "ReferenceError", "RangeError", "SyntaxError"}) {
        const std::string cls = raw;
        CAPTURE(cls);
        const std::string make = "new " + cls + "(\"x\")";

        CHECK(value_of(make + " instanceof " + cls + ";").to_boolean());
        CHECK(value_of(make + " instanceof Error;").to_boolean());
        CHECK(value_of(make + " instanceof Object;").to_boolean());
    }
}

TEST_CASE("podklasy błędów nie są instancjami siebie nawzajem") {
    CHECK_FALSE(value_of("new TypeError(\"x\") instanceof RangeError;").to_boolean());
    CHECK_FALSE(value_of("new RangeError(\"x\") instanceof TypeError;").to_boolean());
    CHECK_FALSE(value_of("new Error(\"x\") instanceof TypeError;").to_boolean());
}

TEST_CASE("name pochodzi z prototypu, nie z instancji") {
    CHECK(value_of("new RangeError(\"r\").name;").to_string() == "RangeError");
    CHECK_FALSE(value_of("new RangeError(\"r\").hasOwnProperty(\"name\");").to_boolean());
    CHECK(value_of("TypeError.prototype.name;").to_string() == "TypeError");
    CHECK(value_of("Error.prototype.name;").to_string() == "Error");
}

TEST_CASE("Error.prototype.toString skleja name i message") {
    CHECK(value_of("new Error(\"boom\").toString();").to_string()   == "Error: boom");
    CHECK(value_of("new TypeError(\"t\").toString();").to_string()  == "TypeError: t");
    CHECK(value_of("new Error().toString();").to_string()           == "Error");
    CHECK(value_of("var e = new Error(\"x\"); e.name = \"Custom\"; e.toString();").to_string() == "Custom: x");
    CHECK(value_of("var e = new Error(\"x\"); e.name = \"\"; e.toString();").to_string()       == "x");
}

TEST_CASE("konkatenacja z błędem idzie przez toString") {
    // Hint Default: valueOf zwraca obiekt, więc wygrywa toString.
    CHECK(value_of("\"\" + new Error(\"boom\");").to_string() == "Error: boom");
}

TEST_CASE("rzucony i złapany Error zachowuje klasę i message") {
    CHECK(value_of("try { throw new TypeError(\"t\"); } catch (e) { e instanceof TypeError && e.message === \"t\"; }").to_boolean());
}

TEST_CASE("błędy zgłaszane przez silnik mają właściwą klasę") {
    CHECK(caught_instance_of("brak;",                               "ReferenceError"));
    CHECK(caught_instance_of("null.x;",                             "TypeError"));
    CHECK(caught_instance_of("undefined();",                        "TypeError"));
    CHECK(caught_instance_of("new 1;",                              "TypeError"));
    CHECK(caught_instance_of("new print();",                        "TypeError"));
    CHECK(caught_instance_of("1 in 1;",                             "TypeError"));
    CHECK(caught_instance_of("1 instanceof 1;",                     "TypeError"));
    CHECK(caught_instance_of("({}) instanceof ({});",               "TypeError"));
    CHECK(caught_instance_of("function f() { return f(); } f();",   "RangeError"));
}

TEST_CASE("komunikat błędu silnika nie powtarza nazwy klasy") {
    // name jest na prototypie, a toString dokleja je sam. Prefiks "TypeError: "
    // w message daje "TypeError: TypeError: ...".
    for (const char* raw : {
            "brak;",
            "null.x;",
            "undefined();",
            "new 1;",
            "1 in 1;",
            "1 instanceof 1;",
            "({}) instanceof ({});",
            "+({valueOf: function () { return {}; }, toString: function () { return {}; }});",
            "function f() { return f(); } f();",
        }) {
        const std::string code = raw;
        const std::string message = thrown(code);
        CAPTURE(code);
        CAPTURE(message);
        CHECK(occurrences(message, "Error:") == 1);
    }
}

// ---------------------------------------------------------------------------
// void (11.4.2)
// ---------------------------------------------------------------------------

TEST_CASE("void zwraca undefined") {
    CHECK(value_of("void 0;").type()     == JSValueType::Undefined);
    CHECK(value_of("void \"x\";").type() == JSValueType::Undefined);
    CHECK(value_of("typeof void 0;").to_string() == "undefined");
    CHECK(std::isnan(number_of("void 0 + 1;")));
}

TEST_CASE("void oblicza operand, więc efekty uboczne zachodzą") {
    CHECK(number_of("var x; void (x = 5); x;") == 5);
    CHECK(number_of("var n = 0; function f() { n++; } void f(); n;") == 1);
}

TEST_CASE("void woła GetValue: nierozwiązywalna nazwa rzuca, w przeciwieństwie do typeof") {
    CHECK(thrown("void brak;").rfind("ReferenceError:", 0) == 0);
    CHECK(value_of("typeof brak;").to_string() == "undefined");
}

TEST_CASE("wyjątek z operandu void się propaguje") {
    CHECK(thrown("void (function () { throw 7; })();") == "7");
}

// ---------------------------------------------------------------------------
// debugger (12.15)
// ---------------------------------------------------------------------------

TEST_CASE("debugger nie zmienia wartości programu") {
    // Bez podłączonego debuggera wynik to (normal, empty, empty),
    // a empty nie nadpisuje dotychczasowej wartości.
    CHECK(number_of("1; debugger;") == 1);
    CHECK(number_of("debugger; 1;") == 1);
    CHECK_FALSE(has_value("debugger;"));
}

TEST_CASE("debugger nie przerywa pętli ani funkcji") {
    CHECK(number_of("var n = 0; while (n < 3) { debugger; n++; } n;") == 3);
    CHECK(number_of("function f() { debugger; return 4; } f();") == 4);
}

// ---------------------------------------------------------------------------
// continue z etykietą (12.6, 12.12)
//
// Pętla wykonuje się z ZBIOREM ETYKIET. continue L kontynuuje pętlę tylko wtedy,
// gdy L należy do jej zbioru; w przeciwnym razie completion idzie wyżej.
// ---------------------------------------------------------------------------

TEST_CASE("continue z etykietą przechodzi do następnej iteracji zewnętrznej pętli") {
    CHECK(number_of(
        "var n = 0;"
        "outer: for (var i = 0; i < 3; i++) {"
        "  for (var j = 0; j < 3; j++) { if (j == 1) continue outer; n++; }"
        "}"
        "n;") == 3);
}

TEST_CASE("continue z etykietą nie kończy po cichu programu") {
    // Regresja: etykietowany CONTINUE przelatywał przez obie pętle i kończył
    // program — przypisanie n = 42 nigdy się nie wykonywało.
    CHECK(number_of(
        "var n = 0;"
        "outer: for (var i = 0; i < 3; i++) { for (var j = 0; j < 3; j++) { continue outer; } }"
        "n = 42; n;") == 42);
}

TEST_CASE("continue z etykietą działa we wszystkich czterech pętlach") {
    CHECK(number_of("var i = 0; a: while (i < 3) { i++; while (true) { continue a; } } i;") == 3);
    CHECK(number_of("var i = 0; a: do { i++; while (true) { continue a; } } while (i < 3); i;") == 3);
    CHECK(number_of("var s = 0; a: for (var i = 0; i < 3; i++) { while (true) { s += i; continue a; } } s;") == 3);
    CHECK(value_of("var r = \"\"; a: for (var k in {x: 1, y: 2}) { while (true) { r += k; continue a; } } r;")
              .to_string() == "xy");
}

TEST_CASE("obie etykiety łańcucha a: b: należą do tej samej pętli") {
    CHECK(value_of("var r = \"\"; a: b: for (var i = 0; i < 3; i++) { r += i; continue a; } r;").to_string() == "012");
    CHECK(value_of("var r = \"\"; a: b: for (var i = 0; i < 3; i++) { r += i; continue b; } r;").to_string() == "012");
}

TEST_CASE("etykieta pętli nie przechodzi na pętlę zagnieżdżoną w jej ciele") {
    // Gdyby ciało dziedziczyło zbiór etykiet, wewnętrzna pętla uznałaby
    // continue a za swoje i wynik byłby "000102101112".
    CHECK(value_of(
        "var r = \"\";"
        "a: for (var i = 0; i < 2; i++) {"
        "  for (var j = 0; j < 3; j++) { r += \"\" + i + j; continue a; }"
        "}"
        "r;").to_string() == "0010");
}

TEST_CASE("continue z etykietą przechodzi przez blok, switch i try/finally") {
    CHECK(number_of("var n = 0; a: while (n < 5) { n++; b: { continue a; } } n;") == 5);
    CHECK(number_of("var n = 0; a: do { n++; switch (n) { case 1: continue a; } } while (n < 4); n;") == 4);
    CHECK(number_of("var n = 0, f = 0; a: while (n < 3) { n++; try { continue a; } finally { f++; } } f;") == 3);
}

TEST_CASE("continue bez etykiety w pętli wewnętrznej nie dotyka zewnętrznej") {
    CHECK(number_of(
        "var s = 0;"
        "a: for (var i = 0; i < 2; i++) { for (var j = 0; j < 3; j++) { if (j == 1) continue; s++; } }"
        "s;") == 4);
}

TEST_CASE("break z etykietą nadal działa obok continue z etykietą") {
    CHECK(number_of("var n = 0; a: while (true) { n++; b: while (true) { if (n > 2) break a; continue a; } } n;") == 3);
}

// ---------------------------------------------------------------------------
// Literał RegExp (7.8.5, 15.10.6, 15.10.7)
//
// Uwaga przy porównywaniu z Node: od ES2015 source i flagi są akcesorami na
// RegExp.prototype. W ES5.1 to WŁASNE, niekonfigurowalne właściwości danych
// instancji — dlatego delete i hasOwnProperty dają tu inny wynik niż w Node.
// ---------------------------------------------------------------------------

TEST_CASE("literał regexp tworzy obiekt dziedziczący po Object.prototype") {
    CHECK(value_of("typeof /x/;").to_string() == "object");
    CHECK(value_of("typeof /x/.hasOwnProperty;").to_string() == "function");
}

TEST_CASE("source i flagi odzwierciedlają literał") {
    CHECK(value_of("/ab+c/.source;").to_string() == "ab+c");
    CHECK(value_of("/[/]/.source;").to_string()  == "[/]");
    CHECK(value_of("/a/g.global;").to_boolean());
    CHECK_FALSE(value_of("/a/.global;").to_boolean());
    CHECK(value_of("/a/i.ignoreCase;").to_boolean());
    CHECK(value_of("/a/gim.multiline;").to_boolean());
    CHECK(number_of("/a/g.lastIndex;") == 0);
}

TEST_CASE("każde wykonanie literału tworzy nowy obiekt (ES5, a nie ES3)") {
    CHECK_FALSE(value_of("function f() { return /a/g; } f() === f();").to_boolean());
    // W ES3 obiekt był jeden na literał i lastIndex przeciekał między wywołaniami.
    CHECK(number_of("function f() { return /a/g; } var r = f(); r.lastIndex = 5; f().lastIndex;") == 0);
}

TEST_CASE("atrybuty właściwości instancji RegExp (15.10.7)") {
    // source, global, ignoreCase, multiline: {W: false, E: false, C: false}
    CHECK(value_of("var r = /a/; r.source = \"zz\"; r.source;").to_string() == "a");
    CHECK(value_of("var r = /a/g; r.global = false; r.global;").to_boolean());
    CHECK_FALSE(value_of("var r = /a/; delete r.source;").to_boolean());
    CHECK(value_of("/a/.hasOwnProperty(\"source\");").to_boolean());

    // lastIndex: {W: true, E: false, C: false}
    CHECK(number_of("var r = /a/; r.lastIndex = 5; r.lastIndex;") == 5);
    CHECK_FALSE(value_of("var r = /a/; delete r.lastIndex;").to_boolean());

    // żadna nie jest wyliczalna
    CHECK(value_of("var s = \"\"; for (var k in /a/g) s += k; s;").to_string() == "");
}

TEST_CASE("RegExp.prototype.toString składa /source/ i flagi w kolejności g, i, m") {
    CHECK(value_of("/a/.toString();").to_string() == "/a/");
    CHECK(value_of("\"\" + /a/g;").to_string()    == "/a/g");
    CHECK(value_of("\"\" + /a/mig;").to_string()  == "/a/gim");
    CHECK(output_of("print(/x+/i);") == "/x+/i\n");
}

TEST_CASE("RegExp.prototype.toString wywołane na nie-RegExp rzuca TypeError") {
    CHECK(caught_instance_of("var o = {toString: /a/.toString}; o.toString();", "TypeError"));
}

// ---------------------------------------------------------------------------
// Nazwane wyrażenie funkcyjne (13)
//
// Nazwa żyje w osobnym środowisku między domknięciem a otoczeniem: widać ją
// tylko w środku funkcji, jest niezmienna i przesłaniają ją parametry oraz var.
// ---------------------------------------------------------------------------

TEST_CASE("nazwa wyrażenia funkcyjnego jest widoczna w jego wnętrzu") {
    CHECK(value_of("var f = function g() { return typeof g; }; f();").to_string() == "function");
    CHECK(number_of("var f = function fact(n) { return n <= 1 ? 1 : n * fact(n - 1); }; f(5);") == 120);
}

TEST_CASE("nazwa wyrażenia funkcyjnego nie wycieka na zewnątrz") {
    CHECK(value_of("var f = function g() {}; typeof g;").to_string() == "undefined");
    CHECK(value_of("var g = 1; var f = function g() { return typeof g; }; f() + typeof g;").to_string()
          == "functionnumber");
}

TEST_CASE("binding nazwy jest niezmienny, ale przypisanie go nie rzuca (tryb nieścisły)") {
    CHECK(value_of("var f = function g() { g = 1; return typeof g; }; f();").to_string() == "function");
}

TEST_CASE("parametr i zmienna lokalna przesłaniają nazwę wyrażenia") {
    CHECK(value_of("var f = function g(g) { return typeof g; }; f(1);").to_string() == "number");
    CHECK(value_of("var f = function g() { var g = 1; return typeof g; }; f();").to_string() == "number");
}

TEST_CASE("anonimowe wyrażenie funkcyjne działa jak dotąd") {
    CHECK(number_of("var f = function () { return 1; }; f();") == 1);
    CHECK(number_of("(function (a, b) { return a + b; })(1, 2);") == 3);
}

// ---------------------------------------------------------------------------
// Granica stosu natywnego
//
// Licznik MAX_CALL_DEPTH liczy wywołania JS, a kończy się stos C++. Prawdziwą
// granicą jest porównanie wskaźnika stosu z limitem wątku.
// ---------------------------------------------------------------------------

TEST_CASE("nieskończona rekurencja daje RangeError, a nie przepełnienie stosu") {
    CHECK(caught_instance_of("function f() { return f(); } f();", "RangeError"));
    CHECK(caught_instance_of("function f() { return g(); } function g() { return f(); } f();", "RangeError"));
}

TEST_CASE("skończona rekurencja nadal działa") {
    // Głębokość celowo niska. Limit nie jest stały: bierze się ze WOLNEGO
    // MIEJSCA NA STOSIE C++, a ramki pod AddressSanitizerem są około dwa razy
    // grubsze niż w zwykłym buildzie. Tutaj sprawdzamy, że rekurencja w ogóle
    // działa — o zachowanie przy przekroczeniu limitu dba test powyżej.
    CHECK(number_of("function f(n) { return n === 0 ? 0 : 1 + f(n - 1); } f(150);") == 150);
    CHECK(number_of("function f(n) { return n === 0 ? 0 : 1 + f(n - 1); } f(1);") == 1);

    // Przekroczenie limitu nie psuje ewaluatora: kolejny program działa.
    CHECK(caught_instance_of("function f() { return f(); } f();", "RangeError"));
    CHECK(number_of("function f(n) { return n === 0 ? 0 : 1 + f(n - 1); } f(50);") == 50);
}

// ---------------------------------------------------------------------------
// Obiekt globalny (15.1, 10.2.3, 10.4.1, 8.7.2)
//
// Środowisko globalne to rekord OBIEKTOWY: nazwy globalne są właściwościami
// obiektu globalnego. Stąd this na górnym poziomie, var jako właściwość
// i różnica w delete między var a niejawną zmienną globalną.
// ---------------------------------------------------------------------------

TEST_CASE("this w kodzie globalnym to obiekt globalny") {
    CHECK(value_of("typeof this;").to_string() == "object");
    CHECK(value_of("this === this;").to_boolean());
}

TEST_CASE("var i funkcje na najwyższym poziomie stają się właściwościami obiektu globalnego") {
    CHECK(number_of("var g = 1; this.g;") == 1);
    CHECK(value_of("function f() {} typeof this.f;").to_string() == "function");

    // Hoisting tworzy właściwość, zanim wykona się przypisanie.
    CHECK(value_of("var widoczna = (\"later\" in this); var later = 1; widoczna;").to_boolean());
}

TEST_CASE("właściwość obiektu globalnego jest widoczna jako zwykła nazwa") {
    CHECK(number_of("this.h = 2; h;") == 2);
    CHECK(value_of("this.print === print;").to_boolean());
    CHECK(value_of("typeof this.NaN;").to_string() == "number");
    CHECK(value_of("typeof this.brak;").to_string() == "undefined");
}

// 10.4.3: w kodzie nieścisłym undefined jako this zamienia się na obiekt globalny.
TEST_CASE("zwykłe wywołanie funkcji dostaje obiekt globalny jako this") {
    CHECK(value_of("(function () { return typeof this; })();").to_string() == "object");
    CHECK(value_of("(function () { return this; })() === this;").to_boolean());
}

TEST_CASE("wywołanie metody nadal dostaje swojego odbiorcę") {
    CHECK(number_of("var o = {v: 7, m: function () { return this.v; }}; o.m();") == 7);
    CHECK(value_of("var o = {m: function () { return this; }}; o.m() === o;").to_boolean());
}

// 10.5 daje var atrybut DontDelete, a 8.7.2 tworzy właściwość konfigurowalną.
TEST_CASE("delete odróżnia var od niejawnej zmiennej globalnej") {
    CHECK_FALSE(value_of("var a = 1; delete a;").to_boolean());
    CHECK(number_of("var a = 1; delete a; a;") == 1);

    CHECK(value_of("b = 1; delete b;").to_boolean());
    CHECK(value_of("b = 1; delete b; typeof b;").to_string() == "undefined");
}

// 15.1.1 — undefined, NaN i Infinity mają {W:false, E:false, C:false}
TEST_CASE("stałe globalne są niezapisywalne i nieusuwalne") {
    CHECK(value_of("NaN = 1; typeof NaN;").to_string() == "number");
    CHECK(value_of("undefined = 1; typeof undefined;").to_string() == "undefined");
    CHECK_FALSE(value_of("delete NaN;").to_boolean());
    CHECK_FALSE(value_of("delete undefined;").to_boolean());
    CHECK(number_of("Infinity = 0; 1 / Infinity;") == 0);
}

TEST_CASE("wbudowane nazwy globalne nie są wyliczalne, a zmienne użytkownika tak") {
    CHECK_FALSE(value_of(
        "var znaleziono = false;"
        "for (var k in this) { if (k === \"print\" || k === \"NaN\") znaleziono = true; }"
        "znaleziono;").to_boolean());

    CHECK(value_of(
        "var moja = 1; var znaleziono = false;"
        "for (var k in this) { if (k === \"moja\") znaleziono = true; }"
        "znaleziono;").to_boolean());
}

// ---------------------------------------------------------------------------
// Obiekt arguments (10.6)
//
// Poza trybem strict arguments[i] i i-ty parametr to TA SAMA komórka.
// Specyfikacja trzyma to powiązanie w osobnej mapie, do której zaglądają
// [[GetOwnProperty]], [[DefineOwnProperty]] i [[Delete]].
// ---------------------------------------------------------------------------

TEST_CASE("arguments zna liczbę przekazanych argumentów, nie parametrów") {
    CHECK(number_of("function f() { return arguments.length; } f(1, 2, 3);") == 3);
    CHECK(number_of("function f(a, b) { return arguments.length; } f(1);") == 1);
    CHECK(number_of("function f() { return arguments.length; } f();") == 0);
    CHECK(value_of("function f() { return typeof arguments; } f();").to_string() == "object");
}

TEST_CASE("zapis przez arguments[i] zmienia parametr") {
    CHECK(number_of("function f(a) { arguments[0] = 9; return a; } f(1);") == 9);
    CHECK(number_of("function f(a, b) { arguments[1] = 9; return b; } f(1, 2);") == 9);
}

TEST_CASE("zapis do parametru zmienia arguments[i]") {
    CHECK(number_of("function f(a) { a = 9; return arguments[0]; } f(1);") == 9);
    CHECK(number_of("function f(a) { var o = arguments; a = 2; return o[0]; } f(1);") == 2);
}

// Mapowany jest tylko argument FAKTYCZNIE przekazany. Parametr bez argumentu
// nie ma swojego indeksu, więc nic się nie synchronizuje w żadną stronę.
TEST_CASE("brakujący argument nie jest mapowany") {
    CHECK(number_of("function f(a) { a = 9; return arguments.length; } f();") == 0);
    CHECK(value_of("function f(a) { a = 9; return typeof arguments[0]; } f();").to_string() == "undefined");
    CHECK(value_of("function f(a, b) { arguments[1] = 7; return typeof b; } f(1);").to_string() == "undefined");
    CHECK(value_of("function f(a) { return typeof arguments[5]; } f(1);").to_string() == "undefined");
}

// 10.6 [[Delete]] i [[DefineOwnProperty]] zrywają powiązanie na trwałe.
TEST_CASE("delete i zmiana na tylko-do-odczytu zrywają powiązanie z parametrem") {
    CHECK(value_of("function f(a) { delete arguments[0]; a = 5; return typeof arguments[0]; } f(1);")
              .to_string() == "undefined");
    CHECK(number_of("function f(a) { delete arguments[0]; a = 5; return a; } f(1);") == 5);
}

TEST_CASE("callee wskazuje na wykonywaną funkcję") {
    CHECK(value_of("function f() { return arguments.callee; } f() === f;").to_boolean());
    CHECK(value_of("var g = function () { return arguments.callee; }; g() === g;").to_boolean());
}

// length i callee są niewyliczalne, indeksy — wyliczalne.
TEST_CASE("for-in po arguments daje same indeksy") {
    CHECK(value_of("function f(a, b) { var r = \"\"; for (var k in arguments) r += k; return r; } f(1, 2);")
              .to_string() == "01");
}

TEST_CASE("arguments jest zwykłą właściwością: da się nadpisać i przesłonić") {
    CHECK(number_of("function f() { var arguments = 5; return arguments; } f();") == 5);
    CHECK(number_of("function f(arguments) { return arguments; } f(5);") == 5);
    CHECK(number_of("function f(a) { arguments.length = 99; return arguments.length; } f(1);") == 99);
}

// 10.6 krok 11: przy powtórzonej nazwie parametru powiązany jest tylko OSTATNI.
TEST_CASE("przy powtórzonej nazwie parametru mapowany jest ostatni") {
    CHECK(number_of("function f(a, a) { return arguments[0]; } f(1, 2);") == 1);
    CHECK(number_of("function f(a, a) { a = 9; return arguments[1]; } f(1, 2);") == 9);
}

TEST_CASE("arguments istnieje tylko wewnątrz funkcji") {
    CHECK(value_of("typeof arguments;").to_string() == "undefined");
    CHECK(number_of("function g() { return arguments.length; } function f() { return g(1, 2); } f();") == 2);
}

TEST_CASE("obiekt arguments przeżywa wyjście z funkcji") {
    CHECK(number_of("function f(a) { return arguments; } f(1)[0];") == 1);
    CHECK(number_of("var keep = (function (a) { return arguments; })(7); keep[0];") == 7);
}

// ---------------------------------------------------------------------------
// with (12.10)
//
// Ciało wykonuje się w rekordzie OBIEKTOWYM nad podanym obiektem. To jedyne
// miejsce, w którym ImplicitThisValue zwraca coś innego niż undefined.
// ---------------------------------------------------------------------------

TEST_CASE("nazwy wewnątrz with rozstrzygają się na właściwościach obiektu") {
    CHECK(number_of("with ({a: 5}) { a; }") == 5);
    CHECK(number_of("var o = {a: 1}; with (o) { a = 2; } o.a;") == 2);
    CHECK(number_of("with ({a: 1}) with ({a: 2}) a;") == 2);
    CHECK(number_of("function f() { with ({x: 1}) { return x; } } f();") == 1);
}

TEST_CASE("nazwa spoza obiektu leci dalej po łańcuchu środowisk") {
    CHECK(number_of("var a = 1; with ({}) { a = 2; } a;") == 2);
    CHECK(value_of("with ({}) typeof print;").to_string() == "function");
}

// HasBinding rekordu obiektowego to [[HasProperty]], czyli cały łańcuch prototypów.
TEST_CASE("with widzi też właściwości odziedziczone") {
    CHECK(value_of("with ({}) typeof toString;").to_string() == "function");
    CHECK(value_of("with ({toString: 1}) typeof toString;").to_string() == "number");
}

// var należy do zakresu funkcji, a nie do obiektu z with.
TEST_CASE("var wewnątrz with trafia do zakresu funkcji") {
    CHECK(number_of("with ({}) { var b = 3; } b;") == 3);
    CHECK(value_of("var o = {a: 1}; with (o) { var a = 9; } o.a + \",\" + typeof a;").to_string()
          == "9,undefined");
}

// 11.2.3 krok 7: wywołanie przez referencję do środowiska bierze this
// z ImplicitThisValue — dla with jest nim obiekt.
TEST_CASE("metoda wołana wewnątrz with dostaje obiekt jako this") {
    CHECK(value_of("var o = {f: function () { return this; }}; with (o) { f() === o; }").to_boolean());
    CHECK(number_of("var o = {v: 7, f: function () { return this.v; }}; with (o) { f(); }") == 7);
}

TEST_CASE("with na undefined i null daje TypeError") {
    CHECK(caught_instance_of("with (undefined) { 1; }", "TypeError"));
    CHECK(caught_instance_of("with (null) { 1; }", "TypeError"));
}

TEST_CASE("delete wewnątrz with usuwa właściwość obiektu") {
    CHECK(value_of("var o = {a: 1}; with (o) { delete a; } typeof o.a;").to_string() == "undefined");
}

TEST_CASE("break z etykietą działa przez with") {
    CHECK(value_of("var r = \"\"; a: with ({}) { for (;;) { r = \"in\"; break a; } } r;").to_string() == "in");
}

// ---------------------------------------------------------------------------
// eval (15.1.2.1, 10.4.2)
//
// eval BEZPOŚREDNI (wywołany przez nazwę "eval") działa w środowisku i z this
// wywołującego. POŚREDNI (przez alias) — w globalnym i z obiektem globalnym.
// ---------------------------------------------------------------------------

TEST_CASE("eval zwraca wartość completion swojego programu") {
    CHECK(number_of("eval(\"1 + 1\");") == 2);
    CHECK(number_of("eval(\"var a = 1; a + 1;\");") == 2);
    CHECK(number_of("eval(\"eval('1+2')\");") == 3);
    CHECK(value_of("typeof eval;").to_string() == "function");
}

TEST_CASE("eval nie-łańcucha zwraca argument bez zmian") {
    CHECK(number_of("eval(42);") == 42);
    CHECK(value_of("typeof eval();").to_string() == "undefined");
    CHECK(value_of("var o = {}; eval(o) === o;").to_boolean());
    CHECK(number_of("eval(5);") == 5);
    CHECK(value_of("typeof eval(undefined);").to_string() == "undefined");
    CHECK(value_of("eval(null) === null;").to_boolean());
    CHECK(value_of("var a = [1]; eval(a) === a;").to_boolean());
}

TEST_CASE("eval bezpośredni widzi i zmienia środowisko wywołującego") {
    CHECK(number_of("var x = 1; eval(\"x + 1\");") == 2);
    CHECK(number_of("eval(\"var y = 5;\"); y;") == 5);
    CHECK(number_of("eval(\"function g() { return 7; }\"); g();") == 7);
    CHECK(number_of("(function () { eval(\"var z = 1;\"); return z; })();") == 1);
    CHECK(number_of("(function () { var n = 1; return eval(\"n + 1\"); })();") == 2);
}

TEST_CASE("eval bezpośredni zachowuje this wywołującego") {
    CHECK(value_of("eval(\"this\") === this;").to_boolean());
    CHECK(value_of("var o = {m: function () { return eval(\"this\"); }}; o.m() === o;").to_boolean());
}

// Ta sama funkcja, inny zakres — decyduje wyłącznie forma wywołania.
TEST_CASE("eval pośredni działa w środowisku globalnym") {
    CHECK(value_of("var e = eval; e(\"typeof this\");").to_string() == "object");
    CHECK(number_of("(function () { var e = eval; e(\"var w = 3;\"); })(); w;") == 3);
    CHECK(value_of("var e = eval; (function () { var lokalna = 1; return e(\"typeof lokalna\"); })();")
          .to_string() == "undefined");
}

TEST_CASE("błąd składni w eval to javascriptowy SyntaxError") {
    CHECK(caught_instance_of("eval(\"{\");", "SyntaxError"));
    CHECK(caught_instance_of("eval(\"var;\");", "SyntaxError"));
    CHECK(throws("eval(\"throw 7;\");"));
    CHECK(thrown("eval(\"throw 7;\");") == "7");
}

// ---------------------------------------------------------------------------
// Tryb strict — zachowanie w czasie wykonania (10.4.2, 10.4.3, 11.4.1, 8.7.2)
//
// Każdy przypadek ma parę: wersję strict i tę samą konstrukcję poza strict.
// Najłatwiej tu zepsuć właśnie kod nieścisły.
// ---------------------------------------------------------------------------

// UWAGA: caught_instance_of opakowuje kod w "try { ... }", więc dyrektywa
// przestałaby być prologiem i program nie byłby strict. Tutaj dyrektywa musi
// stać na samym początku programu, a try dopiero za nią.
static bool strict_throws_instance_of(const std::string& code, const std::string& cls) {
    return value_of("\"use strict\"; var caught = false;"
                    "try { " + code + " } catch (e) { caught = e instanceof " + cls + "; }"
                    "caught;").to_boolean();
}

TEST_CASE("w strict przypisanie do niezadeklarowanej nazwy rzuca ReferenceError") {
    CHECK(strict_throws_instance_of("x = 1;", "ReferenceError"));
    CHECK(number_of("x = 1; x;") == 1);
}

TEST_CASE("w strict this nie jest podmieniane na obiekt globalny") {
    CHECK(value_of("(function () { \"use strict\"; return typeof this; })();").to_string() == "undefined");
    CHECK(value_of("(function () { return typeof this; })();").to_string() == "object");
}

// Strict to cecha KODU funkcji, a nie miejsca wywołania.
TEST_CASE("funkcja strict pozostaje strict po przekazaniu dalej") {
    CHECK(value_of("function f() { \"use strict\"; return typeof this; } var g = f; g();").to_string()
          == "undefined");
    CHECK(value_of("\"use strict\"; function f() { return typeof this; } f();").to_string()
          == "undefined");
    CHECK(value_of("function f() { return typeof this; } (function () { \"use strict\"; return f(); })();")
              .to_string() == "object");
}

TEST_CASE("metoda wołana normalnie dostaje odbiorcę niezależnie od trybu") {
    CHECK(number_of("\"use strict\"; var o = {v: 7, m: function () { return this.v; }}; o.m();") == 7);
}

TEST_CASE("w strict zapis do właściwości tylko do odczytu rzuca TypeError") {
    CHECK(strict_throws_instance_of("NaN = 1;", "TypeError"));
    CHECK(value_of("NaN = 1; typeof NaN;").to_string() == "number");

    CHECK(strict_throws_instance_of("var r = /a/; r.source = \"zz\";", "TypeError"));
    CHECK(value_of("var r = /a/; r.source = \"zz\"; r.source;").to_string() == "a");
}

TEST_CASE("w strict nieudane delete rzuca TypeError") {
    // Node nie jest tu wyrocznią: od ES2015 "source" jest akcesorem na
    // RegExp.prototype, więc delete zwraca tam true. W ES5.1 to własna
    // właściwość niekonfigurowalna — czyli dokładnie przypadek z 11.4.1.
    CHECK(strict_throws_instance_of("var r = /a/; delete r.source;", "TypeError"));
    CHECK_FALSE(value_of("var r = /a/; delete r.source;").to_boolean());

    // Udane delete działa w obu trybach tak samo.
    CHECK(value_of("\"use strict\"; var o = {a: 1}; delete o.a;").to_boolean());
    CHECK(value_of("\"use strict\"; var o = {}; delete o.brak;").to_boolean());
}

TEST_CASE("eval bezpośredni dziedziczy strictness wywołującego") {
    CHECK(strict_throws_instance_of("eval(\"x = 1\");", "ReferenceError"));
    CHECK(number_of("eval(\"x = 1\"); x;") == 1);
}

// 10.4.2: kod strict w eval dostaje własne środowisko zmiennych.
TEST_CASE("var z eval w trybie strict nie wycieka do wywołującego") {
    CHECK(value_of("\"use strict\"; eval(\"var q = 1;\"); typeof q;").to_string() == "undefined");
    CHECK(value_of("eval(\"var q = 1;\"); typeof q;").to_string() == "number");

    // Dyrektywa w samym kodzie eval działa tak samo, nawet gdy wywołujący jest nieścisły.
    CHECK(value_of("eval(\"'use strict'; var q = 1;\"); typeof q;").to_string() == "undefined");

    CHECK(value_of("(function () { \"use strict\"; eval(\"var z = 1;\"); return typeof z; })();")
              .to_string() == "undefined");
    CHECK(value_of("(function () { eval(\"var z = 1;\"); return typeof z; })();").to_string() == "number");
}

TEST_CASE("eval pośredni nie dziedziczy strictness") {
    CHECK(value_of("\"use strict\"; var e = eval; e(\"typeof this\");").to_string() == "object");
    CHECK(number_of("\"use strict\"; var e = eval; e(\"x = 1\"); x;") == 1);
}

TEST_CASE("strict nie zmienia zwykłych operacji") {
    CHECK(number_of("\"use strict\"; var a = 1; a + 1;") == 2);
    CHECK(number_of("\"use strict\"; function f(a) { return a * 2; } f(21);") == 42);
    CHECK(value_of("\"use strict\"; var o = {a: 1}; o.a = 2; o.a;").to_number() == 2);
}

// ---------------------------------------------------------------------------
// arguments w trybie strict (10.6 krok 14, 13.2.3, 15.3.5.4)
// ---------------------------------------------------------------------------

TEST_CASE("w strict arguments nie jest mapowane na parametry") {
    CHECK(number_of("(function (a) { \"use strict\"; arguments[0] = 9; return a; })(1);") == 1);
    CHECK(number_of("(function (a) { \"use strict\"; a = 9; return arguments[0]; })(1);") == 1);

    // Poza strict mapowanie działa jak dotąd — w obie strony.
    CHECK(number_of("(function (a) { arguments[0] = 9; return a; })(1);") == 9);
    CHECK(number_of("(function (a) { a = 9; return arguments[0]; })(1);") == 9);
}

TEST_CASE("w strict arguments nadal jest zwykłą listą argumentów") {
    CHECK(number_of("(function () { \"use strict\"; return arguments.length; })(1, 2);") == 2);
    CHECK(number_of("(function () { \"use strict\"; return arguments[0] + arguments[1]; })(1, 2);") == 3);
    CHECK(value_of("(function () { \"use strict\"; return typeof arguments; })();").to_string() == "object");
    CHECK(value_of("(function () { \"use strict\"; var r = \"\";"
                   "  for (var k in arguments) r += k; return r; })(1, 2);").to_string() == "01");
}

// Zatruty jest getter I setter, więc nawet zapis rzuca.
TEST_CASE("w strict callee jest zatrutym akcesorem") {
    CHECK(value_of("(function () { \"use strict\";"
                   "  try { arguments.callee; } catch (e) { return e instanceof TypeError; } })();").to_boolean());
    CHECK(value_of("(function () { \"use strict\";"
                   "  try { arguments.callee = 1; } catch (e) { return e instanceof TypeError; } })();").to_boolean());

    CHECK(value_of("(function () { return typeof arguments.callee; })();").to_string() == "function");
}

TEST_CASE("funkcje strict mają zatrute caller i arguments") {
    CHECK(value_of("\"use strict\"; function f() {} try { f.caller; } catch (e) { e instanceof TypeError; }")
              .to_boolean());
    CHECK(value_of("\"use strict\"; function f() {} try { f.arguments; } catch (e) { e instanceof TypeError; }")
              .to_boolean());
    CHECK(value_of("\"use strict\"; function f() {} try { f.caller = 1; } catch (e) { e instanceof TypeError; }")
              .to_boolean());

    // Poza strict w ogóle nie definiujemy tych właściwości. Node zwraca tam null,
    // bo śledzi stos wywołań — to rozszerzenie z Annex B, nie wymóg ES5.1.
    CHECK(value_of("function f() {} typeof f.caller;").to_string() == "undefined");
    CHECK(value_of("function f() {} typeof f.arguments;").to_string() == "undefined");
}

// ---------------------------------------------------------------------------
// ToObject i obiekty opakowujące (9.9, 15.5.5)
//
// Dostęp do właściwości prymitywu tworzy opakowanie, które ginie natychmiast
// po operacji. Egzotyczny jest tylko String: length i indeksy znaków są jego
// WŁASNYMI właściwościami.
// ---------------------------------------------------------------------------

TEST_CASE("łańcuch ma length i indeksy znaków") {
    CHECK(number_of("\"abc\".length;") == 3);
    CHECK(number_of("\"\".length;") == 0);
    CHECK(value_of("\"abc\"[1];").to_string() == "b");
    CHECK(value_of("typeof \"abc\"[5];").to_string() == "undefined");
    CHECK(number_of("\"ab\".length + \"cd\".length;") == 4);
}

// 15.5.5.1 i 15.5.5.2: length jest {W:false, E:false, C:false},
// a znak pod indeksem {W:false, E:true, C:false}.
TEST_CASE("właściwości łańcucha są własne, ale niezapisywalne i nieusuwalne") {
    CHECK(value_of("\"abc\".hasOwnProperty(\"0\");").to_boolean());
    CHECK_FALSE(value_of("\"abc\".hasOwnProperty(\"5\");").to_boolean());

    CHECK(number_of("\"abc\".length = 5; \"abc\".length;") == 3);
    CHECK(value_of("var s = \"abc\"; delete s[0]; s[0];").to_string() == "a");
}

TEST_CASE("for-in po łańcuchu daje indeksy znaków") {
    CHECK(value_of("var r = \"\"; for (var k in \"ab\") r += k; r;").to_string() == "01");
    CHECK(value_of("var r = \"\"; for (var k in \"\") r += k; r;").to_string() == "");
}

TEST_CASE("prymityw sięga po metody ze swojego prototypu") {
    CHECK(value_of("typeof \"abc\".toString;").to_string() == "function");
    CHECK(value_of("typeof (5).toString;").to_string() == "function");
    CHECK(value_of("typeof true.toString;").to_string() == "function");
    CHECK(value_of("typeof \"abc\".hasOwnProperty;").to_string() == "function");
}

// 8.7.1 i 8.7.2: opakowanie powstaje na czas jednej operacji i znika.
TEST_CASE("zapis do właściwości prymitywu przepada razem z opakowaniem") {
    CHECK(value_of("(1).x = 5; typeof (1).x;").to_string() == "undefined");
    CHECK(value_of("var s = \"abc\"; s.nowa = 1; typeof s.nowa;").to_string() == "undefined");
}

TEST_CASE("w strict zapis do właściwości prymitywu rzuca TypeError") {
    CHECK(strict_throws_instance_of("(1).x = 5;", "TypeError"));
    CHECK(strict_throws_instance_of("var s = \"abc\"; s.nowa = 1;", "TypeError"));
}

TEST_CASE("with na prymitywie działa przez opakowanie") {
    CHECK(number_of("with (\"ab\") { length; }") == 2);
    CHECK(value_of("with (5) { typeof toString; }").to_string() == "function");

    CHECK(caught_instance_of("with (null) { 1; }", "TypeError"));
    CHECK(caught_instance_of("with (undefined) { 1; }", "TypeError"));
}

TEST_CASE("opakowanie nie zmienia typu samej wartości") {
    CHECK(value_of("typeof \"abc\";").to_string() == "string");
    CHECK(value_of("var s = \"abc\"; s.length; typeof s;").to_string() == "string");
    CHECK(value_of("var n = 5; n.toString; typeof n;").to_string() == "number");
}

// ---------------------------------------------------------------------------
// Łańcuchy jako ciągi jednostek UTF-16 (8.4)
//
// length i indeksowanie liczą JEDNOSTKI, a nie znaki i nie bajty. Znak spoza
// BMP zajmuje dwie jednostki, a łańcuch może zawierać samotny surogat.
// ---------------------------------------------------------------------------

TEST_CASE("length liczy jednostki UTF-16, nie bajty") {
    CHECK(number_of("\"ą\".length;") == 1);
    CHECK(number_of("\"ąćę\".length;") == 3);
    CHECK(number_of("\"abc\".length;") == 3);
    CHECK(number_of("(\"a\" + \"ą\").length;") == 2);
}

TEST_CASE("znak spoza BMP zajmuje dwie jednostki") {
    CHECK(number_of("\"😀\".length;") == 2);
    CHECK(value_of("\"\\uD83D\\uDE00\" === \"😀\";").to_boolean());
}

TEST_CASE("indeksowanie zwraca pojedynczą jednostkę") {
    CHECK(value_of("\"ą\"[0] === \"ą\";").to_boolean());
    CHECK(value_of("\"ąb\"[1];").to_string() == "b");
    CHECK(number_of("\"😀\"[0].length;") == 1);   // samotny górny surogat
}

TEST_CASE("escape \\u działa dla dowolnej jednostki") {
    CHECK(value_of("\"\\u0041\\u0142\";").to_string() == "Ał");
    CHECK(number_of("\"\\u0000\".length;") == 1);
    CHECK(number_of("\"\\uD800\".length;") == 1);   // samotny surogat jest legalny
}

// 11.8.5: porównanie idzie po jednostkach kodowych, a nie po bajtach UTF-8.
TEST_CASE("porównanie łańcuchów po jednostkach kodowych") {
    CHECK(value_of("\"a\" < \"b\";").to_boolean());
    CHECK_FALSE(value_of("\"\\uFF41\" < \"b\";").to_boolean());
    CHECK(value_of("\"ą\" > \"a\";").to_boolean());
}

// ---------------------------------------------------------------------------
// Function.prototype (15.3.4)
// ---------------------------------------------------------------------------

TEST_CASE("call przekazuje this i pozostałe argumenty") {
    CHECK(number_of("function f(a) { return this.v + a; } f.call({v: 1}, 2);") == 3);
    CHECK(number_of("function f() { return arguments.length; } f.call(null);") == 0);
    CHECK(number_of("function f(a, b) { return a + b; } f.call(null, 1, 2);") == 3);

    // this z call podlega tym samym regułom co zwykłe wywołanie (10.4.3).
    CHECK(value_of("function f() { return this; } f.call(null) === this;").to_boolean());
    CHECK(value_of("function f() { return typeof this; } f.call(5);").to_string() == "object");
    CHECK(value_of("\"use strict\"; function f() { return typeof this; } f.call(5);").to_string() == "number");
}

// 15.3.4.3 krok 3: wystarczy obiekt z length — nie musi być tablicą.
TEST_CASE("apply przyjmuje tablicę i obiekt tablicopodobny") {
    CHECK(number_of("function f(a, b) { return a + b; } f.apply(null, [1, 2]);") == 3);
    CHECK(number_of("function f(a) { return this.v + a; } f.apply({v: 1}, {0: 2, length: 1});") == 3);
    CHECK(number_of("function f() { return arguments.length; } f.apply(null);") == 0);
    CHECK(number_of("function f() { return arguments.length; } f.apply(null, null);") == 0);
}

TEST_CASE("apply z argumentem, który nie jest obiektem, rzuca TypeError") {
    CHECK(caught_instance_of("function f() {} f.apply(null, 5);", "TypeError"));
    CHECK(caught_instance_of("function f() {} f.apply(null, \"ab\");", "TypeError"));
}

TEST_CASE("call i apply wymagają wołalnego this") {
    CHECK(caught_instance_of("print.call.call({});", "TypeError"));
    CHECK(caught_instance_of("var f = (function () {}).call; f.call({});", "TypeError"));
}

TEST_CASE("bind wiąże this i argumenty") {
    CHECK(number_of("function f(a) { return this.v + a; } f.bind({v: 10})(1);") == 11);
    CHECK(number_of("function f(a) { return this.v + a; } f.bind({v: 10}, 1)();") == 11);
    CHECK(number_of("var o = {v: 3, m: function () { return this.v; }}; var g = o.m.bind(o); g();") == 3);

    // Wiązanie można składać — kolejne argumenty dokładają się z przodu.
    CHECK(number_of("function f(a, b, c) { return a + b + c; } f.bind(null, 1).bind(null, 2)(3);") == 6);
}

// 15.3.4.5 kroki 15-16
TEST_CASE("length funkcji związanej to reszta po odjęciu związanych argumentów") {
    CHECK(number_of("function f(a, b) {} f.length;") == 2);
    CHECK(number_of("function f(a, b) {} f.bind(null).length;") == 2);
    CHECK(number_of("function f(a, b) {} f.bind(null, 1).length;") == 1);
    CHECK(number_of("function f(a, b) {} f.bind(null, 1, 2, 3).length;") == 0);
}

// 15.3.4.5.2 i 15.3.4.5.3
TEST_CASE("funkcja związana działa z new i z instanceof") {
    CHECK(number_of("function F(a) { this.x = a; } var B = F.bind(null, 5); new B().x;") == 5);
    CHECK(value_of("function F(a) { this.x = a; } var B = F.bind(null, 5); new B() instanceof F;").to_boolean());

    // Przy new związane this jest ignorowane — liczy się nowy obiekt.
    CHECK(number_of("function F() { this.x = 1; } var B = F.bind({x: 99}); new B().x;") == 1);
}

TEST_CASE("funkcja związana ma zatrute caller i arguments") {
    CHECK(caught_instance_of("function f() {} f.bind().caller;", "TypeError"));
    CHECK(caught_instance_of("function f() {} f.bind().arguments;", "TypeError"));
}

TEST_CASE("bind na czymś, co nie jest funkcją, rzuca TypeError") {
    CHECK(caught_instance_of("var o = {}; (function () {}).bind.call(o);", "TypeError"));
}

// 15.3.4.2: postać wyniku jest zależna od implementacji, więc sprawdzamy kontrakt,
// a nie dokładny tekst. Node zwraca tu tekst źródłowy funkcji.
TEST_CASE("toString zwraca opis funkcji") {
    CHECK(value_of("typeof (function () {}).toString();").to_string() == "string");
    CHECK(value_of("(function () {}).toString();").to_string() == "function () { [native code] }");
    CHECK(value_of("function nazwa() {} nazwa.toString();").to_string() == "function nazwa() { [native code] }");
    CHECK(value_of("print.toString();").to_string() == "function print() { [native code] }");
}

TEST_CASE("funkcje natywne też dziedziczą po Function.prototype") {
    CHECK(value_of("typeof print.call;").to_string() == "function");
    CHECK(value_of("typeof eval.apply;").to_string() == "function");
    CHECK(value_of("typeof TypeError.bind;").to_string() == "function");
}

// ---------------------------------------------------------------------------
// Object: konstruktor i metody statyczne (15.2.1-15.2.3)
// ---------------------------------------------------------------------------

TEST_CASE("konstruktor Object tworzy obiekt albo opakowuje wartość") {
    CHECK(value_of("typeof Object();").to_string() == "object");
    CHECK(value_of("typeof new Object();").to_string() == "object");
    CHECK(value_of("Object.getPrototypeOf(Object()) === Object.prototype;").to_boolean());

    // Obiekt wraca bez zmian, prymityw przechodzi przez ToObject.
    CHECK(value_of("var o = {}; Object(o) === o;").to_boolean());
    CHECK(number_of("Object(\"abc\").length;") == 3);
    CHECK(value_of("({}).constructor === Object;").to_boolean());
}

TEST_CASE("getPrototypeOf i create") {
    CHECK(value_of("Object.getPrototypeOf({}) === Object.prototype;").to_boolean());
    CHECK(value_of("Object.getPrototypeOf(Object.create(null));").type() == JSValueType::Null);

    CHECK(number_of("var o = Object.create({p: 1}); o.p;") == 1);
    CHECK(value_of("var base = {}; Object.getPrototypeOf(Object.create(base)) === base;").to_boolean());

    // Obiekt bez prototypu nie ma żadnych odziedziczonych metod.
    CHECK(value_of("var o = Object.create(null); typeof o.toString;").to_string() == "undefined");

    // Drugi argument to ten sam format co defineProperties.
    CHECK(number_of("var o = Object.create({}, {x: {value: 5, enumerable: true}});"
                    "o.x + Object.keys(o).length;") == 6);

    CHECK(caught_instance_of("Object.create(5);", "TypeError"));
}

TEST_CASE("keys widzi tylko własne wyliczalne właściwości") {
    CHECK(number_of("Object.keys({a: 1, b: 2}).length;") == 2);
    CHECK(value_of("Object.keys({a: 1, b: 2})[0];").to_string() == "a");
    CHECK(number_of("Object.keys(Object.create({odziedziczona: 1})).length;") == 0);
    CHECK(number_of("var o = {}; Object.defineProperty(o, \"x\", {value: 1}); Object.keys(o).length;") == 0);
}

// Różnica wobec keys: getOwnPropertyNames pokazuje także niewyliczalne.
TEST_CASE("getOwnPropertyNames widzi też niewyliczalne") {
    CHECK(number_of("Object.getOwnPropertyNames({a: 1}).length;") == 1);
    CHECK(number_of("var o = {}; Object.defineProperty(o, \"x\", {value: 1});"
                    "Object.getOwnPropertyNames(o).length;") == 1);
    CHECK(number_of("Object.getOwnPropertyNames(Object.create({p: 1})).length;") == 0);
}

TEST_CASE("getOwnPropertyDescriptor zwraca deskryptor albo undefined") {
    CHECK(value_of("Object.getOwnPropertyDescriptor({a: 1}, \"a\").writable;").to_boolean());
    CHECK(number_of("Object.getOwnPropertyDescriptor({a: 1}, \"a\").value;") == 1);
    CHECK(value_of("Object.getOwnPropertyDescriptor({a: 1}, \"a\").enumerable;").to_boolean());
    CHECK(value_of("Object.getOwnPropertyDescriptor({a: 1}, \"a\").configurable;").to_boolean());

    CHECK(value_of("typeof Object.getOwnPropertyDescriptor({}, \"brak\");").to_string() == "undefined");
    CHECK(value_of("typeof Object.getOwnPropertyDescriptor(Object.create({p: 1}), \"p\");").to_string()
          == "undefined");

    // Deskryptor akcesorowy ma get i set zamiast value i writable.
    CHECK(value_of("var o = {get x() { return 1; }};"
                   "typeof Object.getOwnPropertyDescriptor(o, \"x\").get;").to_string() == "function");
    CHECK(value_of("var o = {get x() { return 1; }};"
                   "typeof Object.getOwnPropertyDescriptor(o, \"x\").value;").to_string() == "undefined");
}

// 15.2.3.6: atrybuty pominięte w deskryptorze domyślnie są FAŁSZYWE —
// inaczej niż przy zwykłym przypisaniu.
TEST_CASE("defineProperty domyślnie tworzy właściwość niewyliczalną i niezapisywalną") {
    CHECK(number_of("var o = {}; Object.defineProperty(o, \"x\", {value: 1}); o.x;") == 1);
    CHECK(number_of("var o = {}; Object.defineProperty(o, \"x\", {value: 1}); o.x = 2; o.x;") == 1);
    CHECK_FALSE(value_of("var o = {}; Object.defineProperty(o, \"x\", {value: 1});"
                         "Object.getOwnPropertyDescriptor(o, \"x\").enumerable;").to_boolean());
    CHECK_FALSE(value_of("var o = {}; Object.defineProperty(o, \"x\", {value: 1});"
                         "delete o.x;").to_boolean());
}

TEST_CASE("defineProperty z akcesorem") {
    CHECK(number_of("Object.defineProperty({}, \"x\", {get: function () { return 42; }}).x;") == 42);
    CHECK(number_of("var o = {}; Object.defineProperty(o, \"x\", {set: function (v) { this.y = v; }});"
                    "o.x = 7; o.y;") == 7);

    CHECK(caught_instance_of("Object.defineProperty({}, \"x\", 5);", "TypeError"));
    CHECK(caught_instance_of("Object.defineProperty({}, \"x\", {get: 5});", "TypeError"));
    CHECK(caught_instance_of("Object.defineProperty({}, \"x\", {get: function () {}, value: 1});", "TypeError"));
}

TEST_CASE("ponowna definicja niekonfigurowalnej właściwości rzuca TypeError") {
    CHECK(caught_instance_of("var o = {}; Object.defineProperty(o, \"x\", {value: 1});"
                             "Object.defineProperty(o, \"x\", {value: 2});", "TypeError"));

    // Ta sama wartość jest dozwolona — 8.12.9 pozwala na zmianę bez zmiany.
    CHECK(number_of("var o = {}; Object.defineProperty(o, \"x\", {value: 1});"
                    "Object.defineProperty(o, \"x\", {value: 1}); o.x;") == 1);
}

TEST_CASE("defineProperties definiuje wiele właściwości naraz") {
    CHECK(number_of("Object.defineProperties({}, {a: {value: 1}, b: {value: 2}}).a;") == 1);
    CHECK(number_of("var o = Object.defineProperties({}, {a: {value: 1}, b: {value: 2}});"
                    "Object.getOwnPropertyNames(o).length;") == 2);

    // Właściwości niewyliczalne źródła są pomijane.
    CHECK(number_of("var src = {}; Object.defineProperty(src, \"a\", {value: {value: 1}});"
                    "Object.getOwnPropertyNames(Object.defineProperties({}, src)).length;") == 0);
}

// 15.2.3.8-15.2.3.13
TEST_CASE("preventExtensions, seal i freeze") {
    CHECK(value_of("Object.isExtensible({});").to_boolean());
    CHECK_FALSE(value_of("Object.isExtensible(Object.preventExtensions({}));").to_boolean());

    // preventExtensions blokuje tylko dodawanie nowych właściwości.
    CHECK(value_of("var o = Object.preventExtensions({a: 1}); o.b = 2; typeof o.b;").to_string() == "undefined");
    CHECK(number_of("var o = Object.preventExtensions({a: 1}); o.a = 2; o.a;") == 2);

    // seal dokłada zakaz usuwania, freeze — zakaz zapisu.
    CHECK_FALSE(value_of("var o = Object.seal({a: 1}); delete o.a;").to_boolean());
    CHECK(number_of("var o = Object.seal({a: 1}); o.a = 2; o.a;") == 2);
    CHECK(number_of("var o = Object.freeze({a: 1}); o.a = 2; o.a;") == 1);

    CHECK(value_of("Object.isSealed(Object.seal({a: 1}));").to_boolean());
    CHECK(value_of("Object.isFrozen(Object.freeze({a: 1}));").to_boolean());
    CHECK_FALSE(value_of("Object.isFrozen(Object.seal({a: 1}));").to_boolean());
    CHECK(value_of("Object.isSealed(Object.freeze({a: 1}));").to_boolean());

    // Pusty obiekt nierozszerzalny jest jednocześnie zapieczętowany i zamrożony.
    CHECK(value_of("Object.isFrozen(Object.preventExtensions({}));").to_boolean());
}

TEST_CASE("w strict zapis do zamrożonego obiektu rzuca TypeError") {
    CHECK(strict_throws_instance_of("var o = Object.freeze({a: 1}); o.a = 2;", "TypeError"));
    CHECK(strict_throws_instance_of("var o = Object.preventExtensions({}); o.b = 1;", "TypeError"));
}

// 15.2.3 krok 1 każdej z metod: ES5.1 wymaga obiektu (ES2015 to złagodziło).
TEST_CASE("metody Object wymagają obiektu") {
    CHECK(caught_instance_of("Object.keys(5);", "TypeError"));
    CHECK(caught_instance_of("Object.getPrototypeOf(\"abc\");", "TypeError"));
    CHECK(caught_instance_of("Object.freeze(null);", "TypeError"));
    CHECK(caught_instance_of("Object.getOwnPropertyNames(undefined);", "TypeError"));
}

// ---------------------------------------------------------------------------
// Object.prototype (15.2.4)
// ---------------------------------------------------------------------------

TEST_CASE("toString podaje klasę obiektu") {
    CHECK(value_of("Object.prototype.toString.call([]);").to_string() == "[object Array]");
    CHECK(value_of("Object.prototype.toString.call({});").to_string() == "[object Object]");
    CHECK(value_of("Object.prototype.toString.call(function () {});").to_string() == "[object Function]");
    CHECK(value_of("Object.prototype.toString.call(/a/);").to_string() == "[object RegExp]");
    CHECK(value_of("Object.prototype.toString.call(null);").to_string() == "[object Null]");
    CHECK(value_of("Object.prototype.toString.call(undefined);").to_string() == "[object Undefined]");
}

TEST_CASE("isPrototypeOf schodzi po całym łańcuchu") {
    CHECK(value_of("Object.prototype.isPrototypeOf({});").to_boolean());
    CHECK(value_of("var base = {}; base.isPrototypeOf(Object.create(base));").to_boolean());
    CHECK(value_of("var base = {}; base.isPrototypeOf(Object.create(Object.create(base)));").to_boolean());

    CHECK_FALSE(value_of("({}).isPrototypeOf({});").to_boolean());
    CHECK_FALSE(value_of("({}).isPrototypeOf(5);").to_boolean());
    CHECK_FALSE(value_of("var o = {}; o.isPrototypeOf(o);").to_boolean());
}

TEST_CASE("propertyIsEnumerable dotyczy tylko własnych właściwości") {
    CHECK(value_of("({a: 1}).propertyIsEnumerable(\"a\");").to_boolean());
    CHECK_FALSE(value_of("({a: 1}).propertyIsEnumerable(\"b\");").to_boolean());
    CHECK_FALSE(value_of("Object.create({p: 1}).propertyIsEnumerable(\"p\");").to_boolean());
    CHECK_FALSE(value_of("var o = {}; Object.defineProperty(o, \"x\", {value: 1});"
                         "o.propertyIsEnumerable(\"x\");").to_boolean());
}

TEST_CASE("valueOf i toLocaleString") {
    CHECK(value_of("var o = {}; o.valueOf() === o;").to_boolean());
    // Number.prototype.valueOf przesłania Object.prototype.valueOf, więc wraca
    // prymityw, a nie opakowanie z ToObject(this).
    CHECK(value_of("typeof (5).valueOf();").to_string() == "number");
    CHECK(value_of("typeof Object.prototype.valueOf.call(5);").to_string() == "object");
    CHECK(value_of("({}).toLocaleString();").to_string() == "[object Object]");

    // toLocaleString woła this.toString(), więc podmiana toString jest widoczna.
    CHECK(value_of("var o = {toString: function () { return \"moje\"; }}; o.toLocaleString();").to_string()
          == "moje");
}

// ---------------------------------------------------------------------------
// 15.4 Array.prototype
// ---------------------------------------------------------------------------

TEST_CASE("Array: konstruktor rozróżnia długość od elementu") {
    CHECK(number_of("new Array(3).length;") == 3);
    CHECK_FALSE(value_of("new Array(3).hasOwnProperty(\"0\");").to_boolean());

    // Jeden argument liczbowy to długość, ale jeden nieliczbowy to już element.
    CHECK(number_of("Array(\"3\").length;") == 1);
    CHECK(number_of("new Array(1, 2, 3).length;") == 3);
    CHECK(number_of("Array().length;") == 0);

    CHECK(caught_instance_of("new Array(-1);", "RangeError"));
    CHECK(caught_instance_of("new Array(1.5);", "RangeError"));
}

TEST_CASE("Array.isArray odróżnia tablicę od obiektu podobnego do tablicy") {
    CHECK(value_of("Array.isArray([]);").to_boolean());
    CHECK_FALSE(value_of("Array.isArray({length: 0});").to_boolean());
    CHECK_FALSE(value_of("Array.isArray(\"abc\");").to_boolean());
    CHECK_FALSE(value_of("Array.isArray(Array.prototype.slice.call({length: 0}) === null);").to_boolean());
}

TEST_CASE("join i toString") {
    CHECK(value_of("[1, 2, 3].join(\"-\");").to_string() == "1-2-3");
    CHECK(value_of("[1, 2, 3].join();").to_string() == "1,2,3");
    CHECK(value_of("[1, 2, 3].join(undefined);").to_string() == "1,2,3");

    // Krok 7: undefined i null znikają, dziury też.
    CHECK(value_of("[null, undefined, 1].join(\"-\");").to_string() == "--1");
    CHECK(value_of("[1, , 3].join(\"-\");").to_string() == "1--3");

    CHECK(value_of("[1, 2].toString();").to_string() == "1,2");
    CHECK(value_of("String([1, [2, 3]]);").to_string() == "1,2,3");

    // 15.4.4.2 sięga po this.join — podmieniony jest widoczny.
    CHECK(value_of("var a = [1]; a.join = function () { return \"moje\"; }; a.toString();").to_string()
          == "moje");

    // A gdy join nie jest wołalne, wraca zachowanie Object.prototype.toString.
    CHECK(value_of("var a = [1]; a.join = 5; a.toString();").to_string() == "[object Array]");
}

TEST_CASE("push, pop, shift i unshift aktualizują length") {
    CHECK(value_of("var a = [1]; a.push(2, 3) + \"|\" + a.join();").to_string() == "3|1,2,3");
    CHECK(value_of("var a = [1, 2]; a.pop() + \"|\" + a.length;").to_string() == "2|1");
    CHECK(value_of("var a = [1, 2]; a.shift() + \"|\" + a.join();").to_string() == "1|2");
    CHECK(value_of("var a = [3]; a.unshift(1, 2) + \"|\" + a.join();").to_string() == "3|1,2,3");

    CHECK(value_of("typeof [].pop();").to_string() == "undefined");
    CHECK(value_of("typeof [].shift();").to_string() == "undefined");
    CHECK(number_of("var a = []; a.pop(); a.length;") == 0);
}

TEST_CASE("reverse zachowuje dziury") {
    CHECK(value_of("[1, 2, 3].reverse().join();").to_string() == "3,2,1");
    CHECK(value_of("var a = [1, 2]; a.reverse() === a;").to_string() == "true");

    // Dziura po odwróceniu nadal jest dziurą, a nie wartością undefined.
    CHECK_FALSE(value_of("var a = [1, , 3]; a.reverse(); a.hasOwnProperty(\"1\");").to_boolean());
    CHECK_FALSE(value_of("var a = [1, 2, ]; delete a[0]; a.reverse(); a.hasOwnProperty(\"1\");").to_boolean());
}

TEST_CASE("slice i splice") {
    CHECK(value_of("[1, 2, 3, 4, 5].slice(1, 3).join();").to_string() == "2,3");
    CHECK(value_of("[1, 2, 3].slice(-2).join();").to_string() == "2,3");
    CHECK(value_of("[1, 2, 3].slice(2, 1).length;").to_string() == "0");

    // splice zwraca usunięte, a modyfikuje w miejscu.
    CHECK(value_of("var a = [1, 2, 3, 4]; a.splice(1, 2).join() + \"|\" + a.join();").to_string()
          == "2,3|1,4");
    CHECK(value_of("var a = [1, 4]; a.splice(1, 0, 2, 3); a.join();").to_string() == "1,2,3,4");
    CHECK(value_of("var a = [1, 2, 3]; a.splice(1, 1, \"x\", \"y\"); a.join();").to_string() == "1,x,y,3");
    CHECK(number_of("var a = [1, 2, 3]; a.splice(1, 99); a.length;") == 1);
}

TEST_CASE("indexOf i lastIndexOf porównują ściśle") {
    CHECK(number_of("[1, 2, 3, 2].indexOf(2);") == 1);
    CHECK(number_of("[1, 2, 3, 2].lastIndexOf(2);") == 3);
    CHECK(number_of("[1, 2, 3].indexOf(2, 2);") == -1);
    CHECK(number_of("[1, 2, 3].indexOf(\"2\");") == -1);   // bez konwersji
    CHECK(number_of("[NaN].indexOf(NaN);") == -1);          // NaN !== NaN
    CHECK(number_of("[].indexOf(1);") == -1);
    CHECK(number_of("[1, , 3].indexOf(undefined);") == -1); // dziura jest pomijana
}

TEST_CASE("forEach, map, filter, every i some pomijają dziury") {
    CHECK(value_of("[1, 2, 3].map(function (x) { return x * 2; }).join();").to_string() == "2,4,6");
    CHECK(value_of("[1, 2, 3, 4].filter(function (x) { return x % 2 === 0; }).join();").to_string() == "2,4");
    CHECK(value_of("[1, 2].every(function (x) { return x > 0; });").to_string() == "true");
    CHECK(value_of("[1, 2].some(function (x) { return x > 5; });").to_string() == "false");
    CHECK(number_of("var s = 0; [1, 2, 3].forEach(function (x) { s += x; }); s;") == 6);

    CHECK(number_of("var c = 0; [1, , 3].forEach(function () { c++; }); c;") == 2);

    // Drugi argument to this wewnątrz funkcji zwrotnej.
    CHECK(value_of("[1, 2].filter(function (x) { return this.k === x; }, {k: 2}).join();").to_string() == "2");

    // Trzeci argument funkcji zwrotnej to sama tablica.
    CHECK(value_of("[1, 2].map(function (x, i, a) { return a.length; }).join();").to_string() == "2,2");

    CHECK(caught_instance_of("[1].map(1);", "TypeError"));
    CHECK(caught_instance_of("[1].forEach();", "TypeError"));
}

TEST_CASE("reduce i reduceRight") {
    CHECK(number_of("[1, 2, 3].reduce(function (a, b) { return a + b; });") == 6);
    CHECK(number_of("[1, 2, 3].reduce(function (a, b) { return a + b; }, 10);") == 16);
    CHECK(value_of("[1, 2, 3].reduceRight(function (a, b) { return a + \"-\" + b; });").to_string()
          == "3-2-1");

    // Bez wartości początkowej pusta tablica to błąd (krok 8).
    CHECK(caught_instance_of("[].reduce(function (a, b) { return a; });", "TypeError"));
    CHECK(number_of("[].reduce(function (a, b) { return a; }, 7);") == 7);
}

TEST_CASE("sort domyślnie porównuje jako łańcuchy") {
    CHECK(value_of("[3, 1, 10, 2].sort().join();").to_string() == "1,10,2,3");
    CHECK(value_of("[3, 1, 10, 2].sort(function (a, b) { return a - b; }).join();").to_string()
          == "1,2,3,10");
    CHECK(value_of("var a = [2, 1]; a.sort() === a;").to_string() == "true");

    // Krok 11: undefined ląduje na końcu niezależnie od komparatora.
    CHECK(value_of("[undefined, 1, undefined, 2].sort().join(\",\");").to_string() == "1,2,,");

    // Wyjątek z komparatora musi wyjść na zewnątrz, a nie zniknąć w sortowaniu.
    CHECK(caught_instance_of("[2, 1].sort(function () { throw new TypeError(\"x\"); });", "TypeError"));
    CHECK(caught_instance_of("[1].sort(5);", "TypeError"));
}

TEST_CASE("metody Array są generyczne") {
    CHECK(value_of("Array.prototype.join.call({0: \"a\", 1: \"b\", length: 2}, \"+\");").to_string() == "a+b");
    CHECK(value_of("Array.prototype.slice.call({0: \"a\", length: 1}).join();").to_string() == "a");
    CHECK(number_of("Array.prototype.push.call({length: 0}, \"x\");") == 1);
    CHECK(value_of("Array.prototype.map.call(\"ab\", function (c) { return c + \"!\"; }).join();").to_string()
          == "a!,b!");
}

// ---------------------------------------------------------------------------
// 15.5 String.prototype
// ---------------------------------------------------------------------------

TEST_CASE("String: wywołanie kontra new") {
    CHECK(value_of("typeof String(5);").to_string() == "string");
    CHECK(value_of("typeof new String(5);").to_string() == "object");
    CHECK(value_of("String();").to_string() == "");
    CHECK(value_of("String(null) + String(undefined);").to_string() == "nullundefined");

    CHECK(number_of("new String(\"abc\").length;") == 3);
    CHECK(value_of("new String(\"abc\")[1];").to_string() == "b");
    CHECK(value_of("new String(\"abc\").valueOf();").to_string() == "abc");
    CHECK(value_of("Object.prototype.toString.call(new String(\"\"));").to_string() == "[object String]");
    CHECK(value_of("Object.prototype.toString.call(\"x\");").to_string() == "[object String]");
}

TEST_CASE("charAt, charCodeAt i fromCharCode") {
    CHECK(value_of("\"abc\".charAt(1);").to_string() == "b");
    CHECK(value_of("\"abc\".charAt(9);").to_string() == "");
    CHECK(value_of("\"abc\".charAt();").to_string() == "a");   // brak argumentu to 0
    CHECK(number_of("\"abc\".charCodeAt(0);") == 97);
    CHECK(std::isnan(number_of("\"abc\".charCodeAt(9);")));

    CHECK(value_of("String.fromCharCode(72, 105);").to_string() == "Hi");
    CHECK(value_of("String.fromCharCode();").to_string() == "");
}

TEST_CASE("indexOf i lastIndexOf w łańcuchu") {
    CHECK(number_of("\"hello\".indexOf(\"l\");") == 2);
    CHECK(number_of("\"hello\".indexOf(\"l\", 4);") == -1);
    CHECK(number_of("\"hello\".lastIndexOf(\"l\");") == 3);
    CHECK(number_of("\"hello\".lastIndexOf(\"l\", 2);") == 2);
    CHECK(number_of("\"abc\".indexOf(\"\");") == 0);

    // Krok 5: NaN w lastIndexOf znaczy +nieskończoność, nie zero.
    CHECK(number_of("\"abab\".lastIndexOf(\"a\", NaN);") == 2);
}

TEST_CASE("slice, substring i substr różnią się obsługą granic") {
    CHECK(value_of("\"abcdef\".slice(1, 3);").to_string() == "bc");
    CHECK(value_of("\"abcdef\".slice(-2);").to_string() == "ef");
    CHECK(value_of("\"abcdef\".slice(3, 1);").to_string() == "");     // slice NIE zamienia

    CHECK(value_of("\"abcdef\".substring(3, 1);").to_string() == "bc"); // substring zamienia
    CHECK(value_of("\"abcdef\".substring(-1, 2);").to_string() == "ab");

    CHECK(value_of("\"abcdef\".substr(1, 3);").to_string() == "bcd");
    CHECK(value_of("\"abcdef\".substr(-2);").to_string() == "ef");
    CHECK(value_of("\"abcdef\".substr(-2, 1);").to_string() == "e");
}

TEST_CASE("trim, wielkość liter i concat") {
    CHECK(value_of("\"  x  \".trim();").to_string() == "x");
    CHECK(value_of("\"\\t\\n x \\r\\n\".trim();").to_string() == "x");
    CHECK(value_of("\"   \".trim();").to_string() == "");

    CHECK(value_of("\"AbC\".toLowerCase();").to_string() == "abc");
    CHECK(value_of("\"AbC\".toUpperCase();").to_string() == "ABC");
    CHECK(value_of("\"abc\".concat(\"d\", \"e\");").to_string() == "abcde");
    CHECK(value_of("\"x\".concat();").to_string() == "x");
}

TEST_CASE("split") {
    CHECK(value_of("\"a,b,c\".split(\",\").join(\"|\");").to_string() == "a|b|c");
    CHECK(value_of("\"a,b,c\".split(\",\", 2).join(\"|\");").to_string() == "a|b");
    CHECK(value_of("\"abc\".split(\"\").join(\"-\");").to_string() == "a-b-c");
    CHECK(number_of("\"abc\".split().length;") == 1);
    CHECK(number_of("\"abc\".split(\",\", 0).length;") == 0);

    // Krok 10: pusty łańcuch dzieli się na nic tylko pustym separatorem.
    CHECK(number_of("\"\".split(\"\").length;") == 0);
    CHECK(number_of("\"\".split(\",\").length;") == 1);

    CHECK(number_of("\"a,,b\".split(\",\").length;") == 3);
    CHECK(value_of("\",a,\".split(\",\").join(\"|\");").to_string() == "|a|");
}

TEST_CASE("replace podmienia tylko pierwsze wystąpienie") {
    CHECK(value_of("\"aaa\".replace(\"a\", \"b\");").to_string() == "baa");
    CHECK(value_of("\"hello\".replace(\"z\", \"Z\");").to_string() == "hello");

    // Table 22: rozwinięcia z dolarem.
    CHECK(value_of("\"abc\".replace(\"b\", \"[$&]\");").to_string() == "a[b]c");
    CHECK(value_of("\"abc\".replace(\"b\", \"[$`|$']\");").to_string() == "a[a|c]c");
    CHECK(value_of("\"abc\".replace(\"b\", \"$$\");").to_string() == "a$c");

    // Funkcja dostaje dopasowanie, pozycję i cały łańcuch.
    CHECK(value_of("\"hello\".replace(\"ll\", function (m, i, s) { return m + i + s.length; });").to_string()
          == "hell25o");
}

TEST_CASE("metody String są generyczne, valueOf nie jest") {
    CHECK(value_of("String.prototype.charAt.call(123, 1);").to_string() == "2");
    CHECK(value_of("String.prototype.indexOf.call({toString: function () { return \"xyz\"; }}, \"y\");")
          .to_string() == "1");

    CHECK(caught_instance_of("String.prototype.trim.call(null);", "TypeError"));
    CHECK(caught_instance_of("String.prototype.valueOf.call(5);", "TypeError"));
    CHECK(value_of("String.prototype.valueOf.call(new String(\"x\"));").to_string() == "x");
}

// ---------------------------------------------------------------------------
// 15.6 Boolean.prototype
// ---------------------------------------------------------------------------

TEST_CASE("Boolean: opakowanie jest zawsze prawdziwe") {
    CHECK(value_of("Boolean(1) + \"|\" + Boolean(0) + \"|\" + Boolean(\"\");").to_string()
          == "true|false|false");
    CHECK(value_of("Boolean() + \"|\" + Boolean(null) + \"|\" + Boolean(NaN);").to_string()
          == "false|false|false");
    CHECK(value_of("Boolean({}) + \"|\" + Boolean([]);").to_string() == "true|true");

    CHECK(value_of("typeof new Boolean(1);").to_string() == "object");

    // Klasyczna pułapka: obiekt jest prawdziwy, choć opakowuje false.
    CHECK(value_of("new Boolean(false) ? \"prawda\" : \"falsz\";").to_string() == "prawda");
    CHECK(value_of("new Boolean(false).valueOf();").to_string() == "false");
    CHECK(value_of("new Boolean(false) == false;").to_string() == "true");
    CHECK(value_of("new Boolean(false) === false;").to_string() == "false");

    CHECK(value_of("(true).toString() + \"|\" + (false).toString();").to_string() == "true|false");
    CHECK(value_of("Object.prototype.toString.call(new Boolean(1));").to_string() == "[object Boolean]");

    CHECK(caught_instance_of("Boolean.prototype.valueOf.call(5);", "TypeError"));
    CHECK(caught_instance_of("Boolean.prototype.toString.call(\"x\");", "TypeError"));
}

// ---------------------------------------------------------------------------
// 15.7 Number.prototype
// ---------------------------------------------------------------------------

TEST_CASE("Number: wywołanie kontra new i stałe") {
    CHECK(value_of("typeof Number(5);").to_string() == "number");
    CHECK(value_of("typeof new Number(5);").to_string() == "object");
    CHECK(number_of("Number();") == 0);
    CHECK(std::isnan(number_of("Number(\"abc\");")));
    CHECK(number_of("new Number(5) + 1;") == 6);
    CHECK(value_of("Object.prototype.toString.call(5);").to_string() == "[object Number]");

    CHECK(number_of("Number.MAX_VALUE;") == std::numeric_limits<double>::max());
    CHECK(number_of("Number.MIN_VALUE;") == std::numeric_limits<double>::denorm_min());
    CHECK(std::isnan(number_of("Number.NaN;")));
    CHECK(std::isinf(number_of("Number.POSITIVE_INFINITY;")));

    // 15.7.3: stałe są zamrożone.
    CHECK(number_of("Number.MAX_VALUE = 1; Number.MAX_VALUE;") == std::numeric_limits<double>::max());
}

TEST_CASE("toString z podstawą") {
    CHECK(value_of("(255).toString(16);").to_string() == "ff");
    CHECK(value_of("(255).toString(2);").to_string() == "11111111");
    CHECK(value_of("(255).toString(8);").to_string() == "377");
    CHECK(value_of("(-255).toString(16);").to_string() == "-ff");
    CHECK(value_of("(0.5).toString(2);").to_string() == "0.1");
    CHECK(value_of("(0).toString(2);").to_string() == "0");
    CHECK(value_of("(5).toString(10);").to_string() == "5");

    // Powyżej 2^53 najmłodsze cyfry są zastępowane zerami.
    CHECK(value_of("(1e21).toString(36);").to_string() == "5v1j4f4ds7c000");

    CHECK(caught_instance_of("(5).toString(1);", "RangeError"));
    CHECK(caught_instance_of("(5).toString(37);", "RangeError"));
}

TEST_CASE("toFixed zaokrągla połówki w górę") {
    // To odróżnia nas od printf, który zaokrągla do parzystej.
    CHECK(value_of("(0.5).toFixed(0);").to_string() == "1");
    CHECK(value_of("(1.5).toFixed(0);").to_string() == "2");
    CHECK(value_of("(2.5).toFixed(0);").to_string() == "3");

    // 1.005 to w double 1.00499..., więc w dół — mimo pozornej połówki.
    CHECK(value_of("(1.005).toFixed(2);").to_string() == "1.00");

    CHECK(value_of("(123.456).toFixed(2);").to_string() == "123.46");
    CHECK(value_of("(0).toFixed(2);").to_string() == "0.00");
    CHECK(value_of("(1).toFixed();").to_string() == "1");

    // Krok 6: znak jest doklejany zawsze, nawet gdy wynik to zero.
    CHECK(value_of("(-0.4).toFixed(0);").to_string() == "-0");

    // Krok 8: od 10^21 wraca zwykłe ToString.
    CHECK(value_of("(1e21).toFixed(2);").to_string() == "1e+21");

    CHECK(value_of("(NaN).toFixed(2);").to_string() == "NaN");
    CHECK(value_of("(Infinity).toFixed(2);").to_string() == "Infinity");

    CHECK(caught_instance_of("(1).toFixed(21);", "RangeError"));
    CHECK(caught_instance_of("(1).toFixed(-1);", "RangeError"));
}

TEST_CASE("toExponential i toPrecision") {
    CHECK(value_of("(1.25).toExponential(1);").to_string() == "1.3e+0");
    CHECK(value_of("(123456).toExponential(2);").to_string() == "1.23e+5");
    CHECK(value_of("(0.00123).toExponential(2);").to_string() == "1.23e-3");
    CHECK(value_of("(0).toExponential(2);").to_string() == "0.00e+0");

    // Bez argumentu bierzemy najkrótszy jednoznaczny zapis, nie pełne rozwinięcie.
    CHECK(value_of("(123.456).toExponential();").to_string() == "1.23456e+2");

    CHECK(value_of("(123.456).toPrecision(2);").to_string() == "1.2e+2");
    CHECK(value_of("(123.456).toPrecision(6);").to_string() == "123.456");
    CHECK(value_of("(0.000001).toPrecision(2);").to_string() == "0.0000010");
    CHECK(value_of("(0.0000001).toPrecision(2);").to_string() == "1.0e-7");
    CHECK(value_of("(0).toPrecision(3);").to_string() == "0.00");
    CHECK(value_of("(1.5).toPrecision();").to_string() == "1.5");

    CHECK(caught_instance_of("(1).toPrecision(0);", "RangeError"));
    CHECK(caught_instance_of("(1).toPrecision(22);", "RangeError"));
    CHECK(caught_instance_of("Number.prototype.toFixed.call(\"x\");", "TypeError"));
}

// ---------------------------------------------------------------------------
// 15.8 Math
// ---------------------------------------------------------------------------

TEST_CASE("Math nie jest funkcją ani konstruktorem") {
    CHECK(value_of("typeof Math;").to_string() == "object");
    CHECK(value_of("Object.prototype.toString.call(Math);").to_string() == "[object Math]");
    CHECK(caught_instance_of("Math();", "TypeError"));
    CHECK(caught_instance_of("new Math();", "TypeError"));

    CHECK(number_of("Math.PI;") == doctest::Approx(3.141592653589793));
    CHECK(number_of("Math.PI = 3; Math.PI;") == doctest::Approx(3.141592653589793));
}

TEST_CASE("Math.round zaokrągla połówki w stronę plus nieskończoności") {
    CHECK(number_of("Math.round(0.5);") == 1);
    CHECK(number_of("Math.round(1.5);") == 2);
    CHECK(number_of("Math.round(2.5);") == 3);
    CHECK(number_of("Math.round(-1.5);") == -1);   // NIE -2
    CHECK(number_of("Math.round(-2.5);") == -2);
    CHECK(number_of("Math.round(-0.6);") == -1);

    // Naiwne floor(x + 0.5) dałoby tu 1, bo dodanie samo zaokrągla.
    CHECK(number_of("Math.round(0.49999999999999994);") == 0);

    // Wynik zero dziedziczy znak argumentu.
    CHECK(std::isinf(number_of("1 / Math.round(-0.5);")));
    CHECK(number_of("1 / Math.round(-0.5);") < 0);
    CHECK(number_of("1 / Math.round(-0.2);") < 0);

    CHECK(std::isnan(number_of("Math.round(NaN);")));
    CHECK(number_of("Math.round(4503599627370497);") == 4503599627370497.0);
}

TEST_CASE("Math.max i Math.min rozróżniają znak zera") {
    CHECK(number_of("Math.max(1, 2, 3);") == 3);
    CHECK(number_of("Math.min(1, 2, 3);") == 1);
    CHECK(number_of("Math.max();") == -std::numeric_limits<double>::infinity());
    CHECK(number_of("Math.min();") == std::numeric_limits<double>::infinity());
    CHECK(std::isnan(number_of("Math.max(1, NaN, 2);")));
    CHECK(number_of("Math.max(\"5\", 3);") == 5);

    CHECK(number_of("1 / Math.max(0, -0);") > 0);
    CHECK(number_of("1 / Math.min(0, -0);") < 0);

    // Konwersja obejmuje WSZYSTKIE argumenty, także po napotkaniu NaN.
    CHECK(value_of("var log = [];"
                   "Math.max({valueOf: function () { log.push(\"a\"); return NaN; }},"
                   "         {valueOf: function () { log.push(\"b\"); return 1; }});"
                   "log.join();").to_string() == "a,b");

    CHECK(caught_instance_of("Math.max(1, {valueOf: function () { throw new TypeError(\"x\"); }});",
                             "TypeError"));
}

TEST_CASE("Math.pow i pozostałe funkcje") {
    CHECK(number_of("Math.pow(2, 10);") == 1024);

    // std::pow(1, Infinity) daje 1, standard żąda NaN.
    CHECK(std::isnan(number_of("Math.pow(1, Infinity);")));
    CHECK(std::isnan(number_of("Math.pow(-1, Infinity);")));
    CHECK(number_of("Math.pow(NaN, 0);") == 1);

    CHECK(number_of("Math.abs(-5);") == 5);
    CHECK(number_of("Math.ceil(1.1);") == 2);
    CHECK(number_of("Math.floor(-1.1);") == -2);
    CHECK(number_of("Math.sqrt(16);") == 4);
    CHECK(std::isnan(number_of("Math.sqrt(-1);")));
    CHECK(number_of("Math.exp(0);") == 1);
    CHECK(number_of("Math.log(1);") == 0);
    CHECK(number_of("Math.atan2(1, 1);") == doctest::Approx(0.7853981633974483));
    CHECK(std::isnan(number_of("Math.floor(\"abc\");")));

    CHECK(number_of("Math.random() >= 0 && Math.random() < 1 ? 1 : 0;") == 1);
}

TEST_CASE("funkcje wbudowane mają length") {
    CHECK(number_of("Math.max.length;") == 2);
    CHECK(number_of("Math.abs.length;") == 1);
    CHECK(number_of("Math.random.length;") == 0);
    CHECK(number_of("Array.length;") == 1);

    // 15: length jest niezapisywalne i niewyliczalne.
    CHECK(number_of("Math.abs.length = 9; Math.abs.length;") == 1);
    CHECK_FALSE(value_of("Math.abs.propertyIsEnumerable(\"length\");").to_boolean());
}

// ---------------------------------------------------------------------------
// 15.1.2 Funkcje globalne
// ---------------------------------------------------------------------------

TEST_CASE("parseInt bierze najdłuższy pasujący przedrostek") {
    CHECK(number_of("parseInt(\"42\");") == 42);
    CHECK(number_of("parseInt(\"42abc\");") == 42);
    CHECK(number_of("parseInt(\"  42  \");") == 42);
    CHECK(number_of("parseInt(\"-42\");") == -42);
    CHECK(number_of("parseInt(\"+42\");") == 42);
    CHECK(number_of("parseInt(15.99);") == 15);

    // Brak jakiejkolwiek cyfry to NaN, a nie zero.
    CHECK(std::isnan(number_of("parseInt(\"abc\");")));
    CHECK(std::isnan(number_of("parseInt(\"\");")));
    CHECK(std::isnan(number_of("parseInt(null);")));

    // Znak minus przeżywa nawet wtedy, gdy wynikiem jest zero.
    CHECK(number_of("1 / parseInt(\"-0\");") < 0);
}

TEST_CASE("parseInt i podstawa") {
    // Przedrostek 0x obcinamy tylko przy podstawie domyślnej albo 16.
    CHECK(number_of("parseInt(\"0x1f\");") == 31);
    CHECK(number_of("parseInt(\"0x1f\", 16);") == 31);
    CHECK(number_of("parseInt(\"1f\", 16);") == 31);
    CHECK(number_of("parseInt(\"0x1f\", 10);") == 0);   // czyta samo "0"

    CHECK(number_of("parseInt(\"101\", 2);") == 5);
    CHECK(number_of("parseInt(\"z\", 36);") == 35);
    CHECK(number_of("parseInt(\"42\", 0);") == 42);

    CHECK(std::isnan(number_of("parseInt(\"42\", 1);")));
    CHECK(std::isnan(number_of("parseInt(\"42\", 37);")));

    // Bez ósemkowego dziedzictwa: wiodące zero nic nie zmienia.
    CHECK(number_of("parseInt(\"08\");") == 8);

    // Wykładnik nie należy do gramatyki liczb całkowitych.
    CHECK(number_of("parseInt(\"1e3\");") == 1);

    // Klasyczna pułapka: map podaje indeks jako drugi argument.
    CHECK(value_of("[1, 2, 3].map(parseInt).join();").to_string() == "1,NaN,NaN");
}

TEST_CASE("parseFloat czyta StrDecimalLiteral") {
    CHECK(number_of("parseFloat(\"3.14\");") == doctest::Approx(3.14));
    CHECK(number_of("parseFloat(\"3.14abc\");") == doctest::Approx(3.14));
    CHECK(number_of("parseFloat(\".5\");") == 0.5);
    CHECK(number_of("parseFloat(\"-.5e2\");") == -50);
    CHECK(number_of("parseFloat(\"1.\");") == 1);
    CHECK(number_of("parseFloat(\"  1.5  \");") == 1.5);
    CHECK(std::isinf(number_of("parseFloat(\"Infinity\");")));
    CHECK(number_of("parseFloat(\"-Infinity\");") < 0);

    // Wykładnik bez cyfr jest odrzucany, ale sama podstawa zostaje.
    CHECK(number_of("parseFloat(\"1e\");") == 1);
    CHECK(number_of("parseFloat(\"1e+\");") == 1);
    CHECK(number_of("parseFloat(\"1e2\");") == 100);

    // Zapis szesnastkowy NIE należy do gramatyki, choć strtod by go przyjął.
    CHECK(number_of("parseFloat(\"0x10\");") == 0);

    CHECK(number_of("parseFloat(\"1.2.3\");") == doctest::Approx(1.2));
    CHECK(std::isnan(number_of("parseFloat(\".\");")));
    CHECK(std::isnan(number_of("parseFloat(\"abc\");")));
    CHECK(std::isnan(number_of("parseFloat(\"\");")));
}

TEST_CASE("isNaN i isFinite konwertują argument") {
    CHECK(value_of("isNaN(NaN);").to_boolean());
    CHECK_FALSE(value_of("isNaN(1);").to_boolean());
    CHECK(value_of("isNaN(\"abc\");").to_boolean());
    CHECK_FALSE(value_of("isNaN(\"1\");").to_boolean());
    CHECK(value_of("isNaN(undefined);").to_boolean());
    CHECK_FALSE(value_of("isNaN(null);").to_boolean());   // ToNumber(null) to 0

    CHECK(value_of("isFinite(1);").to_boolean());
    CHECK_FALSE(value_of("isFinite(Infinity);").to_boolean());
    CHECK_FALSE(value_of("isFinite(NaN);").to_boolean());
    CHECK(value_of("isFinite(\"5\");").to_boolean());
}

// ---------------------------------------------------------------------------
// 15.1.3 Funkcje URI
// ---------------------------------------------------------------------------

TEST_CASE("encodeURI zostawia strukturę adresu, encodeURIComponent nie") {
    CHECK(value_of("encodeURI(\"http://a.pl/x y?q=1&r=2#z\");").to_string()
          == "http://a.pl/x%20y?q=1&r=2#z");
    CHECK(value_of("encodeURIComponent(\"http://a.pl/x y?q=1&r=2#z\");").to_string()
          == "http%3A%2F%2Fa.pl%2Fx%20y%3Fq%3D1%26r%3D2%23z");

    // uriMark i znaki alfanumeryczne nigdy nie są kodowane.
    CHECK(value_of("encodeURIComponent(\"-_.!~*'()\");").to_string() == "-_.!~*'()");
    CHECK(value_of("encodeURIComponent(\"A1\");").to_string() == "A1");
}

TEST_CASE("kodowanie URI przechodzi przez UTF-8") {
    CHECK(value_of("encodeURIComponent(\"\\u017C\");").to_string() == "%C5%BC");
    CHECK(value_of("encodeURI(\"\\u20AC\");").to_string() == "%E2%82%AC");
    CHECK(value_of("decodeURI(\"%E2%82%AC\");").to_string() == "\u20AC");

    // Para surogatów to jeden znak, więc cztery bajty.
    CHECK(value_of("encodeURIComponent(\"\\uD83D\\uDE00\");").to_string() == "%F0%9F%98%80");
    CHECK(number_of("decodeURIComponent(\"%F0%9F%98%80\").length;") == 2);

    CHECK(value_of("decodeURIComponent(encodeURIComponent(\"\\u017C \\u0105\\u0107\"));").to_string()
          == "\u017C \u0105\u0107");
}

TEST_CASE("decodeURI zachowuje znaki strukturalne") {
    CHECK(value_of("decodeURI(\"a%20b\");").to_string() == "a b");

    // Ukośnik i krzyżyk nie mogą powstać z dekodowania, bo zmieniłyby adres.
    CHECK(value_of("decodeURI(\"%2F\");").to_string() == "%2F");
    CHECK(value_of("decodeURIComponent(\"%2F\");").to_string() == "/");
    CHECK(value_of("decodeURI(\"%23\");").to_string() == "%23");
    CHECK(value_of("decodeURIComponent(\"%23\");").to_string() == "#");

    // Plus nie jest spacją — to konwencja formularzy, nie URI.
    CHECK(value_of("decodeURIComponent(\"a+b\");").to_string() == "a+b");
}

TEST_CASE("URIError przy zepsutym wejściu") {
    CHECK(caught_instance_of("decodeURI(\"%\");", "URIError"));
    CHECK(caught_instance_of("decodeURI(\"%ZZ\");", "URIError"));
    CHECK(caught_instance_of("decodeURI(\"%C5\");", "URIError"));

    // Zapis nadmiarowy: zero na dwóch bajtach.
    CHECK(caught_instance_of("decodeURIComponent(\"%C0%80\");", "URIError"));

    // Surogat zakodowany jako znak nie istnieje w UTF-8.
    CHECK(caught_instance_of("decodeURIComponent(\"%ED%A0%80\");", "URIError"));

    // Surogat bez pary nie ma zapisu w UTF-8, więc kodowanie też zawodzi.
    CHECK(caught_instance_of("encodeURI(\"\\uD800\");", "URIError"));
    CHECK(caught_instance_of("encodeURI(\"\\uDC00x\");", "URIError"));
    CHECK(caught_instance_of("encodeURI(\"\\uD800a\");", "URIError"));
}

TEST_CASE("URIError i EvalError są zwykłymi błędami natywnymi") {
    CHECK(value_of("new URIError(\"x\") instanceof Error;").to_boolean());
    CHECK(value_of("new URIError(\"x\").name;").to_string() == "URIError");
    CHECK(value_of("String(new URIError(\"x\"));").to_string() == "URIError: x");

    CHECK(value_of("new EvalError() instanceof Error;").to_boolean());
    CHECK(value_of("String(new EvalError());").to_string() == "EvalError");
    CHECK(value_of("typeof URIError + \"|\" + typeof EvalError;").to_string() == "function|function");
}

TEST_CASE("funkcje globalne mają length") {
    CHECK(number_of("parseInt.length;") == 2);
    CHECK(number_of("parseFloat.length;") == 1);
    CHECK(number_of("isNaN.length;") == 1);
    CHECK(number_of("encodeURI.length;") == 1);
    CHECK(number_of("decodeURIComponent.length;") == 1);
}

// ---------------------------------------------------------------------------
// Annex C: zapis ósemkowy jako błąd wczesny w trybie ścisłym
// ---------------------------------------------------------------------------

TEST_CASE("zapis ósemkowy działa w trybie swobodnym") {
    CHECK(number_of("010;") == 8);
    CHECK(number_of("010 + 010;") == 16);
    CHECK(number_of("08;") == 8);
    CHECK(value_of("\"\\101\\102\\103\";").to_string() == "ABC");
    CHECK(number_of("({010: 1})[8];") == 1);
}

TEST_CASE("w trybie ścisłym zapis ósemkowy jest błędem składni") {
    CHECK(throws("eval(\"'use strict'; 010;\");"));
    CHECK(throws("eval(\"'use strict'; 08;\");"));
    CHECK(throws("eval(\"'use strict'; '\\\\101';\");"));
    CHECK(throws("eval(\"'use strict'; ({010: 1});\");"));

    // Sam \0 nie jest zapisem ósemkowym i w strict jest dozwolony.
    CHECK(number_of("eval(\"'use strict'; '\\\\0';\").length;") == 1);

    // Ograniczenie dziedziczy się do funkcji zagnieżdżonych.
    CHECK(throws("eval(\"'use strict'; function f() { return 010; }\");"));

    // Poza trybem ścisłym ta sama treść przechodzi.
    CHECK(number_of("eval(\"010;\");") == 8);
}

TEST_CASE("identyfikatory Unicode działają w ewaluatorze") {
    CHECK(number_of("var żółw = 7; żółw;") == 7);
    CHECK(number_of("var o = {}; o.ą = 3; o.ą;") == 3);
    CHECK(number_of("({ą: 4}).ą;") == 4);
    CHECK(value_of("typeof ąćę;").to_string() == "undefined");

    // Nazwa zapisana z ucieczką i wprost to ta sama zmienna.
    CHECK(number_of("var \\u0105 = 5; ą;") == 5);
}

// ---------------------------------------------------------------------------
// 15.9 Date
// ---------------------------------------------------------------------------
//
// Testy celowo liczą w UTC. Wynik metod lokalnych zależy od strefy maszyny,
// więc sprawdzamy dla nich tylko niezmienniki, a nie konkretne liczby.

TEST_CASE("Date: konstruktor i wartość wewnętrzna") {
    CHECK(number_of("new Date(0).getTime();") == 0);
    CHECK(number_of("new Date(0).valueOf();") == 0);
    CHECK(value_of("typeof new Date();").to_string() == "object");
    CHECK(value_of("Object.prototype.toString.call(new Date());").to_string() == "[object Date]");
    CHECK(value_of("new Date(0) instanceof Date;").to_boolean());
    CHECK(value_of("new Date(0).constructor === Date;").to_boolean());

    // 15.9.2.1: bez new zwraca ŁAŃCUCH z bieżącym czasem, ignorując argumenty.
    CHECK(value_of("typeof Date();").to_string() == "string");
    CHECK(value_of("typeof Date(0);").to_string() == "string");

    CHECK(value_of("typeof Date.now();").to_string() == "number");
    CHECK(value_of("Date.now() > 1600000000000;").to_boolean());
}

TEST_CASE("Date.UTC składa czas z pól") {
    CHECK(number_of("Date.UTC(1970, 0, 1);") == 0);
    CHECK(number_of("Date.UTC(2020, 0, 1);") == 1577836800000.0);
    CHECK(number_of("Date.UTC(1970, 0, 2);") == 86400000);

    // 15.9.1.12: miesiąc i dzień spoza zakresu PRZELEWAJĄ się dalej.
    CHECK(number_of("new Date(Date.UTC(2020, 12, 1)).getUTCFullYear();") == 2021);
    CHECK(number_of("new Date(Date.UTC(2020, 12, 1)).getUTCMonth();") == 0);
    CHECK(number_of("new Date(Date.UTC(2020, 0, 32)).getUTCMonth();") == 1);
    CHECK(number_of("new Date(Date.UTC(2020, 0, 0)).getUTCDate();") == 31);

    // 15.9.3.1 krok 9: rok dwucyfrowy znaczy XX wiek.
    CHECK(number_of("new Date(Date.UTC(99, 0, 1)).getUTCFullYear();") == 1999);
    CHECK(number_of("new Date(Date.UTC(1999, 0, 1)).getUTCFullYear();") == 1999);
    CHECK(number_of("new Date(Date.UTC(100, 0, 1)).getUTCFullYear();") == 100);
}

TEST_CASE("data niepoprawna zostaje niepoprawna") {
    CHECK(std::isnan(number_of("new Date(NaN).getTime();")));
    CHECK(std::isnan(number_of("new Date(\"cokolwiek\").getTime();")));
    CHECK(value_of("String(new Date(NaN));").to_string() == "Invalid Date");
    CHECK(value_of("new Date(NaN).toDateString();").to_string() == "Invalid Date");

    // 15.9.1.14: poza zakresem 100 milionów dni data jest niepoprawna,
    // a nie obcinana do brzegu.
    CHECK(number_of("new Date(8.64e15).getTime();") == 8.64e15);
    CHECK(std::isnan(number_of("new Date(8.64e15 + 1).getTime();")));

    // Metody odczytu na niepoprawnej dacie dają NaN, ale nie rzucają.
    CHECK(std::isnan(number_of("new Date(NaN).getUTCFullYear();")));
    CHECK(std::isnan(number_of("new Date(NaN).getTimezoneOffset();")));

    // toISOString to jedyna metoda tekstowa, która rzuca.
    CHECK(caught_instance_of("new Date(NaN).toISOString();", "RangeError"));
    CHECK(value_of("new Date(NaN).toJSON();").type() == JSValueType::Null);
}

TEST_CASE("odczyt pól w UTC") {
    const std::string d = "var d = new Date(Date.UTC(2020, 5, 15, 10, 20, 30, 40));";

    CHECK(number_of(d + "d.getUTCFullYear();") == 2020);
    CHECK(number_of(d + "d.getUTCMonth();") == 5);
    CHECK(number_of(d + "d.getUTCDate();") == 15);
    CHECK(number_of(d + "d.getUTCHours();") == 10);
    CHECK(number_of(d + "d.getUTCMinutes();") == 20);
    CHECK(number_of(d + "d.getUTCSeconds();") == 30);
    CHECK(number_of(d + "d.getUTCMilliseconds();") == 40);

    // 15.9.1.6: 1 stycznia 1970 był czwartkiem.
    CHECK(number_of("new Date(0).getUTCDay();") == 4);
    CHECK(number_of("new Date(Date.UTC(2020, 5, 15)).getUTCDay();") == 1);
}

TEST_CASE("lata przestępne") {
    CHECK(value_of("new Date(Date.UTC(2000, 1, 29)).toISOString();").to_string()
          == "2000-02-29T00:00:00.000Z");

    // 1900 NIE jest przestępny (podzielny przez 100, ale nie przez 400),
    // więc 29 lutego przelewa się na 1 marca.
    CHECK(value_of("new Date(Date.UTC(1900, 1, 29)).toISOString();").to_string()
          == "1900-03-01T00:00:00.000Z");
    CHECK(value_of("new Date(Date.UTC(2100, 1, 29)).toISOString();").to_string()
          == "2100-03-01T00:00:00.000Z");
}

TEST_CASE("czasy przed epoką") {
    CHECK(value_of("new Date(-1).toISOString();").to_string() == "1969-12-31T23:59:59.999Z");
    CHECK(value_of("new Date(Date.UTC(1969, 11, 31, 23, 59, 59, 999)).toISOString();").to_string()
          == "1969-12-31T23:59:59.999Z");
    CHECK(number_of("new Date(Date.UTC(1900, 0, 1)).getUTCFullYear();") == 1900);
    // Uwaga: Date.UTC(1, ...) to rok 1901, bo 1 wpada w zakres dwucyfrowy.
    CHECK(number_of("new Date(Date.UTC(1, 0, 1)).getUTCFullYear();") == 1901);
    CHECK(number_of("var d = new Date(0); d.setUTCFullYear(1); d.getUTCFullYear();") == 1);

    // Rok spoza 0000-9999 dostaje zapis rozszerzony ze znakiem.
    CHECK(value_of("new Date(Date.UTC(-1, 0, 1)).toISOString();").to_string()
          == "-000001-01-01T00:00:00.000Z");
    CHECK(value_of("new Date(Date.UTC(275760, 8, 13)).toISOString();").to_string()
          == "+275760-09-13T00:00:00.000Z");
}

TEST_CASE("toISOString i toUTCString") {
    CHECK(value_of("new Date(0).toISOString();").to_string() == "1970-01-01T00:00:00.000Z");
    CHECK(value_of("new Date(Date.UTC(2020, 5, 15, 12, 30, 45, 123)).toISOString();").to_string()
          == "2020-06-15T12:30:45.123Z");
    CHECK(value_of("new Date(Date.UTC(2020, 5, 15)).toUTCString();").to_string()
          == "Mon, 15 Jun 2020 00:00:00 GMT");

    // toJSON idzie przez toISOString.
    CHECK(value_of("new Date(Date.UTC(2020, 0, 1)).toJSON();").to_string()
          == "2020-01-01T00:00:00.000Z");
}

TEST_CASE("Date.parse rozumie zapis ze specyfikacji") {
    CHECK(number_of("Date.parse(\"1970-01-01T00:00:00.000Z\");") == 0);
    CHECK(number_of("Date.parse(\"2020-01-01T00:00:00Z\");") == 1577836800000.0);
    CHECK(number_of("Date.parse(\"2020-01-01T00:00:00.500Z\");") == 1577836800500.0);
    CHECK(number_of("Date.parse(\"2020-01-01\");") == 1577836800000.0);
    CHECK(number_of("Date.parse(\"2020\");") == 1577836800000.0);
    CHECK(number_of("Date.parse(\"+002020-01-01T00:00:00Z\");") == 1577836800000.0);

    // Przesunięcie strefy jest odejmowane.
    CHECK(number_of("Date.parse(\"2020-01-01T02:00:00+02:00\");") == 1577836800000.0);
    CHECK(number_of("Date.parse(\"2019-12-31T18:30:00-05:30\");") == 1577836800000.0);

    CHECK(std::isnan(number_of("Date.parse(\"2020-13-01\");")));
    CHECK(std::isnan(number_of("Date.parse(\"bzdura\");")));
    CHECK(std::isnan(number_of("Date.parse(\"2020-01-01T00\");")));
    CHECK(std::isnan(number_of("Date.parse(\"2020-01-01T00:00:00Zxx\");")));
}

TEST_CASE("formaty wypisywane dają się wczytać z powrotem") {
    // Bez tego new Date(String(d)) gubiłoby wartość.
    CHECK(value_of("var d = new Date(Date.UTC(2020, 0, 1));"
                   "new Date(d.toString()).getTime() === d.getTime();").to_boolean());
    CHECK(value_of("var d = new Date(Date.UTC(2020, 6, 1));"
                   "new Date(d.toString()).getTime() === d.getTime();").to_boolean());
    CHECK(value_of("var d = new Date(Date.UTC(2020, 0, 1));"
                   "new Date(d.toUTCString()).getTime() === d.getTime();").to_boolean());
    CHECK(value_of("var d = new Date(Date.UTC(2020, 0, 1));"
                   "new Date(d.toISOString()).getTime() === d.getTime();").to_boolean());
}

TEST_CASE("zapis pól") {
    CHECK(value_of("var d = new Date(0); d.setUTCFullYear(2000); d.toISOString();").to_string()
          == "2000-01-01T00:00:00.000Z");
    CHECK(value_of("var d = new Date(0); d.setUTCMonth(5); d.toISOString();").to_string()
          == "1970-06-01T00:00:00.000Z");
    CHECK(value_of("var d = new Date(0); d.setUTCDate(15); d.toISOString();").to_string()
          == "1970-01-15T00:00:00.000Z");
    CHECK(value_of("var d = new Date(0); d.setUTCHours(5, 6, 7, 8); d.toISOString();").to_string()
          == "1970-01-01T05:06:07.008Z");
    CHECK(number_of("var d = new Date(0); d.setTime(86400000); d.getTime();") == 86400000);

    // Setter zwraca nową wartość wewnętrzną.
    CHECK(number_of("new Date(0).setUTCMilliseconds(500);") == 500);
    CHECK(number_of("new Date(0).setUTCSeconds(30, 500);") == 30500);

    // Nadmiarowe argumenty są ignorowane, ale i tak konwertowane.
    CHECK(value_of("var d = new Date(0); d.setUTCHours(1, 2, 3, 4, 5); d.toISOString();").to_string()
          == "1970-01-01T01:02:03.004Z");
    CHECK(value_of("var log = []; var d = new Date(0);"
                   "d.setUTCHours({valueOf: function () { log.push(\"h\"); return 1; }},"
                   "              {valueOf: function () { log.push(\"m\"); return 2; }});"
                   "log.join();").to_string() == "h,m");
}

TEST_CASE("setFullYear jako jedyny ratuje datę niepoprawną") {
    // 15.9.5.40: dla setFullYear niepoprawna data liczy się jak zero.
    CHECK(value_of("var d = new Date(NaN); d.setUTCFullYear(2000); d.toISOString();").to_string()
          == "2000-01-01T00:00:00.000Z");

    // Pozostałe settery zostawiają ją niepoprawną.
    CHECK(std::isnan(number_of("var d = new Date(NaN); d.setUTCHours(5); d.getTime();")));
    CHECK(std::isnan(number_of("var d = new Date(NaN); d.setUTCMonth(5); d.getTime();")));

    // NaN w argumencie psuje datę poprawną.
    CHECK(std::isnan(number_of("var d = new Date(0); d.setUTCFullYear(NaN); d.getTime();")));
}

TEST_CASE("czas lokalny — niezmienniki niezależne od strefy") {
    // Pola lokalne złożone przez konstruktor muszą wrócić w tej samej postaci.
    CHECK(number_of("new Date(2020, 5, 15, 10, 20, 30, 40).getFullYear();") == 2020);
    CHECK(number_of("new Date(2020, 5, 15, 10, 20, 30, 40).getMonth();") == 5);
    CHECK(number_of("new Date(2020, 5, 15, 10, 20, 30, 40).getDate();") == 15);
    CHECK(number_of("new Date(2020, 5, 15, 10, 20, 30, 40).getHours();") == 10);
    CHECK(number_of("new Date(2020, 5, 15, 10, 20, 30, 40).getMinutes();") == 20);
    CHECK(number_of("new Date(2020, 5, 15, 10, 20, 30, 40).getSeconds();") == 30);
    CHECK(number_of("new Date(2020, 5, 15, 10, 20, 30, 40).getMilliseconds();") == 40);

    // Przesunięcie strefy to różnica między odczytem lokalnym a UTC.
    CHECK(value_of("var d = new Date(Date.UTC(2020, 0, 1));"
                   "(d.getTime() - Date.UTC(d.getFullYear(), d.getMonth(), d.getDate(),"
                   "  d.getHours(), d.getMinutes(), d.getSeconds(), d.getMilliseconds()))"
                   "  / 60000 === d.getTimezoneOffset();").to_boolean());

    CHECK(value_of("var d = new Date(2020, 0, 1); d.setHours(5); d.getHours() === 5;").to_boolean());
    CHECK(value_of("var d = new Date(2020, 0, 31); d.setMonth(1);"
                   "d.getMonth() + \"|\" + d.getDate();").to_string() == "2|2");
}

// Jedyny test, który MUSI znać strefę — niejednoznaczność przy zmianie czasu
// nie ma sensu tam, gdzie zmiany czasu nie ma. Ustawiamy ją więc sami, zamiast
// zgadywać, na czym test jest uruchamiany.
namespace {
    struct ScopedTimeZone {
        std::string previous;
        bool had_previous;

        explicit ScopedTimeZone(const char *zone) {
            const char *current = std::getenv("TZ");
            had_previous = current != nullptr;
            if (had_previous) previous = current;

            setenv("TZ", zone, 1);
            tzset();
        }

        ~ScopedTimeZone() {
            if (had_previous) setenv("TZ", previous.c_str(), 1);
            else unsetenv("TZ");

            tzset();
        }
    };
}

TEST_CASE("UTC() rozstrzyga niejednoznaczną godzinę na korzyść wcześniejszej") {
    const ScopedTimeZone zone("Europe/Warsaw");

    // 25 października 2020 o 2:30 w Warszawie wypada DWA razy: raz w czasie
    // letnim (00:30 UTC), raz w zimowym (01:30 UTC). Bierzemy wcześniejsze.
    CHECK(value_of("new Date(2020, 9, 25, 2, 30).toISOString();").to_string()
          == "2020-10-25T00:30:00.000Z");
    CHECK(number_of("new Date(2020, 9, 25, 2, 30).getHours();") == 2);

    // 29 marca o 2:30 nie istnieje wcale — zegar przeskakuje z 2:00 na 3:00.
    // Zostaje chwila wyliczona z przesunięcia SPRZED zmiany, czyli 3:30 lokalnie.
    CHECK(value_of("new Date(2020, 2, 29, 2, 30).toISOString();").to_string()
          == "2020-03-29T01:30:00.000Z");
    CHECK(number_of("new Date(2020, 2, 29, 2, 30).getHours();") == 3);
}

TEST_CASE("getYear i setYear z Annex B") {
    CHECK(number_of("new Date(2020, 0, 1).getYear();") == 120);
    CHECK(number_of("var d = new Date(2020, 0, 1); d.setYear(99); d.getFullYear();") == 1999);
    CHECK(number_of("var d = new Date(2020, 0, 1); d.setYear(2005); d.getFullYear();") == 2005);
    CHECK(std::isnan(number_of("new Date(NaN).getYear();")));
}

TEST_CASE("Date w konwersjach") {
    // 8.12.8: Date bez wskazówki zachowuje się jak z hint String, więc
    // dodawanie sklei łańcuchy, a odejmowanie policzy na liczbach.
    CHECK(value_of("typeof (new Date(0) + 1);").to_string() == "string");
    CHECK(value_of("typeof (new Date(0) - 1);").to_string() == "number");
    CHECK(value_of("(new Date(0) + \"\") === new Date(0).toString();").to_boolean());
    CHECK(number_of("+new Date(0);") == 0);
    CHECK(number_of("new Date(Date.UTC(2020, 0, 1)) - new Date(Date.UTC(2019, 0, 1));")
          == 31536000000.0);
    CHECK(value_of("new Date(Date.UTC(2020, 0, 1)) > new Date(Date.UTC(2019, 0, 1));").to_boolean());

    // Porównanie === działa na referencjach, nie na wartości czasu.
    CHECK_FALSE(value_of("new Date(0) === new Date(0);").to_boolean());
}

TEST_CASE("metody Date nie są generyczne") {
    CHECK(caught_instance_of("Date.prototype.getTime.call({});", "TypeError"));
    CHECK(caught_instance_of("Date.prototype.getTime.call(0);", "TypeError"));
    CHECK(caught_instance_of("Date.prototype.toISOString.call(\"x\");", "TypeError"));

    // 15.9.5.1: sam prototyp jest obiektem Date o wartości NaN.
    CHECK(std::isnan(number_of("Date.prototype.getTime();")));
    CHECK(value_of("Object.prototype.toString.call(Date.prototype);").to_string() == "[object Date]");
}

TEST_CASE("arności metod Date") {
    CHECK(number_of("Date.length;") == 7);
    CHECK(number_of("Date.UTC.length;") == 7);
    CHECK(number_of("Date.parse.length;") == 1);
    CHECK(number_of("Date.now.length;") == 0);
    CHECK(number_of("Date.prototype.setHours.length;") == 4);
    CHECK(number_of("Date.prototype.setMonth.length;") == 2);
    CHECK(number_of("Date.prototype.getTime.length;") == 0);
}

// ---------------------------------------------------------------------------
// 15.3.1-15.3.2 Konstruktor Function
// ---------------------------------------------------------------------------

TEST_CASE("Function buduje funkcję ze źródła") {
    CHECK(number_of("new Function(\"a\", \"b\", \"return a + b;\")(2, 3);") == 5);
    CHECK(number_of("new Function(\"return 42;\")();") == 42);

    // 15.3.1.1: wywołanie bez new działa tak samo.
    CHECK(number_of("Function(\"a\", \"b\", \"return a * b;\")(3, 4);") == 12);

    // Lista parametrów może być rozdzielona przecinkami w JEDNYM argumencie.
    CHECK(number_of("new Function(\"a, b\", \"return a * b;\")(3, 4);") == 12);

    // Bez argumentów powstaje pusta funkcja.
    CHECK(value_of("typeof new Function();").to_string() == "function");
    CHECK(value_of("typeof new Function()();").to_string() == "undefined");

    // Argumenty są konwertowane na łańcuch.
    CHECK(number_of("new Function({toString: function () { return \"a\"; }}, \"return a;\")(5);") == 5);
}

TEST_CASE("Function i reszta realmu") {
    CHECK(value_of("typeof Function;").to_string() == "function");
    CHECK(number_of("Function.length;") == 1);
    CHECK(value_of("(function () {}).constructor === Function;").to_boolean());
    CHECK(value_of("Function.prototype.constructor === Function;").to_boolean());
    CHECK(value_of("new Function(\"return 1;\") instanceof Function;").to_boolean());
    CHECK(value_of("Object.getPrototypeOf(new Function()) === Function.prototype;").to_boolean());
    CHECK(value_of("Object.prototype.toString.call(new Function());").to_string() == "[object Function]");

    // 15.3.4: sam Function.prototype jest funkcją zwracającą undefined.
    CHECK(value_of("typeof Function.prototype;").to_string() == "function");
    CHECK(value_of("typeof Function.prototype();").to_string() == "undefined");

    // Powstała funkcja ma komplet własności zwykłej funkcji.
    CHECK(number_of("new Function(\"a\", \"return a;\").length;") == 1);
    CHECK(number_of("new Function(\"a\", \"b\", \"return 1;\").length;") == 2);
    CHECK(number_of("new Function().length;") == 0);
    CHECK(value_of("new Function(\"x\", \"return x;\").hasOwnProperty(\"prototype\");").to_boolean());
    CHECK(value_of("var f = new Function(\"return 1;\"); f.prototype.constructor === f;").to_boolean());
}

TEST_CASE("zasięgiem jest środowisko globalne, nie miejsce wywołania") {
    // 15.3.2.1 krok 16 — to jedyna różnica względem eval, który widzi
    // zmienne lokalne miejsca wywołania.
    CHECK(value_of("(function () { var local = 9;"
                   "  return new Function(\"return typeof local;\")(); })();").to_string()
          == "undefined");

    // Zmienne globalne są widoczne.
    CHECK(number_of("var g = 1; new Function(\"return g;\")();") == 1);
    CHECK(value_of("var x = 5; new Function(\"return typeof x;\")();").to_string() == "number");

    // Domknięcie wewnątrz zbudowanej funkcji działa normalnie.
    CHECK(number_of("new Function(\"a\", \"return function () { return a; };\")(5)();") == 5);
}

TEST_CASE("błędne źródło daje SyntaxError, nie wyjątek C++") {
    CHECK(caught_instance_of("new Function(\"return\", \"1\");", "SyntaxError"));
    CHECK(caught_instance_of("new Function(\"a\", \"{\");", "SyntaxError"));
    CHECK(caught_instance_of("new Function(\"1a\", \"return 1;\");", "SyntaxError"));

    // Próba przemycenia drugiej funkcji przez domknięcie nawiasu: bez kontroli
    // kształtu drzewa zwrócona zostałaby ta ostatnia z listy przecinkowej.
    CHECK(caught_instance_of("new Function(\"a) {} , function(b\", \"\");", "SyntaxError"));

    // Ciało w trybie ścisłym podlega tym samym błędom wczesnym co zwykły kod.
    CHECK(caught_instance_of("new Function(\"a\", \"a\", \"'use strict'; return a;\");", "SyntaxError"));

    // Poza trybem ścisłym powtórzony parametr jest dozwolony — wygrywa ostatni.
    CHECK(number_of("new Function(\"a\", \"a\", \"return a;\")(1, 2);") == 2);
}

TEST_CASE("komentarz w parametrach nie zjada nawiasu") {
    // Źródło składamy z nowym wierszem po liście parametrów właśnie po to,
    // żeby "//" nie zakomentowało zamknięcia nagłówka.
    CHECK(value_of("typeof new Function(\"a//komentarz\", \"return 1;\");").to_string() == "function");
    CHECK(number_of("new Function(\"a//komentarz\", \"return 1;\")();") == 1);
    CHECK(number_of("new Function(\"a\", \"//komentarz\\nreturn a;\")(7);") == 7);
}

TEST_CASE("tryb ścisły ciała jest rozpoznawany z prologu") {
    CHECK(value_of("typeof new Function(\"'use strict'; return this;\")();").to_string()
          == "undefined");
    CHECK(value_of("new Function(\"'use strict'; return this === undefined;\")();").to_boolean());

    // Bez prologu this jest podmieniane na obiekt globalny.
    CHECK(value_of("typeof new Function(\"return this;\")();").to_string() == "object");
    CHECK_FALSE(value_of("new Function(\"return this === undefined;\")();").to_boolean());
}

TEST_CASE("zbudowana funkcja zachowuje się jak każda inna") {
    CHECK(number_of("new Function(\"return arguments.length;\")(1, 2, 3);") == 3);
    CHECK(value_of("new Function(\"return typeof arguments;\")();").to_string() == "object");
    CHECK(number_of("new Function(\"x\", \"return x * 2;\").call(null, 21);") == 42);
    CHECK(number_of("new Function(\"x\", \"return x * 2;\").apply(null, [21]);") == 42);
    CHECK(number_of("new Function(\"x\", \"return x * 2;\").bind(null, 21)();") == 42);
    CHECK(number_of("new Function(\"return [1, 2, 3].length;\")();") == 3);
}

// ---------------------------------------------------------------------------
// 15.12 JSON
// ---------------------------------------------------------------------------

TEST_CASE("JSON.stringify: wartości proste") {
    CHECK(value_of("JSON.stringify({a: 1, b: \"x\"});").to_string() == "{\"a\":1,\"b\":\"x\"}");
    CHECK(value_of("JSON.stringify([1, \"x\", null]);").to_string() == "[1,\"x\",null]");
    CHECK(value_of("JSON.stringify({a: [1, {b: 2}]});").to_string() == "{\"a\":[1,{\"b\":2}]}");
    CHECK(value_of("JSON.stringify(\"hi\");").to_string() == "\"hi\"");
    CHECK(value_of("JSON.stringify(42);").to_string() == "42");
    CHECK(value_of("JSON.stringify(true);").to_string() == "true");
    CHECK(value_of("JSON.stringify(null);").to_string() == "null");

    // NaN i nieskończoności nie mają zapisu w JSON.
    CHECK(value_of("JSON.stringify({a: NaN, b: Infinity});").to_string() == "{\"a\":null,\"b\":null}");
}

TEST_CASE("wartości bez reprezentacji znikają inaczej w obiekcie niż w tablicy") {
    // W obiekcie klucz po prostu znika.
    CHECK(value_of("JSON.stringify({a: undefined, b: 1});").to_string() == "{\"b\":1}");
    CHECK(value_of("JSON.stringify({f: function () {}});").to_string() == "{}");

    // W tablicy pozycja musi zostać, więc wchodzi null.
    CHECK(value_of("JSON.stringify([undefined, function () {}, 1]);").to_string()
          == "[null,null,1]");

    // Sama wartość bez reprezentacji daje undefined, a nie pusty łańcuch.
    CHECK(value_of("typeof JSON.stringify(undefined);").to_string() == "undefined");
    CHECK(value_of("typeof JSON.stringify(function () {});").to_string() == "undefined");
}

TEST_CASE("opakowania są rozpakowywane do prymitywów") {
    CHECK(value_of("JSON.stringify({a: new Number(5), b: new String(\"s\"),"
                   "                c: new Boolean(true)});").to_string()
          == "{\"a\":5,\"b\":\"s\",\"c\":true}");
}

TEST_CASE("trzeci argument steruje wcięciami") {
    CHECK(value_of("JSON.stringify({a: 1}, null, 2);").to_string() == "{\n  \"a\": 1\n}");
    CHECK(value_of("JSON.stringify({a: {b: 1}}, null, 2);").to_string()
          == "{\n  \"a\": {\n    \"b\": 1\n  }\n}");
    CHECK(value_of("JSON.stringify([1, 2], null, 2);").to_string() == "[\n  1,\n  2\n]");
    CHECK(value_of("JSON.stringify({a: 1}, null, \"\\t\");").to_string() == "{\n\t\"a\": 1\n}");

    // Pusta struktura nie dostaje wcięcia.
    CHECK(value_of("JSON.stringify({}, null, 2);").to_string() == "{}");

    // Liczba i łańcuch są obcinane do dziesięciu znaków.
    CHECK(value_of("JSON.stringify({a: 1}, null, 20);").to_string()
          == "{\n          \"a\": 1\n}");
    CHECK(value_of("JSON.stringify({a: 1}, null, \"----------------\");").to_string()
          == "{\n----------\"a\": 1\n}");

    // Liczba mniejsza od jedynki znaczy brak wcięć.
    CHECK(value_of("JSON.stringify({a: 1}, null, -1);").to_string() == "{\"a\":1}");
}

TEST_CASE("drugi argument: funkcja albo biała lista kluczy") {
    CHECK(value_of("JSON.stringify({a: 1, b: 2}, [\"a\"]);").to_string() == "{\"a\":1}");

    // Kolejność bierze się z listy, a powtórzenia są pomijane.
    CHECK(value_of("JSON.stringify({a: 1, b: 2}, [\"b\", \"a\", \"b\"]);").to_string()
          == "{\"b\":2,\"a\":1}");
    CHECK(value_of("JSON.stringify({a: 1}, []);").to_string() == "{}");

    // Lista NIE dotyczy tablic.
    CHECK(value_of("JSON.stringify([1, 2], [\"0\"]);").to_string() == "[1,2]");

    CHECK(value_of("JSON.stringify({a: 1, b: 2},"
                   "  function (k, v) { return typeof v === \"number\" ? v * 2 : v; });").to_string()
          == "{\"a\":2,\"b\":4}");
    CHECK(value_of("JSON.stringify({a: 1},"
                   "  function (k, v) { return k === \"a\" ? undefined : v; });").to_string()
          == "{}");
}

TEST_CASE("toJSON ma pierwszeństwo przed wszystkim innym") {
    CHECK(value_of("JSON.stringify({d: {toJSON: function () { return \"X\"; }}});").to_string()
          == "{\"d\":\"X\"}");

    // Data korzysta z tego samego mechanizmu.
    CHECK(value_of("JSON.stringify(new Date(Date.UTC(2020, 0, 1)));").to_string()
          == "\"2020-01-01T00:00:00.000Z\"");
}

TEST_CASE("cytowanie łańcuchów") {
    CHECK(value_of("JSON.stringify({s: \"a\\\"b\\\\c\\nd\"});").to_string()
          == "{\"s\":\"a\\\"b\\\\c\\nd\"}");

    // Znaki sterujące poniżej 0x20 idą jako \\uXXXX.
    CHECK(value_of("JSON.stringify(\"\\u0001\\u001f\");").to_string() == "\"\\u0001\\u001f\"");

    // Znaki spoza ASCII zostają dosłownie.
    CHECK(value_of("JSON.stringify(\"\\u00e9\");").to_string() == "\"\u00e9\"");
}

TEST_CASE("cykl to TypeError, nie zapętlenie") {
    CHECK(caught_instance_of("var o = {}; o.self = o; JSON.stringify(o);", "TypeError"));
    CHECK(caught_instance_of("var a = []; a[0] = a; JSON.stringify(a);", "TypeError"));

    // Ten sam obiekt użyty dwa razy obok siebie to NIE cykl.
    CHECK(value_of("var o = {v: 1}; JSON.stringify([o, o]);").to_string()
          == "[{\"v\":1},{\"v\":1}]");
}

TEST_CASE("stringify pomija właściwości odziedziczone i niewyliczalne") {
    CHECK(value_of("JSON.stringify(Object.create({inherited: 1}));").to_string() == "{}");
    CHECK(value_of("var o = {}; Object.defineProperty(o, \"h\", {value: 1, enumerable: false});"
                   "JSON.stringify(o);").to_string() == "{}");
}

TEST_CASE("JSON.parse czyta gramatykę JSON") {
    CHECK(number_of("JSON.parse(\"1\");") == 1);
    CHECK(value_of("JSON.parse(\"true\");").to_boolean());
    CHECK(value_of("JSON.parse(\"null\") === null;").to_boolean());
    CHECK(value_of("JSON.parse(\"\\\"x\\\"\");").to_string() == "x");
    CHECK(number_of("JSON.parse(\"[1, 2, 3]\").length;") == 3);
    CHECK(number_of("JSON.parse(\"{\\\"a\\\": 1}\").a;") == 1);
    CHECK(number_of("JSON.parse(\"{\\\"a\\\": {\\\"b\\\": [1, 2]}}\").a.b[1];") == 2);
    CHECK(number_of("JSON.parse(\"-1.5e3\");") == -1500);

    // Białe znaki wolno stawiać wszędzie między elementami.
    CHECK(number_of("JSON.parse(\" \\t\\n [1] \")[0];") == 1);

    CHECK(value_of("Array.isArray(JSON.parse(\"[]\"));").to_boolean());
    CHECK(value_of("typeof JSON.parse(\"{}\");").to_string() == "object");

    // Ucieczki dopuszczone przez gramatykę JSON.
    CHECK(value_of("JSON.parse(\"\\\"\\\\u0041\\\"\");").to_string() == "A");
    CHECK(number_of("JSON.parse(\"\\\"\\\\u0041\\\\n\\\\t\\\\/\\\"\").length;") == 4);
}

TEST_CASE("gramatyka JSON jest węższa niż literały JavaScriptu") {
    // Wiodące zero, wiodący plus i zapis szesnastkowy są zabronione.
    CHECK(caught_instance_of("JSON.parse(\"01\");", "SyntaxError"));
    CHECK(caught_instance_of("JSON.parse(\"+1\");", "SyntaxError"));
    CHECK(caught_instance_of("JSON.parse(\"0x10\");", "SyntaxError"));

    // Apostrof nie jest cudzysłowem, a klucz musi być w cudzysłowie.
    CHECK(caught_instance_of("JSON.parse(\"'x'\");", "SyntaxError"));
    CHECK(caught_instance_of("JSON.parse(\"{a: 1}\");", "SyntaxError"));

    // Przecinek na końcu listy jest błędem, choć w JavaScripcie bywa dozwolony.
    CHECK(caught_instance_of("JSON.parse(\"[1,]\");", "SyntaxError"));
    CHECK(caught_instance_of("JSON.parse(\"{\\\"a\\\": 1,}\");", "SyntaxError"));

    // Ucieczka \\x działa w JS, ale nie w JSON.
    CHECK(caught_instance_of("JSON.parse(\"\\\"\\\\x41\\\"\");", "SyntaxError"));

    CHECK(caught_instance_of("JSON.parse(\"\");", "SyntaxError"));
    CHECK(caught_instance_of("JSON.parse(\"[1] x\");", "SyntaxError"));
}

TEST_CASE("reviver przechodzi wynik od liści w górę") {
    CHECK(number_of("JSON.parse(\"{\\\"a\\\": 1, \\\"b\\\": 2}\","
                    "  function (k, v) { return typeof v === \"number\" ? v * 10 : v; }).b;") == 20);
    CHECK(value_of("JSON.parse(\"[1, 2]\","
                   "  function (k, v) { return typeof v === \"number\" ? v + 1 : v; }).join();")
          .to_string() == "2,3");

    // Zwrócone undefined KASUJE klucz.
    CHECK_FALSE(value_of("JSON.parse(\"{\\\"a\\\": 1, \\\"b\\\": 2}\","
                         "  function (k, v) { return k === \"a\" ? undefined : v; })"
                         "  .hasOwnProperty(\"a\");").to_boolean());

    // Korzeń też przechodzi przez reviver, pod pustym kluczem.
    CHECK(number_of("JSON.parse(\"1\", function (k, v) { return v * 5; });") == 5);
    CHECK(value_of("JSON.parse(\"1\", function (k, v) { return k; });").to_string() == "");
}

TEST_CASE("JSON jako obiekt") {
    CHECK(value_of("typeof JSON;").to_string() == "object");
    CHECK(value_of("Object.prototype.toString.call(JSON);").to_string() == "[object JSON]");
    CHECK(caught_instance_of("JSON();", "TypeError"));
    CHECK(number_of("JSON.parse.length;") == 2);
    CHECK(number_of("JSON.stringify.length;") == 3);

    // Obieg w obie strony.
    CHECK(value_of("JSON.stringify(JSON.parse(\"{\\\"a\\\": [1, 2], \\\"b\\\": \\\"x\\\"}\"));")
          .to_string() == "{\"a\":[1,2],\"b\":\"x\"}");
}

// ---------------------------------------------------------------------------
// 15.10 RegExp
// ---------------------------------------------------------------------------

TEST_CASE("RegExp: dopasowanie podstawowe") {
    CHECK(value_of("/abc/.test(\"xxabcxx\");").to_boolean());
    CHECK_FALSE(value_of("/abc/.test(\"xxabxx\");").to_boolean());
    CHECK(value_of("/a+/.exec(\"caaat\")[0];").to_string() == "aaa");
    CHECK(number_of("/a+/.exec(\"caaat\").index;") == 1);
    CHECK(value_of("/a+/.exec(\"caaat\").input;").to_string() == "caaat");
    CHECK(value_of("/x/.exec(\"abc\") === null;").to_boolean());

    CHECK(value_of("/^abc$/.test(\"abc\");").to_boolean());
    CHECK_FALSE(value_of("/^abc$/.test(\"xabc\");").to_boolean());

    // Kropka nie łapie końca wiersza.
    CHECK(value_of("/a.c/.test(\"abc\");").to_boolean());
    CHECK_FALSE(value_of("/a.c/.test(\"a\\nc\");").to_boolean());
}

TEST_CASE("klasy znaków i skróty") {
    CHECK(value_of("/\\d+/.exec(\"ab123cd\")[0];").to_string() == "123");
    CHECK(value_of("/\\w+/.exec(\" foo_1 \")[0];").to_string() == "foo_1");
    CHECK(number_of("/\\s+/.exec(\"a  b\")[0].length;") == 2);
    CHECK(value_of("/\\D/.test(\"x\");").to_boolean());
    CHECK_FALSE(value_of("/\\D/.test(\"5\");").to_boolean());

    CHECK(value_of("/[abc]+/.exec(\"xxabcaxx\")[0];").to_string() == "abca");
    CHECK(value_of("/[^abc]+/.exec(\"abcxyzabc\")[0];").to_string() == "xyz");
    CHECK(value_of("/[a-z]+/.exec(\"ABCdefGHI\")[0];").to_string() == "def");

    // Skrót wewnątrz klasy wnosi swój zbiór; \\D musi zostać rozwinięty
    // na dopełnienie, bo znacznik negacji dotyczy całej klasy.
    CHECK(value_of("/[\\d]+/.exec(\"ab12\")[0];").to_string() == "12");
    CHECK(value_of("/[\\D]+/.exec(\"12ab34\")[0];").to_string() == "ab");

    // Wewnątrz klasy \\b to backspace, a nie granica słowa.
    CHECK(value_of("/[\\b]/.test(\"\\b\");").to_boolean());
}

TEST_CASE("krotności i zachłanność") {
    CHECK(value_of("/a{2}/.test(\"aa\");").to_boolean());
    CHECK_FALSE(value_of("/a{2}/.test(\"a\");").to_boolean());
    CHECK(value_of("/a{2,}/.exec(\"aaaa\")[0];").to_string() == "aaaa");
    CHECK(value_of("/a{2,3}/.exec(\"aaaa\")[0];").to_string() == "aaa");

    // Znak zapytania po krotności wyłącza zachłanność.
    CHECK(value_of("/a{2,3}?/.exec(\"aaaa\")[0];").to_string() == "aa");
    CHECK(value_of("/a+?/.exec(\"aaa\")[0];").to_string() == "a");
    CHECK(number_of("/a*/.exec(\"bbb\")[0].length;") == 0);

    // Niepoprawna klamra jest zwykłym tekstem, a nie błędem.
    CHECK(value_of("new RegExp(\"a{,2}\").test(\"a{,2}\");").to_boolean());
    CHECK(caught_instance_of("new RegExp(\"a{2,1}\");", "SyntaxError"));
}

TEST_CASE("grupy, odwołania wstecz i wgląd w przód") {
    CHECK(value_of("/(a)(b)/.exec(\"ab\").join(\",\");").to_string() == "ab,a,b");
    CHECK(number_of("/(a)(b)(c)/.exec(\"abc\").length;") == 4);

    // Grupa, która nie wzięła udziału, daje undefined — nie pusty łańcuch.
    CHECK(value_of("typeof /(a)(b)?/.exec(\"a\")[2];").to_string() == "undefined");
    CHECK(value_of("typeof /(a)|(b)/.exec(\"b\")[1];").to_string() == "undefined");

    CHECK(value_of("/(\\w)\\1/.test(\"aa\");").to_boolean());
    CHECK_FALSE(value_of("/(\\w)\\1/.test(\"ab\");").to_boolean());

    CHECK(value_of("/a(?=b)/.exec(\"ab\")[0];").to_string() == "a");
    CHECK_FALSE(value_of("/a(?=b)/.test(\"ac\");").to_boolean());
    CHECK(value_of("/a(?!b)/.test(\"ac\");").to_boolean());
    CHECK_FALSE(value_of("/a(?!b)/.test(\"ab\");").to_boolean());

    // Grupa nieprzechwytująca nie powiększa wyniku.
    CHECK(number_of("/(?:ab)+/.exec(\"abab\").length;") == 1);

    // Wgląd w przód nie przesuwa pozycji, ale ZOSTAWIA przechwycenia.
    CHECK(value_of("/(?=(a+))/.exec(\"baaabac\").join(\",\");").to_string() == ",aaa");
    CHECK(value_of("/(?=(a+))a*b\\1/.exec(\"baaabac\").join(\",\");").to_string() == "aba,a");
}

TEST_CASE("nawroty wymagają czyszczenia przechwyceń") {
    // 15.10.2.5 krok 4: każdy obrót powtórzenia zeruje grupy w środku.
    CHECK(value_of("/(z)((a+)?(b+)?(c))*/.exec(\"zaacbbbcac\").join(\",\");").to_string()
          == "zaacbbbcac,z,ac,a,,c");
    CHECK(value_of("/(a*)b\\1+/.exec(\"baaaac\").join(\",\");").to_string() == "b,");
    CHECK(value_of("/(a)?a/.exec(\"a\").join(\",\");").to_string() == "a,");

    // Alternatywa wybiera PIERWSZĄ pasującą gałąź, nie najdłuższą.
    CHECK(value_of("/a|ab/.exec(\"ab\")[0];").to_string() == "a");
    CHECK(value_of("/((a)|(ab))((c)|(bc))/.exec(\"abc\").join(\",\");").to_string()
          == "abc,a,a,,bc,,bc");

    // Klasyczny przypadek wymagający wielokrotnego nawrotu.
    CHECK(value_of("/^(a+)\\1*,\\1+$/.exec(\"aaaaaaaaaa,aaaaaaaaaaaaaaa\")[1];").to_string()
          == "aaaaa");
}

TEST_CASE("flagi") {
    CHECK(value_of("/abc/i.test(\"ABC\");").to_boolean());
    CHECK(value_of("/[a-z]+/i.exec(\"ABC\")[0];").to_string() == "ABC");

    CHECK(value_of("/^b/m.test(\"a\\nb\");").to_boolean());
    CHECK_FALSE(value_of("/^b/.test(\"a\\nb\");").to_boolean());
    CHECK(value_of("/a$/m.test(\"a\\nb\");").to_boolean());

    CHECK(value_of("/a/.global + \"|\" + /a/g.global;").to_string() == "false|true");
    CHECK(value_of("/a/gi.ignoreCase + \"|\" + /a/m.multiline;").to_string() == "true|true");

    CHECK(caught_instance_of("new RegExp(\"a\", \"x\");", "SyntaxError"));
    CHECK(caught_instance_of("new RegExp(\"a\", \"gg\");", "SyntaxError"));
}

TEST_CASE("lastIndex działa tylko przy fladze global") {
    CHECK(number_of("/x/g.lastIndex;") == 0);
    CHECK(number_of("var r = /a/g; r.test(\"aa\"); r.lastIndex;") == 1);
    CHECK(number_of("var r = /a/g; r.exec(\"aa\"); r.exec(\"aa\").index;") == 1);

    // Po wyczerpaniu dopasowań lastIndex wraca na zero, a exec oddaje null.
    CHECK(value_of("var r = /a/g; r.exec(\"aa\"); r.exec(\"aa\");"
                   "r.exec(\"aa\") === null;").to_boolean());
    CHECK(number_of("var r = /a/g; r.exec(\"aa\"); r.exec(\"aa\"); r.exec(\"aa\");"
                    "r.lastIndex;") == 0);

    // Bez flagi global lastIndex jest ignorowane i nietknięte.
    CHECK(number_of("var r = /a/; r.exec(\"aa\"); r.lastIndex;") == 0);
}

TEST_CASE("konstruktor RegExp") {
    CHECK(value_of("typeof RegExp;").to_string() == "function");
    CHECK(value_of("new RegExp(\"a+\").test(\"aaa\");").to_boolean());
    CHECK(value_of("new RegExp(\"a\", \"g\").global;").to_boolean());
    CHECK(value_of("Object.prototype.toString.call(/a/);").to_string() == "[object RegExp]");
    CHECK(value_of("/a/ instanceof RegExp;").to_boolean());

    // 15.10.3.1: wywołanie bez new oddaje TEN SAM obiekt, new robi kopię.
    CHECK(value_of("var r = /a/g; RegExp(r) === r;").to_boolean());
    CHECK_FALSE(value_of("var r = /a/g; new RegExp(r) === r;").to_boolean());
    CHECK(value_of("new RegExp(/a/g).global;").to_boolean());
    CHECK(caught_instance_of("new RegExp(/a/, \"i\");", "TypeError"));

    CHECK(value_of("new RegExp(\"\").source;").to_string() == "(?:)");
    CHECK(value_of("String(/ab+c/gi);").to_string() == "/ab+c/gi");
    CHECK(caught_instance_of("new RegExp(\"(\");", "SyntaxError"));
}

TEST_CASE("String.prototype.match") {
    CHECK(value_of("\"abc\".match(/b/)[0];").to_string() == "b");
    CHECK(number_of("\"abc\".match(/b/).index;") == 1);
    CHECK(value_of("\"abc\".match(/(a)(b)/).join(\",\");").to_string() == "ab,a,b");
    CHECK(value_of("\"abc\".match(/x/) === null;").to_boolean());

    // Z flagą global dostajemy same dopasowania, BEZ grup i bez index.
    CHECK(value_of("\"a1b2c3\".match(/\\d/g).join(\",\");").to_string() == "1,2,3");
    CHECK(value_of("\"abc\".match(/x/g) === null;").to_boolean());

    // Argument nie będący wzorcem jest na wzorzec zamieniany.
    CHECK(value_of("\"abc\".match(\"b\")[0];").to_string() == "b");
}

TEST_CASE("String.prototype.search nie rusza lastIndex") {
    CHECK(number_of("\"abc\".search(/b/);") == 1);
    CHECK(number_of("\"abc\".search(/x/);") == -1);
    CHECK(number_of("\"abc\".search(\"c\");") == 2);
    CHECK(value_of("var r = /b/g; r.lastIndex = 5; \"abc\".search(r) + \"|\" + r.lastIndex;")
              .to_string() == "1|5");
}

TEST_CASE("String.prototype.replace ze wzorcem") {
    CHECK(value_of("\"a1b2\".replace(/\\d/, \"#\");").to_string() == "a#b2");
    CHECK(value_of("\"a1b2\".replace(/\\d/g, \"#\");").to_string() == "a#b#");
    CHECK(value_of("\"abc\".replace(/(a)(b)/, \"$2$1\");").to_string() == "bac");
    CHECK(value_of("\"foo bar\".replace(/(\\w+) (\\w+)/, \"$2 $1\");").to_string() == "bar foo");
    CHECK(value_of("\"2020\".replace(/(\\d{2})(\\d{2})/, \"$1-$2\");").to_string() == "20-20");

    // Numer, który nie wskazuje grupy, zostaje zwykłym tekstem.
    CHECK(value_of("\"abc\".replace(/b/, \"$1\");").to_string() == "a$1c");
    CHECK(value_of("\"abc\".replace(/(a)/, \"$01\");").to_string() == "abc");

    // Funkcja dostaje dopasowanie, potem grupy, pozycję i cały łańcuch.
    CHECK(value_of("\"a1b\".replace(/(\\d)/, function (m, p1, i, s) {"
                   "  return \"[\" + m + p1 + i + s + \"]\"; });").to_string() == "a[111a1b]b");
    CHECK(value_of("\"abcabc\".replace(/abc/g, function () { return \"X\"; });").to_string() == "XX");

    // Dopasowanie puste musi przesuwać pozycję, inaczej pętla staje.
    CHECK(value_of("\"abc\".replace(/(?:)/g, \"-\");").to_string() == "-a-b-c-");
    CHECK(value_of("\"\".replace(/x*/g, \"-\");").to_string() == "-");
}

TEST_CASE("String.prototype.split ze wzorcem") {
    CHECK(value_of("\"a1b2c\".split(/\\d/).join(\"|\");").to_string() == "a|b|c");
    CHECK(value_of("\"2020-01-02\".split(/-/).join(\"/\");").to_string() == "2020/01/02");
    CHECK(value_of("\"a b  c\".split(/\\s+/).join(\",\");").to_string() == "a,b,c");
    CHECK(number_of("\"abc\".split(/x/).length;") == 1);
    CHECK(value_of("\"a1b2c3\".split(/\\d/, 2).join(\"|\");").to_string() == "a|b");

    // Grupy separatora TRAFIAJĄ do wyniku.
    CHECK(value_of("\"a1b2c\".split(/(\\d)/).join(\"|\");").to_string() == "a|1|b|2|c");
    CHECK(number_of("\"ab\".split(/(a)(x)?/).length;") == 4);
    CHECK(value_of("typeof \"ab\".split(/(a)(x)?/)[2];").to_string() == "undefined");
}

// ---------------------------------------------------------------------------
// Domknięte długi
// ---------------------------------------------------------------------------

TEST_CASE("niepoprawna długość tablicy daje RangeError") {
    // 15.4.5.1 krok 3.c: to RangeError, a nie ciche odrzucenie ani TypeError,
    // i leci niezależnie od trybu ścisłego.
    CHECK(caught_instance_of("[].length = -1;", "RangeError"));
    CHECK(caught_instance_of("[].length = 1.5;", "RangeError"));
    CHECK(caught_instance_of("[].length = \"abc\";", "RangeError"));
    CHECK(caught_instance_of("Object.defineProperty([], \"length\", {value: -1});", "RangeError"));

    // Poprawne długości nadal działają, także skracające.
    CHECK(value_of("var a = [1, 2, 3]; a.length = 2; a.join();").to_string() == "1,2");
    CHECK(number_of("var a = []; a.length = 5; a.length;") == 5);
}

TEST_CASE("odwzorowanie wielkości liter obejmuje Unicode") {
    CHECK(value_of("\"\\u017C\\u00F3\\u0142w\".toUpperCase();").to_string() == "\u017B\u00D3\u0141W");
    CHECK(value_of("\"\\u017B\\u00D3\\u0141W\".toLowerCase();").to_string() == "\u017C\u00F3\u0142w");
    CHECK(value_of("\"\\u03A9\".toLowerCase();").to_string() == "\u03C9");
    CHECK(value_of("\"\\u03C9\".toUpperCase();").to_string() == "\u03A9");

    // Odwzorowanie pełne potrafi zmienić DŁUGOŚĆ łańcucha.
    CHECK(value_of("\"Stra\\u00DFe\".toUpperCase();").to_string() == "STRASSE");
    CHECK(number_of("\"\\u00DF\".toUpperCase().length;") == 2);

    // Flaga ignorowania wielkości liter korzysta z tego samego odwzorowania.
    CHECK(value_of("/\\u0105/i.test(\"\\u0104\");").to_boolean());
    CHECK(value_of("/[a-\\u017C]/i.test(\"\\u00D3\");").to_boolean());
}

TEST_CASE("lekser przyjmuje tylko flagi z ES5.1") {
    CHECK(value_of("typeof /a/gim;").to_string() == "object");
    CHECK(throws("eval(\"/x/y\");"));
    CHECK(throws("eval(\"/x/u\");"));
    CHECK(throws("eval(\"/x/gg\");"));
}

TEST_CASE("głęboka struktura w JSON daje RangeError, a nie przepełnienie stosu") {
    CHECK(caught_instance_of(
        "var deep = {}; var at = deep;"
        "for (var i = 0; i < 100000; i++) { at.next = {}; at = at.next; }"
        "JSON.stringify(deep);", "RangeError"));

    // Po złapaniu błędu ewaluator nadal działa.
    CHECK(value_of("JSON.stringify({a: 1});").to_string() == "{\"a\":1}");
}
