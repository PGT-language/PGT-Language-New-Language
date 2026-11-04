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
    prog.package = expect(Tok::Ident, "after 'package'").text;

    while (check(Tok::KwUse)) parseImports(prog);

    while (check(Tok::KwFn)) prog.functions.push_back(parseFunction());

    if (check(Tok::KwReturn)) {
        advance();
        prog.exitCode = parseExpr();
    }

    if (!check(Tok::Eof)) {
        if (prog.exitCode) errorHere("unexpected code after the final 'return'");
        errorHere(std::string("expected 'fn' or 'return', found ") + tokName(cur().kind));
    }
    return prog;
}

void Parser::parseImports(Program& prog) {
    advance();
    auto readOne = [&] {
        const Token& t = expect(Tok::String, "as the package path");
        if (t.text.empty()) throw CompileError("empty import path", t.line, t.col);
        prog.imports.push_back(Import{t.text, t.line, t.col});
    };
    if (check(Tok::LParen)) {
        advance();
        while (!check(Tok::RParen)) {
            if (check(Tok::Eof)) errorHere("unexpected end of file, expected ')'");
            readOne();
        }
        advance();
    } else {
        readOne();
    }
}

Function Parser::parseFunction() {
    Function fn;
    fn.line = cur().line;
    fn.col = cur().col;
    expect(Tok::KwFn, "");
    fn.name = expect(Tok::Ident, "after 'fn'").text;
    expect(Tok::LParen, "after the function name");
    expect(Tok::RParen, "(parameters are not supported yet)");
    fn.body = parseBlock("before the function body");
    return fn;
}

Block Parser::parseBlock(const char* context) {
    expect(Tok::LBrace, context);
    Block body;
    while (!check(Tok::RBrace)) {
        if (check(Tok::Eof)) errorHere("unexpected end of file, expected '}'");
        body.push_back(parseStatement());
    }
    advance();
    return body;
}

bool Parser::atAssignment() const {
    if (!check(Tok::Ident)) return false;
    switch (peekTok().kind) {
    case Tok::Assign:
    case Tok::PlusAssign:
    case Tok::MinusAssign:
    case Tok::StarAssign:
    case Tok::SlashAssign:
    case Tok::PercentAssign:
    case Tok::PlusPlus:
    case Tok::MinusMinus:
        return true;
    default:
        return false;
    }
}

StmtPtr Parser::parseVarDecl() {
    auto decl = make<VarDecl>(cur());
    advance();
    decl->name = expect(Tok::Ident, "after 'var*'").text;
    expect(Tok::Assign, "after the variable name");
    decl->init = parseExpr();
    return decl;
}

StmtPtr Parser::parseSimpleStatement() {
    if (check(Tok::KwVar)) return parseVarDecl();
    if (!atAssignment()) errorHere("expected a variable declaration or an assignment");

    auto assign = make<Assign>(cur());
    assign->name = advance().text;
    const Token& opTok = advance();

    auto one = [&] {
        auto lit = make<IntLit>(opTok);
        lit->value = 1;
        return lit;
    };

    switch (opTok.kind) {
    case Tok::Assign: assign->op = AssignOp::Set; break;
    case Tok::PlusAssign: assign->op = AssignOp::Add; break;
    case Tok::MinusAssign: assign->op = AssignOp::Sub; break;
    case Tok::StarAssign: assign->op = AssignOp::Mul; break;
    case Tok::SlashAssign: assign->op = AssignOp::Div; break;
    case Tok::PercentAssign: assign->op = AssignOp::Mod; break;
    case Tok::PlusPlus:
        assign->op = AssignOp::Add;
        assign->value = one();
        return assign;
    case Tok::MinusMinus:
        assign->op = AssignOp::Sub;
        assign->value = one();
        return assign;
    default: break;
    }
    assign->value = parseExpr();
    return assign;
}

StmtPtr Parser::parseStatement() {
    switch (cur().kind) {
    case Tok::KwVar: return parseVarDecl();
    case Tok::KwIf: return parseIf();
    case Tok::KwWhile: return parseWhile();
    case Tok::KwFor: return parseFor();
    case Tok::KwBreak: {
        auto stmt = make<Break>(cur());
        advance();
        return stmt;
    }
    case Tok::KwContinue: {
        auto stmt = make<Continue>(cur());
        advance();
        return stmt;
    }
    default: break;
    }

    if (atAssignment()) return parseSimpleStatement();

    auto stmt = make<ExprStmt>(cur());
    stmt->expr = parseExpr();
    if (!dynamic_cast<Call*>(stmt->expr.get()))
        throw CompileError("expression result is unused; only calls can be statements",
                           stmt->line, stmt->col);
    return stmt;
}

StmtPtr Parser::parseIf() {
    auto node = make<If>(cur());
    advance();
    node->cond = parseExpr();
    node->thenBody = parseBlock("after the 'if' condition");
    if (check(Tok::KwElse)) {
        advance();
        node->hasElse = true;
        if (check(Tok::KwIf))
            node->elseBody.push_back(parseIf());
        else
            node->elseBody = parseBlock("after 'else'");
    }
    return node;
}

StmtPtr Parser::parseWhile() {
    auto loop = make<Loop>(cur());
    advance();
    loop->cond = parseExpr();
    loop->body = parseBlock("after the 'while' condition");
    return loop;
}

