#include <doctest/doctest.h>

#include "lexer/lexer.h"
#include "utils/utf.h"

#include <string>
#include <variant>
#include <vector>

using namespace perunejs;

static std::vector<TOKEN_TYPE> types_of(const std::string& src) {
    Lexer lexer(src);
    std::vector<TOKEN_TYPE> out;
    for (const Token* t : lexer.scan_tokens()) out.push_back(t->type);
    return out;
}

static std::vector<uint32_t> starts_of(const std::string& src) {
    Lexer lexer(src);
    std::vector<uint32_t> out;
    for (const Token* t : lexer.scan_tokens()) out.push_back(t->start);
    return out;
}

static double number_of(const std::string& src) {
    Lexer lexer(src);
    return std::get<double>(lexer.scan_tokens().at(0)->value);
}

// Identyfikatory i wzorce regexpów są w UTF-8, literały łańcuchowe w UTF-16.
static std::string text_of(const std::string& src) {
    Lexer lexer(src);
    const auto& value = lexer.scan_tokens().at(0)->value;

    if (std::holds_alternative<std::u16string>(value)) {
        return utf16_to_utf8(std::get<std::u16string>(value));
    }

    return std::get<std::string>(value);
}

static bool single(const std::string& src, TOKEN_TYPE expected) {
    return types_of(src) == std::vector<TOKEN_TYPE>{expected, END_OF_FILE};
}

TEST_CASE("one sign lexems") {
    CHECK(single("(", LPAREN));    CHECK(single(")", RPAREN));
    CHECK(single("{", LBRACE));    CHECK(single("}", RBRACE));
    CHECK(single("[", LBRACK));    CHECK(single("]", RBRACK));
    CHECK(single(";", SEMICOLON)); CHECK(single(",", COMMA));
    CHECK(single(":", COLON));     CHECK(single(".", PERIOD));
    CHECK(single("~", BIT_NOT));   CHECK(single("@", AT));
    CHECK(single("`", TEMPLATE));
    CHECK(single("+", ADD));       CHECK(single("-", SUB));
    CHECK(single("*", MUL));
    CHECK(single("%", MOD));       CHECK(single("!", NOT));
    CHECK(single("<", LT));        CHECK(single(">", GT));
    CHECK(single("=", ASSIGN));    CHECK(single("?", CONDITIONAL));
    CHECK(single("&", BIT_AND));   CHECK(single("|", BIT_OR));
    CHECK(single("^", BIT_XOR));
}

TEST_CASE("aritmetic operators") {
    CHECK(single("**",  EXP));
    CHECK(single("**=", ASSIGN_EXP));
    CHECK(single("+=",  ASSIGN_ADD));
    CHECK(single("-=",  ASSIGN_SUB));
    CHECK(single("*=",  ASSIGN_MUL));
    CHECK(single("%=",  ASSIGN_MOD));
    CHECK(single("++",  INCREMENT));
    CHECK(single("--",  DECREMENT));
}

TEST_CASE("div with operator context") {
    CHECK(types_of("a / b")  == std::vector<TOKEN_TYPE>{IDENTIFIER, DIV, IDENTIFIER, END_OF_FILE});
    CHECK(types_of("1 / 2")  == std::vector<TOKEN_TYPE>{NUMBER, DIV, NUMBER, END_OF_FILE});
    CHECK(types_of("a /= b") == std::vector<TOKEN_TYPE>{IDENTIFIER, ASSIGN_DIV, IDENTIFIER, END_OF_FILE});
    CHECK(types_of("(1)/2")  == std::vector<TOKEN_TYPE>{LPAREN, NUMBER, RPAREN, DIV, NUMBER, END_OF_FILE});
}

TEST_CASE("comparison operators") {
    CHECK(single("==",  EQ));
    CHECK(single("===", EQ_STRICT));
    CHECK(single("!=",  NE));
    CHECK(single("!==", NE_STRICT));
    CHECK(single("<=",  LTE));      // uwaga: sprawdź komentarze w enumie!
    CHECK(single(">=",  GTE));
}

