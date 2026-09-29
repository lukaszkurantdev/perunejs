#include <doctest/doctest.h>

#include "memory/js_object.h"

#include <string>
#include <vector>

using namespace perunejs;

// ---------------------------------------------------------------------------
// Helpery
// ---------------------------------------------------------------------------

static bool is_index(const std::string& name) {
    uint32_t index = 0;
    return is_array_index(name, index);
}

static uint32_t index_of(const std::string& name) {
    uint32_t index = 0;
    REQUIRE(is_array_index(name, index));
    return index;
}

// właściwość danych: zapisywalna, enumerowalna, konfigurowalna
static void put(JSObject& object, const std::string& name, double value = 0) {
    object.define_own_property(
        name, PropertyDescriptor::data(JSValue::number(value), true, true, true));
}

// ---------------------------------------------------------------------------
// PropertyDescriptor — rodzaj wynika z obecnych pól (8.10.1-8.10.3)
// ---------------------------------------------------------------------------

TEST_CASE("pusty deskryptor jest generyczny") {
    const PropertyDescriptor descriptor;

    CHECK(descriptor.is_generic());
    CHECK_FALSE(descriptor.is_data());
    CHECK_FALSE(descriptor.is_accessor());
}

TEST_CASE("obecność value albo writable czyni deskryptor deskryptorem danych") {
    PropertyDescriptor with_value;
    with_value.value = JSValue::number(1);
    CHECK(with_value.is_data());
    CHECK_FALSE(with_value.is_accessor());

    PropertyDescriptor with_writable;
    with_writable.writable = false;
    CHECK(with_writable.is_data());
}

TEST_CASE("{value: undefined} to deskryptor DANYCH, nie generyczny") {
    // różnica między "pole nieobecne" a "pole obecne o wartości undefined"
    PropertyDescriptor descriptor;
    descriptor.value = JSValue::undefined();

    CHECK(descriptor.is_data());
    CHECK_FALSE(descriptor.is_generic());
}

TEST_CASE("{get: undefined} to deskryptor AKCESOROWY, nie generyczny") {
    PropertyDescriptor descriptor;
    descriptor.get = nullptr;              // obecne, ale puste

    CHECK(descriptor.is_accessor());
    CHECK_FALSE(descriptor.is_generic());
    CHECK_FALSE(descriptor.is_data());
}

TEST_CASE("complete() uzupełnia brakujące pola deskryptora danych") {
    PropertyDescriptor descriptor;
    descriptor.value = JSValue::number(1);
    descriptor.complete();

    CHECK(descriptor.get_value().to_number() == 1);
    CHECK(descriptor.is_writable()     == false);
    CHECK(descriptor.is_enumerable()   == false);
    CHECK(descriptor.is_configurable() == false);
}

TEST_CASE("complete() na deskryptorze akcesorowym nie dokłada value") {
    PropertyDescriptor descriptor;
    descriptor.get = nullptr;
    descriptor.complete();

    CHECK(descriptor.is_accessor());
    CHECK_FALSE(descriptor.value.has_value());
    CHECK_FALSE(descriptor.writable.has_value());
    CHECK(descriptor.set.has_value());     // uzupełnione na "brak settera"
}

TEST_CASE("complete() na pustym deskryptorze robi z niego deskryptor danych") {
    PropertyDescriptor descriptor;
    descriptor.complete();

    CHECK(descriptor.is_data());
    CHECK(descriptor.get_value().type() == JSValueType::Undefined);
}

// ---------------------------------------------------------------------------
// Indeksy tablicowe (15.4)
// ---------------------------------------------------------------------------

TEST_CASE("co jest indeksem tablicowym") {
    CHECK(is_index("0"));
    CHECK(is_index("1"));
    CHECK(is_index("42"));
    CHECK(is_index("4294967294"));          // 2^32 - 2, największy dopuszczalny
}

TEST_CASE("co NIE jest indeksem tablicowym") {
    CHECK_FALSE(is_index(""));
    CHECK_FALSE(is_index("01"));            // ToString(ToUint32("01")) daje "1"
    CHECK_FALSE(is_index("00"));
    CHECK_FALSE(is_index("-1"));
    CHECK_FALSE(is_index("1.5"));
    CHECK_FALSE(is_index(" 1"));
    CHECK_FALSE(is_index("1 "));
    CHECK_FALSE(is_index("+1"));
    CHECK_FALSE(is_index("a"));
    CHECK_FALSE(is_index("1a"));
    CHECK_FALSE(is_index("1e2"));
    CHECK_FALSE(is_index("4294967295"));    // 2^32 - 1 zarezerwowane jako max length
    CHECK_FALSE(is_index("4294967296"));
    CHECK_FALSE(is_index("99999999999"));   // 11 cyfr, nie zmieści się w uint32
}

TEST_CASE("wartość indeksu") {
    CHECK(index_of("0")          == 0u);
    CHECK(index_of("42")         == 42u);
    CHECK(index_of("4294967294") == 4294967294u);
}

// ---------------------------------------------------------------------------
// Kolejność enumeracji własnych właściwości
// ---------------------------------------------------------------------------

TEST_CASE("pusty obiekt nie ma kluczy") {
    const JSObject object;
    CHECK(object.own_keys().empty());
}

TEST_CASE("klucze nieindeksowe zachowują kolejność wstawienia") {
    JSObject object;
    put(object, "b");
    put(object, "a");
    put(object, "c");

    CHECK(object.own_keys() == std::vector<std::string>{"b", "a", "c"});
}