StmtPtr Parser::parseFor() {
    const Token& forTok = cur();
    advance();

    if (check(Tok::Ident) && peekTok().kind == Tok::KwIn) {
        auto range = make<RangeLoop>(forTok);
        range->var = advance().text;
        advance();
        range->from = parseExpr();
        expect(Tok::DotDot, "in the range of 'for'");
        range->to = parseExpr();
        range->body = parseBlock("after the 'for' range");
        return range;
    }

    auto loop = make<Loop>(forTok);

    if (check(Tok::LBrace)) {
        loop->body = parseBlock("");
        return loop;
    }

    if (check(Tok::KwVar) || atAssignment()) {
        loop->init = parseSimpleStatement();
        expect(Tok::Semi, "after the 'for' initializer");
        loop->cond = parseExpr();
        expect(Tok::Semi, "after the 'for' condition");
        if (!check(Tok::LBrace)) loop->post = parseSimpleStatement();
        loop->body = parseBlock("after the 'for' header");
        return loop;
    }

    loop->cond = parseExpr();
    loop->body = parseBlock("after the 'for' condition");
    return loop;
}

ExprPtr Parser::parseExpr() { return parseOr(); }

ExprPtr Parser::parseOr() {
    ExprPtr lhs = parseAnd();
    while (check(Tok::OrOr)) {
        auto bin = make<Binary>(cur());
        advance();
        bin->op = BinOp::Or;
        bin->lhs = std::move(lhs);
        bin->rhs = parseAnd();
        lhs = std::move(bin);
    }
    return lhs;
}

ExprPtr Parser::parseAnd() {
    ExprPtr lhs = parseEquality();
    while (check(Tok::AndAnd)) {
        auto bin = make<Binary>(cur());
        advance();
        bin->op = BinOp::And;
        bin->lhs = std::move(lhs);
        bin->rhs = parseEquality();
        lhs = std::move(bin);
    }
    return lhs;
}

ExprPtr Parser::parseEquality() {
    ExprPtr lhs = parseRelational();
    while (check(Tok::Eq) || check(Tok::Ne)) {
        auto bin = make<Binary>(cur());
        bin->op = advance().kind == Tok::Eq ? BinOp::Eq : BinOp::Ne;
        bin->lhs = std::move(lhs);
        bin->rhs = parseRelational();
        lhs = std::move(bin);
    }
    return lhs;
}

ExprPtr Parser::parseRelational() {
    ExprPtr lhs = parseAdditive();
    while (check(Tok::Lt) || check(Tok::Gt) || check(Tok::Le) || check(Tok::Ge)) {
        auto bin = make<Binary>(cur());
        switch (advance().kind) {
        case Tok::Lt: bin->op = BinOp::Lt; break;
        case Tok::Gt: bin->op = BinOp::Gt; break;
        case Tok::Le: bin->op = BinOp::Le; break;
        default: bin->op = BinOp::Ge; break;
        }
        bin->lhs = std::move(lhs);
        bin->rhs = parseAdditive();
        lhs = std::move(bin);
    }
    return lhs;
}

ExprPtr Parser::parseAdditive() {
    ExprPtr lhs = parseTerm();
    while (check(Tok::Plus) || check(Tok::Minus)) {
        auto bin = make<Binary>(cur());
        bin->op = advance().kind == Tok::Plus ? BinOp::Add : BinOp::Sub;
        bin->lhs = std::move(lhs);
        bin->rhs = parseTerm();
        lhs = std::move(bin);
    }
    return lhs;
}

ExprPtr Parser::parseTerm() {
    ExprPtr lhs = parseUnary();
    while (check(Tok::Star) || check(Tok::Slash) || check(Tok::Percent)) {
        auto bin = make<Binary>(cur());
        switch (advance().kind) {
        case Tok::Star: bin->op = BinOp::Mul; break;
        case Tok::Slash: bin->op = BinOp::Div; break;
        default: bin->op = BinOp::Mod; break;
        }
        bin->lhs = std::move(lhs);
        bin->rhs = parseUnary();
        lhs = std::move(bin);
    }
    return lhs;
}

ExprPtr Parser::parseUnary() {
    if (check(Tok::Minus) || check(Tok::Bang)) {
        auto un = make<Unary>(cur());
        un->op = advance().kind == Tok::Minus ? UnOp::Neg : UnOp::Not;
        un->operand = parseUnary();
        return un;
    }
    return parsePrimary();
}

ExprPtr Parser::parsePrimary() {
    const Token& t = cur();

    switch (t.kind) {
    case Tok::Int: {
        auto lit = make<IntLit>(t);
        try {
            lit->value = std::stoll(t.text);
        } catch (const std::out_of_range&) {
            errorHere("integer literal is too large");
        }
        advance();
        return lit;
    }
    case Tok::KwTrue:
    case Tok::KwFalse: {
        auto lit = make<BoolLit>(t);
        lit->value = t.kind == Tok::KwTrue;
        advance();
        return lit;
    }
    case Tok::String: {
        auto lit = make<StringLit>(t);
        lit->value = t.text;
        advance();
        return lit;
    }
    case Tok::Ident: {
        std::string name = t.text;
        advance();
        std::string qualifier;
        if (check(Tok::Dot)) {
            advance();
            qualifier = std::move(name);
            name = expect(Tok::Ident, "after '.'").text;
            if (!check(Tok::LParen)) errorHere("expected '(' after a qualified name");
        }
        if (check(Tok::LParen)) {
            advance();
            auto call = make<Call>(t);
            call->qualifier = std::move(qualifier);
            call->callee = std::move(name);
            if (!check(Tok::RParen)) {
                do {
                    call->args.push_back(parseExpr());
                } while (check(Tok::Comma) && (advance(), true));
            }
            expect(Tok::RParen, "after call arguments");
            return call;
        }
        auto ref = make<VarRef>(t);
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