TEST_CASE("binary operators") {
    CHECK(single("<<",   SHL));
    CHECK(single("<<=",  ASSIGN_SHL));
    CHECK(single(">>",   SAR));
    CHECK(single(">>=",  ASSIGN_SAR));
    CHECK(single(">>>",  SHR));
    CHECK(single(">>>=", ASSIGN_SHR));
}

TEST_CASE("binary and logical operators") {
    CHECK(single("&&",  AND));
    CHECK(single("&&=", ASSIGN_AND));
    CHECK(single("&=",  ASSIGN_BIT_AND));
    CHECK(single("||",  OR));
    CHECK(single("||=", ASSIGN_OR));
    CHECK(single("|=",  ASSIGN_BIT_OR));
    CHECK(single("^=",  ASSIGN_BIT_XOR));
    CHECK(single("??",  NULLISH));
    CHECK(single("??=", ASSIGN_NULLISH));
}

TEST_CASE("other operators") {
    CHECK(single("=>",  ARROW));
    CHECK(single("?.",  OPTIONAL));
    CHECK(single("...", ELLIPSIS));
}

TEST_CASE("longest boundaries") {
    CHECK(types_of("+ +")  == std::vector<TOKEN_TYPE>{ADD, ADD, END_OF_FILE});
    CHECK(types_of("++")   == std::vector<TOKEN_TYPE>{INCREMENT, END_OF_FILE});
    CHECK(types_of("+++")  == std::vector<TOKEN_TYPE>{INCREMENT, ADD, END_OF_FILE});
    CHECK(types_of("====") == std::vector<TOKEN_TYPE>{EQ_STRICT, ASSIGN, END_OF_FILE});
    CHECK(types_of(">>>>") == std::vector<TOKEN_TYPE>{SHR, GT, END_OF_FILE});
    CHECK(types_of("..")   == std::vector<TOKEN_TYPE>{PERIOD, PERIOD, END_OF_FILE});
}

TEST_CASE("number values") {
    CHECK(number_of("0")    == 0.0);
    CHECK(number_of("1")    == 1.0);
    CHECK(number_of("42")   == 42.0);
    CHECK(number_of("3.14") == doctest::Approx(3.14));
    CHECK(number_of("100")  == 100.0);
}

TEST_CASE("number end values") {
    CHECK(types_of("1")     == std::vector<TOKEN_TYPE>{NUMBER, END_OF_FILE});
    CHECK(types_of("1 ")    == std::vector<TOKEN_TYPE>{NUMBER, END_OF_FILE});
    CHECK(types_of("1;")    == std::vector<TOKEN_TYPE>{NUMBER, SEMICOLON, END_OF_FILE});
    CHECK(types_of("(1)")   == std::vector<TOKEN_TYPE>{LPAREN, NUMBER, RPAREN, END_OF_FILE});
    CHECK(types_of("1+2")   == std::vector<TOKEN_TYPE>{NUMBER, ADD, NUMBER, END_OF_FILE});
    CHECK(types_of("(1 + 2)") == std::vector<TOKEN_TYPE>{
        LPAREN, NUMBER, ADD, NUMBER, RPAREN, END_OF_FILE});
}

TEST_CASE("identifiers") {
    CHECK(single("abc",   IDENTIFIER));
    CHECK(single("_x",    IDENTIFIER));
    CHECK(single("$x",    IDENTIFIER));
    CHECK(single("a1",    IDENTIFIER));
    CHECK(text_of("abc")  == "abc");
    CHECK(text_of("_foo") == "_foo");
}

TEST_CASE("keywords") {
    CHECK(single("variable", IDENTIFIER));
    CHECK(single("iffy",     IDENTIFIER));
    CHECK(single("forEach",  IDENTIFIER));
    CHECK(single("news",     IDENTIFIER));
    CHECK(text_of("variable") == "variable");
}

