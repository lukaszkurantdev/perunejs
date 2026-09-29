#ifndef PERUNEJS_PARSER_H
#define PERUNEJS_PARSER_H

#include <vector>
#include "lexer/token.h"
#include "nodes.h"
#include "parser/depth_guard.h"
#include "utils/native_stack.h"

namespace perunejs {
    class Parser {
        u_int32_t pos;
        const std::uintptr_t stack_limit = native_stack_limit();
        std::vector<Token*> tokens;
        uint32_t function_depth = 0;
        uint32_t loop_depth = 0;
        uint32_t switch_depth = 0;

        std::vector<LabelInfo> labels;

        struct FunctionParts {
            std::string name;
            std::vector<std::unique_ptr<IdentifierLiteral>> params;
            std::unique_ptr<BlockStatement> body;
            bool strict = false;
        };

        bool allow_in = true;
        bool strict_code = false;

        class InScope {
            bool& flag_;
            bool  saved_;
        public:
            InScope(bool& flag, bool value) : flag_(flag), saved_(flag) { flag_ = value; }
            ~InScope() { flag_ = saved_; }
            InScope(const InScope&) = delete;
            InScope& operator=(const InScope&) = delete;
        };

        Token* peek() const;
        TOKEN_TYPE peek_type(std::size_t ahead) const;
        void advance();
        bool at(TOKEN_TYPE type);
        void expect(TOKEN_TYPE type, const std::string& error);
        bool consume(TOKEN_TYPE type);
        void expect_semicolon();

        const Token* previous() const;
        [[noreturn]] void error(const std::string& message) const;
        template <typename T>
        std::unique_ptr<T> finish(std::unique_ptr<T> node, uint32_t start) {
            node->start = start;
            node->end   = previous()->end;
            return node;
        }
        bool labelled_body_is_loop() const;
        bool has_use_strict_directive(std::size_t ahead = 0) const;
        bool has_label(const std::string& name) const;
        bool has_loop_label(const std::string& name) const;
        std::string parse_property_key();
        static bool is_strict_reserved_word(const std::string& name);
        void check_binding_name(const std::string& name, bool strict);
        void check_legacy_octal(const Token* token) const;
        void check_assignment_target(const Expression& target);



        std::unique_ptr<Program> parse_program();
        ExpressionPtr parse_primary();
        ExpressionPtr parse_expression();
        ExpressionPtr parse_conditional();
        ExpressionPtr parse_logical_or();
        ExpressionPtr parse_logical_and();
        ExpressionPtr parse_bitwise_or();
        ExpressionPtr parse_bitwise_xor();
        ExpressionPtr parse_bitwise_and();
        ExpressionPtr parse_equality();
        ExpressionPtr parse_relational();
        ExpressionPtr parse_shift();
        ExpressionPtr parse_additive();
        ExpressionPtr parse_multiplicative();
        ExpressionPtr parse_unary();
        StatementPtr parse_statement();
        StatementPtr parse_block();
        StatementPtr parse_variable_declaration();
        StatementPtr parse_variable_declaration_list();
        StatementPtr parse_expression_statement();
        ExpressionPtr parse_assignment();
        ExpressionPtr parse_postfix();
        ExpressionPtr parse_object_literal();

        StatementPtr parse_if();
        StatementPtr parse_while();
        StatementPtr parse_for();
        StatementPtr parse_do_while();
        StatementPtr parse_break();
        StatementPtr parse_labeled();
        StatementPtr parse_switch();
        StatementPtr parse_continue();
        StatementPtr parse_return();
        StatementPtr parse_function_declaration();
        ExpressionPtr parse_function_expression();
        StatementPtr parse_throw();
        StatementPtr parse_try();
        StatementPtr parse_debugger();
        ExpressionPtr parse_array_literal();
        ExpressionPtr parse_member_suffix(ExpressionPtr object, uint32_t start);
        ExpressionPtr parse_new();
        StatementPtr parse_with();

        FunctionParts parse_function_tail();
        FunctionParts parse_function(bool name_required);
        ExpressionPtr parse_left_hand_side();
        std::unique_ptr<BlockStatement> parse_block_node();

    public:
        Parser(std::vector<Token*> tokens, bool strict = false);
        std::unique_ptr<Program> parse();
        static void print_ast_tree(std::unique_ptr<Program> node);
    };
}

#endif //PERUNEJS_PARSER_H
