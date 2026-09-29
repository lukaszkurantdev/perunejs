#include "token.h"

namespace perunejs {
    Token::Token(TOKEN_TYPE type, uint32_t start, std::variant<RegExpValue, double, std::string, std::u16string, std::monostate> value) {
        this->type = type;
        this->start = start;
        this->value = std::move(value);
    }

    const char* token_text(TOKEN_TYPE type) {
        switch (type) {
            case ASYNC:       return "async";
            case AWAIT:       return "await";
            case BREAK:       return "break";
            case CASE:        return "case";
            case CATCH:       return "catch";
            case CLASS:       return "class";
            case CONST:       return "const";
            case CONTINUE:    return "continue";
            case DEBUGGER:    return "debugger";
            case DEFAULT:     return "default";
            case DELETE:      return "delete";
            case DO:          return "do";
            case ELSE:        return "else";
            case ENUM:        return "enum";
            case EXPORT:      return "export";
            case EXTENDS:     return "extends";
            case FALSE:       return "false";
            case FINALLY:     return "finally";
            case FOR:         return "for";
            case FUNCTION:    return "function";
            case IF:          return "if";
            case IMPORT:      return "import";
            case IN:          return "in";
            case INSTANCE_OF: return "instanceof";
            case LET:         return "let";
            case NEW:         return "new";
            case NULL_T:      return "null";
            case RETURN:      return "return";
            case SUPER:       return "super";
            case SWITCH:      return "switch";
            case THIS:        return "this";
            case THROW:       return "throw";
            case TRUE:        return "true";
            case TRY:         return "try";
            case TYPEOF:      return "typeof";
            case VAR:         return "var";
            case VOID:        return "void";
            case WHILE:       return "while";
            case WITH:        return "with";
            case YIELD:       return "yield";

            default:          return nullptr;   // punktuatory, literały, identyfikatory
        }
    }
}