TEST_CASE("keywords all") {
    CHECK(single("var", VAR));           CHECK(single("let", LET));
    CHECK(single("const", CONST));       CHECK(single("if", IF));
    CHECK(single("else", ELSE));         CHECK(single("while", WHILE));
    CHECK(single("do", DO));             CHECK(single("for", FOR));
    CHECK(single("break", BREAK));       CHECK(single("continue", CONTINUE));
    CHECK(single("switch", SWITCH));     CHECK(single("case", CASE));
    CHECK(single("default", DEFAULT));   CHECK(single("function", FUNCTION));
    CHECK(single("return", RETURN));     CHECK(single("try", TRY));
    CHECK(single("catch", CATCH));       CHECK(single("finally", FINALLY));
    CHECK(single("throw", THROW));       CHECK(single("new", NEW));
    CHECK(single("delete", DELETE));     CHECK(single("typeof", TYPEOF));
    CHECK(single("void", VOID));         CHECK(single("in", IN));
    CHECK(single("instanceof", INSTANCE_OF));
    CHECK(single("this", THIS));         CHECK(single("null", NULL_T));
    CHECK(single("true", TRUE));         CHECK(single("false", FALSE));
    CHECK(single("class", CLASS));       CHECK(single("extends", EXTENDS));
    CHECK(single("super", SUPER));       CHECK(single("import", IMPORT));
    CHECK(single("export", EXPORT));     CHECK(single("enum", ENUM));
    CHECK(single("async", ASYNC));       CHECK(single("await", AWAIT));
    CHECK(single("yield", YIELD));       CHECK(single("with", WITH));
    CHECK(single("debugger", DEBUGGER));
}

TEST_CASE("strings") {
    CHECK(single("'abc'",  STRING));
    CHECK(single("\"abc\"", STRING));
    CHECK(text_of("'abc'")   == "abc");
    CHECK(text_of("\"abc\"") == "abc");
    CHECK(text_of("''")      == "");
    CHECK(text_of("'a b'")   == "a b");
    CHECK(text_of("'he said \"x\"'") == "he said \"x\"");
}

TEST_CASE("white characters") {
    CHECK(types_of("")       == std::vector<TOKEN_TYPE>{END_OF_FILE});
    CHECK(types_of("   ")    == std::vector<TOKEN_TYPE>{END_OF_FILE});
    CHECK(types_of("\n\t\r") == std::vector<TOKEN_TYPE>{END_OF_FILE});
}

TEST_CASE("comments") {
    CHECK(types_of("// komentarz\n1") == std::vector<TOKEN_TYPE>{NUMBER, END_OF_FILE});
    CHECK(types_of("1 // komentarz\n") == std::vector<TOKEN_TYPE>{NUMBER, END_OF_FILE});
    CHECK(types_of("/* komentarz */ 1") == std::vector<TOKEN_TYPE>{NUMBER, END_OF_FILE});
    CHECK(types_of("1 /* a */ + /* b */ 2") ==
          std::vector<TOKEN_TYPE>{NUMBER, ADD, NUMBER, END_OF_FILE});
    CHECK(types_of("/* wielo\nliniowy */ 1") == std::vector<TOKEN_TYPE>{NUMBER, END_OF_FILE});
    CHECK(types_of("/* gwiazdka * w środku */ 1") ==
          std::vector<TOKEN_TYPE>{NUMBER, END_OF_FILE});
}

TEST_CASE("lexems positions") {
    CHECK(starts_of("1 + 2") == std::vector<uint32_t>{0, 2, 4, 5});
    CHECK(starts_of("ab cd") == std::vector<uint32_t>{0, 3, 5});
    CHECK(starts_of("(1)")   == std::vector<uint32_t>{0, 1, 2, 3});
}

