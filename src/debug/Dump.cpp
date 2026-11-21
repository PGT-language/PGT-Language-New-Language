#include "debug/Dump.h"

#include "ast/AST.h"
#include "common/Error.h"
#include "lexer/Lexer.h"
#include "parser/Parser.h"

#include <algorithm>
#include <cstdio>
#include <iomanip>
#include <optional>
#include <sstream>

namespace pgt
{
    namespace
    {

        // A neutral tree built from the AST; printed either as text or as JSON.
        struct Node
        {
            std::string kind;  // Function, Binary, IntLit, ...
            std::string label; // what is shown: "fn main()", "+", "42"
            int line = 0, col = 0;
            std::vector<Node> kids;
        };

        template <typename T>
        Node node(std::string kind, std::string label, const T &at)
        {
            Node n;
            n.kind = std::move(kind);
            n.label = std::move(label);
            n.line = at.line;
            n.col = at.col;
            return n;
        }

        Node group(std::string kind, std::string label, int line, int col)
        {
            Node n;
            n.kind = std::move(kind);
            n.label = std::move(label);
            n.line = line;
            n.col = col;
            return n;
        }

        std::string quote(const std::string &s, char q = '"')
        {
            std::string out(1, q);
            for (char c : s)
            {
                switch (c)
                {
                case '\n':
                    out += "\\n";
                    break;
                case '\t':
                    out += "\\t";
                    break;
                case '\r':
                    out += "\\r";
                    break;
                case '\0':
                    out += "\\0";
                    break;
                case '\\':
                    out += "\\\\";
                    break;
                default:
                    if (c == q)
                        out += '\\';
                    out += c;
                }
            }
            return out + q;
        }

        std::string params(const std::vector<Param> &ps)
        {
            std::string s;
            for (size_t i = 0; i < ps.size(); ++i)
                s += (i ? ", " : "") + ps[i].name + " " + ps[i].type.text();
            return s;
        }

        Node expr(const Expr *e);
        Node stmt(const Stmt *s);

        Node block(const Block &b, std::string label, int line, int col)
        {
            Node n = group("Block", std::move(label), line, col);
            for (const StmtPtr &s : b)
                n.kids.push_back(stmt(s.get()));
            return n;
        }

        Node wrap(std::string label, const Expr *e)
        {
            Node inner = expr(e);
            Node n = group("Part", std::move(label), inner.line, inner.col);
            n.kids.push_back(std::move(inner));
            return n;
        }

