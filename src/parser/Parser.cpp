#include "parser/Parser.h"

#include "common/Error.h"

namespace pgt {

const Token& Parser::peekTok(size_t ahead) const {
    size_t i = pos_ + ahead;
    return i < toks_.size() ? toks_[i] : toks_.back();
}

const Token& Parser::advance() {
    const Token& t = toks_[pos_];
    if (pos_ + 1 < toks_.size()) ++pos_;
    return t;
}

void Parser::errorHere(const std::string& msg) const {
    throw CompileError(msg, cur().line, cur().col);
}

const Token& Parser::expect(Tok k, const char* context) {
    if (!check(k)) {
        errorHere(std::string("expected ") + tokName(k) + " " + context + ", found " +
                  tokName(cur().kind));
    }
    return advance();
}

Program Parser::parseProgram() {
    Program prog;
    expect(Tok::KwPackage, "at the start of the file");
    const Token& name = expect(Tok::Ident, "after 'package'");
    if (name.text != "main")
        throw CompileError("the entry package must be named 'main', found '" + name.text + "'",
                           name.line, name.col);
    prog.package = name.text;

    while (check(Tok::KwFn)) prog.functions.push_back(parseFunction());

    if (!check(Tok::KwReturn))
        errorHere(std::string("expected 'fn' or the final 'return', found ") + tokName(cur().kind));
    advance();
    prog.exitCode = parseExpr();

    if (!check(Tok::Eof)) errorHere("unexpected code after the final 'return'");
    return prog;
}

Function Parser::parseFunction() {
    Function fn;
    fn.line = cur().line;
    fn.col = cur().col;
    expect(Tok::KwFn, "");
    fn.name = expect(Tok::Ident, "after 'fn'").text;
    expect(Tok::LParen, "after the function name");
    expect(Tok::RParen, "(parameters are not supported yet)");
    expect(Tok::LBrace, "before the function body");
    while (!check(Tok::RBrace)) {
        if (check(Tok::Eof)) errorHere("unexpected end of file, expected '}'");
        fn.body.push_back(parseStatement());
    }
    expect(Tok::RBrace, "after the function body");
    return fn;
}

StmtPtr Parser::parseStatement() {
    int line = cur().line, col = cur().col;

    if (check(Tok::KwVar)) {
        advance();
        auto decl = std::make_unique<VarDecl>();
        decl->line = line;
        decl->col = col;
        decl->name = expect(Tok::Ident, "after 'var*'").text;
        expect(Tok::Assign, "after the variable name");
        decl->init = parseExpr();
        return decl;
    }

    auto stmt = std::make_unique<ExprStmt>();
    stmt->line = line;
    stmt->col = col;
    stmt->expr = parseExpr();
    if (!dynamic_cast<Call*>(stmt->expr.get()))
        throw CompileError("expression result is unused; only calls can be statements", line, col);
    return stmt;
}

ExprPtr Parser::parseExpr() {
    ExprPtr lhs = parseTerm();
    while (check(Tok::Plus) || check(Tok::Minus)) {
        auto bin = std::make_unique<Binary>();
        bin->line = cur().line;
        bin->col = cur().col;
        bin->op = advance().text[0];
        bin->lhs = std::move(lhs);
        bin->rhs = parseTerm();
        lhs = std::move(bin);
    }
    return lhs;
}

ExprPtr Parser::parseTerm() {
    ExprPtr lhs = parseFactor();
    while (check(Tok::Star) || check(Tok::Slash)) {
        auto bin = std::make_unique<Binary>();
        bin->line = cur().line;
        bin->col = cur().col;
        bin->op = advance().text[0];
        bin->lhs = std::move(lhs);
        bin->rhs = parseFactor();
        lhs = std::move(bin);
    }
    return lhs;
}

ExprPtr Parser::parseFactor() {
    if (check(Tok::Minus)) {
        auto un = std::make_unique<Unary>();
        un->line = cur().line;
        un->col = cur().col;
        advance();
        un->operand = parseFactor();
        return un;
    }
    return parsePrimary();
}

ExprPtr Parser::parsePrimary() {
    const Token& t = cur();
    int line = t.line, col = t.col;

    switch (t.kind) {
    case Tok::Int: {
        auto lit = std::make_unique<IntLit>();
        lit->line = line;
        lit->col = col;
        try {
            lit->value = std::stoll(t.text);
        } catch (const std::out_of_range&) {
            errorHere("integer literal is too large");
        }
        advance();
        return lit;
    }
    case Tok::String: {
        auto lit = std::make_unique<StringLit>();
        lit->line = line;
        lit->col = col;
        lit->value = t.text;
        advance();
        return lit;
    }
    case Tok::Ident: {
        std::string name = t.text;
        advance();
        if (check(Tok::LParen)) {
            advance();
            auto call = std::make_unique<Call>();
            call->line = line;
            call->col = col;
            call->callee = std::move(name);
            if (!check(Tok::RParen)) {
                do {
                    call->args.push_back(parseExpr());
                } while (check(Tok::Comma) && (advance(), true));
            }
            expect(Tok::RParen, "after call arguments");
            return call;
        }
        auto ref = std::make_unique<VarRef>();
        ref->line = line;
        ref->col = col;
        ref->name = std::move(name);
        return ref;
    }
    case Tok::LParen: {
        advance();
        ExprPtr inner = parseExpr();
        expect(Tok::RParen, "to close the parenthesized expression");
        return inner;
    }
    default:
        errorHere(std::string("expected an expression, found ") + tokName(t.kind));
    }
}

}