TEST_CASE("real code") {
    CHECK(types_of("var x = 1 + 2;") == std::vector<TOKEN_TYPE>{
        VAR, IDENTIFIER, ASSIGN, NUMBER, ADD, NUMBER, SEMICOLON, END_OF_FILE});

    CHECK(types_of("if (a === b) { c(); }") == std::vector<TOKEN_TYPE>{
        IF, LPAREN, IDENTIFIER, EQ_STRICT, IDENTIFIER, RPAREN,
        LBRACE, IDENTIFIER, LPAREN, RPAREN, SEMICOLON, RBRACE, END_OF_FILE});

    CHECK(types_of("while (i < 10) i++;") == std::vector<TOKEN_TYPE>{
        WHILE, LPAREN, IDENTIFIER, LT, NUMBER, RPAREN,
        IDENTIFIER, INCREMENT, SEMICOLON, END_OF_FILE});
}

TEST_CASE("regular expressions") {
    CHECK(single("/ab/", REGEXP));
    CHECK(single("/ab/gi", REGEXP));
    CHECK(single("/[/]/", REGEXP));
    CHECK(single("/a\\/b/", REGEXP));
    CHECK(single("/=/", REGEXP));

    CHECK(types_of("a / b")     == std::vector<TOKEN_TYPE>{IDENTIFIER, DIV, IDENTIFIER, END_OF_FILE});
    CHECK(types_of("a /= b")    == std::vector<TOKEN_TYPE>{IDENTIFIER, ASSIGN_DIV, IDENTIFIER, END_OF_FILE});
    CHECK(types_of("return /a/") == std::vector<TOKEN_TYPE>{RETURN, REGEXP, END_OF_FILE});
    CHECK(types_of("x = /a/")   == std::vector<TOKEN_TYPE>{IDENTIFIER, ASSIGN, REGEXP, END_OF_FILE});
    CHECK(types_of("(1)/2")     == std::vector<TOKEN_TYPE>{LPAREN, NUMBER, RPAREN, DIV, NUMBER, END_OF_FILE});
    CHECK(types_of("// comment") == std::vector<TOKEN_TYPE>{END_OF_FILE});

    CHECK_THROWS(types_of("/ab"));
    CHECK_THROWS(types_of("/ab/gg"));
    CHECK_THROWS(types_of("/a\nb/"));
}
// ---------------------------------------------------------------------------
// Literały liczbowe (7.8.3)
// ---------------------------------------------------------------------------

TEST_CASE("literały szesnastkowe") {
    CHECK(number_of("0xFF")       == 255);
    CHECK(number_of("0X10")       == 16);
    CHECK(number_of("0x0")        == 0);
    CHECK(number_of("0xabcdef")   == 11259375);
    CHECK(number_of("0xABCDEF")   == 11259375);
    CHECK(single("0xFF", NUMBER));   // jeden token, nie NUMBER + IDENTIFIER
}

TEST_CASE("notacja wykładnicza") {
    CHECK(number_of("1e3")     == 1000);
    CHECK(number_of("1E3")     == 1000);
    CHECK(number_of("1e+2")    == 100);
    CHECK(number_of("1.5e-2")  == doctest::Approx(0.015));
    CHECK(number_of("5e0")     == 5);
    CHECK(single("1e3", NUMBER));
}

// 7.8.3 druga produkcja DecimalLiteral: ". DecimalDigits"
TEST_CASE("literał zaczynający się kropką") {
    CHECK(number_of(".5")    == 0.5);
    CHECK(number_of(".25")   == 0.25);
    CHECK(number_of(".5e1")  == 5);
    CHECK(single(".5", NUMBER));   // nie PERIOD + NUMBER
}

TEST_CASE("kropka poza literałem pozostaje operatorem") {
    CHECK(single(".", PERIOD));
    CHECK(types_of("a.b") == std::vector<TOKEN_TYPE>{IDENTIFIER, PERIOD, IDENTIFIER, END_OF_FILE});
    CHECK(types_of("...") == std::vector<TOKEN_TYPE>{ELLIPSIS, END_OF_FILE});
}

