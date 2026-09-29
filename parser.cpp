#include "parser.h"
#include "ast_printer.h"

#include <utility>
#include <map>

#include "memory/js_value.h"
#include "parser/depth_guard.h"
#include "parser/syntax_error.h"

namespace perunejs {
    Parser::Parser(std::vector<Token *> tokens, bool strict) {
        this->tokens = std::move(tokens);
        this->pos = 0;
        this->strict_code = strict;
    }

    Token *Parser::peek() const {
        return this->tokens.at(pos);
    }

    TOKEN_TYPE Parser::peek_type(std::size_t ahead) const {
        std::size_t index = pos + ahead;

        if (index < tokens.size()) {
            return tokens.at(index)->type;
        }

        return END_OF_FILE;
    }

    void Parser::advance() {
        if (peek()->type != END_OF_FILE) {
            pos++;
        }
    }

    bool Parser::at(TOKEN_TYPE type) {
        return peek()->type == type;
    }

    void Parser::expect(TOKEN_TYPE type, const std::string& err) {
        if (!at(type)) error(err);
        advance();
    }

    bool Parser::consume(TOKEN_TYPE type) {
        if (!at(type))  return false;
        advance();
        return true;
    }

    void Parser::expect_semicolon() {
        if (consume(SEMICOLON)) return;

        if (at(RBRACE) || at(END_OF_FILE) || peek()->newline_before)
            return;

        error("; expected");
    }

    const Token* Parser::previous() const {
        return tokens.at(pos > 0 ? pos-1 : 0);
    }

    void Parser::error(const std::string& message) const {
        throw SyntaxError(message, peek()->start);
    }

    bool Parser::has_label(const std::string& name) const {
        for (auto iter = labels.rbegin(); iter != labels.rend(); ++iter) {
            if (iter->label == name) return true;
        }

        return false;
    }

    bool Parser::has_loop_label(const std::string& name) const {
        for (auto iter = labels.rbegin(); iter != labels.rend(); ++iter) {
            if (iter->label == name) return iter->is_loop;
        }
        return false;
    }

    bool Parser::labelled_body_is_loop() const {
        std::size_t i = 0;
        while (peek_type(i) == IDENTIFIER && peek_type(i + 1) == COLON) {
            i +=2;
        }
        const TOKEN_TYPE type = peek_type(i);
        return type == WHILE || type == DO || type == FOR;
    }

    bool Parser::has_use_strict_directive(std::size_t ahead) const {
        for (std::size_t i = ahead; peek_type(i) == STRING; ) {
            const TOKEN_TYPE after = peek_type(i + 1);
            const std::size_t next = pos + i + 1;

            const bool ends_statement = after == SEMICOLON || after == RBRACE || after == END_OF_FILE
                                     || (next < tokens.size() && tokens.at(next)->newline_before);

            if (!ends_statement) return false;

            if (get<std::u16string>(tokens.at(pos + i)->value) == u"use strict") return true;

            i += (after == SEMICOLON) ? 2 : 1;
        }

        return false;
    }

    std::unique_ptr<Program> Parser::parse() {
        return parse_program();
    }

    ExpressionPtr Parser::parse_expression() {
        auto first = parse_assignment();

        if (!at(COMMA)) {
            return first;
        }

        auto second = std::make_unique<SequenceExpression>();
        second->expressions.push_back(std::move(first));

        while (at(COMMA)) {
            advance();
            second->expressions.push_back(parse_assignment());
        }

        return second;
    }

    ExpressionPtr Parser::parse_conditional() {
        const uint32_t start = peek()->start;
        auto test = parse_logical_or();

        if (!at(CONDITIONAL)) return test;

        advance();

        auto consequent = parse_assignment();

        expect(COLON, "colon expected");

        auto alternate = parse_assignment();

        auto cond = std::make_unique<ConditionalExpression>();
        cond->alternate = std::move(alternate);
        cond->consequent = std::move(consequent);
        cond->test = std::move(test);
        cond->start = start;

        return cond;
    }

    ExpressionPtr Parser::parse_logical_or() {
        const uint32_t start = peek()->start;
        ExpressionPtr left = parse_logical_and();

        while (true) {
            BinaryOperator op;
            if (at(OR)) op = BinaryOperator::LogicalOr;
            else return left;

            advance();
            ExpressionPtr right = parse_logical_and();
            left = finish(std::make_unique<BinaryExpression>(op, std::move(left), std::move(right)), start);
        }
    }