TEST_CASE("indeksy wychodzą rosnąco liczbowo, nie leksykograficznie") {
    JSObject object;
    put(object, "10");
    put(object, "2");
    put(object, "1");

    // std::map dałoby "1", "10", "2"
    CHECK(object.own_keys() == std::vector<std::string>{"1", "2", "10"});
}

TEST_CASE("indeksy idą przed resztą, niezależnie od kolejności wstawienia") {
    JSObject object;
    put(object, "b");
    put(object, "2");
    put(object, "a");
    put(object, "10");

    CHECK(object.own_keys() == std::vector<std::string>{"2", "10", "b", "a"});
}

TEST_CASE("klucze wyglądające na liczby, ale niebędące indeksami, idą do reszty") {
    JSObject object;
    put(object, "01");
    put(object, "1");
    put(object, "-1");
    put(object, "1.5");

    CHECK(object.own_keys() == std::vector<std::string>{"1", "01", "-1", "1.5"});
}

TEST_CASE("nadpisanie wartości nie przesuwa właściwości na koniec") {
    JSObject object;
    put(object, "a", 1);
    put(object, "b", 2);
    put(object, "a", 3);

    CHECK(object.own_keys() == std::vector<std::string>{"a", "b"});
    CHECK(object.get_own_property("a")->get_value().to_number() == 3);
}

TEST_CASE("usunięcie i ponowne wstawienie przesuwa na koniec") {
    JSObject object;
    put(object, "a");
    put(object, "b");
    put(object, "c");

    CHECK(object.delete_own_property("b"));
    CHECK(object.own_keys() == std::vector<std::string>{"a", "c"});

    put(object, "b");
    CHECK(object.own_keys() == std::vector<std::string>{"a", "c", "b"});
}

// ---------------------------------------------------------------------------
// Usuwanie (8.12.7)
// ---------------------------------------------------------------------------

TEST_CASE("usunięcie nieistniejącej właściwości się udaje") {
    JSObject object;
    CHECK(object.delete_own_property("brak"));
}

TEST_CASE("właściwości niekonfigurowalnej nie da się usunąć") {
    JSObject object;
    object.define_own_property(
        "x", PropertyDescriptor::data(JSValue::number(1), true, true, false));

    CHECK_FALSE(object.delete_own_property("x"));
    CHECK(object.own_keys() == std::vector<std::string>{"x"});
    CHECK(object.get_own_property("x") != nullptr);
}

TEST_CASE("get_own_property zwraca nullptr dla nieistniejącej właściwości") {
    JSObject object;
    put(object, "a");

    CHECK(object.get_own_property("a")    != nullptr);
    CHECK(object.get_own_property("brak") == nullptr);
}

// ---------------------------------------------------------------------------
// Łańcuch prototypów (8.12.2, 8.12.6)
//
// get_own_property patrzy TYLKO na własne właściwości, get_property schodzi
// w dół łańcucha. Obie są zdefiniowane w kategoriach tej pierwszej, więc
// obiekty egzotyczne dostaną poprawne zachowanie nadpisując tylko ją.
// ---------------------------------------------------------------------------

TEST_CASE("get_property znajduje właściwość z prototypu") {
    JSObject base;
    put(base, "inherited", 1);

    JSObject derived;
    derived.prototype = &base;

    CHECK(derived.get_own_property("inherited") == nullptr);   // nie własna
    CHECK(derived.get_property("inherited")     != nullptr);   // ale odziedziczona
    CHECK(derived.get_property("inherited")->get_value().to_number() == 1);
}

TEST_CASE("własna właściwość przesłania odziedziczoną") {
    JSObject base;
    put(base, "x", 1);

    JSObject derived;
    derived.prototype = &base;
    put(derived, "x", 2);

    CHECK(derived.get_property("x")->get_value().to_number()     == 2);
    CHECK(derived.get_own_property("x")->get_value().to_number() == 2);
    // oryginał w prototypie nietknięty
    CHECK(base.get_property("x")->get_value().to_number() == 1);
}

TEST_CASE("łańcuch trzech poziomów") {
    JSObject grandparent;
    put(grandparent, "deep", 1);

    JSObject parent;
    parent.prototype = &grandparent;
    put(parent, "middle", 2);

    JSObject child;
    child.prototype = &parent;
    put(child, "own", 3);

    CHECK(child.get_property("own")->get_value().to_number()    == 3);
    CHECK(child.get_property("middle")->get_value().to_number() == 2);
    CHECK(child.get_property("deep")->get_value().to_number()   == 1);
}

TEST_CASE("brak właściwości w całym łańcuchu daje nullptr") {
    JSObject base;
    JSObject derived;
    derived.prototype = &base;

    CHECK(derived.get_property("brak") == nullptr);
    CHECK_FALSE(derived.has_property("brak"));
}

TEST_CASE("has_property widzi całą ścieżkę, own_keys tylko własne") {
    JSObject base;
    put(base, "inherited");

    JSObject derived;
    derived.prototype = &base;
    put(derived, "own");

    CHECK(derived.has_property("own"));
    CHECK(derived.has_property("inherited"));

    // enumeracja własnych kluczy NIE zawiera odziedziczonych
    CHECK(derived.own_keys() == std::vector<std::string>{"own"});
}

TEST_CASE("obiekt bez prototypu kończy łańcuch") {
    JSObject object;
    put(object, "x");

    CHECK(object.prototype == nullptr);
    CHECK(object.has_property("x"));
    CHECK_FALSE(object.has_property("brak"));
}

