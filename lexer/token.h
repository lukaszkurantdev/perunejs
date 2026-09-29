#ifndef PERUNEJS_TOKEN_H
#define PERUNEJS_TOKEN_H

#include <iostream>

namespace perunejs {
    enum TOKEN_TYPE {
        NULLISH, // ??
        NOT, // !
        OR, // ||
        AND, // &&
        BIT_OR, // |
        BIT_XOR, // ^
        BIT_AND, // &
        BIT_NOT, // ~
        SHL, // <<
        SAR, // >>
        SHR, // >>>
        MUL, // *
        DIV, // /
        MOD, // %
        EXP, // **
        ADD, // +
        SUB, // -

        TEMPLATE, // `
        PERIOD, // .
        LBRACK, // [
        RBRACK, // ]
        OPTIONAL, // ?.
        LPAREN, // (
        RPAREN, // )
        LBRACE, // {
        RBRACE, // }
        COLON, // :
        ELLIPSIS, // ...
        CONDITIONAL, // ?
        SEMICOLON, // ;
        EOS, // EOS
        ARROW, // =>
        ASSIGN, // =
        ASSIGN_NULLISH, // ??=
        ASSIGN_OR, // ||=
        ASSIGN_AND, // &&=
        ASSIGN_BIT_OR, // |=
        ASSIGN_BIT_XOR, // ^=
        ASSIGN_BIT_AND, // &=
        ASSIGN_SHL, // <<=
        ASSIGN_SAR, // >>=
        ASSIGN_SHR, // >>>=
        ASSIGN_MUL, // *=
        ASSIGN_DIV, // /=
        ASSIGN_MOD, // %=
        ASSIGN_EXP, // **=
        ASSIGN_ADD, // +=
        ASSIGN_SUB, // -=
        COMMA, // ,
        INCREMENT, // ++
        DECREMENT, // --
        EQ, // ==
        EQ_STRICT, // ===
        NE, // !=
        NE_STRICT, // !==
        LT, // <
        GT, // >
        LTE, // <=
        GTE, // >=
        AT, // @

        INSTANCE_OF, // instanceof
        IN, // in
        DELETE, // delete
        TYPEOF, // typeof
        VOID, // void
        BREAK, // break
        CASE, // case
        CATCH, // catch
        CONTINUE, // continue
        DEBUGGER, // debugger
        DEFAULT, // default,
        DO, // do
        ELSE, // else
        FINALLY, // finally
        FOR, // for
        FUNCTION, // function
        IF, // if
        NEW, // new
        RETURN, // return
        SWITCH, // switch
        THROW, // throw
        TRY, // try
        VAR, // var
        WHILE, // while
        WITH, // with
        THIS, // this
        NULL_T, // null
        TRUE, // true
        FALSE, // false
        SUPER, // super
        AWAIT, // await
        ASYNC, // async
        YIELD, // yield
        CLASS, // class
        LET, // let
        CONST, // const
        EXPORT, // export
        EXTENDS, // extends
        IMPORT, // import
        ENUM, // enum,

        REGEXP,
        NUMBER,
        STRING,
        BIGINT,
        IDENTIFIER,
        PRIVATE_IDENTIFIER,

        END_OF_FILE,
    };

    struct RegExpValue {
        std::string pattern;
        std::string flags;
    };

    class Token {
    public:
        TOKEN_TYPE type;
        uint32_t start;
        uint32_t end;
        bool newline_before = false;
        bool legacy_octal = false;
        std::variant<RegExpValue, double, std::string, std::u16string, std::monostate> value;

        Token( TOKEN_TYPE type, uint32_t start, std::variant<RegExpValue, double, std::string, std::u16string, std::monostate> value);
    };

    const char* token_text(TOKEN_TYPE type);
}
#endif //PERUNEJS_TOKEN_H
