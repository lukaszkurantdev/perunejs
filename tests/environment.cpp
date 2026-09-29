#include <doctest/doctest.h>

#include "evaluator/evaluator.h"
#include "memory/environment.h"
#include "memory/js_native_function.h"
#include "memory/js_object.h"
#include "runtime/globals.h"

#include <string>
#include <vector>

using namespace perunejs;

// ---------------------------------------------------------------------------
// Helpery
//
// Od czasu rozbicia Environment na rekord deklaratywny i obiektowy operacje,
// które mogą uruchomić kod użytkownika (getter, setter) albo rzucić wyjątek JS,
// biorą Evaluator& i zwracają Completion. Błędy nie są już wyjątkami C++.
//
// Druga zmiana: set_binding i get_binding_value dotyczą TYLKO tego rekordu.
// Przejście po łańcuchu outer to osobna operacja — find_environment (10.2.2.1).
// ---------------------------------------------------------------------------

namespace {
    struct Fixture {
        Heap heap;
        Evaluator evaluator{heap};

        // Potrzebne są tylko prototypy błędów, żeby throw_error miał czym rzucać.
        // Środowisko globalne rejestruje się samo w ewaluatorze.
        Fixture() { setup_globals(heap, evaluator); }
    };

    JSValue value_of(const Completion& completion) {
        REQUIRE(completion.type == COMPLETION_TYPE::NORMAL);
        return completion.get_value_or_undefined();
    }

    // Czy completion to rzut obiektem dziedziczącym po danym prototypie błędu?
    bool threw(const Completion& completion, const JSObject* prototype) {
        if (completion.type != COMPLETION_TYPE::THROW) return false;

        const JSValue value = completion.get_value_or_undefined();
        if (value.type() != JSValueType::Object) return false;

        for (const JSObject *object = value.as_object()->prototype;
             object != nullptr; object = object->prototype) {
            if (object == prototype) return true;
        }
        return false;
    }

    bool normal(const Completion& completion) { return completion.type == COMPLETION_TYPE::NORMAL; }

    Completion getter_42(Evaluator&, const JSValue&, const std::vector<JSValue>&) {
        return Completion::normal(JSValue::number(42));
    }

    double last_set_value = 0;
    Completion recording_setter(Evaluator&, const JSValue&, const std::vector<JSValue>& args) {
        last_set_value = args.empty() ? 0 : args[0].to_number();
        return Completion::normal(JSValue::undefined());
    }

    Completion throwing_getter(Evaluator&, const JSValue&, const std::vector<JSValue>&) {
        return Completion::throw_value(JSValue::string("z gettera"));
    }
}

// ===========================================================================
// Rekord deklaratywny (10.2.1.1)
// ===========================================================================

// ---------- HasBinding (10.2.1.1.1) ----------

TEST_CASE("has_binding widzi tylko istniejące nazwy") {
    Fixture f;
    DeclarativeEnvironment env;

    CHECK(env.has_binding("a") == false);

    env.create_mutable_binding(f.evaluator, "a", false);
    CHECK(env.has_binding("a") == true);
    CHECK(env.has_binding("b") == false);
}

// 10.2.1.1.2 — nowy binding jest inicjalizowany wartością undefined
TEST_CASE("create_mutable_binding tworzy binding o wartości undefined") {
    Fixture f;
    DeclarativeEnvironment env;

    CHECK(normal(env.create_mutable_binding(f.evaluator, "a", false)));

    CHECK(env.has_binding("a") == true);
    CHECK(value_of(env.get_binding_value(f.evaluator, "a", false)).type() == JSValueType::Undefined);
}

// ---------- SetMutableBinding (10.2.1.1.3) ----------

TEST_CASE("set_binding zmienia wartość istniejącego bindingu mutable") {
    Fixture f;
    DeclarativeEnvironment env;
    env.create_mutable_binding(f.evaluator, "a", false);

    CHECK(normal(env.set_binding(f.evaluator, "a", JSValue::number(1), false)));
    CHECK(value_of(env.get_binding_value(f.evaluator, "a", false)).to_number() == 1);

    CHECK(normal(env.set_binding(f.evaluator, "a", JSValue::string("txt"), false)));
    CHECK(value_of(env.get_binding_value(f.evaluator, "a", false)).to_string() == "txt");
}