TEST_CASE("usunięcie własnej właściwości odsłania odziedziczoną") {
    JSObject base;
    put(base, "x", 1);

    JSObject derived;
    derived.prototype = &base;
    put(derived, "x", 2);

    CHECK(derived.get_property("x")->get_value().to_number() == 2);
    CHECK(derived.delete_own_property("x"));
    CHECK(derived.get_property("x")->get_value().to_number() == 1);   // znów widać prototyp
}

// ---------------------------------------------------------------------------
// SameValue (9.12) — różni się od === dokładnie w dwóch miejscach
// ---------------------------------------------------------------------------

TEST_CASE("SameValue kontra ===") {
    const JSValue nan  = JSValue::number(std::numeric_limits<double>::quiet_NaN());
    const JSValue zero = JSValue::number(0.0);
    const JSValue minus_zero = JSValue::number(-0.0);

    // NaN: === mówi nie, SameValue mówi tak
    CHECK_FALSE(JSValue::is_strictly_equal(nan, nan));
    CHECK(JSValue::same_value(nan, nan));

    // -0 i +0: === mówi tak, SameValue mówi nie
    CHECK(JSValue::is_strictly_equal(zero, minus_zero));
    CHECK_FALSE(JSValue::same_value(zero, minus_zero));

    // reszta identyczna
    CHECK(JSValue::same_value(JSValue::number(1), JSValue::number(1)));
    CHECK(JSValue::same_value(JSValue::string("a"), JSValue::string("a")));
    CHECK(JSValue::same_value(JSValue::undefined(), JSValue::undefined()));
    CHECK(JSValue::same_value(JSValue::null(), JSValue::null()));
    CHECK_FALSE(JSValue::same_value(JSValue::number(1), JSValue::string("1")));
}

// ---------------------------------------------------------------------------
// [[DefineOwnProperty]] (8.12.9)
// ---------------------------------------------------------------------------

static PropertyDescriptor only_writable(bool writable) {
    PropertyDescriptor descriptor;
    descriptor.writable = writable;
    return descriptor;
}

static PropertyDescriptor only_enumerable(bool enumerable) {
    PropertyDescriptor descriptor;
    descriptor.enumerable = enumerable;
    return descriptor;
}

static PropertyDescriptor only_configurable(bool configurable) {
    PropertyDescriptor descriptor;
    descriptor.configurable = configurable;
    return descriptor;
}

// --- kroki 2-4: właściwość nie istnieje -------------------------------------

TEST_CASE("nowa właściwość powstaje z uzupełnionymi polami domyślnymi") {
    JSObject object;
    CHECK(object.define_own_property("x", PropertyDescriptor::only_value(JSValue::number(1))));

    const PropertyDescriptor *created = object.get_own_property("x");
    REQUIRE(created != nullptr);

    CHECK(created->get_value().to_number() == 1);
    CHECK_FALSE(created->is_writable());        // brakujące pola -> false
    CHECK_FALSE(created->is_enumerable());
    CHECK_FALSE(created->is_configurable());
}

TEST_CASE("do obiektu nierozszerzalnego nie da się nic dodać") {
    JSObject object;
    object.extensible = false;

    CHECK_FALSE(object.define_own_property("x", PropertyDescriptor::only_value(JSValue::number(1))));
    CHECK(object.get_own_property("x") == nullptr);
}

TEST_CASE("nierozszerzalność nie blokuje zmiany istniejącej właściwości") {
    JSObject object;
    put(object, "x", 1);
    object.extensible = false;

    CHECK(object.define_own_property("x", PropertyDescriptor::only_value(JSValue::number(2))));
    CHECK(object.get_own_property("x")->get_value().to_number() == 2);
}

// --- kroki 5-6: brak zmiany -------------------------------------------------

TEST_CASE("pusty deskryptor zawsze się udaje, także na niekonfigurowalnej") {
    JSObject object;
    object.define_own_property("x", PropertyDescriptor::data(JSValue::number(1), false, false, false));

    CHECK(object.define_own_property("x", PropertyDescriptor()));
}

TEST_CASE("ustawienie tej samej wartości na niezapisywalnej się udaje (krok 6)") {
    JSObject object;
    object.define_own_property("x", PropertyDescriptor::data(JSValue::number(1), false, false, false));

    CHECK(object.define_own_property("x", PropertyDescriptor::only_value(JSValue::number(1))));
    CHECK_FALSE(object.define_own_property("x", PropertyDescriptor::only_value(JSValue::number(2))));
}

TEST_CASE("krok 6 używa SameValue, nie ===") {
    const JSValue nan = JSValue::number(std::numeric_limits<double>::quiet_NaN());

    JSObject with_nan;
    with_nan.define_own_property("x", PropertyDescriptor::data(nan, false, false, false));
    // NaN na NaN to ta sama wartość -> przechodzi
    CHECK(with_nan.define_own_property("x", PropertyDescriptor::only_value(nan)));

    JSObject with_zero;
    with_zero.define_own_property("x",
        PropertyDescriptor::data(JSValue::number(0.0), false, false, false));
    // +0 na -0 to RÓŻNE wartości -> odrzucone
    CHECK_FALSE(with_zero.define_own_property("x",
        PropertyDescriptor::only_value(JSValue::number(-0.0))));
}

// --- krok 7: właściwość niekonfigurowalna -----------------------------------

TEST_CASE("na niekonfigurowalnej nie da się włączyć configurable") {
    JSObject object;
    object.define_own_property("x", PropertyDescriptor::data(JSValue::number(1), true, true, false));

    CHECK_FALSE(object.define_own_property("x", only_configurable(true)));
    CHECK(object.define_own_property("x", only_configurable(false)));   // bez zmiany
}

