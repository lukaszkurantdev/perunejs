#include <doctest/doctest.h>
#include "memory/js_value.h"
#include "memory/js_object.h"

#include <cmath>
#include <limits>
#include <string>

static const double NaN = std::numeric_limits<double>::quiet_NaN();
static const double Inf = std::numeric_limits<double>::infinity();

using namespace perunejs;

// ---------- fabryki i typ ----------

TEST_CASE("fabryki ustawiają właściwy typ") {
    CHECK(JSValue::undefined().type()      == JSValueType::Undefined);
    CHECK(JSValue::null().type()           == JSValueType::Null);
    CHECK(JSValue::boolean(true).type()    == JSValueType::Boolean);
    CHECK(JSValue::number(1.0).type()      == JSValueType::Number);
    CHECK(JSValue::string("x").type()      == JSValueType::String);
}

// ---------- ToBoolean (7.1.2) ----------

TEST_CASE("ToBoolean") {
    CHECK(JSValue::undefined().to_boolean()      == false);
    CHECK(JSValue::null().to_boolean()           == false);
    CHECK(JSValue::boolean(true).to_boolean()    == true);
    CHECK(JSValue::boolean(false).to_boolean()   == false);

    CHECK(JSValue::number(0.0).to_boolean()      == false);
    CHECK(JSValue::number(-0.0).to_boolean()     == false);
    CHECK(JSValue::number(NaN).to_boolean()      == false);   // ← brakuje w kodzie
    CHECK(JSValue::number(1.0).to_boolean()      == true);
    CHECK(JSValue::number(-1.0).to_boolean()     == true);
    CHECK(JSValue::number(Inf).to_boolean()      == true);

    CHECK(JSValue::string("").to_boolean()       == false);
    CHECK(JSValue::string("0").to_boolean()      == true);    // napis "0" jest PRAWDZIWY
    CHECK(JSValue::string(" ").to_boolean()      == true);
    CHECK(JSValue::string("false").to_boolean()  == true);
}

// ---------- ToNumber (7.1.4) ----------

TEST_CASE("ToNumber — typy proste") {
    CHECK(std::isnan(JSValue::undefined().to_number()));       // ← zwraca 0
    CHECK(JSValue::null().to_number()          == 0.0);
    CHECK(JSValue::boolean(true).to_number()   == 1.0);
    CHECK(JSValue::boolean(false).to_number()  == 0.0);
    CHECK(JSValue::number(3.5).to_number()     == 3.5);
    CHECK(std::isnan(JSValue::number(NaN).to_number()));
}

TEST_CASE("ToNumber — napisy (StringToNumber)") {
    CHECK(JSValue::string("").to_number()        == 0.0);
    CHECK(JSValue::string("   ").to_number()     == 0.0);
    CHECK(JSValue::string("0").to_number()       == 0.0);
    CHECK(JSValue::string("12").to_number()      == 12.0);
    CHECK(JSValue::string(" 12 ").to_number()    == 12.0);
    CHECK(JSValue::string("+5").to_number()      == 5.0);
    CHECK(JSValue::string("-5").to_number()      == -5.0);
    CHECK(JSValue::string("3.5").to_number()     == 3.5);
    CHECK(JSValue::string(".5").to_number()      == 0.5);
    CHECK(JSValue::string("5.").to_number()      == 5.0);
    CHECK(JSValue::string("1e3").to_number()     == 1000.0);
    CHECK(JSValue::string("0x10").to_number()    == 16.0);
    CHECK(JSValue::string("Infinity").to_number() == Inf);
    CHECK(std::isnan(JSValue::string("12abc").to_number()));
    CHECK(std::isnan(JSValue::string("1_0").to_number()));     // separatory niedozwolone
    CHECK(std::isnan(JSValue::string("nan").to_number()));     // małymi literami — NaN
}

// ---------- ToString (7.1.17) ----------

TEST_CASE("ToString — typy proste") {
    CHECK(JSValue::undefined().to_string()     == "undefined");
    CHECK(JSValue::null().to_string()          == "null");
    CHECK(JSValue::boolean(true).to_string()   == "true");
    CHECK(JSValue::boolean(false).to_string()  == "false");
    CHECK(JSValue::string("abc").to_string()   == "abc");      // ← zwraca 0, crash
    CHECK(JSValue::string("").to_string()      == "");
}