// Warunek wstępny: binding musi już istnieć. Zapis pod nieistniejącą nazwą
// nie może po cichu tworzyć bindingu — niejawna zmienna globalna powstaje
// w PutValue (8.7.2), na obiekcie globalnym, a nie tutaj.
TEST_CASE("set_binding nie tworzy bindingu, gdy nazwy nie ma") {
    Fixture f;
    DeclarativeEnvironment env;

    CHECK(threw(env.set_binding(f.evaluator, "ghost", JSValue::number(1), false),
                f.evaluator.reference_error_prototype));
    CHECK(env.has_binding("ghost") == false);
}

// 10.2.1.1.3 krok 3 — poza strict zapis do immutable ma przepaść bez śladu
TEST_CASE("zapis do immutable poza strict jest cicho ignorowany") {
    Fixture f;
    DeclarativeEnvironment env;
    env.create_immutable_binding("g");
    env.initialize_immutable_binding("g", JSValue::number(2));

    CHECK(normal(env.set_binding(f.evaluator, "g", JSValue::number(99), false)));
    CHECK(value_of(env.get_binding_value(f.evaluator, "g", false)).to_number() == 2);
}

TEST_CASE("zapis do immutable w strict daje TypeError") {
    Fixture f;
    DeclarativeEnvironment env;
    env.create_immutable_binding("g");
    env.initialize_immutable_binding("g", JSValue::number(2));

    CHECK(threw(env.set_binding(f.evaluator, "g", JSValue::number(99), true),
                f.evaluator.type_error_prototype));
    CHECK(value_of(env.get_binding_value(f.evaluator, "g", false)).to_number() == 2);
}

// ---------- CreateImmutableBinding / InitializeImmutableBinding ----------
// (10.2.1.1.7, 10.2.1.1.8)

TEST_CASE("immutable binding powstaje niezainicjalizowany i daje się zainicjalizować raz") {
    Fixture f;
    DeclarativeEnvironment env;
    env.create_immutable_binding("g");
    CHECK(env.has_binding("g") == true);

    env.initialize_immutable_binding("g", JSValue::number(2));
    CHECK(value_of(env.get_binding_value(f.evaluator, "g", false)).to_number() == 2);
}

// Te dwa przypadki to błędy SILNIKA, nie JavaScriptu: ewaluator woła
// initialize_immutable_binding wyłącznie tuż po create_immutable_binding.
TEST_CASE("powtórna inicjalizacja immutable bindingu to błąd silnika") {
    Fixture f;
    DeclarativeEnvironment env;
    env.create_immutable_binding("g");
    env.initialize_immutable_binding("g", JSValue::number(2));

    CHECK_THROWS_AS(env.initialize_immutable_binding("g", JSValue::number(3)), std::logic_error);
    CHECK(value_of(env.get_binding_value(f.evaluator, "g", false)).to_number() == 2);
}

TEST_CASE("inicjalizacja nieistniejącego bindingu to błąd silnika") {
    DeclarativeEnvironment env;
    CHECK_THROWS_AS(env.initialize_immutable_binding("g", JSValue::number(1)), std::logic_error);
}

// ---------- GetBindingValue (10.2.1.1.4) ----------

// Krok 3.a: niezainicjalizowany immutable daje undefined poza strict,
// a ReferenceError w strict. To jedyne miejsce w ES5.1, w którym stan
// "niezainicjalizowany" jest w ogóle obserwowalny.
TEST_CASE("odczyt niezainicjalizowanego immutable zależy od trybu strict") {
    Fixture f;
    DeclarativeEnvironment env;
    env.create_immutable_binding("g");

    CHECK(value_of(env.get_binding_value(f.evaluator, "g", false)).type() == JSValueType::Undefined);
    CHECK(threw(env.get_binding_value(f.evaluator, "g", true), f.evaluator.reference_error_prototype));
}

TEST_CASE("odczyt nieistniejącej nazwy daje ReferenceError") {
    Fixture f;
    DeclarativeEnvironment env;

    CHECK(threw(env.get_binding_value(f.evaluator, "ghost", false), f.evaluator.reference_error_prototype));
    CHECK(threw(env.get_binding_value(f.evaluator, "ghost", true),  f.evaluator.reference_error_prototype));
}