TEST_CASE("na niekonfigurowalnej nie da się zmienić enumerable") {
    JSObject object;
    object.define_own_property("x", PropertyDescriptor::data(JSValue::number(1), true, true, false));

    CHECK_FALSE(object.define_own_property("x", only_enumerable(false)));
    CHECK(object.define_own_property("x", only_enumerable(true)));      // bez zmiany
}

TEST_CASE("na konfigurowalnej wolno wszystko") {
    JSObject object;
    object.define_own_property("x", PropertyDescriptor::data(JSValue::number(1), false, false, true));

    CHECK(object.define_own_property("x", only_configurable(true)));
    CHECK(object.define_own_property("x", only_enumerable(true)));
    CHECK(object.define_own_property("x", only_writable(true)));
    CHECK(object.define_own_property("x", PropertyDescriptor::only_value(JSValue::number(9))));

    const PropertyDescriptor *result = object.get_own_property("x");
    CHECK(result->get_value().to_number() == 9);
    CHECK(result->is_writable());
    CHECK(result->is_enumerable());
    CHECK(result->is_configurable());
}

// --- krok 9: zmiana rodzaju -------------------------------------------------

TEST_CASE("zmiana danych na akcesor wymaga konfigurowalności") {
    JSObject locked;
    locked.define_own_property("x", PropertyDescriptor::data(JSValue::number(1), true, true, false));
    CHECK_FALSE(locked.define_own_property("x",
        PropertyDescriptor::accessor(nullptr, nullptr, true, false)));

    JSObject open;
    open.define_own_property("x", PropertyDescriptor::data(JSValue::number(1), true, true, true));
    CHECK(open.define_own_property("x",
        PropertyDescriptor::accessor(nullptr, nullptr, true, true)));
    CHECK(open.get_own_property("x")->is_accessor());
}

TEST_CASE("zmiana rodzaju usuwa pola poprzedniego rodzaju") {
    JSObject object;
    object.define_own_property("x", PropertyDescriptor::data(JSValue::number(1), true, true, true));

    PropertyDescriptor to_accessor;
    to_accessor.get = nullptr;
    CHECK(object.define_own_property("x", to_accessor));

    const PropertyDescriptor *result = object.get_own_property("x");
    CHECK(result->is_accessor());
    CHECK_FALSE(result->value.has_value());      // value ZNIKA, nie zostaje obok
    CHECK_FALSE(result->writable.has_value());
}

TEST_CASE("zmiana rodzaju zachowuje enumerable i configurable") {
    JSObject object;
    object.define_own_property("x", PropertyDescriptor::data(JSValue::number(1), true, true, true));

    PropertyDescriptor to_accessor;
    to_accessor.get = nullptr;
    object.define_own_property("x", to_accessor);

    const PropertyDescriptor *result = object.get_own_property("x");
    CHECK(result->is_enumerable());
    CHECK(result->is_configurable());
}

// --- krok 10: oba deskryptory danych ----------------------------------------

TEST_CASE("niekonfigurowalna i niezapisywalna: wartości nie zmienisz") {
    JSObject object;
    object.define_own_property("x", PropertyDescriptor::data(JSValue::number(1), false, true, false));

    CHECK_FALSE(object.define_own_property("x", PropertyDescriptor::only_value(JSValue::number(2))));
    CHECK_FALSE(object.define_own_property("x", only_writable(true)));
    CHECK(object.get_own_property("x")->get_value().to_number() == 1);
}

TEST_CASE("niekonfigurowalna, ale zapisywalna: wartość wolno zmienić") {
    JSObject object;
    object.define_own_property("x", PropertyDescriptor::data(JSValue::number(1), true, true, false));

    CHECK(object.define_own_property("x", PropertyDescriptor::only_value(JSValue::number(2))));
    CHECK(object.get_own_property("x")->get_value().to_number() == 2);
}

TEST_CASE("writable to droga w jedną stronę przy niekonfigurowalnej") {
    JSObject object;
    object.define_own_property("x", PropertyDescriptor::data(JSValue::number(1), true, true, false));

    CHECK(object.define_own_property("x", only_writable(false)));       // true -> false wolno
    CHECK_FALSE(object.define_own_property("x", only_writable(true)));  // z powrotem już nie
}

// --- krok 12: scalanie pól --------------------------------------------------

TEST_CASE("podanie samego value nie rusza pozostałych pól") {
    JSObject object;
    object.define_own_property("x", PropertyDescriptor::data(JSValue::number(1), true, true, true));

    CHECK(object.define_own_property("x", PropertyDescriptor::only_value(JSValue::number(2))));

    const PropertyDescriptor *result = object.get_own_property("x");
    CHECK(result->get_value().to_number() == 2);
    CHECK(result->is_writable());          // niezmienione
    CHECK(result->is_enumerable());
    CHECK(result->is_configurable());
}

TEST_CASE("redefinicja nie przesuwa właściwości w kolejności enumeracji") {
    JSObject object;
    put(object, "a");
    put(object, "b");

    object.define_own_property("a", PropertyDescriptor::only_value(JSValue::number(9)));

    CHECK(object.own_keys() == std::vector<std::string>{"a", "b"});
}

// ---------------------------------------------------------------------------
// [[CanPut]] (8.12.4)
//
// Odpowiada wyłącznie na pytanie "czy zapis jest dozwolony". Sam zapis
// wykonuje [[Put]], które dojdzie razem z setterami.
// ---------------------------------------------------------------------------

static void put_readonly(JSObject& object, const std::string& name, double value = 0) {
    object.define_own_property(
        name, PropertyDescriptor::data(JSValue::number(value), false, true, true));
}