TEST_CASE("ToString — liczby (Number::toString)") {
    CHECK(JSValue::number(0.0).to_string()     == "0");
    CHECK(JSValue::number(-0.0).to_string()    == "0");        // minus znika!
    CHECK(JSValue::number(1.0).to_string()     == "1");
    CHECK(JSValue::number(-1.0).to_string()    == "-1");
    CHECK(JSValue::number(3.5).to_string()     == "3.5");
    CHECK(JSValue::number(0.1).to_string()     == "0.1");      // najkrótszy zapis
    CHECK(JSValue::number(NaN).to_string()     == "NaN");
    CHECK(JSValue::number(Inf).to_string()     == "Infinity");
    CHECK(JSValue::number(-Inf).to_string()    == "-Infinity");
    CHECK(JSValue::number(1e20).to_string()    == "100000000000000000000");
    CHECK(JSValue::number(1e21).to_string()    == "1e+21");    // granica notacji
    CHECK(JSValue::number(1e-7).to_string()    == "1e-7");
    CHECK(JSValue::number(0.000001).to_string() == "0.000001");
}

// ---------- IsStrictlyEqual (7.2.16) ----------

TEST_CASE("IsStrictlyEqual") {
    CHECK(JSValue::is_strictly_equal(JSValue::undefined(), JSValue::undefined()));
    CHECK(JSValue::is_strictly_equal(JSValue::null(), JSValue::null()));
    CHECK(JSValue::is_strictly_equal(JSValue::number(1), JSValue::number(1)));
    CHECK(JSValue::is_strictly_equal(JSValue::string("a"), JSValue::string("a")));
    CHECK(JSValue::is_strictly_equal(JSValue::boolean(true), JSValue::boolean(true)));

    CHECK_FALSE(JSValue::is_strictly_equal(JSValue::null(), JSValue::undefined()));
    CHECK_FALSE(JSValue::is_strictly_equal(JSValue::number(1), JSValue::string("1")));
    CHECK_FALSE(JSValue::is_strictly_equal(JSValue::number(1), JSValue::number(2)));

    // przypadki brzegowe IEEE 754
    CHECK_FALSE(JSValue::is_strictly_equal(JSValue::number(NaN), JSValue::number(NaN)));
    CHECK(JSValue::is_strictly_equal(JSValue::number(0.0), JSValue::number(-0.0)));
}

// ---------- IsLooselyEqual (7.2.15) ----------

TEST_CASE("IsLooselyEqual — te same typy") {
    CHECK(JSValue::is_loosely_equal(JSValue::number(1), JSValue::number(1)));
    CHECK(JSValue::is_loosely_equal(JSValue::string("a"), JSValue::string("a")));
    CHECK(JSValue::is_loosely_equal(JSValue::boolean(true), JSValue::boolean(true)));
    CHECK_FALSE(JSValue::is_loosely_equal(JSValue::number(NaN), JSValue::number(NaN)));
}

TEST_CASE("IsLooselyEqual — null i undefined") {
    CHECK(JSValue::is_loosely_equal(JSValue::null(), JSValue::undefined()));
    CHECK(JSValue::is_loosely_equal(JSValue::undefined(), JSValue::null()));

    // null i undefined nie są równe NICZEMU innemu
    CHECK_FALSE(JSValue::is_loosely_equal(JSValue::null(), JSValue::number(0)));
    CHECK_FALSE(JSValue::is_loosely_equal(JSValue::null(), JSValue::boolean(false)));
    CHECK_FALSE(JSValue::is_loosely_equal(JSValue::undefined(), JSValue::number(0)));
    CHECK_FALSE(JSValue::is_loosely_equal(JSValue::undefined(), JSValue::boolean(false)));
    CHECK_FALSE(JSValue::is_loosely_equal(JSValue::null(), JSValue::string("")));
}

TEST_CASE("IsLooselyEqual — konwersje") {
    CHECK(JSValue::is_loosely_equal(JSValue::number(1), JSValue::string("1")));
    CHECK(JSValue::is_loosely_equal(JSValue::string("1"), JSValue::number(1)));   // drugi kierunek
    CHECK(JSValue::is_loosely_equal(JSValue::string(""), JSValue::number(0)));
    CHECK(JSValue::is_loosely_equal(JSValue::boolean(true), JSValue::number(1)));
    CHECK(JSValue::is_loosely_equal(JSValue::boolean(false), JSValue::number(0)));
    CHECK(JSValue::is_loosely_equal(JSValue::string("1"), JSValue::boolean(true)));
    CHECK_FALSE(JSValue::is_loosely_equal(JSValue::string("a"), JSValue::number(0)));
}