// "binding istnieje z wartością undefined" i "bindingu nie ma" to dwa różne stany.
TEST_CASE("undefined jako wartość to nie to samo co brak bindingu") {
    Fixture f;
    DeclarativeEnvironment env;
    env.create_mutable_binding(f.evaluator, "a", false);

    CHECK(env.has_binding("a") == true);
    CHECK(value_of(env.get_binding_value(f.evaluator, "a", false)).type() == JSValueType::Undefined);

    CHECK(env.has_binding("b") == false);
    CHECK(threw(env.get_binding_value(f.evaluator, "b", false), f.evaluator.reference_error_prototype));
}

// ---------- DeleteBinding (10.2.1.1.5) ----------

// Krok 2 — usunięcie nieistniejącej nazwy to sukces: "delete" znaczy
// "dopilnuj, żeby tego nie było", a nie "usuń istniejące".
TEST_CASE("delete_binding nieistniejącej nazwy zwraca true") {
    DeclarativeEnvironment env;
    CHECK(env.delete_binding("ghost") == true);
}

TEST_CASE("delete_binding usuwa tylko bindingi oznaczone jako usuwalne") {
    Fixture f;
    DeclarativeEnvironment env;
    env.create_mutable_binding(f.evaluator, "trwaly", false);
    env.create_mutable_binding(f.evaluator, "ulotny", true);

    CHECK(env.delete_binding("trwaly") == false);
    CHECK(env.has_binding("trwaly")    == true);

    CHECK(env.delete_binding("ulotny") == true);
    CHECK(env.has_binding("ulotny")    == false);
}

// var-bindingi powstają z D = false (10.5), więc "var a = 1; delete a;" daje false.
TEST_CASE("immutable bindingi nie są usuwalne") {
    DeclarativeEnvironment env;
    env.create_immutable_binding("g");

    CHECK(env.delete_binding("g") == false);
    CHECK(env.has_binding("g")    == true);
}

// ---------- ImplicitThisValue (10.2.1.1.6) ----------

TEST_CASE("implicit_this_value dla rekordu deklaratywnego to zawsze undefined") {
    Fixture f;
    DeclarativeEnvironment env;
    CHECK(env.implicit_this_value().type() == JSValueType::Undefined);

    env.create_mutable_binding(f.evaluator, "a", false);
    env.set_binding(f.evaluator, "a", JSValue::number(1), false);
    CHECK(env.implicit_this_value().type() == JSValueType::Undefined);
}

// ---------- Niezależność środowisk ----------

TEST_CASE("bindingi nie przeciekają między środowiskami") {
    Fixture f;
    DeclarativeEnvironment outer_env;
    outer_env.create_mutable_binding(f.evaluator, "a", false);
    outer_env.set_binding(f.evaluator, "a", JSValue::number(1), false);

    DeclarativeEnvironment inner_env(&outer_env);

    // has_binding pyta WYŁĄCZNIE o własny rekord (10.2.1.1.1). Gdyby schodziło
    // po outer, hoisting nie utworzyłby lokalnego bindingu i przesłanianie
    // zmiennych przestałoby działać.
    CHECK(inner_env.has_binding("a") == false);

    inner_env.create_mutable_binding(f.evaluator, "a", false);
    inner_env.set_binding(f.evaluator, "a", JSValue::number(2), false);

    CHECK(value_of(inner_env.get_binding_value(f.evaluator, "a", false)).to_number() == 2);
    CHECK(value_of(outer_env.get_binding_value(f.evaluator, "a", false)).to_number() == 1);
}

// ===========================================================================
// Łańcuch środowisk — GetIdentifierReference (10.2.2.1)
//
// Rekord (HasBinding, SetMutableBinding, GetBindingValue, DeleteBinding)
// patrzy wyłącznie na siebie. Łańcuch przechodzi dopiero find_environment.
// ===========================================================================

TEST_CASE("find_environment schodzi po łańcuchu do rekordu, który ma nazwę") {
    Fixture f;
    DeclarativeEnvironment outer;
    outer.create_mutable_binding(f.evaluator, "a", false);

    DeclarativeEnvironment inner(&outer);

    CHECK(inner.find_environment("a") == &outer);
    CHECK(outer.find_environment("a") == &outer);
    CHECK(inner.find_environment("nieznana") == nullptr);
}

TEST_CASE("find_environment zwraca najbliższy rekord przy przesłonięciu") {
    Fixture f;
    DeclarativeEnvironment outer;
    outer.create_mutable_binding(f.evaluator, "a", false);

    DeclarativeEnvironment inner(&outer);
    inner.create_mutable_binding(f.evaluator, "a", false);

    CHECK(inner.find_environment("a") == &inner);
    CHECK(outer.find_environment("a") == &outer);
}