TEST_CASE("niekompletne literały liczbowe są błędem") {
    CHECK_THROWS_AS(types_of("0x"),  std::runtime_error);
    CHECK_THROWS_AS(types_of("1e"),  std::runtime_error);
    CHECK_THROWS_AS(types_of("1e+"), std::runtime_error);
}

// 7.8.3: znak bezpośrednio po liczbie nie może być początkiem identyfikatora
// ani cyfrą - dlatego "3in" to błąd, a nie NUMBER + IN.
TEST_CASE("po literale liczbowym nie może stać identyfikator") {
    CHECK_THROWS_AS(types_of("3in"),  std::runtime_error);
    CHECK_THROWS_AS(types_of("3abc"), std::runtime_error);
    CHECK_THROWS_AS(types_of("0xFFg"), std::runtime_error);
    // ale operator i spacja są w porządku
    CHECK(types_of("3+4") == std::vector<TOKEN_TYPE>{NUMBER, ADD, NUMBER, END_OF_FILE});
    CHECK(types_of("3 in") == std::vector<TOKEN_TYPE>{NUMBER, IN, END_OF_FILE});
}

// ---------------------------------------------------------------------------
// Sekwencje ucieczki w napisach (7.8.4)
// ---------------------------------------------------------------------------

TEST_CASE("sekwencje ucieczki z tablicy 4") {
    CHECK(text_of("\"a\\nb\"")  == "a\nb");
    CHECK(text_of("\"a\\tb\"")  == "a\tb");
    CHECK(text_of("\"a\\rb\"")  == "a\rb");
    CHECK(text_of("\"\\b\"")    == "\b");
    CHECK(text_of("\"\\f\"")    == "\f");
    CHECK(text_of("\"\\v\"")    == "\v");
    CHECK(text_of("\"a\\nb\"").size() == 3);   // trzy znaki, nie cztery
}

TEST_CASE("ucieczka cudzysłowu i ukośnika") {
    CHECK(text_of("\"a\\\"b\"") == "a\"b");
    CHECK(text_of("'a\\'b'")    == "a'b");
    CHECK(text_of("\"a\\\\b\"") == "a\\b");
    CHECK(text_of("\"a\\\\b\"").size() == 3);
}

TEST_CASE("ucieczki szesnastkowe") {
    CHECK(text_of("\"\\x41\"")   == "A");
    CHECK(text_of("\"\\u0041\"") == "A");
    CHECK(text_of("\"\\x41\\x42\"") == "AB");
    CHECK(text_of("\"\\u0041\"").size() == 1);
}

TEST_CASE("niekompletne ucieczki szesnastkowe są błędem") {
    CHECK_THROWS_AS(text_of("\"\\x4\""),    std::runtime_error);
    CHECK_THROWS_AS(text_of("\"\\u041\""),  std::runtime_error);
    CHECK_THROWS_AS(text_of("\"\\xZZ\""),   std::runtime_error);
}

// LineContinuation - jedyna sekwencja dająca ZERO znaków
TEST_CASE("ukośnik przed nową linią to kontynuacja wiersza") {
    CHECK(text_of("\"a\\\nb\"")       == "ab");
    CHECK(text_of("\"a\\\nb\"").size() == 2);
}

TEST_CASE("goła nowa linia w napisie jest błędem") {
    CHECK_THROWS_AS(text_of("\"a\nb\""), std::runtime_error);
    CHECK_THROWS_AS(text_of("\"abc"),    std::runtime_error);
}

// NonEscapeCharacter (7.8.4): nieznana sekwencja daje sam znak
TEST_CASE("nieznana sekwencja ucieczki daje sam znak") {
    CHECK(text_of("\"a\\qb\"") == "aqb");
    CHECK(text_of("\"\\a\"")   == "a");
}