        Node expr(const Expr *e)
        {
            if (auto *x = dynamic_cast<const IntLit *>(e))
                return node("IntLit", std::to_string(x->value), *x);
            if (auto *x = dynamic_cast<const FloatLit *>(e))
            {
                std::ostringstream ss;
                ss << x->value;
                return node("FloatLit", ss.str(), *x);
            }
            if (auto *x = dynamic_cast<const CharLit *>(e))
                return node("CharLit", quote(std::string(1, x->value), '\''), *x);
            if (auto *x = dynamic_cast<const BoolLit *>(e))
                return node("BoolLit", x->value ? "true" : "false", *x);
            if (auto *x = dynamic_cast<const StringLit *>(e))
                return node("StringLit", quote(x->value), *x);
            if (auto *x = dynamic_cast<const NilLit *>(e))
                return node("NilLit", "nil", *x);
            if (auto *x = dynamic_cast<const VarRef *>(e))
                return node("Name", x->name, *x);
            if (auto *x = dynamic_cast<const Unary *>(e))
            {
                Node n = node("Unary", opText(x->op), *x);
                n.kids.push_back(expr(x->operand.get()));
                return n;
            }
            if (auto *x = dynamic_cast<const Binary *>(e))
            {
                Node n = node("Binary", opText(x->op), *x);
                n.kids.push_back(expr(x->lhs.get()));
                n.kids.push_back(expr(x->rhs.get()));
                return n;
            }
            if (auto *x = dynamic_cast<const Call *>(e))
            {
                Node n = node("Call", "call", *x);
                n.kids.push_back(expr(x->callee.get()));
                if (x->typeArg)
                    n.kids.push_back(node("Type", x->typeArg->text(), *x->typeArg));
                for (const ExprPtr &a : x->args)
                    n.kids.push_back(expr(a.get()));
                return n;
            }
            if (auto *x = dynamic_cast<const Selector *>(e))
            {
                Node n = node("Selector", "." + x->name, *x);
                n.kids.push_back(expr(x->base.get()));
                return n;
            }
            if (auto *x = dynamic_cast<const Index *>(e))
            {
                Node n = node("Index", "[]", *x);
                n.kids.push_back(expr(x->base.get()));
                n.kids.push_back(expr(x->index.get()));
                return n;
            }
            if (auto *x = dynamic_cast<const ArrayLit *>(e))
            {
                Node n = node("ArrayLit", "[" + std::to_string(x->elems.size()) + " elements]", *x);
                for (const ExprPtr &a : x->elems)
                    n.kids.push_back(expr(a.get()));
                return n;
            }
            if (auto *x = dynamic_cast<const StructLit *>(e))
            {
                Node n = node("StructLit", x->type.text() + "{}", *x);
                for (const FieldInit &f : x->fields)
                {
                    Node fn = node("FieldInit", f.name + ":", f);
                    fn.kids.push_back(expr(f.value.get()));
                    n.kids.push_back(std::move(fn));
                }
                return n;
            }
            return group("Unknown", "?", e ? e->line : 0, e ? e->col : 0);
        }

        Node varDecl(const VarDecl &d)
        {
            std::string label = (d.isConst ? "const " : "var* ") + d.name;
            if (d.type)
                label += " " + d.type->text();
            Node n = node(d.isConst ? "Const" : "VarDecl", label, d);
            if (d.init)
                n.kids.push_back(expr(d.init.get()));
            return n;
        }

        Node stmt(const Stmt *s)
        {
            if (auto *x = dynamic_cast<const VarDecl *>(s))
                return varDecl(*x);
            if (auto *x = dynamic_cast<const Assign *>(s))
            {
                static const char *ops[] = {"=", "+=", "-=", "*=", "/=", "%="};
                Node n = node("Assign", ops[static_cast<int>(x->op)], *x);
                n.kids.push_back(expr(x->target.get()));
                n.kids.push_back(expr(x->value.get()));
                return n;
            }
            if (auto *x = dynamic_cast<const ExprStmt *>(s))
                return expr(x->expr.get());
            if (auto *x = dynamic_cast<const If *>(s))
            {
                Node n = node("If", "if", *x);
                n.kids.push_back(wrap("condition", x->cond.get()));
                n.kids.push_back(block(x->thenBody, "then", x->line, x->col));
                if (x->hasElse)
                    n.kids.push_back(block(x->elseBody, "else", x->line, x->col));
                return n;
            }
            if (auto *x = dynamic_cast<const Loop *>(s))
            {
                Node n = node("Loop", x->init || x->post ? "for ;;" : x->cond ? "while" : "for (infinite)", *x);
                if (x->init)
                {
                    Node p = group("Part", "init", x->line, x->col);
                    p.kids.push_back(stmt(x->init.get()));
                    n.kids.push_back(std::move(p));
                }
                if (x->cond)
                    n.kids.push_back(wrap("condition", x->cond.get()));
                if (x->post)
                {
                    Node p = group("Part", "post", x->line, x->col);
                    p.kids.push_back(stmt(x->post.get()));
                    n.kids.push_back(std::move(p));
                }
                n.kids.push_back(block(x->body, "body", x->line, x->col));
                return n;
            }
            if (auto *x = dynamic_cast<const ForIn *>(s))
            {
                std::string vars = x->key.empty() ? x->value : x->key + ", " + x->value;
                Node n = node("ForIn", "for " + vars + " in", *x);
                if (x->rangeEnd)
                {
                    n.kids.push_back(wrap("from", x->iterable.get()));
                    n.kids.push_back(wrap("to", x->rangeEnd.get()));
                }
                else
                {
                    n.kids.push_back(wrap("iterable", x->iterable.get()));
                }
                n.kids.push_back(block(x->body, "body", x->line, x->col));
                return n;
            }
            if (auto *x = dynamic_cast<const Switch *>(s))
            {
                Node n = node("Switch", "switch", *x);
                if (x->tag)
                    n.kids.push_back(wrap("value", x->tag.get()));
                for (const Case &c : x->cases)
                {
                    Node cn = node("Case", "case", c);
                    for (const ExprPtr &v : c.values)
                        cn.kids.push_back(expr(v.get()));
                    cn.kids.push_back(block(c.body, "body", c.line, c.col));
                    n.kids.push_back(std::move(cn));
                }
                if (x->hasDefault)
                    n.kids.push_back(block(x->defaultBody, "default", x->line, x->col));
                return n;
            }
            if (auto *x = dynamic_cast<const Return *>(s))
            {
                Node n = node("Return", "return", *x);
                if (x->value)
                    n.kids.push_back(expr(x->value.get()));
                return n;
            }
            if (auto *x = dynamic_cast<const Break *>(s))
                return node("Break", "break", *x);
            if (auto *x = dynamic_cast<const Continue *>(s))
                return node("Continue", "continue", *x);
            return group("Unknown", "?", s ? s->line : 0, s ? s->col : 0);
        }