    ExpressionPtr Parser::parse_logical_and() {
        const uint32_t start = peek()->start;
        ExpressionPtr left = parse_bitwise_or();

        while (true) {
            BinaryOperator op;
            if (at(AND)) op = BinaryOperator::LogicalAnd;
            else return left;

            advance();
            ExpressionPtr right = parse_bitwise_or();
            left = finish(std::make_unique<BinaryExpression>(op, std::move(left), std::move(right)), start);
        }
    }

    ExpressionPtr Parser::parse_equality() {
        const uint32_t start = peek()->start;
        ExpressionPtr left = parse_relational();

        while (true) {
            BinaryOperator op;
            if (at(EQ)) op = BinaryOperator::Eq;
            else if (at(NE)) op = BinaryOperator::NotEq;
            else if (at(EQ_STRICT)) op = BinaryOperator::StrictEq;
            else if (at(NE_STRICT)) op = BinaryOperator::StrictNotEq;
            else return left;

            advance();
            ExpressionPtr right = parse_relational();
            left = finish(std::make_unique<BinaryExpression>(op, std::move(left), std::move(right)), start);
        }
    }

    ExpressionPtr Parser::parse_relational() {
        const uint32_t start = peek()->start;
        ExpressionPtr left = parse_shift();

        while (true) {
            BinaryOperator op;
            if (at(LT)) op = BinaryOperator::Lt;
            else if (at(GT)) op = BinaryOperator::Gt;
            else if (at(LTE)) op = BinaryOperator::LtEq;
            else if (at(GTE)) op = BinaryOperator::GtEq;
            else if (at(IN) && allow_in) op = BinaryOperator::In;
            else if (at(INSTANCE_OF)) op = BinaryOperator::InstanceOf;
            else return left;

            advance();
            ExpressionPtr right = parse_shift();
            left = finish(std::make_unique<BinaryExpression>(op, std::move(left), std::move(right)), start);
        }
    }

    ExpressionPtr Parser::parse_bitwise_or() {
        const uint32_t start = peek()->start;
        ExpressionPtr left = parse_bitwise_xor();

        while (true) {
            BinaryOperator op;
            if (at(BIT_OR)) op = BinaryOperator::BitOr;
            else return left;

            advance();
            ExpressionPtr right = parse_bitwise_xor();
            left = finish(std::make_unique<BinaryExpression>(op, std::move(left), std::move(right)), start);
        }
    }

    ExpressionPtr Parser::parse_bitwise_xor() {
        const uint32_t start = peek()->start;
        ExpressionPtr left = parse_bitwise_and();

        while (true) {
            BinaryOperator op;
            if (at(BIT_XOR)) op = BinaryOperator::BitXor;
            else return left;

            advance();
            ExpressionPtr right = parse_bitwise_and();
            left = finish(std::make_unique<BinaryExpression>(op, std::move(left), std::move(right)), start);
        }
    }

    ExpressionPtr Parser::parse_bitwise_and() {
        const uint32_t start = peek()->start;
        ExpressionPtr left = parse_equality();

        while (true) {
            BinaryOperator op;
            if (at(BIT_AND)) op = BinaryOperator::BitAnd;
            else return left;

            advance();
            ExpressionPtr right = parse_equality();
            left = finish(std::make_unique<BinaryExpression>(op, std::move(left), std::move(right)), start);
        }
    }

    ExpressionPtr Parser::parse_shift() {
        const uint32_t start = peek()->start;
        ExpressionPtr left = parse_additive();

        while (true) {
            BinaryOperator op;
            if (at(SHL)) op = BinaryOperator::Shl;
            else if (at(SAR)) op = BinaryOperator::Sar;
            else if (at(SHR)) op = BinaryOperator::Shr;
            else return left;

            advance();
            ExpressionPtr right = parse_additive();
            left = finish(std::make_unique<BinaryExpression>(op, std::move(left), std::move(right)), start);
        }
    }

    void Parser::print_ast_tree(std::unique_ptr<Program> node) {
        for (auto it = node->body.begin(); it != node->body.end(); ++it) {
            AstPrinter::dump(it->get(), std::cout, 0);
        }

    }

    ExpressionPtr Parser::parse_additive() {
        const u_int32_t start = peek()->start;
        ExpressionPtr left = parse_multiplicative();

        while (true) {
            BinaryOperator op;
            if (at(ADD)) op = BinaryOperator::Add;
            else if (at(SUB)) op = BinaryOperator::Sub;
            else return left;

            advance();
            ExpressionPtr right = parse_multiplicative();
            left = finish(std::make_unique<BinaryExpression>(op, std::move(left), std::move(right)), start);
        }
    }