TEST_CASE("ucieczka zerowa daje znak NUL") {
    CHECK(text_of("\"\\0\"").size() == 1);
    CHECK(text_of("\"\\0\"")[0]     == '\0');
}

TEST_CASE("ucieczki ósemkowe z Annex B.1.2") {
    CHECK(text_of("\"\\01\"")[0]   == 1);
    CHECK(text_of("\"\\101\"")     == "A");
    CHECK(text_of("\"\\101\\102\\103\"") == "ABC");

    // Pierwsza cyfra powyżej 3 dopuszcza tylko dwie cyfry, bo 0777 nie
    // zmieściłoby się w bajcie: \501 to \50 (znak 0x28) i zwykła jedynka.
    CHECK(text_of("\"\\501\"") == "(1");
    // text_of oddaje UTF-8, a 0xFF zapisuje się tam na dwóch bajtach.
    CHECK(text_of("\"\\377\"") == "\u00FF");

    // Ósemka i dziewiątka nie są cyframi ósemkowymi.
    CHECK(text_of("\"\\8\"") == "8");
    CHECK(text_of("\"\\09\"") == std::string("\0", 1) + "9");
}

// ---------------------------------------------------------------------------
// Literały łańcuchowe w UTF-16 (8.4, 7.8.4)
// ---------------------------------------------------------------------------

TEST_CASE("escape \\u przyjmuje dowolną jednostkę, nie tylko ASCII") {
    CHECK(text_of("'\\u0041'")       == "A");
    CHECK(text_of("'\\u0041\\u0142'") == "Ał");
    CHECK(text_of("'\\x41'")         == "A");
    CHECK(text_of("'\\u00e4'")       == "ä");
}

TEST_CASE("znak spoza BMP to para surogatów") {
    // Zapisany wprost i przez escape'y daje ten sam łańcuch.
    CHECK(text_of("'\\uD83D\\uDE00'") == text_of("'😀'"));
}

TEST_CASE("znaki spoza ASCII w źródle trafiają do literału") {
    CHECK(text_of("'ąćę'") == "ąćę");
}

// ---------------------------------------------------------------------------
// 7.6 Identyfikatory spoza ASCII
// ---------------------------------------------------------------------------

TEST_CASE("identyfikator może być zbudowany z liter Unicode") {
    CHECK(text_of("ą")      == "ą");
    CHECK(text_of("żółw")   == "żółw");
    CHECK(text_of("Ω")      == "Ω");
    CHECK(text_of("привет") == "привет");
    CHECK(text_of("日本")    == "日本");
    CHECK(text_of("ᾀ")      == "ᾀ");

    CHECK(single("ą", IDENTIFIER));
    CHECK(single("_$a1", IDENTIFIER));

    // Cyfra nie może zaczynać identyfikatora, ale może w nim wystąpić dalej.
    CHECK(text_of("a\u0301b") == "a\u0301b");   // znak łączący, kategoria Mn

    // ZWNJ i ZWJ są w kategorii Cf, więc gramatyka wymienia je osobno.
    CHECK(text_of("a\u200Cb") == "a\u200Cb");
    CHECK(text_of("a\u200Db") == "a\u200Db");
    CHECK_THROWS_AS(types_of("\u200Ca"), std::runtime_error);
}

TEST_CASE("znak spoza BMP nie jest literą w ES5.1") {
    // Kod źródłowy to ciąg JEDNOSTEK UTF-16, a pojedynczy surogat nie jest literą.
    CHECK_THROWS_AS(types_of("var \U0001D400 = 1;"), std::runtime_error);
}