        Node program(const Program &p)
        {
            Node root = group("Program", "package " + p.package, 1, 1);
            for (const Import &imp : p.imports)
                root.kids.push_back(node("Import", "#use " + quote(imp.path), imp));
            for (const auto &g : p.globals)
                root.kids.push_back(varDecl(*g));
            for (const StructDecl &s : p.structs)
            {
                Node n = node("Struct", "struct " + s.name, s);
                for (const Field &f : s.fields)
                    n.kids.push_back(node("Field", f.name + " " + f.type.text(), f));
                root.kids.push_back(std::move(n));
            }
            for (const InterfaceDecl &i : p.interfaces)
            {
                Node n = node("Interface", "interface " + i.name, i);
                for (const MethodSig &m : i.methods)
                    n.kids.push_back(node("Method", m.name + "(" + params(m.params) + ")" +
                                                        (m.ret ? " " + m.ret->text() : ""),
                                          m));
                root.kids.push_back(std::move(n));
            }
            for (const Function &f : p.functions)
            {
                std::string label = f.isExtern ? "extern fn " : "fn ";
                if (f.receiver)
                    label += "(" + f.receiver->name + " " + (f.receiver->pointer ? "*" : "") +
                             f.receiver->typeName + ") ";
                label += f.name + "(" + params(f.params) + ")";
                if (f.ret)
                    label += " " + f.ret->text();
                Node n = node(f.isExtern ? "Extern" : f.receiver ? "Method" : "Function", label, f);
                if (!f.isExtern)
                    for (const StmtPtr &s : f.body)
                        n.kids.push_back(stmt(s.get()));
                root.kids.push_back(std::move(n));
            }
            // Declarations are stored by kind; show them in source order.
            std::stable_sort(root.kids.begin(), root.kids.end(), [](const Node &a, const Node &b) {
                return a.line != b.line ? a.line < b.line : a.col < b.col;
            });
            if (p.exitCode)
            {
                Node n = node("ExitCode", "return (exit code)", *p.exitCode);
                n.kids.push_back(expr(p.exitCode.get()));
                root.kids.push_back(std::move(n));
            }
            return root;
        }

        // ---------------------------------------------------------------- output