TEST_CASE("find_environment działa na łańcuchu głębszym niż dwa poziomy") {
    Fixture f;
    DeclarativeEnvironment global;
    global.create_mutable_binding(f.evaluator, "g", false);

    DeclarativeEnvironment middle(&global);
    middle.create_mutable_binding(f.evaluator, "m", false);

    DeclarativeEnvironment inner(&middle);
    inner.create_mutable_binding(f.evaluator, "i", false);

    CHECK(inner.find_environment("i") == &inner);
    CHECK(inner.find_environment("m") == &middle);
    CHECK(inner.find_environment("g") == &global);
    CHECK(middle.find_environment("i") == nullptr);
}

TEST_CASE("has_binding nie schodzi po łańcuchu") {
    Fixture f;
    DeclarativeEnvironment outer;
    outer.create_mutable_binding(f.evaluator, "a", false);

    DeclarativeEnvironment inner(&outer);

    CHECK(inner.has_binding("a") == false);
    CHECK(inner.find_environment("a") == &outer);
}

TEST_CASE("delete_binding nie schodzi po łańcuchu") {
    Fixture f;
    DeclarativeEnvironment outer;
    outer.create_mutable_binding(f.evaluator, "a", true);

    DeclarativeEnvironment inner(&outer);

    // Brak nazwy w tym rekordzie => "nie ma czego usuwać", czyli true.
    CHECK(inner.delete_binding("a") == true);
    CHECK(outer.has_binding("a") == true);
}

// Ta para testów pilnuje nowego podziału obowiązków: sam rekord nie zagląda
// do outer, a pełne rozstrzygnięcie nazwy to find_environment + operacja.

TEST_CASE("odczyt i zapis bez find_environment dotyczą tylko własnego rekordu") {
    Fixture f;
    DeclarativeEnvironment outer;
    outer.create_mutable_binding(f.evaluator, "a", false);
    outer.set_binding(f.evaluator, "a", JSValue::number(1), false);

    DeclarativeEnvironment inner(&outer);

    CHECK(threw(inner.get_binding_value(f.evaluator, "a", false), f.evaluator.reference_error_prototype));
    CHECK(threw(inner.set_binding(f.evaluator, "a", JSValue::number(5), false),
                f.evaluator.reference_error_prototype));
    CHECK(value_of(outer.get_binding_value(f.evaluator, "a", false)).to_number() == 1);
}

TEST_CASE("rozstrzygnięcie nazwy przez łańcuch czyta i zapisuje właściwy rekord") {
    Fixture f;
    DeclarativeEnvironment outer;
    outer.create_mutable_binding(f.evaluator, "a", false);
    outer.set_binding(f.evaluator, "a", JSValue::number(1), false);

    DeclarativeEnvironment inner(&outer);

    Environment *found = inner.find_environment("a");
    REQUIRE(found == &outer);

    CHECK(value_of(found->get_binding_value(f.evaluator, "a", false)).to_number() == 1);
    CHECK(normal(found->set_binding(f.evaluator, "a", JSValue::number(5), false)));

    CHECK(value_of(outer.get_binding_value(f.evaluator, "a", false)).to_number() == 5);
    CHECK(inner.has_binding("a") == false);   // zapis nie tworzy kopii bindingu
}

TEST_CASE("przy przesłonięciu łańcuch wybiera rekord wewnętrzny") {
    Fixture f;
    DeclarativeEnvironment outer;
    outer.create_mutable_binding(f.evaluator, "a", false);
    outer.set_binding(f.evaluator, "a", JSValue::number(1), false);

    DeclarativeEnvironment inner(&outer);
    inner.create_mutable_binding(f.evaluator, "a", false);
    inner.set_binding(f.evaluator, "a", JSValue::number(2), false);

    CHECK(inner.find_environment("a") == &inner);
    CHECK(value_of(inner.get_binding_value(f.evaluator, "a", false)).to_number() == 2);
    CHECK(value_of(outer.get_binding_value(f.evaluator, "a", false)).to_number() == 1);
}

TEST_CASE("nazwa nieobecna w całym łańcuchu nie rozstrzyga się i niczego nie tworzy") {
    Fixture f;
    DeclarativeEnvironment outer;
    DeclarativeEnvironment inner(&outer);

    CHECK(inner.find_environment("ghost") == nullptr);
    CHECK(inner.has_binding("ghost") == false);
    CHECK(outer.has_binding("ghost") == false);
}