// ---------- ApplyStringOrNumericBinaryOperator (13.15.3) ----------

static std::string apply(const JSValue& l, BinaryOperator op, const JSValue& r) {
    return JSValue::apply_string_or_numeric_binary_operator(l, op, r).to_string();
}

TEST_CASE("arytmetyka na liczbach") {
    CHECK(apply(JSValue::number(2), BinaryOperator::Add, JSValue::number(3)) == "5");
    CHECK(apply(JSValue::number(2), BinaryOperator::Sub, JSValue::number(3)) == "-1");
    CHECK(apply(JSValue::number(2), BinaryOperator::Mul, JSValue::number(3)) == "6");
    CHECK(apply(JSValue::number(6), BinaryOperator::Div, JSValue::number(3)) == "2");
}

TEST_CASE("'+' jest jedynym operatorem sklejającym napisy") {
    CHECK(apply(JSValue::number(1), BinaryOperator::Add, JSValue::string("2")) == "12");
    CHECK(apply(JSValue::string("1"), BinaryOperator::Add, JSValue::number(2)) == "12");
    CHECK(apply(JSValue::string("a"), BinaryOperator::Add, JSValue::string("b")) == "ab");

    // pozostałe operatory idą prosto przez ToNumber
    CHECK(apply(JSValue::number(1), BinaryOperator::Mul, JSValue::string("2")) == "2");
    CHECK(apply(JSValue::string("3"), BinaryOperator::Mul, JSValue::string("4")) == "12");
    CHECK(apply(JSValue::string("6"), BinaryOperator::Sub, JSValue::number(1)) == "5");
}

TEST_CASE("koercje prymitywów") {
    CHECK(apply(JSValue::boolean(true), BinaryOperator::Add, JSValue::boolean(true)) == "2");
    CHECK(apply(JSValue::null(), BinaryOperator::Add, JSValue::number(1)) == "1");
    CHECK(apply(JSValue::undefined(), BinaryOperator::Add, JSValue::number(1)) == "NaN");
}

TEST_CASE("przypadki brzegowe IEEE 754") {
    CHECK(apply(JSValue::number(1), BinaryOperator::Div, JSValue::number(0))  == "Infinity");
    CHECK(apply(JSValue::number(-1), BinaryOperator::Div, JSValue::number(0)) == "-Infinity");
    CHECK(apply(JSValue::number(0), BinaryOperator::Div, JSValue::number(0))  == "NaN");
    CHECK(apply(JSValue::number(0.1), BinaryOperator::Add, JSValue::number(0.2)) == "0.30000000000000004");
}

TEST_CASE("'%' to fmod, nie modulo matematyczne") {
    CHECK(apply(JSValue::number(5),  BinaryOperator::Mod, JSValue::number(3))  == "2");
    CHECK(apply(JSValue::number(-5), BinaryOperator::Mod, JSValue::number(3))  == "-2");   // NIE 1
    CHECK(apply(JSValue::number(5),  BinaryOperator::Mod, JSValue::number(-3)) == "2");
}
// ---------- ToInt32 (9.5) i ToUint32 (9.6) ----------
//
// Operatory bitowe działają na 32 bitach, mimo że każda liczba w JS to double.
// Kroki 1-4 obu konwersji są identyczne; różnią się dopiero interpretacją
// wyniku: ToInt32 traktuje wartości od 2^31 w górę jako liczby ujemne.

TEST_CASE("ToInt32/ToUint32: wartości bez konwersji dają zero") {
    CHECK(JSValue::number(0.0).to_int32()   == 0);
    CHECK(JSValue::number(-0.0).to_int32()  == 0);
    CHECK(JSValue::number(NaN).to_int32()   == 0);
    CHECK(JSValue::number(Inf).to_int32()   == 0);
    CHECK(JSValue::number(-Inf).to_int32()  == 0);

    CHECK(JSValue::number(NaN).to_uint32()  == 0u);
    CHECK(JSValue::number(Inf).to_uint32()  == 0u);
    CHECK(JSValue::number(-Inf).to_uint32() == 0u);
}