TEST_CASE("własna właściwość rozstrzyga sama (krok 2)") {
    JSObject object;
    put(object, "writable");
    put_readonly(object, "readonly");

    CHECK(object.can_put("writable"));
    CHECK_FALSE(object.can_put("readonly"));
}

TEST_CASE("brak właściwości i brak prototypu: decyduje extensible (kroki 3-4)") {
    JSObject open;
    CHECK(open.can_put("brak"));

    JSObject sealed;
    sealed.extensible = false;
    CHECK_FALSE(sealed.can_put("brak"));
}

TEST_CASE("brak właściwości w całym łańcuchu: decyduje extensible (kroki 5-6)") {
    JSObject base;
    put(base, "inna");

    JSObject derived;
    derived.prototype = &base;
    CHECK(derived.can_put("brak"));

    derived.extensible = false;
    CHECK_FALSE(derived.can_put("brak"));
}

TEST_CASE("odziedziczona właściwość tylko do odczytu blokuje zapis do potomka (krok 8)") {
    JSObject base;
    put_readonly(base, "x", 1);

    JSObject derived;
    derived.prototype = &base;

    CHECK_FALSE(derived.can_put("x"));
}

TEST_CASE("odziedziczona właściwość zapisywalna pozwala na zapis") {
    JSObject base;
    put(base, "x", 1);

    JSObject derived;
    derived.prototype = &base;

    CHECK(derived.can_put("x"));
}

TEST_CASE("własna zapisywalna wygrywa z odziedziczoną niezapisywalną") {
    JSObject base;
    put_readonly(base, "x", 1);

    JSObject derived;
    derived.prototype = &base;
    put(derived, "x", 2);          // własna, zapisywalna

    // krok 2 kończy sprawę, do łańcucha w ogóle nie schodzimy
    CHECK(derived.can_put("x"));
}

TEST_CASE("własna niezapisywalna blokuje mimo zapisywalnej w prototypie") {
    JSObject base;
    put(base, "x", 1);

    JSObject derived;
    derived.prototype = &base;
    put_readonly(derived, "x", 2);

    CHECK_FALSE(derived.can_put("x"));
}

TEST_CASE("nierozszerzalność nie blokuje zapisu do WŁASNEJ zapisywalnej właściwości") {
    JSObject object;
    put(object, "x");
    object.extensible = false;

    CHECK(object.can_put("x"));    // krok 2 nie patrzy na extensible
}

TEST_CASE("nierozszerzalność blokuje zapis przez odziedziczoną właściwość danych") {
    JSObject base;
    put(base, "x", 1);             // zapisywalna w prototypie

    JSObject derived;
    derived.prototype = &base;
    CHECK(derived.can_put("x"));

    derived.extensible = false;
    CHECK_FALSE(derived.can_put("x"));   // bo zapis wymagałby utworzenia własnej
}

// --- akcesory ---------------------------------------------------------------

TEST_CASE("własny akcesor: decyduje obecność settera") {
    JSObject fake_function;        // wystarczy dowolny obiekt jako "funkcja"

    JSObject with_setter;
    with_setter.define_own_property(
        "x", PropertyDescriptor::accessor(nullptr, &fake_function, true, true));
    CHECK(with_setter.can_put("x"));

    JSObject without_setter;
    without_setter.define_own_property(
        "x", PropertyDescriptor::accessor(&fake_function, nullptr, true, true));
    CHECK_FALSE(without_setter.can_put("x"));
}

TEST_CASE("odziedziczony akcesor: też decyduje obecność settera (krok 7)") {
    JSObject fake_function;

    JSObject base_with_setter;
    base_with_setter.define_own_property(
        "x", PropertyDescriptor::accessor(nullptr, &fake_function, true, true));

    JSObject derived_ok;
    derived_ok.prototype = &base_with_setter;
    CHECK(derived_ok.can_put("x"));

    JSObject base_without_setter;
    base_without_setter.define_own_property(
        "x", PropertyDescriptor::accessor(&fake_function, nullptr, true, true));

    JSObject derived_blocked;
    derived_blocked.prototype = &base_without_setter;
    CHECK_FALSE(derived_blocked.can_put("x"));
}

TEST_CASE("odziedziczony akcesor z setterem działa mimo nierozszerzalności") {
    JSObject fake_function;

    JSObject base;
    base.define_own_property(
        "x", PropertyDescriptor::accessor(nullptr, &fake_function, true, true));

    JSObject derived;
    derived.prototype = &base;
    derived.extensible = false;

    // zapis pójdzie przez setter prototypu, nie utworzy własnej właściwości,
    // więc extensible nie ma tu nic do rzeczy
    CHECK(derived.can_put("x"));
}

// ---------------------------------------------------------------------------
// [[Get]] i [[Put]] (8.12.3, 8.12.5)
//
// Docelowo te include'y należą na górę pliku.
// ---------------------------------------------------------------------------

#include "evaluator/evaluator.h"
#include "memory/heap.h"
#include "memory/js_native_function.h"

// Wskaźnik na funkcję nie ma domknięcia, więc akcesory zapisują to,
// co zobaczyły, do zmiennych pliku.
namespace {
    JSValue seen_this;
    JSValue seen_value;
    int getter_calls = 0;
    int setter_calls = 0;

    Completion recording_getter(Evaluator&, const JSValue& this_value,
                                const std::vector<JSValue>&) {
        seen_this = this_value;
        ++getter_calls;
        return Completion::normal(JSValue::number(42));
    }

