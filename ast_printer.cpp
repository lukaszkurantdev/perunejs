#include "ast_printer.h"

#include "utils/utf.h"

using namespace perunejs;

static void indent(std::ostream& out, std::size_t depth) {
  for (size_t i = 0; i < depth; ++i) out << "  ";
}

void AstPrinter::dump(Node *node, std::ostream &out, std::size_t depth) {
  indent(out, depth);

  if (node == nullptr) {
    out << "<null>";
    return;
  }

  switch (node->kind) {
    case NodeKind::NumberLiteral: {
      const auto *n = dynamic_cast<NumberLiteral *>(node);
      out << n->value << "\n";
    } break;
    case NodeKind::Identifier: {
      const auto *n = dynamic_cast<IdentifierLiteral *>(node);
      out << n->name << "\n";
    } break;
    case NodeKind::StringLiteral: {
      const auto *n = dynamic_cast<StringLiteral *>(node);
      out << utf16_to_utf8(n->value) << "\n";
    } break;
    case NodeKind::BooleanLiteral: {
      const auto *n = dynamic_cast<BooleanLiteral *>(node);
      out << n->value << "\n";
    } break;
    case NodeKind::NullLiteral: {
      out << "null" << "\n";
    } break;
    case NodeKind::BinaryExpression: {
      const auto *n = dynamic_cast<BinaryExpression *>(node);
      out << "BinaryExpression(" << to_string(n->op) << ") \n";
      dump(n->left.get(), out, depth + 1);
      dump(n->right.get(), out, depth + 1);
    } break;
    case NodeKind::UnaryExpression: {
      const auto *n = dynamic_cast<UnaryExpression *>(node);
      out << "UnaryExpression(" << to_string(n->op) << ") \n";
      dump(n->operand.get(), out, depth + 1);
    } break;
    case NodeKind::AssignExpression: {
      const auto *n = dynamic_cast<AssignExpression *>(node);
      out << "AssignExpression(" << to_string(n->op) << ") \n";
      dump(n->target.get(), out, depth + 1);
      dump(n->value.get(), out, depth + 1);
    } break;
    case NodeKind::ExpressionStatement: {
      const auto *n = dynamic_cast<ExpressionStatement *>(node);
      out << "ExpressionStatement \n";
      dump(n->expr.get(), out, depth + 1);
    } break;
    case NodeKind::VariableDeclaration: {
      const auto *n = dynamic_cast<VariableDeclaration *>(node);
      out << "VariableDeclaration(" << to_string(n->decl) << ") \n";

      for (const Declarator &decl : n->declarators) {
        dump(decl.id.get(),   out, depth + 1);
        dump(decl.init.get(), out, depth + 1);
      }
    } break;
    case NodeKind::ConditionalExpression: {
      const auto *n = dynamic_cast<ConditionalExpression *>(node);
      out << "ConditionalExpression \n";
      dump(n->test.get(), out, depth + 1);
      dump(n->consequent.get(), out, depth + 1);
      dump(n->alternate.get(), out, depth + 1);
    } break;
    case NodeKind::SequenceExpression: {
      const auto *n = dynamic_cast<SequenceExpression *>(node);
      out << "SequenceExpression \n";

      for (const ExpressionPtr &expr : n->expressions) {
        dump(expr.get(),   out, depth + 1);
      }
    } break;
    case NodeKind::CallExpression: {
      const auto *n = dynamic_cast<CallExpression *>(node);
      out << "CallExpression \n";
      dump(n->callee.get(), out, depth + 1);

      for (const ExpressionPtr &arg : n->args) {
        dump(arg.get(),   out, depth + 1);
      }
    } break;
    case NodeKind::ReturnStatement: {
      const auto *n = dynamic_cast<ReturnStatement *>(node);
      out << "ReturnStatement \n";
      dump(n->argument.get(), out, depth + 1);
    } break;
    case NodeKind::FunctionDeclaration: {
      const auto *n = dynamic_cast<FunctionDeclaration *>(node);
      out << "FunctionDeclaration(" << n->name << ") \n";

      for (const std::unique_ptr<IdentifierLiteral> &param : n->params) {
        dump(param.get(),   out, depth + 1);
      }

      dump(n->body.get(), out, depth + 1);
    } break;
    case NodeKind::FunctionExpression: {
      const auto *n = dynamic_cast<FunctionExpression *>(node);
      out << "FunctionExpression(" << n->name << ") \n";

      for (const std::unique_ptr<IdentifierLiteral> &param : n->params) {
        dump(param.get(),   out, depth + 1);
      }

      dump(n->body.get(), out, depth + 1);
    } break;
    case NodeKind::RegExpLiteral: {
      const auto *n = dynamic_cast<RegExpLiteral *>(node);
      out << "RegExp /" << n->pattern << "/" << n->flags << "\n";
    } break;
    case NodeKind::DebuggerStatement: {
      out << "DebuggerStatement\n";
    } break;
    case NodeKind::Program:
      out << "Program";
      // dump(node., out, depth + 1);
      break;
    default:
      break;
  }

}