// Krok 3 to obcięcie w stronę zera, nie zaokrąglenie w dół:
// -1.9 daje -1, a nie -2.
TEST_CASE("ToInt32 obcina część ułamkową w stronę zera") {
    CHECK(JSValue::number(1.9).to_int32()   == 1);
    CHECK(JSValue::number(-1.9).to_int32()  == -1);
    CHECK(JSValue::number(0.5).to_int32()   == 0);
    CHECK(JSValue::number(-0.5).to_int32()  == 0);
    CHECK(JSValue::number(-0.5).to_uint32() == 0u);
}

TEST_CASE("ToInt32 zawija na granicy 2^31") {
    CHECK(JSValue::number(2147483647.0).to_int32() == 2147483647);      // 2^31-1
    CHECK(JSValue::number(2147483648.0).to_int32() == -2147483647 - 1); // 2^31 → ujemne
    CHECK(JSValue::number(4294967295.0).to_int32() == -1);              // 2^32-1
}

// Krok 4 to modulo matematyczne — wynik zawsze w [0, 2^32),
// także dla liczb ujemnych.
TEST_CASE("ToUint32 zwraca wartość bez znaku") {
    CHECK(JSValue::number(-1.0).to_uint32()         == 4294967295u);
    CHECK(JSValue::number(-1.9).to_uint32()         == 4294967295u);
    CHECK(JSValue::number(2147483648.0).to_uint32() == 2147483648u);
    CHECK(JSValue::number(4294967295.0).to_uint32() == 4294967295u);
}

TEST_CASE("ToInt32/ToUint32 redukują modulo 2^32") {
    CHECK(JSValue::number(4294967296.0).to_int32()  == 0);   // 2^32
    CHECK(JSValue::number(4294967297.0).to_int32()  == 1);   // 2^32+1
    CHECK(JSValue::number(-4294967295.0).to_int32() == 1);   // -(2^32-1)
    CHECK(JSValue::number(1e20).to_int32()          == 1661992960);
    CHECK(JSValue::number(1e20).to_uint32()         == 1661992960u);
}

// Krok 1 obu konwersji to ToNumber, więc działają na dowolnym typie.
TEST_CASE("ToInt32 zaczyna od ToNumber") {
    CHECK(JSValue::string("12").to_int32()    == 12);
    CHECK(JSValue::string("abc").to_int32()   == 0);    // ToNumber daje NaN
    CHECK(JSValue::string("").to_int32()      == 0);
    CHECK(JSValue::boolean(true).to_int32()   == 1);
    CHECK(JSValue::boolean(false).to_int32()  == 0);
    CHECK(JSValue::null().to_int32()          == 0);
    CHECK(JSValue::undefined().to_int32()     == 0);    // ToNumber daje NaN
}

// ---------- Abstract Relational Comparison (11.8.5) ----------
//
// Wynik jest TRÓJSTANOWY: true, false albo undefined (brak wartości).
// Trzeci stan pojawia się przy NaN i jest niezbędny, bo "<=" i ">="
// definiuje się przez negację - gdyby NaN dawało zwykłe false,
// "NaN <= 1" wyszłoby po negacji true.

TEST_CASE("is_less_than porównuje liczby") {
    CHECK(JSValue::is_less_than(JSValue::number(1), JSValue::number(2)) == true);
    CHECK(JSValue::is_less_than(JSValue::number(2), JSValue::number(1)) == false);
    CHECK(JSValue::is_less_than(JSValue::number(1), JSValue::number(1)) == false);
    CHECK(JSValue::is_less_than(JSValue::number(-Inf), JSValue::number(0)) == true);
    CHECK(JSValue::is_less_than(JSValue::number(0), JSValue::number(Inf))  == true);
    CHECK(JSValue::is_less_than(JSValue::number(0.0), JSValue::number(-0.0)) == false);
}

TEST_CASE("is_less_than zwraca brak wartości dla NaN") {
    CHECK(JSValue::is_less_than(JSValue::number(NaN), JSValue::number(1)).has_value() == false);
    CHECK(JSValue::is_less_than(JSValue::number(1), JSValue::number(NaN)).has_value() == false);
    CHECK(JSValue::is_less_than(JSValue::number(NaN), JSValue::number(NaN)).has_value() == false);
}