    ExpressionPtr Parser::parse_multiplicative() {
        const uint32_t start = peek()->start;
        ExpressionPtr left = parse_unary();

        while (true) {
            BinaryOperator op;
            if (at(MUL)) op = BinaryOperator::Mul;
            else if (at(DIV)) op = BinaryOperator::Div;
            else if (at(MOD)) op = BinaryOperator::Mod;
            else return left;

            advance();
            ExpressionPtr right = parse_unary();
            left = finish(std::make_unique<BinaryExpression>(op, std::move(left), std::move(right)), start);
        }
    }

    ExpressionPtr Parser::parse_unary() {
        if (at(INCREMENT) || at(DECREMENT)) {
            UpdateOperator op;

            if (at(INCREMENT)) op = UpdateOperator::Inc;
            else if (at(DECREMENT)) op = UpdateOperator::Dec;
            else return parse_postfix();

            advance();
            ExpressionPtr operand = parse_unary();
            check_assignment_target(*operand);

            return std::make_unique<UpdateExpression>(op, true, std::move(operand));
        }

        UnaryOperator op;

        if (at(SUB)) op = UnaryOperator::Minus;
        else if (at(ADD)) op = UnaryOperator::Plus;
        else if (at(NOT)) op = UnaryOperator::Not;
        else if (at(BIT_NOT)) op = UnaryOperator::BitNot;
        else if (at(TYPEOF)) op = UnaryOperator::Typeof;
        else if (at(DELETE)) op = UnaryOperator::Delete;
        else if (at(VOID)) op = UnaryOperator::Void;
        else return parse_postfix();

        advance();
        ExpressionPtr operand = parse_unary();

        if (op == UnaryOperator::Delete && strict_code && operand->kind == NodeKind::Identifier) {
            error("Delete of an unqualified identifier in strict mode");
        }

        return std::make_unique<UnaryExpression>(op, std::move(operand));
    }

    ExpressionPtr Parser::parse_primary() {
        if (native_stack_pointer() < stack_limit) error("Maximum call stack size exceeded");

        const uint32_t start = peek()->start;

        if (at(NUMBER)) {
            check_legacy_octal(peek());

            double value = get<double>(peek()->value);
            advance();
            return finish(std::make_unique<NumberLiteral>(value), start);
        }
        if (at(IDENTIFIER)) {
            std::string identifier = get<std::string>(peek()->value);

            if (strict_code && is_strict_reserved_word(identifier)) {
                error("Unexpected reserved word '" + identifier + "' in strict mode");
            }

            advance();
            return finish(std::make_unique<IdentifierLiteral>(identifier), start);
        }
        if (at(LPAREN)) {
            InScope allow(allow_in, true);
            advance();
            ExpressionPtr expression = parse_expression();
            expect(RPAREN, "')' Expected");
            return expression;
        }
        if (at(STRING)) {
            check_legacy_octal(peek());

            std::u16string value = get<std::u16string>(peek()->value);
            advance();
            return finish(std::make_unique<StringLiteral>(value), start);
        }
        if (at(REGEXP)) {
            const RegExpValue &regexp = get<RegExpValue>(peek()->value);
            auto node = std::make_unique<RegExpLiteral>(regexp.pattern, regexp.flags);
            advance();
            return finish(std::move(node), start);
        }
        if (at(TRUE)) {
            advance();
            return std::make_unique<BooleanLiteral>(true);
        }
        if (at(FALSE)) {
            advance();
            return std::make_unique<BooleanLiteral>(false);
        }
        if (at(NULL_T)) {
            advance();
            return std::make_unique<NullLiteral>();
        }
        if (at(FUNCTION)) {
            return parse_function_expression();
        }
        if (at(LBRACE)) {
            return parse_object_literal();
        }
        if (at(LBRACK)) {
            return parse_array_literal();
        }
        if (at(THIS)) {
            advance();
            return finish(std::make_unique<ThisExpression>(), start);
        }

        error("Expression expected");
    }

    std::unique_ptr<Program> Parser::parse_program() {
        auto program = std::make_unique<Program>();
        program->start = peek()->start;

        InScope strict(strict_code, strict_code || has_use_strict_directive());
        program->strict = strict_code;

        while (!at(END_OF_FILE)) {
            program->body.push_back(parse_statement());
        }

        program->end = peek()->end;
        return program;
    }