        std::string json(const std::string &s)
        {
            std::string out = "\"";
            for (unsigned char c : s)
            {
                switch (c)
                {
                case '"':
                    out += "\\\"";
                    break;
                case '\\':
                    out += "\\\\";
                    break;
                case '\n':
                    out += "\\n";
                    break;
                case '\r':
                    out += "\\r";
                    break;
                case '\t':
                    out += "\\t";
                    break;
                default:
                    if (c < 0x20)
                    {
                        char buf[8];
                        std::snprintf(buf, sizeof buf, "\\u%04x", c);
                        out += buf;
                    }
                    else
                        out += static_cast<char>(c);
                }
            }
            return out + "\"";
        }

        void printJson(const Node &n, std::ostream &out)
        {
            out << "{\"kind\":" << json(n.kind) << ",\"label\":" << json(n.label) << ",\"line\":" << n.line
                << ",\"col\":" << n.col;
            if (!n.kids.empty())
            {
                out << ",\"children\":[";
                for (size_t i = 0; i < n.kids.size(); ++i)
                {
                    if (i)
                        out << ",";
                    printJson(n.kids[i], out);
                }
                out << "]";
            }
            out << "}";
        }

        void printTree(const Node &n, std::ostream &out, const std::string &prefix, bool last, bool root)
        {
            std::string pos = n.line ? " (" + std::to_string(n.line) + ":" + std::to_string(n.col) + ")" : "";
            out << prefix << (root ? "" : last ? "└─ " : "├─ ") << n.kind << " " << n.label << pos << "\n";
            std::string next = root ? "" : prefix + (last ? "   " : "│  ");
            for (size_t i = 0; i < n.kids.size(); ++i)
                printTree(n.kids[i], out, next, i + 1 == n.kids.size(), false);
        }

        std::string tokenText(const Token &t)
        {
            switch (t.kind)
            {
            case Tok::String:
                return quote(t.text);
            case Tok::Char:
                return quote(t.text, '\'');
            case Tok::Eof:
                return "";
            default:
                return t.text;
            }
        }

    }

    bool dumpSources(const std::vector<DebugSource> &sources, bool asJson, std::ostream &out)
    {
        bool ok = true;
        if (asJson)
            out << "{\"files\":[";

        for (size_t fi = 0; fi < sources.size(); ++fi)
        {
            const DebugSource &src = sources[fi];
            std::vector<Token> tokens;
            std::optional<Program> prog;
            std::optional<CompileError> error;
            try
            {
                tokens = Lexer(src.text).tokenize();
                prog = Parser(tokens).parseProgram();
            }
            catch (const CompileError &e)
            {
                error = e;
                ok = false;
            }

            if (asJson)
            {
                if (fi)
                    out << ",";
                out << "{\"file\":" << json(src.path) << ",\"tokens\":[";
                for (size_t i = 0; i < tokens.size(); ++i)
                {
                    const Token &t = tokens[i];
                    out << (i ? "," : "") << "{\"kind\":" << json(tokKindName(t.kind))
                        << ",\"text\":" << json(t.text) << ",\"line\":" << t.line << ",\"col\":" << t.col << "}";
                }
                out << "]";
                if (prog)
                {
                    out << ",\"ast\":";
                    printJson(program(*prog), out);
                }
                if (error)
                    out << ",\"error\":{\"message\":" << json(error->what()) << ",\"line\":" << error->line
                        << ",\"col\":" << error->col << "}";
                out << "}";
                continue;
            }

            out << "==== " << src.path << " ====\n\n-- tokens (" << tokens.size() << ")\n";
            for (const Token &t : tokens)
            {
                std::string pos = std::to_string(t.line) + ":" + std::to_string(t.col);
                out << "  " << std::left << std::setw(8) << pos << std::setw(14) << tokKindName(t.kind)
                    << tokenText(t) << "\n";
            }
            if (prog)
            {
                out << "\n-- syntax tree\n";
                printTree(program(*prog), out, "", true, true);
            }
            if (error)
                out << "\n" << src.path << ":" << error->line << ":" << error->col << ": error: " << error->what()
                    << "\n";
            out << "\n";
        }
        if (asJson)
            out << "]}\n";
        return ok;
    }

}
