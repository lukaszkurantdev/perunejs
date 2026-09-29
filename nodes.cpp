#include "nodes.h"

namespace perunejs {
    const char* to_string(BinaryOperator op) {
        switch (op) {
            case BinaryOperator::Add:          return "+";
            case BinaryOperator::Sub:          return "-";
            case BinaryOperator::Mul:          return "*";
            case BinaryOperator::Div:          return "/";
            case BinaryOperator::Mod:          return "%";
            case BinaryOperator::Eq:           return "==";
            case BinaryOperator::NotEq:        return "!=";
            case BinaryOperator::StrictEq:     return "===";
            case BinaryOperator::StrictNotEq:  return "!==";
            case BinaryOperator::Lt:           return "<";
            case BinaryOperator::Gt:           return ">";
            case BinaryOperator::LtEq:         return "<=";
            case BinaryOperator::GtEq:         return ">=";
            case BinaryOperator::In:           return "in";
            case BinaryOperator::LogicalAnd:   return "&&";
            case BinaryOperator::LogicalOr:    return "||";
            case BinaryOperator::BitAnd:       return "&";
            case BinaryOperator::BitOr:        return "|";
            case BinaryOperator::BitXor:       return "^";
            case BinaryOperator::Sar:          return ">>";
            case BinaryOperator::Shr:          return ">>>";
            case BinaryOperator::Shl:          return "<<";
            case BinaryOperator::InstanceOf:   return "instanceof";
        }
        return "?";
    }

    const char* to_string(UnaryOperator op) {
        switch (op) {
            case UnaryOperator::Minus:          return "-";
            case UnaryOperator::Plus:           return "+";
            case UnaryOperator::Not:            return "!";
            case UnaryOperator::Typeof:         return "typeof";
            case UnaryOperator::BitNot:         return "~";
            case UnaryOperator::Delete:         return "delete";
            case UnaryOperator::Void:           return "void";
        }
        return "?";
    }

    const char* to_string(AssignOperator op) {
        switch (op) {
            case AssignOperator::Assign:                return "=";
            case AssignOperator::AddAssign:             return "+=";
            case AssignOperator::SubAssign:             return "-=";
            case AssignOperator::MulAssign:             return "*=";
            case AssignOperator::DivAssign:             return "/=";
            case AssignOperator::ModAssign:             return "%=";
            case AssignOperator::BitAndAssign:          return "&=";
            case AssignOperator::BitOrAssign:          return "|=";
            case AssignOperator::BitXorAssign:         return "^=";
            case AssignOperator::SarAssign:            return ">>=";
            case AssignOperator::ShrAssign:            return ">>>=";
            case AssignOperator::ShlAssign:            return "<<=";
        }
        return "?";
    }

    const char* to_string(DeclarationKind op) {
        switch (op) {
            case DeclarationKind::Var:             return "var";
            case DeclarationKind::Let:             return "let";
            case DeclarationKind::Const:           return "const";
        }
        return "?";
    }

    const char* to_string(UpdateOperator op) {
        switch (op) {
            case UpdateOperator::Inc:              return "++";
            case UpdateOperator::Dec:              return "--";
        }

        return "?";
    }
}