TEST_CASE("zapis przez łańcuch do immutable respektuje tryb strict") {
    Fixture f;
    DeclarativeEnvironment outer;
    outer.create_immutable_binding("g");
    outer.initialize_immutable_binding("g", JSValue::number(7));

    DeclarativeEnvironment inner(&outer);
    Environment *found = inner.find_environment("g");
    REQUIRE(found == &outer);

    CHECK(normal(found->set_binding(f.evaluator, "g", JSValue::number(99), false)));
    CHECK(value_of(outer.get_binding_value(f.evaluator, "g", false)).to_number() == 7);

    CHECK(threw(found->set_binding(f.evaluator, "g", JSValue::number(99), true),
                f.evaluator.type_error_prototype));
    CHECK(value_of(outer.get_binding_value(f.evaluator, "g", false)).to_number() == 7);
}

// ===========================================================================
// Rekord obiektowy (10.2.1.2)
//
// Bindingami są właściwości obiektu. Używa go obiekt globalny (C.2)
// i `with` (C.5) — stąd flaga provide_this.
// ===========================================================================

TEST_CASE("binding rekordu obiektowego to właściwość obiektu") {
    Fixture f;
    JSObject *target = f.heap.allocate<JSObject>();
    ObjectEnvironment env(target);

    CHECK(env.has_binding("a") == false);

    CHECK(normal(env.create_mutable_binding(f.evaluator, "a", false)));
    CHECK(env.has_binding("a") == true);
    CHECK(target->get_own_property("a") != nullptr);

    CHECK(normal(env.set_binding(f.evaluator, "a", JSValue::number(1), false)));
    CHECK(value_of(env.get_binding_value(f.evaluator, "a", false)).to_number() == 1);
    CHECK(target->get_own_property("a")->get_value().to_number() == 1);
}

// Właściwość ustawiona bezpośrednio na obiekcie jest widoczna jako nazwa —
// to dlatego po C.2 `Object` i `print` będą po prostu właściwościami globalu.
TEST_CASE("właściwość dodana z zewnątrz jest od razu bindingiem") {
    Fixture f;
    JSObject *target = f.heap.allocate<JSObject>();
    ObjectEnvironment env(target);

    target->define_own_property("x", PropertyDescriptor::data(JSValue::number(5), true, true, true));

    CHECK(env.has_binding("x") == true);
    CHECK(value_of(env.get_binding_value(f.evaluator, "x", false)).to_number() == 5);
}

// 10.2.1.2.1 — HasBinding używa [[HasProperty]], czyli CAŁEGO łańcucha prototypów.
TEST_CASE("has_binding rekordu obiektowego widzi właściwości z prototypu") {
    Fixture f;
    JSObject *base = f.heap.allocate<JSObject>();
    base->define_own_property("odziedziczona", PropertyDescriptor::data(JSValue::number(7), true, true, true));

    JSObject *target = f.heap.allocate<JSObject>();
    target->prototype = base;

    ObjectEnvironment env(target);

    CHECK(env.has_binding("odziedziczona") == true);
    CHECK(value_of(env.get_binding_value(f.evaluator, "odziedziczona", false)).to_number() == 7);
    CHECK(target->get_own_property("odziedziczona") == nullptr);
}

TEST_CASE("usuwalność bindingu to konfigurowalność właściwości") {
    Fixture f;
    JSObject *target = f.heap.allocate<JSObject>();
    ObjectEnvironment env(target);

    env.create_mutable_binding(f.evaluator, "trwaly", false);
    env.create_mutable_binding(f.evaluator, "ulotny", true);

    CHECK(env.delete_binding("trwaly") == false);
    CHECK(env.has_binding("trwaly") == true);

    CHECK(env.delete_binding("ulotny") == true);
    CHECK(env.has_binding("ulotny") == false);
}

TEST_CASE("odczyt bindingu wywołuje getter") {
    Fixture f;
    JSObject *target = f.heap.allocate<JSObject>();
    target->define_own_property("x", PropertyDescriptor::accessor(
        f.heap.allocate<JSNativeFunction>("get x", getter_42), nullptr, true, true));

    ObjectEnvironment env(target);

    CHECK(env.has_binding("x") == true);
    CHECK(value_of(env.get_binding_value(f.evaluator, "x", false)).to_number() == 42);
}