TEST_CASE("ucieczka \\u w identyfikatorze") {
    CHECK(text_of("\\u0061bc") == "abc");
    CHECK(text_of("a\\u0062c") == "abc");
    CHECK(text_of("\\u017C")   == "ż");

    // Słowo kluczowe zapisane z ucieczką przestaje nim być.
    CHECK(single("\\u0069f", IDENTIFIER));
    CHECK(text_of("\\u0069f") == "if");
    CHECK(single("if", IF));

    // Ucieczka musi dawać znak dozwolony w identyfikatorze.
    CHECK_THROWS_AS(types_of("\\u0020"), std::runtime_error);
    CHECK_THROWS_AS(types_of("a\\u0020"), std::runtime_error);

    // Sam ukośnik poza identyfikatorem nie znaczy nic.
    CHECK_THROWS_AS(types_of("\\x41"), std::runtime_error);
}

// ---------------------------------------------------------------------------
// 7.2 i 7.3 Białe znaki i terminatory linii spoza ASCII
// ---------------------------------------------------------------------------

TEST_CASE("NBSP, BOM i spacje Unicode są białymi znakami") {
    CHECK(single("\u00A0" "1" "\u00A0", NUMBER));
    CHECK(single("\uFEFF" "1", NUMBER));
    CHECK(single("\u2003" "1", NUMBER));
    CHECK(single("\u3000" "1", NUMBER));

    // BOM na początku pliku nie psuje pierwszego tokenu.
    CHECK(starts_of("\uFEFF" "1").at(0) == 3);
}

TEST_CASE("U+2028 i U+2029 kończą wiersz") {
    // Terminator linii ustawia znacznik potrzebny do automatycznego średnika.
    Lexer lexer("a\u2028" "b");
    const auto tokens = lexer.scan_tokens();

    REQUIRE(tokens.size() >= 2);
    CHECK(tokens.at(1)->newline_before);

    // Kończy też komentarz do końca wiersza.
    CHECK(types_of("// x\u2029" "1") == std::vector<TOKEN_TYPE>{NUMBER, END_OF_FILE});

    // W łańcuchu bez ukośnika jest błędem, a z ukośnikiem kontynuacją wiersza.
    CHECK_THROWS_AS(types_of("\"a\u2028" "b\""), std::runtime_error);
    CHECK(text_of("\"a\\\u2028" "b\"") == "ab");
}

TEST_CASE("powrót karetki też kończy wiersz") {
    Lexer lexer("a\rb");
    const auto tokens = lexer.scan_tokens();

    REQUIRE(tokens.size() >= 2);
    CHECK(tokens.at(1)->newline_before);

    // CRLF to jeden koniec wiersza, nie dwa.
    CHECK(types_of("// x\r\n1") == std::vector<TOKEN_TYPE>{NUMBER, END_OF_FILE});
}

// ---------------------------------------------------------------------------
// B.1.1 Literały ósemkowe
// ---------------------------------------------------------------------------

TEST_CASE("literał ósemkowy z Annex B") {
    CHECK(number_of("010") == 8);
    CHECK(number_of("017") == 15);
    CHECK(number_of("0777") == 511);
    CHECK(number_of("0") == 0);

    // Ósemka lub dziewiątka psuje ósemkowość CAŁEGO ciągu.
    CHECK(number_of("018") == 18);
    CHECK(number_of("08") == 8);
    CHECK(number_of("09") == 9);

    // Zero z kropką albo wykładnikiem to zwykła liczba dziesiętna.
    CHECK(number_of("0.5") == 0.5);
    CHECK(number_of("0x10") == 16);
}

TEST_CASE("zapis z Annex B jest oznaczany dla parsera") {
    const auto octal_flag = [](const std::string &src) {
        Lexer lexer(src);
        return lexer.scan_tokens().at(0)->legacy_octal;
    };

    CHECK(octal_flag("010"));
    CHECK(octal_flag("08"));
    CHECK(octal_flag("\"\\101\""));

    CHECK_FALSE(octal_flag("10"));
    CHECK_FALSE(octal_flag("0"));
    CHECK_FALSE(octal_flag("0.5"));
    CHECK_FALSE(octal_flag("0x10"));
    CHECK_FALSE(octal_flag("\"\\0\""));   // samo \0 należy do głównej specyfikacji
}