    StatementPtr Parser::parse_statement() {
        if (native_stack_pointer() < stack_limit) error("Maximum call stack size exceeded");

        if (at(LBRACE)) return parse_block();
        if (at(SEMICOLON)) {advance(); return std::make_unique<EmptyStatement>(); }
        if (at(VAR) || at(LET) || at(CONST)) return parse_variable_declaration();
        if (at(IF))       return parse_if();
        if (at(WHILE))    return parse_while();
        if (at(DO))       return parse_do_while();
        if (at(FOR))      return parse_for();
        if (at(BREAK))    return parse_break();
        if (at(CONTINUE)) return parse_continue();
        if (at(SWITCH))   return parse_switch();
        if (at(RETURN))   return parse_return();
        if (at(FUNCTION)) return parse_function_declaration();
        if (at(THROW))   return parse_throw();
        if (at(TRY))     return parse_try();
        if (at(DEBUGGER)) return parse_debugger();
        if (at(WITH))     return parse_with();

        if (at(IDENTIFIER) && peek_type(1) == COLON) return parse_labeled();

        return parse_expression_statement();
    }

    StatementPtr Parser::parse_block() {
        return parse_block_node();
    }

    StatementPtr Parser::parse_variable_declaration() {
        StatementPtr declaration = parse_variable_declaration_list();
        expect_semicolon();
        return declaration;
    }

    StatementPtr Parser::parse_variable_declaration_list() {
        const uint32_t start = peek()->start;

        DeclarationKind kind;
        if (at(VAR)) kind = DeclarationKind::Var;
        else if (at(LET)) kind = DeclarationKind::Let;
        else kind = DeclarationKind::Const;
        advance();

        auto declaration = std::make_unique<VariableDeclaration>(kind);

        do {
            if (!at(IDENTIFIER)) error("Identifier expected");

            const uint32_t id_start = peek()->start;
            auto name = get<std::string>(peek()->value);
            check_binding_name(name, strict_code);
            advance();

            Declarator declarator;
            declarator.id = finish(std::make_unique<IdentifierLiteral>(std::move(name)), id_start);

            if (consume(ASSIGN)) {
                declarator.init = parse_assignment();
            }

            declaration->declarators.push_back(std::move(declarator));
        } while (consume(COMMA));

        return finish(std::move(declaration), start);
    }

    StatementPtr Parser::parse_expression_statement() {
        ExpressionPtr expression = parse_expression();
        expect_semicolon();
        return std::make_unique<ExpressionStatement>(std::move(expression));
    }

    ExpressionPtr Parser::parse_assignment() {
        ExpressionPtr left = parse_conditional();

        AssignOperator op;
        if (at(ASSIGN)) op = AssignOperator::Assign;
        else if (at(ASSIGN_ADD)) op = AssignOperator::AddAssign;
        else if (at(ASSIGN_SUB)) op = AssignOperator::SubAssign;
        else if (at(ASSIGN_MUL)) op = AssignOperator::MulAssign;
        else if (at(ASSIGN_DIV)) op = AssignOperator::DivAssign;
        else if (at(ASSIGN_MOD)) op = AssignOperator::ModAssign;
        else if (at(ASSIGN_BIT_AND)) op = AssignOperator::BitAndAssign;
        else if (at(ASSIGN_BIT_OR)) op = AssignOperator::BitOrAssign;
        else if (at(ASSIGN_BIT_XOR)) op = AssignOperator::BitXorAssign;
        else if (at(ASSIGN_SAR)) op = AssignOperator::SarAssign;
        else if (at(ASSIGN_SHL)) op = AssignOperator::ShlAssign;
        else if (at(ASSIGN_SHR)) op = AssignOperator::ShrAssign;
        else return left;

        if (left->kind != NodeKind::Identifier && left->kind != NodeKind::MemberExpression) {
            error("Invalid assignment target");
        }

        check_assignment_target(*left);

        advance();
        ExpressionPtr right = parse_assignment();

        return std::make_unique<AssignExpression>(op, std::move(left), std::move(right));
    }

    ExpressionPtr Parser::parse_postfix() {
        ExpressionPtr left = parse_left_hand_side();

        UpdateOperator op;
        if (at(INCREMENT ) && !peek()->newline_before) op = UpdateOperator::Inc;
        else if (at(DECREMENT) && !peek()->newline_before) op = UpdateOperator::Dec;
        else return left;

        advance();
        check_assignment_target(*left);

        return std::make_unique<UpdateExpression>(op, false, std::move(left));
    }