    Completion recording_setter(Evaluator&, const JSValue& this_value,
                                const std::vector<JSValue>& args) {
        seen_this  = this_value;
        seen_value = args.empty() ? JSValue::undefined() : args[0];
        ++setter_calls;
        return Completion::normal(JSValue::undefined());
    }

    void reset_recording() {
        seen_this    = JSValue::undefined();
        seen_value   = JSValue::undefined();
        getter_calls = 0;
        setter_calls = 0;
    }
}

static JSValue value_from_get(Evaluator& evaluator, JSObject& object, const std::string& name) {
    Completion result = object.get(evaluator, name);
    REQUIRE(result.type == COMPLETION_TYPE::NORMAL);
    return result.get_value_or_undefined();
}

// --- [[Get]] ----------------------------------------------------------------

TEST_CASE("get nieistniejącej właściwości daje undefined") {
    Heap heap; Evaluator evaluator(heap);
    JSObject object;

    CHECK(value_from_get(evaluator, object, "brak").type() == JSValueType::Undefined);
}

TEST_CASE("get czyta własną i odziedziczoną właściwość danych") {
    Heap heap; Evaluator evaluator(heap);

    JSObject base;
    put(base, "inherited", 1);

    JSObject derived;
    derived.prototype = &base;
    put(derived, "own", 2);

    CHECK(value_from_get(evaluator, derived, "own").to_number()       == 2);
    CHECK(value_from_get(evaluator, derived, "inherited").to_number() == 1);
}

TEST_CASE("get na akcesorze wywołuje getter") {
    Heap heap; Evaluator evaluator(heap);
    reset_recording();

    JSObject object;
    object.define_own_property("x", PropertyDescriptor::accessor(
        heap.allocate<JSNativeFunction>("get x", recording_getter), nullptr, true, true));

    CHECK(value_from_get(evaluator, object, "x").to_number() == 42);
    CHECK(getter_calls == 1);
}

TEST_CASE("this w getterze to obiekt, na którym zaczęto szukanie") {
    Heap heap; Evaluator evaluator(heap);
    reset_recording();

    JSObject base;
    base.define_own_property("x", PropertyDescriptor::accessor(
        heap.allocate<JSNativeFunction>("get x", recording_getter), nullptr, true, true));

    JSObject derived;
    derived.prototype = &base;

    CHECK(value_from_get(evaluator, derived, "x").to_number() == 42);

    // NIE prototyp, w którym getter siedzi — tylko obiekt, przez który czytano
    CHECK(seen_this.as_object() == &derived);
    CHECK(seen_this.as_object() != &base);
}

TEST_CASE("akcesor bez gettera daje undefined i niczego nie woła") {
    Heap heap; Evaluator evaluator(heap);
    reset_recording();

    JSObject object;
    object.define_own_property("x", PropertyDescriptor::accessor(
        nullptr, heap.allocate<JSNativeFunction>("set x", recording_setter), true, true));

    CHECK(value_from_get(evaluator, object, "x").type() == JSValueType::Undefined);
    CHECK(getter_calls == 0);
}

// --- [[Put]] ----------------------------------------------------------------

TEST_CASE("put tworzy nową właściwość z pełnymi uprawnieniami (krok 6)") {
    Heap heap; Evaluator evaluator(heap);
    JSObject object;

    CHECK(object.put(evaluator, "x", JSValue::number(1), false).type == COMPLETION_TYPE::NORMAL);

    const PropertyDescriptor *created = object.get_own_property("x");
    REQUIRE(created != nullptr);
    CHECK(created->get_value().to_number() == 1);
    CHECK(created->is_writable());          // inaczej niż przy define_own_property
    CHECK(created->is_enumerable());
    CHECK(created->is_configurable());
}

TEST_CASE("put na własnej właściwości danych zmienia samą wartość") {
    Heap heap; Evaluator evaluator(heap);

    JSObject object;
    object.define_own_property("x", PropertyDescriptor::data(JSValue::number(1), true, false, false));
    object.put(evaluator, "x", JSValue::number(2), false);

    const PropertyDescriptor *result = object.get_own_property("x");
    CHECK(result->get_value().to_number() == 2);
    CHECK_FALSE(result->is_enumerable());   // flagi nietknięte
    CHECK_FALSE(result->is_configurable());
}

TEST_CASE("put na właściwości tylko do odczytu: po cichu nic albo TypeError") {
    Heap heap; Evaluator evaluator(heap);

    JSObject object;
    put_readonly(object, "x", 1);

    // tryb zwykły
    CHECK(object.put(evaluator, "x", JSValue::number(2), false).type == COMPLETION_TYPE::NORMAL);
    CHECK(object.get_own_property("x")->get_value().to_number() == 1);

    // tryb strict
    CHECK(object.put(evaluator, "x", JSValue::number(2), true).type == COMPLETION_TYPE::THROW);
    CHECK(object.get_own_property("x")->get_value().to_number() == 1);
}

TEST_CASE("zapis przez odziedziczony setter nie tworzy własnej właściwości") {
    Heap heap; Evaluator evaluator(heap);
    reset_recording();

    JSObject base;
    base.define_own_property("x", PropertyDescriptor::accessor(
        nullptr, heap.allocate<JSNativeFunction>("set x", recording_setter), true, true));

    JSObject derived;
    derived.prototype = &base;

    CHECK(derived.put(evaluator, "x", JSValue::number(7), false).type == COMPLETION_TYPE::NORMAL);

    CHECK(setter_calls == 1);
    CHECK(seen_value.to_number() == 7);
    CHECK(seen_this.as_object() == &derived);            // this to obiekt zapisywany
    CHECK(derived.get_own_property("x") == nullptr);     // NIE powstała własna właściwość
}

