#ifndef PERUNEJS_NODES_H
#define PERUNEJS_NODES_H

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace perunejs {
    enum class NodeKind : uint8_t {
        NumberLiteral, StringLiteral, BooleanLiteral, NullLiteral,
        Identifier,
        UnaryExpression, BinaryExpression, AssignExpression,
        ExpressionStatement, VariableDeclaration,
        Program,
        BlockStatement, EmptyStatement,
        IfStatement, WhileStatement, DoWhileStatement, ForStatement,
        BreakStatement, ContinueStatement, SwitchStatement, LabeledStatement,
        ConditionalExpression,
        SequenceExpression,
        UpdateExpression,
        FunctionDeclaration, FunctionExpression, CallExpression, ReturnStatement,
        ThrowStatement, TryStatement,
        MemberExpression, ObjectExpression, ArrayExpression, NewExpression, ThisExpression,
        ForInStatement, DebuggerStatement, RegExpLiteral, WithStatement
    };

    enum class UnaryOperator : uint8_t {
        Minus, Plus, Not, Typeof, BitNot, Delete, Void
    };

    enum class UpdateOperator : uint8_t {
        Inc, Dec
    };

    enum class BinaryOperator: uint8_t {
        Add, Sub, Mul, Div, Mod, // TODO: potegowanie
        Eq, NotEq, StrictEq, StrictNotEq,
        Lt, Gt, LtEq, GtEq,
        In, InstanceOf,
        LogicalAnd, LogicalOr,
        BitAnd, BitOr, BitXor, Shl, Sar, Shr
    };


    enum class AssignOperator : uint8_t {
        Assign, AddAssign, SubAssign, MulAssign, DivAssign, ModAssign,
        BitOrAssign, BitAndAssign, BitXorAssign, ShlAssign, ShrAssign, SarAssign,
    };

    enum class DeclarationKind : uint8_t {
        Var, Let, Const
    };

    const char* to_string(BinaryOperator op);
    const char* to_string(UnaryOperator op);
    const char* to_string(AssignOperator op);
    const char* to_string(DeclarationKind kind);

    struct Node {
        NodeKind kind;
        uint32_t start = 0;
        uint32_t end = 0;
        explicit Node(NodeKind kind) : kind(kind) {}
        virtual ~Node() = default;
    };

    struct Expression : Node { using Node::Node; };
    struct Statement : Node { using Node::Node; };
    using ExpressionPtr = std::unique_ptr<Expression>;
    using StatementPtr = std::unique_ptr<Statement>;

    struct NumberLiteral : Expression {
        double value;
        explicit NumberLiteral(double value) : Expression(NodeKind::NumberLiteral), value(value) {}
    };

    struct StringLiteral : Expression {
        std::u16string value;
        explicit StringLiteral(std::u16string value) :
            Expression(NodeKind::StringLiteral), value(std::move(value)) {}
    };

    struct RegExpLiteral : Expression {
        std::string pattern;
        std::string flags;
        RegExpLiteral(std::string pattern, std::string flags)
            : Expression(NodeKind::RegExpLiteral), pattern(std::move(pattern)), flags(std::move(flags)) {}
    };

    struct BooleanLiteral : Expression {
        bool value;
        explicit BooleanLiteral(bool value) : Expression(NodeKind::BooleanLiteral), value(value) {}
    };

    struct NullLiteral : Expression {
        NullLiteral() : Expression(NodeKind::NullLiteral) {}
    };

    struct IdentifierLiteral : Expression {
        std::string name;
        explicit IdentifierLiteral(std::string name) : Expression(NodeKind::Identifier), name(std::move(name)) {}
    };

    struct UnaryExpression : Expression {
        UnaryOperator op;
        ExpressionPtr operand;
        UnaryExpression(UnaryOperator op, ExpressionPtr operand)
            : Expression(NodeKind::UnaryExpression), op(op), operand(std::move(operand)) {}
    };

    struct BinaryExpression : Expression {
        BinaryOperator op;
        ExpressionPtr left;
        ExpressionPtr right;
        BinaryExpression(BinaryOperator op, ExpressionPtr left, ExpressionPtr right)
            : Expression(NodeKind::BinaryExpression), op(op), left(std::move(left)), right(std::move(right)) {}
    };

    struct AssignExpression : Expression {
        AssignOperator op;
        ExpressionPtr target;
        ExpressionPtr value;
        AssignExpression(AssignOperator op, ExpressionPtr target, ExpressionPtr value)
            : Expression(NodeKind::AssignExpression), op(op), target(std::move(target)), value(std::move(value)) {}
    };

    struct ExpressionStatement : Statement {
        ExpressionPtr expr;
        explicit ExpressionStatement(ExpressionPtr e)
            : Statement(NodeKind::ExpressionStatement), expr(std::move(e)) {}
    };

    struct Declarator {
        std::unique_ptr<IdentifierLiteral> id;
        ExpressionPtr init;
    };

    struct VariableDeclaration : Statement {
        DeclarationKind decl;
        std::vector<Declarator> declarators;   //  np var a = 1, b;
        explicit VariableDeclaration(DeclarationKind d)
            : Statement(NodeKind::VariableDeclaration), decl(d) {}
    };

    struct Program : Node {
        std::vector<StatementPtr> body;
        bool strict = false;

        // Źródło i offsety początków linii — potrzebne wyłącznie do
        // budowania Error.prototype.stack. Indeks liczymy raz przy
        // kompilacji, żeby zrzut stosu był O(log n) na ramkę.
        std::string source;
        std::vector<uint32_t> line_starts;

        Program() : Node(NodeKind::Program) {}
    };

    struct BlockStatement : Statement {
        std::vector<StatementPtr> body;
        BlockStatement() : Statement(NodeKind::BlockStatement) {}
    };

    struct EmptyStatement : Statement {
        EmptyStatement() : Statement(NodeKind::EmptyStatement) {}
    };

    struct IfStatement : Statement {
        ExpressionPtr test;
        StatementPtr  consequent;
        StatementPtr  alternate;
        IfStatement() : Statement(NodeKind::IfStatement) {}
    };

    struct WhileStatement : Statement {
        ExpressionPtr test;
        StatementPtr  body;
        WhileStatement() : Statement(NodeKind::WhileStatement) {}
    };

    struct DoWhileStatement : Statement {
        StatementPtr  body;
        ExpressionPtr test;
        DoWhileStatement() : Statement(NodeKind::DoWhileStatement) {}
    };

    struct ForStatement : Statement {
        StatementPtr  init;
        ExpressionPtr test;
        ExpressionPtr update;
        StatementPtr  body;
        ForStatement() : Statement(NodeKind::ForStatement) {}
    };

    struct BreakStatement : Statement {
        std::string label;
        BreakStatement() : Statement(NodeKind::BreakStatement) {}
    };

    struct ContinueStatement : Statement {
        std::string label;
        ContinueStatement() : Statement(NodeKind::ContinueStatement) {}
    };

    struct LabeledStatement : Statement {
        std::string  label;
        StatementPtr body;
        LabeledStatement() : Statement(NodeKind::LabeledStatement) {}
    };

    struct SwitchCase {
        ExpressionPtr test;
        std::vector<StatementPtr> body;
    };

    struct SwitchStatement : Statement {
        ExpressionPtr discriminant;
        std::vector<SwitchCase> cases;
        SwitchStatement() : Statement(NodeKind::SwitchStatement) {}
    };

    struct ConditionalExpression : Expression {
        ExpressionPtr test;
        ExpressionPtr consequent;
        ExpressionPtr alternate;
        ConditionalExpression() : Expression(NodeKind::ConditionalExpression) {}
    };

    struct SequenceExpression : Expression {
        std::vector<ExpressionPtr> expressions;
        SequenceExpression() : Expression(NodeKind::SequenceExpression) {}
    };

    struct UpdateExpression : Expression {
        UpdateOperator op;
        bool prefix;
        ExpressionPtr operand;
        UpdateExpression(UpdateOperator op, bool prefix, ExpressionPtr operand)
          : Expression(NodeKind::UpdateExpression), op(op), prefix(prefix), operand(std::move(operand)) {}
    };

    struct FunctionDeclaration : Statement {
        std::vector<std::unique_ptr<IdentifierLiteral>> params;
        std::unique_ptr<BlockStatement> body;
        std::string name;
        bool strict = false;

        FunctionDeclaration(
            std::vector<std::unique_ptr<IdentifierLiteral>> params,
            std::unique_ptr<BlockStatement> body,
            std::string name) :
                Statement(NodeKind::FunctionDeclaration), params(std::move(params)), body(std::move(body)), name(std::move(name)) {}
    };

    struct FunctionExpression : Expression {
        std::vector<std::unique_ptr<IdentifierLiteral>> params;
        std::unique_ptr<BlockStatement> body;
        std::string name;
        bool strict = false;

        FunctionExpression(
            std::vector<std::unique_ptr<IdentifierLiteral>> params,
            std::unique_ptr<BlockStatement> body) :
                Expression(NodeKind::FunctionExpression), params(std::move(params)), body(std::move(body)) {}

        FunctionExpression(
            std::vector<std::unique_ptr<IdentifierLiteral>> params,
            std::unique_ptr<BlockStatement> body,
            std::string name) :
                Expression(NodeKind::FunctionExpression), params(std::move(params)), body(std::move(body)), name(std::move(name)) {}
    };

    struct CallExpression : Expression {
        ExpressionPtr callee;
        std::vector<ExpressionPtr> args;

        CallExpression(ExpressionPtr callee, std::vector<ExpressionPtr> args) : Expression(NodeKind::CallExpression), callee(std::move(callee)), args(std::move(args))  {}
    };

    struct ReturnStatement : Statement {
        ExpressionPtr argument;
        ReturnStatement() : Statement(NodeKind::ReturnStatement) {}
        ReturnStatement(ExpressionPtr argument) : Statement(NodeKind::ReturnStatement), argument(std::move(argument)) {}
    };

    struct ThrowStatement : Statement {
        ExpressionPtr argument;
        ThrowStatement(ExpressionPtr argument) : Statement(NodeKind::ThrowStatement), argument(std::move(argument)) {}
    };

    struct TryStatement : Statement {
        std::unique_ptr<BlockStatement> block;
        std::unique_ptr<IdentifierLiteral> catch_param = nullptr;
        std::unique_ptr<BlockStatement> catch_block;
        std::unique_ptr<BlockStatement> finally_block;

        TryStatement(std::unique_ptr<BlockStatement> block,
        std::unique_ptr<IdentifierLiteral> catch_param,
        std::unique_ptr<BlockStatement> catch_block,
        std::unique_ptr<BlockStatement> finally_block): Statement(NodeKind::TryStatement), block(std::move(block)),
            catch_param(std::move(catch_param)), catch_block(std::move(catch_block)), finally_block(std::move(finally_block)) {}
    };

    struct MemberExpression : Expression {
        ExpressionPtr object;
        ExpressionPtr property;
        bool computed = false;

        MemberExpression() : Expression(NodeKind::MemberExpression) {}
    };

    struct ObjectProperty {
        enum class Kind : uint8_t { Init, Get, Set };

        Kind kind = Kind::Init;
        std::string key;
        ExpressionPtr value; // Kind::Init
        std::unique_ptr<FunctionExpression> accessor; // Kind::Get / Kind::Set
    };

    struct ObjectExpression : Expression {
        std::vector<ObjectProperty> properties;
        ObjectExpression() : Expression(NodeKind::ObjectExpression) {}
    };

    struct ArrayExpression : Expression {
        std::vector<ExpressionPtr> elements;
        ArrayExpression() : Expression(NodeKind::ArrayExpression) {}
    };

    struct NewExpression : Expression {
        ExpressionPtr callee;
        std::vector<ExpressionPtr> args;
        NewExpression() : Expression(NodeKind::NewExpression) {}
    };

    struct ThisExpression : Expression {
        ThisExpression() : Expression(NodeKind::ThisExpression) {}
    };

    struct ForInStatement : Statement {
        StatementPtr  declaration;
        ExpressionPtr target;
        ExpressionPtr right;
        StatementPtr  body;

        ForInStatement() : Statement(NodeKind::ForInStatement) {}
    };

    struct DebuggerStatement : Statement {
        DebuggerStatement() : Statement(NodeKind::DebuggerStatement) {}
    };

    struct WithStatement : Statement {
        ExpressionPtr object;
        StatementPtr  body;

        WithStatement() : Statement(NodeKind::WithStatement) {}
    };
}

#endif //PERUNEJS_NODES_H