    std::string Parser::parse_property_key() {
        const Token *token = peek();

        if (at(STRING)) {
            check_legacy_octal(token);

            auto key = utf16_to_utf8(get<std::u16string>(token->value));
            advance();
            return key;
        }

        if (at(IDENTIFIER)) {
            auto key = get<std::string>(token->value);
            advance();
            return key;
        }

        if (at(NUMBER)) {
            check_legacy_octal(token);

            const double value = get<double>(token->value);
            advance();
            return JSValue::number(value).to_string();
        }

        if (const char *keyword = token_text(token->type)) {
            advance();
            return keyword;
        }

        error("property name expected");
    }

    bool Parser::is_strict_reserved_word(const std::string &name) {
        static constexpr const char *reserved[] = {
            "implements", "interface", "let", "package",
            "private", "protected", "public", "static", "yield"};

        for (const char *word : reserved) {
            if (name == word) return true;
        }

        return false;
    }

    void Parser::check_binding_name(const std::string &name, bool strict) {
        if (!strict) return;

        if (name == "eval" || name == "arguments") {
            error("Unexpected " + name + " in strict mode");
        }

        if (is_strict_reserved_word(name)) {
            error("Unexpected reserved word '" + name + "' in strict mode");
        }
    }

    void Parser::check_legacy_octal(const Token *token) const {
        if (!strict_code || !token->legacy_octal) return;

        error("Octal literals are not allowed in strict mode.");
    }

    void Parser::check_assignment_target(const Expression &target) {
        if (!strict_code || target.kind != NodeKind::Identifier) return;

        const std::string &name = static_cast<const IdentifierLiteral&>(target).name;

        if (name == "eval" || name == "arguments") {
            error("Cannot assign to '" + name + "' in strict mode");
        }
    }

    ExpressionPtr Parser::parse_object_literal() {
        const uint32_t start = peek()->start;
        expect(LBRACE, "'{' expected");

        auto node = std::make_unique<ObjectExpression>();

        struct KeyInfo { bool data = false; bool getter = false; bool setter = false; };
        std::map<std::string, KeyInfo> seen;

        while (!at(RBRACE)) {
            ObjectProperty property;

            const bool is_accessor = at(IDENTIFIER) &&
                (get<std::string>(peek()->value) == "get" || get<std::string>(peek()->value) == "set") &&
                    peek_type(1) != COLON &&
                        peek_type(1 ) != COMMA &&
                            peek_type(1) != RBRACE;

            if (is_accessor) {
                property.kind = get<std::string>(peek()->value) == "get" ? ObjectProperty::Kind::Get : ObjectProperty::Kind::Set;

                advance();
                property.key = parse_property_key();
                auto parts = parse_function_tail();
                property.accessor = std::make_unique<FunctionExpression>(std::move(parts.params), std::move(parts.body));
                property.accessor->strict = parts.strict;
            } else {
                property.key = parse_property_key();
                expect(COLON, ": expected");
                InScope allow(allow_in, true);
                property.value = parse_assignment();
            }

            KeyInfo &info = seen[property.key];

            const bool is_data = property.kind == ObjectProperty::Kind::Init;
            const bool mixes = (is_data && (info.getter || info.setter)) || (!is_data && info.data);
            const bool double_get = property.kind == ObjectProperty::Kind::Get && info.getter;
            const bool double_set = property.kind == ObjectProperty::Kind::Set && info.setter;

            if (mixes || double_get || double_set || (is_data && info.data && strict_code)) {
                error("Duplicate property '" + property.key + "' in object literal");
            }

            info.data   = info.data   || is_data;
            info.getter = info.getter || property.kind == ObjectProperty::Kind::Get;
            info.setter = info.setter || property.kind == ObjectProperty::Kind::Set;

            node->properties.push_back(std::move(property));

            if (!consume(COMMA)) break;
        }

        expect(RBRACE, "} expected");
        return finish(std::move(node), start);
    }

    StatementPtr Parser::parse_if() {
        advance();
        auto node = std::make_unique<IfStatement>();

        expect(LPAREN, " expected (");
        node->test = parse_expression();
        expect(RPAREN, " expected )");

        node->consequent = parse_statement();
        if (consume(ELSE)) node->alternate = parse_statement();
        return node;
    }

    StatementPtr Parser::parse_while() {
        const uint32_t start = peek()->start;
        advance();
        auto node = std::make_unique<WhileStatement>();
        expect(LPAREN, " expected (");
        node->test = parse_expression();
        expect(RPAREN, " expected )");

        {
            DepthGuard guard(loop_depth);
            node->body = parse_statement();
        }

        return finish(std::move(node), start);
    }