TEST_CASE("odziedziczona właściwość tylko do odczytu blokuje zapis") {
    Heap heap; Evaluator evaluator(heap);

    JSObject base;
    put_readonly(base, "x", 1);

    JSObject derived;
    derived.prototype = &base;

    CHECK(derived.put(evaluator, "x", JSValue::number(2), false).type == COMPLETION_TYPE::NORMAL);
    CHECK(derived.get_own_property("x") == nullptr);     // nic nie powstało

    CHECK(derived.put(evaluator, "x", JSValue::number(2), true).type == COMPLETION_TYPE::THROW);
}

TEST_CASE("akcesor bez settera blokuje zapis") {
    Heap heap; Evaluator evaluator(heap);
    reset_recording();

    JSObject object;
    object.define_own_property("x", PropertyDescriptor::accessor(
        heap.allocate<JSNativeFunction>("get x", recording_getter), nullptr, true, true));

    CHECK(object.put(evaluator, "x", JSValue::number(1), true).type == COMPLETION_TYPE::THROW);
    CHECK(setter_calls == 0);
}

TEST_CASE("put do obiektu nierozszerzalnego nie tworzy nowej właściwości") {
    Heap heap; Evaluator evaluator(heap);

    JSObject object;
    object.extensible = false;

    CHECK(object.put(evaluator, "x", JSValue::number(1), false).type == COMPLETION_TYPE::NORMAL);
    CHECK(object.get_own_property("x") == nullptr);

    CHECK(object.put(evaluator, "x", JSValue::number(1), true).type == COMPLETION_TYPE::THROW);
}

// ---------------------------------------------------------------------------
// [[DefaultValue]] i ToPrimitive (8.12.8, 9.1)
//
// Bez Object.prototype żaden obiekt nie ma valueOf ani toString, więc
// domyślnie konwersja daje TypeError. Metody podstawiamy jako natywne.
// ---------------------------------------------------------------------------

namespace {
    Completion number_value_of(Evaluator&, const JSValue&, const std::vector<JSValue>&) {
        return Completion::normal(JSValue::number(7));
    }

    Completion string_to_string(Evaluator&, const JSValue&, const std::vector<JSValue>&) {
        return Completion::normal(JSValue::string("napis"));
    }

    JSObject non_primitive;   // cokolwiek, byle nie prymityw

    // zwraca OBIEKT — czyli nie prymityw, więc próbowana jest druga metoda
    Completion object_returning(Evaluator&, const JSValue&, const std::vector<JSValue>&) {
        return Completion::normal(JSValue::object(&non_primitive));
    }

    Completion throwing(Evaluator&, const JSValue&, const std::vector<JSValue>&) {
        return Completion::throw_value(JSValue::string("z valueOf"));
    }
}

// Obiekty w tych testach alokujemy NA STERCIE. Obiekt na stosie C++ trzyma
// właściwości w mapie poza stosem — GC tej mapy nie widzi, więc druga install()
// mogłaby zebrać funkcję zainstalowaną pierwszą (wychodzi to w PERUN_GC_STRESS).
static void install(Heap& heap, JSObject& object, const std::string& name, NativeFunction fn) {
    object.define_own_property(
        name, PropertyDescriptor::data(
            JSValue::object(heap.allocate<JSNativeFunction>(name, fn)), true, false, true));
}

TEST_CASE("obiekt bez valueOf i toString nie daje się skonwertować") {
    Heap heap; Evaluator evaluator(heap);
    JSObject& object = *heap.allocate<JSObject>();

    CHECK(evaluator.to_primitive(JSValue::object(&object), Hint::Number).type
          == COMPLETION_TYPE::THROW);
    CHECK(evaluator.to_number(JSValue::object(&object)).type == COMPLETION_TYPE::THROW);
    CHECK(evaluator.to_string(JSValue::object(&object)).type == COMPLETION_TYPE::THROW);
}

TEST_CASE("prymitywy przechodzą przez ToPrimitive bez zmian") {
    Heap heap; Evaluator evaluator(heap);

    Completion number = evaluator.to_primitive(JSValue::number(1), Hint::Number);
    CHECK(number.type == COMPLETION_TYPE::NORMAL);
    CHECK(number.get_value_or_undefined().to_number() == 1);

    Completion text = evaluator.to_primitive(JSValue::string("a"), Hint::String);
    CHECK(text.get_value_or_undefined().to_string() == "a");

    CHECK(evaluator.to_primitive(JSValue::undefined(), Hint::Default).type
          == COMPLETION_TYPE::NORMAL);
}

TEST_CASE("podpowiedź Number próbuje najpierw valueOf") {
    Heap heap; Evaluator evaluator(heap);

    JSObject& object = *heap.allocate<JSObject>();
    install(heap, object, "valueOf",  number_value_of);
    install(heap, object, "toString", string_to_string);

    Completion result = evaluator.to_primitive(JSValue::object(&object), Hint::Number);
    REQUIRE(result.type == COMPLETION_TYPE::NORMAL);
    CHECK(result.get_value_or_undefined().to_number() == 7);
}

TEST_CASE("podpowiedź String odwraca kolejność prób") {
    Heap heap; Evaluator evaluator(heap);

    JSObject& object = *heap.allocate<JSObject>();
    install(heap, object, "valueOf",  number_value_of);
    install(heap, object, "toString", string_to_string);

    Completion result = evaluator.to_primitive(JSValue::object(&object), Hint::String);
    REQUIRE(result.type == COMPLETION_TYPE::NORMAL);
    CHECK(result.get_value_or_undefined().to_string() == "napis");
}