// Krok 4: gdy OBA operandy są stringami, porównanie jest leksykograficzne.
// Wystarczy, że jeden nie jest stringiem, żeby oba poszły przez ToNumber.
TEST_CASE("is_less_than porównuje stringi leksykograficznie") {
    CHECK(JSValue::is_less_than(JSValue::string("a"),  JSValue::string("b"))  == true);
    CHECK(JSValue::is_less_than(JSValue::string("b"),  JSValue::string("a"))  == false);
    CHECK(JSValue::is_less_than(JSValue::string("a"),  JSValue::string("ab")) == true);  // prefiks
    CHECK(JSValue::is_less_than(JSValue::string("10"), JSValue::string("9"))  == true);  // znak po znaku!
    CHECK(JSValue::is_less_than(JSValue::string("10"), JSValue::number(9))    == false); // przez ToNumber
    CHECK(JSValue::is_less_than(JSValue::string(""),   JSValue::string("a"))  == true);
}

TEST_CASE("is_less_than konwertuje operandy nieliczbowe przez ToNumber") {
    CHECK(JSValue::is_less_than(JSValue::null(), JSValue::number(1))          == true);  // null -> 0
    CHECK(JSValue::is_less_than(JSValue::boolean(true), JSValue::number(2))   == true);  // true -> 1
    CHECK(JSValue::is_less_than(JSValue::boolean(false), JSValue::number(1))  == true);
    CHECK(JSValue::is_less_than(JSValue::undefined(), JSValue::number(1)).has_value() == false); // -> NaN
}

// ---------------------------------------------------------------------------
// Wartości obiektowe
//
// JSValue niesie obiekt przez wskaźnik na komórkę sterty. Obiekty są bytami
// tożsamościowymi: liczy się który to obiekt, a nie co w nim jest.
// ---------------------------------------------------------------------------

namespace {
    // Minimalny obiekt wołalny — do czasu, aż powstanie prawdziwy obiekt
    // funkcji z 13.2, tyle wystarczy, żeby sprawdzić typeof (11.4.3).
    struct CallableObject : JSObject {
        bool is_callable() const override { return true; }
    };
}

TEST_CASE("fabryka obiektu ustawia typ Object i zwraca ten sam wskaźnik") {
    JSObject object;
    JSValue value = JSValue::object(&object);

    CHECK(value.type() == JSValueType::Object);
    CHECK(value.as_object() == &object);
}

// as_object jest jedynym wejściem do wnętrza wartości — dla prymitywów ma
// zwracać nullptr, a nie rzucać.
TEST_CASE("as_object zwraca nullptr dla prymitywów") {
    CHECK(JSValue::undefined().as_object()   == nullptr);
    CHECK(JSValue::null().as_object()        == nullptr);
    CHECK(JSValue::boolean(true).as_object() == nullptr);
    CHECK(JSValue::number(1).as_object()     == nullptr);
    CHECK(JSValue::string("x").as_object()   == nullptr);
}

// 11.4.3 tabela 20 — typeof rozdziela obiekty wołalne i pozostałe.
// To jedyne miejsce, w którym typeof rozjeżdża się z JSValueType.
TEST_CASE("typeof rozróżnia obiekt zwykły i wołalny") {
    JSObject plain;
    CallableObject callable;

    CHECK(JSValue::object(&plain).get_type()    == "object");
    CHECK(JSValue::object(&callable).get_type() == "function");
    CHECK(JSValue::null().get_type()            == "object");
}

// 9.2 tabela 11 — każdy obiekt jest prawdziwy, bez wyjątków
TEST_CASE("ToBoolean obiektu jest zawsze true") {
    JSObject plain;
    CallableObject callable;

    CHECK(JSValue::object(&plain).to_boolean()    == true);
    CHECK(JSValue::object(&callable).to_boolean() == true);
}

// Kontrakt warstwy: ToPrimitive(obiekt) woła valueOf/toString użytkownika,
// więc należy do ewaluatora. Konwersje na JSValue przyjmują WYŁĄCZNIE
// prymitywy — obiekt to błąd wywołującego (std::logic_error), a nie błąd
// JavaScriptu. Semantykę ToNumber/ToString obiektów testuje evaluator.cpp.
TEST_CASE("ToNumber i ToString na JSValue odrzucają obiekt jako błąd wywołującego") {
    JSObject object;
    JSValue value = JSValue::object(&object);

    CHECK_THROWS_AS(value.to_number(), std::logic_error);
    CHECK_THROWS_AS(value.to_string(), std::logic_error);
    CHECK_THROWS_AS(value.to_int32(),  std::logic_error);
    CHECK_THROWS_AS(value.to_uint32(), std::logic_error);
}