    StatementPtr Parser::parse_for() {
    const uint32_t start = peek()->start;
    advance();                                       // for
    expect(LPAREN, "'(' expected");

    StatementPtr  init_declaration;
    ExpressionPtr init_expression;

    if (!at(SEMICOLON)) {
        InScope no_in(allow_in, false);

        if (at(VAR) || at(LET) || at(CONST)) {
            init_declaration = parse_variable_declaration_list();
        } else {
            init_expression = parse_expression();
        }
    }

    if (at(IN)) {
        advance();

        auto node = std::make_unique<ForInStatement>();

        if (init_declaration != nullptr) {

            const auto &declaration = static_cast<const VariableDeclaration&>(*init_declaration);
            if (declaration.declarators.size() != 1) {
                error("only one variable allowed in for-in loop");
            }
            node->declaration = std::move(init_declaration);
        } else {
            if (init_expression == nullptr ||
                (init_expression->kind != NodeKind::Identifier && init_expression->kind != NodeKind::MemberExpression)) {
                error("invalid left-hand side in for-in loop");
            }
            node->target = std::move(init_expression);
        }

        node->right = parse_expression();
        expect(RPAREN, "')' expected");

        {
            DepthGuard guard(loop_depth);
            node->body = parse_statement();
        }
        return finish(std::move(node), start);
    }

    auto node = std::make_unique<ForStatement>();

    if (init_declaration != nullptr) {
        node->init = std::move(init_declaration);
    } else if (init_expression != nullptr) {
        node->init = std::make_unique<ExpressionStatement>(std::move(init_expression));
    }

    expect(SEMICOLON, "';' expected");

    if (!at(SEMICOLON)) node->test = parse_expression();
    expect(SEMICOLON, "';' expected");

    if (!at(RPAREN)) node->update = parse_expression();
    expect(RPAREN, "')' expected");

    {
        DepthGuard guard(loop_depth);
        node->body = parse_statement();
    }
    return finish(std::move(node), start);
}

    StatementPtr Parser::parse_do_while() {
        const uint32_t start = peek()->start;
        advance();
        auto node = std::make_unique<DoWhileStatement>();
        {
            DepthGuard guard(loop_depth);
            node->body = parse_statement();
        }
        expect(WHILE, " expected while");
        expect(LPAREN, " expected (");
        node->test = parse_expression();
        expect(RPAREN, " expected )");
        consume(SEMICOLON);
        return finish(std::move(node), start);
    }

    StatementPtr Parser::parse_break() {
        const uint32_t start = peek()->start;
        advance();
        auto node = std::make_unique<BreakStatement>();

        if (at(IDENTIFIER ) && !peek()->newline_before) {
            node->label = get<std::string>(peek()->value);
            if (!has_label(node->label))
                error("Undefined label '" + node->label + "'");
            advance();
        } else if (loop_depth == 0 && switch_depth == 0) {
            error("Illegal break statement");
        }

        expect_semicolon();
        return finish(std::move(node), start);;
    }

    StatementPtr Parser::parse_labeled() {
        const uint32_t start = peek()->start;

        auto node = std::make_unique<LabeledStatement>();
        node->label = get<std::string>(peek()->value);

        if (has_label(node->label))
            error("Label '" + node->label + "' has already been declared");

        const bool is_loop = labelled_body_is_loop();

        advance();
        advance();

        {
            LabelGuard guard(labels, node->label, is_loop);
            node->body = parse_statement();
        }
        return finish(std::move(node), start);
    }

    StatementPtr Parser::parse_switch() {
        const uint32_t start = peek()->start;
        advance();

        auto node = std::make_unique<SwitchStatement>();

        expect(LPAREN, " expected (");
        node->discriminant = parse_expression();
        expect(RPAREN, " expected )");
        expect(LBRACE, " expected {");

        bool seen_default = false;

        {
            DepthGuard guard(switch_depth);
            while (!at(RBRACE)) {
                if (at(END_OF_FILE)) error("expected }");

                SwitchCase branch;
                if (consume(CASE)) {
                    branch.test = parse_expression();
                } else {
                    expect(DEFAULT, "expected case or default");
                    if (seen_default) error("duplicate default clause");
                    seen_default = true;
                }

                expect(COLON, "expected :");

                while (!at(CASE) && !at(DEFAULT) && !at(RBRACE)) {
                    if (at(END_OF_FILE)) error("expected }");
                    branch.body.push_back(parse_statement());
                }

                node->cases.push_back(std::move(branch));
            }
        }

        advance();

        return finish(std::move(node), start);
    }