TEST_CASE("podpowiedź domyślna zachowuje się jak Number") {
    Heap heap; Evaluator evaluator(heap);

    JSObject& object = *heap.allocate<JSObject>();
    install(heap, object, "valueOf",  number_value_of);
    install(heap, object, "toString", string_to_string);

    Completion result = evaluator.to_primitive(JSValue::object(&object), Hint::Default);
    CHECK(result.get_value_or_undefined().to_number() == 7);
}

TEST_CASE("metoda zwracająca obiekt jest pomijana, próbowana jest druga") {
    Heap heap; Evaluator evaluator(heap);

    JSObject& object = *heap.allocate<JSObject>();
    install(heap, object, "valueOf",  object_returning);     // nie daje prymitywu
    install(heap, object, "toString", string_to_string);

    Completion result = evaluator.to_primitive(JSValue::object(&object), Hint::Number);
    REQUIRE(result.type == COMPLETION_TYPE::NORMAL);
    CHECK(result.get_value_or_undefined().to_string() == "napis");
}

TEST_CASE("obie metody zwracające obiekt dają TypeError") {
    Heap heap; Evaluator evaluator(heap);

    JSObject& object = *heap.allocate<JSObject>();
    install(heap, object, "valueOf",  object_returning);
    install(heap, object, "toString", object_returning);

    CHECK(evaluator.to_primitive(JSValue::object(&object), Hint::Number).type
          == COMPLETION_TYPE::THROW);
}

TEST_CASE("rzucenie w valueOf kończy sprawę — toString NIE jest próbowane") {
    Heap heap; Evaluator evaluator(heap);

    JSObject& object = *heap.allocate<JSObject>();
    install(heap, object, "valueOf",  throwing);
    install(heap, object, "toString", string_to_string);

    Completion result = evaluator.to_primitive(JSValue::object(&object), Hint::Number);
    REQUIRE(result.type == COMPLETION_TYPE::THROW);
    // wyjątek z valueOf, a nie wynik toString
    CHECK(result.get_value_or_undefined().to_string() == "z valueOf");
}

TEST_CASE("niewywoływalne valueOf jest pomijane") {
    Heap heap; Evaluator evaluator(heap);

    JSObject& object = *heap.allocate<JSObject>();
    object.define_own_property("valueOf",
        PropertyDescriptor::data(JSValue::number(1), true, false, true));   // liczba, nie funkcja
    install(heap, object, "toString", string_to_string);

    Completion result = evaluator.to_primitive(JSValue::object(&object), Hint::Number);
    REQUIRE(result.type == COMPLETION_TYPE::NORMAL);
    CHECK(result.get_value_or_undefined().to_string() == "napis");
}

TEST_CASE("valueOf odziedziczone z prototypu też działa") {
    Heap heap; Evaluator evaluator(heap);

    JSObject& base = *heap.allocate<JSObject>();
    install(heap, base, "valueOf", number_value_of);

    JSObject& derived = *heap.allocate<JSObject>();
    derived.prototype = &base;

    Completion result = evaluator.to_primitive(JSValue::object(&derived), Hint::Number);
    REQUIRE(result.type == COMPLETION_TYPE::NORMAL);
    CHECK(result.get_value_or_undefined().to_number() == 7);
}

// --- konwersje wyższego poziomu --------------------------------------------

TEST_CASE("to_number i to_string na obiekcie przechodzą przez ToPrimitive") {
    Heap heap; Evaluator evaluator(heap);

    JSObject& object = *heap.allocate<JSObject>();
    install(heap, object, "valueOf",  number_value_of);
    install(heap, object, "toString", string_to_string);

    CHECK(evaluator.to_number(JSValue::object(&object)).get_value_or_undefined().to_number() == 7);
    CHECK(evaluator.to_string(JSValue::object(&object)).get_value_or_undefined().to_string() == "napis");
}

TEST_CASE("arytmetyka na obiekcie z valueOf") {
    Heap heap; Evaluator evaluator(heap);

    JSObject& object = *heap.allocate<JSObject>();
    install(heap, object, "valueOf", number_value_of);
    const JSValue boxed = JSValue::object(&object);

    // 7 + 1
    Completion sum = evaluator.apply_binary_operator(boxed, BinaryOperator::Add, JSValue::number(1));
    CHECK(sum.get_value_or_undefined().to_number() == 8);

    // 7 * 2
    Completion product = evaluator.apply_binary_operator(boxed, BinaryOperator::Mul, JSValue::number(2));
    CHECK(product.get_value_or_undefined().to_number() == 14);
}

TEST_CASE("'+' z obiektem, którego toString daje napis, skleja napisy") {
    Heap heap; Evaluator evaluator(heap);

    JSObject& object = *heap.allocate<JSObject>();
    install(heap, object, "toString", string_to_string);     // brak valueOf
    const JSValue boxed = JSValue::object(&object);

    Completion result = evaluator.apply_binary_operator(boxed, BinaryOperator::Add,
                                                        JSValue::string("!"));
    REQUIRE(result.type == COMPLETION_TYPE::NORMAL);
    CHECK(result.get_value_or_undefined().to_string() == "napis!");
}

TEST_CASE("wyjątek z valueOf propaguje się przez operator") {
    Heap heap; Evaluator evaluator(heap);

    JSObject& object = *heap.allocate<JSObject>();
    install(heap, object, "valueOf", throwing);
    const JSValue boxed = JSValue::object(&object);

    CHECK(evaluator.apply_binary_operator(boxed, BinaryOperator::Mul, JSValue::number(2)).type
          == COMPLETION_TYPE::THROW);
}