TEST_CASE("zapis bindingu wywołuje setter") {
    Fixture f;
    JSObject *target = f.heap.allocate<JSObject>();
    target->define_own_property("x", PropertyDescriptor::accessor(
        nullptr, f.heap.allocate<JSNativeFunction>("set x", recording_setter), true, true));

    ObjectEnvironment env(target);

    last_set_value = 0;
    CHECK(normal(env.set_binding(f.evaluator, "x", JSValue::number(11), false)));
    CHECK(last_set_value == 11);
}

// To jest powód, dla którego get_binding_value musiało zacząć zwracać Completion.
TEST_CASE("wyjątek z gettera wraca jako THROW, a nie wyjątek C++") {
    Fixture f;
    JSObject *target = f.heap.allocate<JSObject>();
    target->define_own_property("x", PropertyDescriptor::accessor(
        f.heap.allocate<JSNativeFunction>("get x", throwing_getter), nullptr, true, true));

    ObjectEnvironment env(target);

    Completion result = env.get_binding_value(f.evaluator, "x", false);
    REQUIRE(result.type == COMPLETION_TYPE::THROW);
    CHECK(result.get_value_or_undefined().to_string() == "z gettera");
}

TEST_CASE("zapis do właściwości tylko do odczytu zależy od trybu strict") {
    Fixture f;
    JSObject *target = f.heap.allocate<JSObject>();
    target->define_own_property("stala", PropertyDescriptor::data(JSValue::number(1), false, true, true));

    ObjectEnvironment env(target);

    CHECK(normal(env.set_binding(f.evaluator, "stala", JSValue::number(2), false)));
    CHECK(value_of(env.get_binding_value(f.evaluator, "stala", false)).to_number() == 1);

    CHECK(threw(env.set_binding(f.evaluator, "stala", JSValue::number(2), true),
                f.evaluator.type_error_prototype));
}

// 10.2.1.2.4 — właściwość mogła zniknąć między rozstrzygnięciem nazwy a odczytem.
TEST_CASE("odczyt nieistniejącej nazwy: undefined poza strict, ReferenceError w strict") {
    Fixture f;
    JSObject *target = f.heap.allocate<JSObject>();
    ObjectEnvironment env(target);

    CHECK(value_of(env.get_binding_value(f.evaluator, "ghost", false)).type() == JSValueType::Undefined);
    CHECK(threw(env.get_binding_value(f.evaluator, "ghost", true), f.evaluator.reference_error_prototype));
}

// 10.2.1.2.6 — tylko `with` podstawia swój obiekt jako this.
TEST_CASE("implicit_this_value zależy od provide_this") {
    Fixture f;
    JSObject *target = f.heap.allocate<JSObject>();

    ObjectEnvironment as_global(target, nullptr, false);
    CHECK(as_global.implicit_this_value().type() == JSValueType::Undefined);

    ObjectEnvironment as_with(target, nullptr, true);
    CHECK(as_with.implicit_this_value().type() == JSValueType::Object);
    CHECK(as_with.implicit_this_value().as_object() == target);
}

TEST_CASE("łańcuch może mieszać rekordy deklaratywne i obiektowe") {
    Fixture f;
    JSObject *target = f.heap.allocate<JSObject>();
    target->define_own_property("g", PropertyDescriptor::data(JSValue::number(1), true, true, true));

    ObjectEnvironment object_env(target);
    DeclarativeEnvironment inner(&object_env);
    inner.create_mutable_binding(f.evaluator, "lokalna", false);

    CHECK(inner.find_environment("lokalna") == &inner);
    CHECK(inner.find_environment("g") == &object_env);
    CHECK(inner.find_environment("ghost") == nullptr);
}

TEST_CASE("GC: rekord obiektowy utrzymuje swój obiekt przy życiu") {
    // Przy skanowaniu konserwatywnym obiekt i tak zwykle przeżyje, bo wskaźnik
    // zostaje na stosie. Test jest rozstrzygający dopiero pod ASanem.
    Fixture f;
    auto *env = f.heap.allocate<ObjectEnvironment>(f.heap.allocate<JSObject>(), nullptr, false);
    env->create_mutable_binding(f.evaluator, "a", false);
    env->set_binding(f.evaluator, "a", JSValue::number(1), false);

    f.evaluator.root_environments.push_back(env);
    f.heap.collect_garbage();
    f.evaluator.root_environments.pop_back();

    CHECK(value_of(env->get_binding_value(f.evaluator, "a", false)).to_number() == 1);
}