    StatementPtr Parser::parse_continue() {
        const uint32_t start = peek()->start;

        advance();
        auto node = std::make_unique<ContinueStatement>();

        if (at(IDENTIFIER)&& !peek()->newline_before) {
            node->label = get<std::string>(peek()->value);
            if (!has_loop_label(node->label))
                error("Undefined label '" + node->label + "'");
            advance();
        } else if (loop_depth == 0) {
            error("Illegal continue statement");   // switch NIE liczy się dla continue
        }

        expect_semicolon();
        return finish(std::move(node), start);
    }

    StatementPtr Parser::parse_return() {
        if (function_depth == 0) {
            error("SyntaxError: Illegal return statement");
        }

        advance();
        auto node = std::make_unique<ReturnStatement>();

        if (peek()->newline_before) {
            return node;
        }

        if (!at(SEMICOLON) && !at(RBRACE) && !at(END_OF_FILE)) {
            node->argument = parse_expression();
        }

        expect_semicolon();
        return node;
    }

    StatementPtr Parser::parse_function_declaration() {
        FunctionParts parts = parse_function(true);

        auto node = std::make_unique<FunctionDeclaration>(
            std::move(parts.params), std::move(parts.body), std::move(parts.name));
        node->strict = parts.strict;

        return node;
    }

    ExpressionPtr Parser::parse_function_expression() {
        FunctionParts parts = parse_function(false);

        auto node = std::make_unique<FunctionExpression>(
            std::move(parts.params), std::move(parts.body), std::move(parts.name));
        node->strict = parts.strict;

        return node;
    }

    StatementPtr Parser::parse_throw() {
        expect(THROW, "expected throw");

        if (peek()->newline_before) {
            throw std::runtime_error("Illegal newline after throw");
        }

        auto node = parse_expression();

        return std::make_unique<ThrowStatement>(std::move(node));
    }

    StatementPtr Parser::parse_try() {
        const uint32_t start = peek()->start;
        expect(TRY, "'try' expected");

        auto block = parse_block_node();

        std::unique_ptr<IdentifierLiteral> identifier;
        std::unique_ptr<BlockStatement> catch_block;
        std::unique_ptr<BlockStatement> finally_block;

        if (at(CATCH)) {
            advance();
            expect(LPAREN, "'(' expected");
            if (!at(IDENTIFIER)) error("identifier expected in catch clause");

            const uint32_t id_start = peek()->start;
            auto name = get<std::string>(peek()->value);
            check_binding_name(name, strict_code);
            advance();
            identifier = finish(std::make_unique<IdentifierLiteral>(std::move(name)), id_start);

            expect(RPAREN, "')' expected");
            catch_block = parse_block_node();
        }

        if (at(FINALLY)) {
            advance();
            finally_block = parse_block_node();
        }

        if (catch_block == nullptr && finally_block == nullptr) {
            error("missing catch or finally after try");
        }

        return finish(std::make_unique<TryStatement>(
            std::move(block), std::move(identifier),
            std::move(catch_block), std::move(finally_block)), start);
    }

    StatementPtr Parser::parse_debugger() {
        const uint32_t start = peek()->start;
        advance();

        expect_semicolon();
        return finish(std::make_unique<DebuggerStatement>(), start);
    }

    ExpressionPtr Parser::parse_array_literal() {
        const uint32_t start = peek()->start;
        expect(LBRACK, "'[' expected");
        InScope allow(allow_in, true);

        auto node = std::make_unique<ArrayExpression>();

        while (!at(RBRACK)) {
            if (at(COMMA)) { //empty space tutaj
                advance();
                node->elements.push_back(nullptr);
                continue;
            }

            node->elements.push_back(parse_assignment());

            if (!consume(COMMA)) break;
        }

        expect(RBRACK, "']' expected");
        return finish(std::move(node), start);
    }