// 11.9.6 — dwa obiekty są ściśle równe tylko wtedy, gdy to ten sam obiekt
TEST_CASE("=== dla obiektów porównuje tożsamość, nie zawartość") {
    JSObject first;
    JSObject second;

    CHECK(JSValue::is_strictly_equal(JSValue::object(&first), JSValue::object(&first))  == true);
    CHECK(JSValue::is_strictly_equal(JSValue::object(&first), JSValue::object(&second)) == false);
}

TEST_CASE("=== obiektu z prymitywem jest fałszywe bez konwersji") {
    JSObject object;
    JSValue value = JSValue::object(&object);

    CHECK(JSValue::is_strictly_equal(value, JSValue::number(0))      == false);
    CHECK(JSValue::is_strictly_equal(value, JSValue::string("x"))    == false);
    CHECK(JSValue::is_strictly_equal(value, JSValue::boolean(true))  == false);
    CHECK(JSValue::is_strictly_equal(value, JSValue::null())         == false);
    CHECK(JSValue::is_strictly_equal(value, JSValue::undefined())    == false);
}

// 11.9.3 — obiekt vs obiekt schodzi do porównania tożsamości
TEST_CASE("== dla dwóch obiektów porównuje tożsamość") {
    JSObject first;
    JSObject second;

    CHECK(JSValue::is_loosely_equal(JSValue::object(&first), JSValue::object(&first))  == true);
    CHECK(JSValue::is_loosely_equal(JSValue::object(&first), JSValue::object(&second)) == false);
}

// 11.9.3 kroki 8-9 wymagają ToPrimitive — to robi Evaluator::loosely_equals.
// Statyczne is_loosely_equal dostające obiekt z liczbą/łańcuchem oznacza,
// że ktoś ominął ewaluator. Ważne, że w obie strony.
TEST_CASE("JSValue::is_loosely_equal odrzuca parę obiekt-prymityw wymagającą ToPrimitive") {
    JSObject object;
    JSValue value = JSValue::object(&object);

    CHECK_THROWS_AS(JSValue::is_loosely_equal(value, JSValue::number(1)),   std::logic_error);
    CHECK_THROWS_AS(JSValue::is_loosely_equal(JSValue::number(1), value),   std::logic_error);
    CHECK_THROWS_AS(JSValue::is_loosely_equal(value, JSValue::string("x")), std::logic_error);
    CHECK_THROWS_AS(JSValue::is_loosely_equal(JSValue::string("x"), value), std::logic_error);
}

// Boolean najpierw idzie do liczby (kroki 6-7), a potem trafia na parę
// obiekt-liczba — więc to też wymaga ewaluatora.
TEST_CASE("JSValue::is_loosely_equal obiektu z booleanem też wymaga ewaluatora") {
    JSObject object;
    JSValue value = JSValue::object(&object);

    CHECK_THROWS_AS(JSValue::is_loosely_equal(value, JSValue::boolean(true)), std::logic_error);
    CHECK_THROWS_AS(JSValue::is_loosely_equal(JSValue::boolean(true), value), std::logic_error);
}

// Dla null i undefined spec nie ma wiersza z obiektem — wynik to false,
// bez żadnej konwersji (więc bez wyjątku).
TEST_CASE("== obiektu z null i undefined jest fałszywe bez konwersji") {
    JSObject object;
    JSValue value = JSValue::object(&object);

    CHECK(JSValue::is_loosely_equal(value, JSValue::null())      == false);
    CHECK(JSValue::is_loosely_equal(value, JSValue::undefined()) == false);
    CHECK(JSValue::is_loosely_equal(JSValue::null(), value)      == false);
    CHECK(JSValue::is_loosely_equal(JSValue::undefined(), value) == false);
}

// 11.8.5 — ToPrimitive operandów robi Evaluator::less_than; is_less_than
// dostaje już prymitywy.
TEST_CASE("JSValue::is_less_than odrzuca obiekt jako błąd wywołującego") {
    JSObject object;
    JSValue value = JSValue::object(&object);

    CHECK_THROWS_AS(JSValue::is_less_than(value, JSValue::number(1)), std::logic_error);
    CHECK_THROWS_AS(JSValue::is_less_than(JSValue::number(1), value), std::logic_error);
}