    ExpressionPtr Parser::parse_member_suffix(ExpressionPtr object, uint32_t start) {
        auto member = std::make_unique<MemberExpression>();
        member->object = std::move(object);

        if (at(PERIOD)) {
            advance();
            const uint32_t name_start = peek()->start;

            std::string name;
            if (at(IDENTIFIER)) {
                name = get<std::string>(peek()->value);
            } else if (const char *keyword = token_text(peek()->type)) {
                name = keyword;
            } else {
                error("property name expected after '.'");
            }
            advance();

            member->property = finish(std::make_unique<IdentifierLiteral>(std::move(name)), name_start);
            member->computed = false;
        } else {
            expect(LBRACK, "'[' expected");
            InScope allow(allow_in, true);
            member->property = parse_expression();
            member->computed = true;
            expect(RBRACK, "']' expected");
        }

        return finish(std::move(member), start);
    }

    ExpressionPtr Parser::parse_new() {
        const uint32_t start = peek()->start;
        expect(NEW, "'new' expected");

        ExpressionPtr callee = at(NEW) ? parse_new() : parse_primary();

        while (at(PERIOD) || at(LBRACK)) {
            callee = parse_member_suffix(std::move(callee), start);
        }

        InScope allow(allow_in, true);

        auto node = std::make_unique<NewExpression>();
        node->callee = std::move(callee);

        if (at(LPAREN)) {
            advance();
            if (!at(RPAREN)) {
                do { node->args.push_back(parse_assignment()); } while (consume(COMMA));
            }
            expect(RPAREN, "')' expected");
        }

        return finish(std::move(node), start);
    }

    StatementPtr Parser::parse_with() {
        if (strict_code) error("Strict mode code may not include a with statement");

        const uint32_t start = peek()->start;
        advance();

        auto node = std::make_unique<WithStatement>();

        expect(LPAREN, " expected (");
        node->object = parse_expression();
        expect(RPAREN, " expected )");

        node->body = parse_statement();

        return finish(std::move(node), start);
    }

    Parser::FunctionParts Parser::parse_function_tail() {
        FunctionParts parts;

        expect(LPAREN, "'(' expected");

        if (!at(RPAREN)) {
            do {
                if (!at(IDENTIFIER)) error("Identifier expected");

                const uint32_t id_start = peek()->start;
                auto name = get<std::string>(peek()->value);
                advance();

                parts.params.push_back(
                    finish(std::make_unique<IdentifierLiteral>(std::move(name)), id_start));
            } while (consume(COMMA));
        }

        expect(RPAREN, "')' expected");

        InScope strict(strict_code, strict_code || has_use_strict_directive(1));
        parts.strict = strict_code;

        if (parts.strict) {
            for (std::size_t i = 0; i < parts.params.size(); ++i) {
                check_binding_name(parts.params[i]->name, true);

                for (std::size_t j = 0; j < i; ++j) {
                    if (parts.params[j]->name == parts.params[i]->name) {
                        error("Duplicate parameter name not allowed in this context");
                    }
                }
            }
        }

        {
            FunctionScope scope(function_depth, loop_depth, switch_depth, labels);
            InScope allow(allow_in, true);
            parts.body = parse_block_node();
        }

        return parts;
    }

    Parser::FunctionParts Parser::parse_function(bool name_required) {
        expect(FUNCTION, "'function' expected");

        std::string name;
        if (at(IDENTIFIER)) {
            name = get<std::string>(peek()->value);
            advance();
        } else if (name_required) {
            error("SyntaxError: Function statements require a function name");
        }

        FunctionParts parts = parse_function_tail();
        check_binding_name(name, parts.strict);

        parts.name = std::move(name);
        return parts;
    }

    ExpressionPtr Parser::parse_left_hand_side() {
        const uint32_t start = peek()->start;

        ExpressionPtr expression = at(NEW) ? parse_new() : parse_primary();

        while (true) {
            if (at(PERIOD) || at(LBRACK)) {
                expression = parse_member_suffix(std::move(expression), start);

            } else if (at(LPAREN)) {
                advance();
                InScope allow(allow_in, true);
                std::vector<ExpressionPtr> args;
                if (!at(RPAREN)) {
                    do { args.push_back(parse_assignment()); } while (consume(COMMA));
                }
                expect(RPAREN, "')' expected");

                expression = finish(std::make_unique<CallExpression>(
                    std::move(expression), std::move(args)), start);
            } else {
                return expression;
            }
        }
    }

    std::unique_ptr<BlockStatement> Parser::parse_block_node() {
        const uint32_t start = peek()->start;
        expect(LBRACE, "'{' expected");

        auto block = std::make_unique<BlockStatement>();

        while (!at(RBRACE)) {
            if (at(END_OF_FILE)) error("'}' expected");
            block->body.push_back(parse_statement());
        }
        advance();
        return finish(std::move(block), start);

    }
}
