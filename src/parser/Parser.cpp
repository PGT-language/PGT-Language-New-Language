#include "parser/Parser.h"

#include "ast/Analysis.h"
#include "common/Error.h"
#include "lexer/Lexer.h"

namespace pgt
{

    namespace
    {

        struct FlagGuard
        {
            bool &flag;
            bool saved;
            FlagGuard(bool &f, bool value) : flag(f), saved(f)
            {
                flag = value;
            }
            ~FlagGuard()
            {
                flag = saved;
            }
        };

        std::string normalizeImportPath(const std::string &path)
        {
            for (const char *prefix : {"pgt/std/", "std/"})
            {
                size_t n = std::string(prefix).size();
                if (path.size() > n && path.compare(0, n, prefix) == 0)
                    return path.substr(n);
            }
            return path;
        }

        bool isAssignOp(Tok k)
        {
            switch (k)
            {
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

        struct OpLevel
        {
            std::vector<std::pair<Tok, BinOp>> ops;
        };

        const std::vector<OpLevel> &opLevels()
        {
            static const std::vector<OpLevel> levels = {
                {{{Tok::OrOr, BinOp::Or}}},
                {{{Tok::AndAnd, BinOp::And}}},
                {{{Tok::Eq, BinOp::Eq},
                  {Tok::Ne, BinOp::Ne},
                  {Tok::Lt, BinOp::Lt},
                  {Tok::Gt, BinOp::Gt},
                  {Tok::Le, BinOp::Le},
                  {Tok::Ge, BinOp::Ge}}},
                {{{Tok::Plus, BinOp::Add},
                  {Tok::Minus, BinOp::Sub},
                  {Tok::Pipe, BinOp::BitOr},
                  {Tok::Caret, BinOp::BitXor}}},
                {{{Tok::Star, BinOp::Mul},
                  {Tok::Slash, BinOp::Div},
                  {Tok::Percent, BinOp::Mod},
                  {Tok::Shl, BinOp::Shl},
                  {Tok::Shr, BinOp::Shr},
                  {Tok::Amp, BinOp::BitAnd}}},
            };
            return levels;
        }

    }

    const Token &Parser::peekTok(size_t ahead) const
    {
        size_t i = pos_ + ahead;
        return i < toks_.size() ? toks_[i] : toks_.back();
    }

    const Token &Parser::advance()
    {
        const Token &t = toks_[pos_];
        if (pos_ + 1 < toks_.size())
            ++pos_;
        return t;
    }

    void Parser::errorHere(const std::string &msg) const
    {
        throw CompileError(msg, cur().line, cur().col);
    }

    const Token &Parser::expect(Tok k, const char *context)
    {
        if (!check(k))
        {
            errorHere(std::string("expected ") + tokName(k) + (*context ? " " : "") + context +
                      ", found " + tokName(cur().kind));
        }
        return advance();
    }

    Program Parser::parseProgram()
    {
        Program prog;
        expect(Tok::KwPackage, "at the start of the file");
        prog.package = expect(Tok::Ident, "after 'package'").text;

        while (check(Tok::KwUse))
            parseImports(prog);

        while (true)
        {
            switch (cur().kind)
            {
            case Tok::KwFn:
                prog.functions.push_back(parseFunction());
                continue;
            case Tok::KwExtern:
                prog.functions.push_back(parseExtern());
                continue;
            case Tok::KwStruct:
                prog.structs.push_back(parseStruct());
                continue;
            case Tok::KwInterface:
                prog.interfaces.push_back(parseInterface());
                continue;
            case Tok::KwConst:
                parseConsts(prog.globals);
                continue;
            case Tok::KwVar:
                prog.globals.push_back(parseVarDecl());
                continue;
            case Tok::KwVarManual:
                errorHere("'var&' is only allowed inside functions");
            case Tok::KwUse:
                errorHere("'#use' must come before all declarations");
            default:
                break;
            }
            break;
        }

        if (check(Tok::KwReturn))
        {
            advance();
            prog.exitCode = parseExpr();
        }

        if (!check(Tok::Eof))
        {
            if (prog.exitCode)
                errorHere("unexpected code after the final 'return'");
            errorHere(std::string("expected a declaration ('fn', 'struct', 'interface', 'const', "
                                  "'var*') or the final 'return', found ") +
                      tokName(cur().kind));
        }
        for (size_t i = 0; i + 1 < toks_.size(); ++i)
        {
            if (toks_[i].kind == Tok::KwVarManual)
                prog.usesMalloc = true;
            if (toks_[i].kind == Tok::KwAsm)
                prog.usesAsm = true;
            if (toks_[i].kind == Tok::KwMap ||
                (toks_[i].kind == Tok::Ident && (toks_[i].text == "has" || toks_[i].text == "delete") &&
                 toks_[i + 1].kind == Tok::LParen && (i == 0 || toks_[i - 1].kind != Tok::Dot)))
                prog.usesMaps = true;
            if (toks_[i].kind == Tok::Ident && toks_[i + 1].kind == Tok::Dot &&
                (i == 0 || toks_[i - 1].kind != Tok::Dot))
                prog.qualifiers.insert(toks_[i].text);
        }
        return prog;
    }

    void Parser::parseImports(Program &prog)
    {
        advance();
        auto readOne = [&]
        {
            const Token &t = expect(Tok::String, "as the package path");
            if (t.text.empty())
                throw CompileError("empty import path", t.line, t.col);
            prog.imports.push_back(Import{normalizeImportPath(t.text), t.line, t.col});
        };
        if (check(Tok::LParen))
        {
            advance();
            while (!check(Tok::RParen))
            {
                if (check(Tok::Eof))
                    errorHere("unexpected end of file, expected ')'");
                readOne();
            }
            advance();
        }
        else
        {
            readOne();
        }
    }

    bool Parser::atTypeStart() const
    {
        return check(Tok::Ident) || check(Tok::LBracket) || check(Tok::Star) || check(Tok::KwMap);
    }

    TypeExpr Parser::parseType(const char *context)
    {
        const Token &t = cur();
        if (check(Tok::LBracket))
        {
            advance();
            expect(Tok::RBracket, "in the array type '[]T'");
            TypeExpr arr;
            arr.kind = TypeExpr::Kind::Array;
            arr.line = t.line;
            arr.col = t.col;
            arr.elem = std::make_shared<TypeExpr>(parseType("as the array element type"));
            return arr;
        }
        if (check(Tok::KwMap))
        {
            advance();
            expect(Tok::LBracket, "after 'map'");
            TypeExpr m;
            m.kind = TypeExpr::Kind::Map;
            m.line = t.line;
            m.col = t.col;
            m.key = std::make_shared<TypeExpr>(parseType("as the map key type"));
            expect(Tok::RBracket, "after the map key type");
            m.elem = std::make_shared<TypeExpr>(parseType("as the map value type"));
            return m;
        }
        if (check(Tok::Star))
        {
            advance();
            TypeExpr ptr;
            ptr.kind = TypeExpr::Kind::Pointer;
            ptr.line = t.line;
            ptr.col = t.col;
            ptr.elem = std::make_shared<TypeExpr>(parseType("after '*'"));
            return ptr;
        }
        const Token &name = expect(Tok::Ident, context);
        TypeExpr out = TypeExpr::named(name.text, name.line, name.col);
        if (check(Tok::Dot) && peekTok().kind == Tok::Ident)
        {
            advance();
            out.qualifier = out.name;
            out.name = advance().text;
        }
        return out;
    }

    TypeExpr Parser::parseResult()
    {
        if (!check(Tok::LParen))
            return parseType("as the result type");
        TypeExpr tuple;
        tuple.kind = TypeExpr::Kind::Tuple;
        tuple.line = cur().line;
        tuple.col = cur().col;
        advance();
        do
        {
            tuple.elems.push_back(parseType("as a result type"));
        } while (accept(Tok::Comma));
        expect(Tok::RParen, "after the result types");
        if (tuple.elems.size() < 2)
            throw CompileError("a result list needs at least two types; write a single result type "
                               "without parentheses",
                               tuple.line, tuple.col);
        return tuple;
    }

    std::vector<Param> Parser::parseParams(bool allowVariadic)
    {
        std::vector<Param> params;
        expect(Tok::LParen, "before the parameters");
        if (!check(Tok::RParen))
        {
            do
            {
                if (check(Tok::RParen))
                    break;
                Param param;
                param.line = cur().line;
                param.col = cur().col;
                param.name = expect(Tok::Ident, "as a parameter name").text;
                if (check(Tok::Ellipsis))
                {
                    if (!allowVariadic)
                        errorHere("variadic parameters are not allowed here");
                    advance();
                    TypeExpr arr;
                    arr.kind = TypeExpr::Kind::Array;
                    arr.line = cur().line;
                    arr.col = cur().col;
                    arr.elem = std::make_shared<TypeExpr>(parseType("as the variadic parameter type"));
                    param.type = std::move(arr);
                    param.variadic = true;
                    params.push_back(std::move(param));
                    if (check(Tok::Comma))
                        errorHere("a variadic parameter must be the last parameter");
                    break;
                }
                if (atTypeStart())
                    param.type = parseType("as a parameter type");
                else
                    param.type = TypeExpr::named("int", param.line, param.col);
                params.push_back(std::move(param));
            } while (accept(Tok::Comma));
        }
        expect(Tok::RParen, "after the parameters");
        return params;
    }

    Function Parser::parseFunction()
    {
        Function fn;
        fn.line = cur().line;
        fn.col = cur().col;
        expect(Tok::KwFn, "");
        if (check(Tok::LParen))
        {
            advance();
            Receiver recv;
            recv.line = cur().line;
            recv.col = cur().col;
            recv.name = expect(Tok::Ident, "as the receiver name").text;
            recv.pointer = accept(Tok::Star);
            recv.typeName = expect(Tok::Ident, "as the receiver type").text;
            expect(Tok::RParen, "after the receiver");
            fn.receiver = recv;
        }
        fn.name = expect(Tok::Ident, "as the function name").text;
        fn.params = parseParams(true);
        if (atTypeStart() || check(Tok::LParen))
            fn.ret = parseResult();
        fn.body = parseBlock("before the function body");
        if (!fn.ret && hasValueReturn(fn.body))
            fn.ret = TypeExpr::named("int", fn.line, fn.col);
        return fn;
    }

    Function Parser::parseExtern()
    {
        Function fn;
        fn.line = cur().line;
        fn.col = cur().col;
        fn.isExtern = true;
        advance();
        expect(Tok::KwFn, "after 'extern'");
        fn.name = expect(Tok::Ident, "as the name of the external function").text;
        fn.params = parseParams(false);
        if (atTypeStart() || check(Tok::LParen))
            fn.ret = parseResult();
        return fn;
    }

    StructDecl Parser::parseStruct()
    {
        StructDecl decl;
        decl.line = cur().line;
        decl.col = cur().col;
        advance();
        decl.name = expect(Tok::Ident, "after 'struct'").text;
        expect(Tok::LBrace, "after the struct name");
        while (!check(Tok::RBrace))
        {
            if (check(Tok::Eof))
                errorHere("unexpected end of file, expected '}'");
            std::vector<Field> group;
            do
            {
                Field f;
                f.line = cur().line;
                f.col = cur().col;
                f.name = expect(Tok::Ident, "as a field name").text;
                group.push_back(std::move(f));
            } while (accept(Tok::Comma));
            TypeExpr type = parseType("as the field type");
            for (Field &f : group)
            {
                f.type = type;
                decl.fields.push_back(std::move(f));
            }
            accept(Tok::Semi);
        }
        advance();
        return decl;
    }

    InterfaceDecl Parser::parseInterface()
    {
        InterfaceDecl decl;
        decl.line = cur().line;
        decl.col = cur().col;
        advance();
        decl.name = expect(Tok::Ident, "after 'interface'").text;
        expect(Tok::LBrace, "after the interface name");
        while (!check(Tok::RBrace))
        {
            if (check(Tok::Eof))
                errorHere("unexpected end of file, expected '}'");
            MethodSig m;
            m.line = cur().line;
            m.col = cur().col;
            m.name = expect(Tok::Ident, "as a method name").text;
            m.params = parseParams(false);
            if (check(Tok::LParen) ||
                (atTypeStart() && !(check(Tok::Ident) && peekTok().kind == Tok::LParen)))
                m.ret = parseResult();
            decl.methods.push_back(std::move(m));
            accept(Tok::Semi);
        }
        advance();
        return decl;
    }

    std::unique_ptr<VarDecl> Parser::parseConstSpec(int iota, ConstTemplate &tmpl)
    {
        auto decl = make<VarDecl>(cur());
        decl->isConst = true;
        const Token &nameTok = expect(Tok::Ident, "as the constant name");
        decl->name = nameTok.text;
        long long savedIota = iota_;
        iota_ = iota;
        bool implicit = tmpl.valid && (atLineStart() || check(Tok::RParen));
        if (implicit)
        {
            size_t resume = pos_;
            pos_ = tmpl.typePos;
            if (tmpl.hasType)
                decl->type = parseType("after the constant name");
            pos_ = tmpl.exprPos;
            decl->init = parseExpr();
            pos_ = resume;
        }
        else
        {
            if (atTypeStart())
            {
                tmpl.hasType = true;
                tmpl.typePos = pos_;
                decl->type = parseType("after the constant name");
            }
            else
            {
                tmpl.hasType = false;
                tmpl.typePos = pos_;
            }
            expect(Tok::Assign, "after the constant name (a constant needs a value)");
            tmpl.exprPos = pos_;
            tmpl.valid = true;
            decl->init = parseExpr();
        }
        iota_ = savedIota;
        return decl;
    }

    void Parser::parseConsts(std::vector<std::unique_ptr<VarDecl>> &out)
    {
        advance();
        ConstTemplate tmpl;
        if (accept(Tok::LParen))
        {
            int iota = 0;
            while (!check(Tok::RParen))
            {
                if (check(Tok::Eof))
                    errorHere("unexpected end of file, expected ')'");
                out.push_back(parseConstSpec(iota++, tmpl));
            }
            advance();
            return;
        }
        out.push_back(parseConstSpec(0, tmpl));
    }

    Block Parser::parseBlock(const char *context)
    {
        expect(Tok::LBrace, context);
        FlagGuard guard(noStructLit_, false);
        Block body;
        while (!check(Tok::RBrace))
        {
            if (check(Tok::Eof))
                errorHere("unexpected end of file, expected '}'");
            body.push_back(parseStatement());
            accept(Tok::Semi);
        }
        advance();
        return body;
    }

    std::unique_ptr<VarDecl> Parser::parseVarDecl()
    {
        auto decl = make<VarDecl>(cur());
        advance();
        decl->name = expect(Tok::Ident, "after 'var*'").text;
        if (check(Tok::Plus) && peekTok().kind == Tok::LBrace)
        {
            advance();
            advance();
            decl->type = parseType("inside '{ }'");
            expect(Tok::RBrace, "after the type");
        }
        else if (atTypeStart())
        {
            decl->type = parseType("after the variable name");
        }
        if (accept(Tok::Assign))
            decl->init = parseExpr();
        else if (!decl->type)
            errorHere("expected '=' or a type after the variable name");
        return decl;
    }

    StmtPtr Parser::parseVarStmt()
    {
        if (peekTok(2).kind != Tok::Comma)
            return parseVarDecl();
        auto decl = make<MultiVarDecl>(cur());
        advance();
        do
        {
            decl->cols.push_back(cur().col);
            decl->names.push_back(expect(Tok::Ident, "after 'var*'").text);
        } while (accept(Tok::Comma));
        expect(Tok::Assign, "after the variable names");
        do
        {
            decl->values.push_back(parseExpr());
        } while (accept(Tok::Comma));
        return decl;
    }

    std::unique_ptr<VarDecl> Parser::parseManualVarDecl()
    {
        auto decl = make<VarDecl>(cur());
        decl->isManual = true;
        advance();
        expect(Tok::ColonAssign, "after 'var&' (write: var& := name = malloc(free[T] N))");
        decl->name = expect(Tok::Ident, "after 'var& :='").text;
        expect(Tok::Assign, "after the variable name (write: var& := name = malloc(free[T] N))");
        const Token &initTok = cur();
        {
            FlagGuard guard(allowMalloc_, true);
            decl->init = parseExpr();
        }
        if (!dynamic_cast<MallocExpr *>(decl->init.get()))
            throw CompileError("'var&' must be initialized with malloc(free[T] N)", initTok.line,
                               initTok.col);
        return decl;
    }

    ExprPtr Parser::parseMalloc()
    {
        const Token &at = cur();
        if (!allowMalloc_)
            errorHere("'malloc' can only be used as the initializer of 'var&': "
                      "var& := name = malloc(free[T] N)");
        auto node = make<MallocExpr>(at);
        advance();
        expect(Tok::LParen, "after 'malloc'");
        if (!(check(Tok::Ident) && cur().text == "free" && peekTok().kind == Tok::LBracket))
            errorHere("'malloc' needs a block description: malloc(free[T] N)");
        advance();
        advance();
        node->elem = parseType("as the element type in 'free[T]'");
        expect(Tok::RBracket, "after the element type in 'free[T]'");
        {
            FlagGuard guard(allowMalloc_, false);
            FlagGuard structGuard(noStructLit_, false);
            node->count = parseExpr();
        }
        expect(Tok::RParen, "after the size in 'malloc'");
        return node;
    }

    StmtPtr Parser::parseSimpleStatement()
    {
        if (check(Tok::KwVar))
            return parseVarStmt();
        if (check(Tok::KwVarManual))
            return parseManualVarDecl();

        const Token &start = cur();
        ExprPtr target = parseExpr();
        if (check(Tok::Comma))
        {
            auto multi = make<MultiAssign>(start);
            multi->targets.push_back(std::move(target));
            while (accept(Tok::Comma))
                multi->targets.push_back(parseExpr());
            expect(Tok::Assign, "after the assignment targets");
            do
            {
                multi->values.push_back(parseExpr());
            } while (accept(Tok::Comma));
            return multi;
        }
        if (!isAssignOp(cur().kind))
        {
            auto stmt = make<ExprStmt>(start);
            if (!dynamic_cast<Call *>(target.get()))
                throw CompileError("expression result is unused; only calls can be statements",
                                   start.line, start.col);
            stmt->expr = std::move(target);
            return stmt;
        }

        auto assign = make<Assign>(start);
        assign->target = std::move(target);
        const Token &opTok = advance();

        auto one = [&]
        {
            auto lit = make<IntLit>(opTok);
            lit->value = 1;
            return lit;
        };

        switch (opTok.kind)
        {
        case Tok::Assign:
            assign->op = AssignOp::Set;
            break;
        case Tok::PlusAssign:
            assign->op = AssignOp::Add;
            break;
        case Tok::MinusAssign:
            assign->op = AssignOp::Sub;
            break;
        case Tok::StarAssign:
            assign->op = AssignOp::Mul;
            break;
        case Tok::SlashAssign:
            assign->op = AssignOp::Div;
            break;
        case Tok::PercentAssign:
            assign->op = AssignOp::Mod;
            break;
        case Tok::PlusPlus:
            assign->op = AssignOp::Add;
            assign->value = one();
            return assign;
        case Tok::MinusMinus:
            assign->op = AssignOp::Sub;
            assign->value = one();
            return assign;
        default:
            break;
        }
        assign->value = parseExpr();
        return assign;
    }

    StmtPtr Parser::parseStatement()
    {
        switch (cur().kind)
        {
        case Tok::KwVar:
            return parseVarStmt();
        case Tok::KwVarManual:
            return parseManualVarDecl();
        case Tok::KwConst:
            errorHere("constants can only be declared at the top level of a file");
        case Tok::KwIf:
            return parseIf();
        case Tok::KwWhile:
            return parseWhile();
        case Tok::KwFor:
            return parseFor();
        case Tok::KwSwitch:
            return parseSwitch();
        case Tok::KwReturn:
        {
            const Token &retTok = cur();
            auto stmt = make<Return>(retTok);
            advance();
            if (!check(Tok::RBrace) && !check(Tok::Semi) && cur().line == retTok.line)
            {
                stmt->value = parseExpr();
                while (accept(Tok::Comma))
                    stmt->rest.push_back(parseExpr());
            }
            return stmt;
        }
        case Tok::KwBreak:
        {
            auto stmt = make<Break>(cur());
            advance();
            return stmt;
        }
        case Tok::KwAsm:
        {
            auto stmt = make<AsmStmt>(cur());
            stmt->text = cur().text;
            stmt->names = asmPlaceholders(stmt->text);
            advance();
            return stmt;
        }
        case Tok::KwContinue:
        {
            auto stmt = make<Continue>(cur());
            advance();
            return stmt;
        }
        default:
            break;
        }
        return parseSimpleStatement();
    }

    ExprPtr Parser::parseHeaderExpr()
    {
        FlagGuard guard(noStructLit_, true);
        return parseExpr();
    }

    StmtPtr Parser::parseIf()
    {
        auto node = make<If>(cur());
        advance();
        node->cond = parseHeaderExpr();
        node->thenBody = parseBlock("after the 'if' condition");
        if (check(Tok::KwElse))
        {
            advance();
            node->hasElse = true;
            if (check(Tok::KwIf))
                node->elseBody.push_back(parseIf());
            else
                node->elseBody = parseBlock("after 'else'");
        }
        return node;
    }

    StmtPtr Parser::parseWhile()
    {
        auto loop = make<Loop>(cur());
        advance();
        loop->cond = parseHeaderExpr();
        loop->body = parseBlock("after the 'while' condition");
        return loop;
    }

    StmtPtr Parser::parseFor()
    {
        const Token &forTok = cur();
        advance();

        bool oneName = check(Tok::Ident) && peekTok().kind == Tok::KwIn;
        bool twoNames = check(Tok::Ident) && peekTok().kind == Tok::Comma &&
                        peekTok(2).kind == Tok::Ident && peekTok(3).kind == Tok::KwIn;
        if (oneName || twoNames)
        {
            auto loop = make<ForIn>(forTok);
            if (twoNames)
            {
                loop->key = advance().text;
                advance();
            }
            loop->value = advance().text;
            advance();
            loop->iterable = parseHeaderExpr();
            if (accept(Tok::DotDot))
            {
                if (twoNames)
                    throw CompileError("a range 'a..b' has only one loop variable", forTok.line,
                                       forTok.col);
                loop->rangeEnd = parseHeaderExpr();
            }
            loop->body = parseBlock("after the 'for' header");
            return loop;
        }

        auto loop = make<Loop>(forTok);
        if (check(Tok::LBrace))
        {
            loop->body = parseBlock("");
            return loop;
        }

        StmtPtr first;
        {
            FlagGuard guard(noStructLit_, true);
            if (check(Tok::Semi))
                first = nullptr;
            else if (check(Tok::KwVar))
                first = parseVarDecl();
            else if (check(Tok::KwVarManual))
                first = parseManualVarDecl();
            else
            {
                size_t start = pos_;
                ExprPtr e = parseExpr();
                if (!isAssignOp(cur().kind) && !check(Tok::Semi))
                {
                    loop->cond = std::move(e);
                    loop->body = parseBlock("after the 'for' condition");
                    return loop;
                }
                pos_ = start;
                first = parseSimpleStatement();
            }
        }

        loop->init = std::move(first);
        expect(Tok::Semi, "after the 'for' initializer");
        if (!check(Tok::Semi))
            loop->cond = parseHeaderExpr();
        expect(Tok::Semi, "after the 'for' condition");
        if (!check(Tok::LBrace))
        {
            FlagGuard guard(noStructLit_, true);
            loop->post = parseSimpleStatement();
        }
        loop->body = parseBlock("after the 'for' header");
        return loop;
    }

    StmtPtr Parser::parseSwitch()
    {
        auto sw = make<Switch>(cur());
        advance();
        if (!check(Tok::LBrace))
        {
            if (check(Tok::Ident) && peekTok().kind == Tok::Assign)
            {
                sw->bind = cur().text;
                advance();
                advance();
            }
            sw->tag = parseHeaderExpr();
            auto *ta = dynamic_cast<TypeAssert *>(sw->tag.get());
            if (ta && ta->isTypeKeyword)
            {
                sw->typeSwitch = true;
                ExprPtr base = std::move(ta->base);
                sw->tag = std::move(base);
            }
            else if (!sw->bind.empty())
                errorHere("'name =' in a 'switch' header needs a type switch: switch v = x.(type)");
        }
        expect(Tok::LBrace, "after the 'switch' value");
        while (!check(Tok::RBrace))
        {
            if (check(Tok::KwCase))
            {
                Case c;
                c.line = cur().line;
                c.col = cur().col;
                advance();
                do
                {
                    if (!sw->typeSwitch)
                        c.values.push_back(parseHeaderExpr());
                    else if (check(Tok::KwNil))
                    {
                        c.types.push_back(TypeExpr::named("nil", cur().line, cur().col));
                        advance();
                    }
                    else
                        c.types.push_back(parseType("as a type in 'case'"));
                } while (accept(Tok::Comma));
                c.body = parseBlock("after the 'case' values");
                sw->cases.push_back(std::move(c));
            }
            else if (check(Tok::KwDefault))
            {
                if (sw->hasDefault)
                    errorHere("a 'switch' can have only one 'default'");
                advance();
                sw->hasDefault = true;
                sw->defaultBody = parseBlock("after 'default'");
            }
            else
            {
                errorHere(std::string("expected 'case', 'default' or '}' in 'switch', found ") +
                          tokName(cur().kind));
            }
        }
        advance();
        return sw;
    }

    ExprPtr Parser::parseExpr()
    {
        return parseBinary(0);
    }

    ExprPtr Parser::parseBinary(int level)
    {
        const auto &levels = opLevels();
        if (level >= static_cast<int>(levels.size()))
            return parseUnary();

        ExprPtr lhs = parseBinary(level + 1);
        while (true)
        {
            const BinOp *found = nullptr;
            if (atLineStart())
                return lhs;
            for (const auto &[tok, op] : levels[level].ops)
                if (check(tok))
                    found = &op;
            if (!found)
                return lhs;
            auto bin = make<Binary>(cur());
            advance();
            bin->op = *found;
            bin->lhs = std::move(lhs);
            bin->rhs = parseBinary(level + 1);
            lhs = std::move(bin);
        }
    }

    ExprPtr Parser::parseUnary()
    {
        UnOp op;
        switch (cur().kind)
        {
        case Tok::Minus:
            op = UnOp::Neg;
            break;
        case Tok::Bang:
            op = UnOp::Not;
            break;
        case Tok::Tilde:
            op = UnOp::BitNot;
            break;
        case Tok::Star:
            op = UnOp::Deref;
            break;
        case Tok::Amp:
            op = UnOp::AddrOf;
            break;
        default:
            return parsePostfix();
        }
        auto un = make<Unary>(cur());
        advance();
        un->op = op;
        un->operand = parseUnary();
        return un;
    }

    ExprPtr Parser::parsePostfix()
    {
        ExprPtr e = parsePrimary();
        while (true)
        {
            if (check(Tok::Dot) && peekTok().kind == Tok::LParen)
            {
                auto ta = make<TypeAssert>(cur());
                advance();
                advance();
                if (check(Tok::Ident) && cur().text == "type" && peekTok().kind == Tok::RParen)
                {
                    ta->isTypeKeyword = true;
                    advance();
                }
                else
                {
                    FlagGuard guard(noStructLit_, false);
                    ta->type = parseType("in a type assertion");
                }
                expect(Tok::RParen, "after the asserted type");
                ta->base = std::move(e);
                e = std::move(ta);
            }
            else if (check(Tok::Dot))
            {
                auto sel = make<Selector>(cur());
                advance();
                sel->name = expect(Tok::Ident, "after '.'").text;
                sel->base = std::move(e);
                e = std::move(sel);
            }
            else if (check(Tok::LBracket) && !atLineStart())
            {
                auto idx = make<Index>(cur());
                advance();
                FlagGuard guard(noStructLit_, false);
                idx->index = parseExpr();
                expect(Tok::RBracket, "after the index");
                idx->base = std::move(e);
                e = std::move(idx);
            }
            else if (check(Tok::LParen) && !atLineStart())
            {
                auto call = make<Call>(cur());
                call->line = e->line;
                call->col = e->col;
                advance();
                FlagGuard guard(noStructLit_, false);
                auto *ref = dynamic_cast<VarRef *>(e.get());
                if (ref && (ref->name == "make" || ref->name == "new") && !check(Tok::RParen))
                {
                    call->typeArg = parseType("as the type argument");
                    if (!check(Tok::RParen))
                        expect(Tok::Comma, "after the type argument");
                }
                while (!check(Tok::RParen))
                {
                    call->args.push_back(parseExpr());
                    if (check(Tok::Ellipsis))
                    {
                        advance();
                        call->spread = true;
                        if (check(Tok::Comma))
                            errorHere("'...' must be applied to the last argument");
                        break;
                    }
                    if (!accept(Tok::Comma))
                        break;
                }
                expect(Tok::RParen, "after call arguments");
                call->callee = std::move(e);
                e = std::move(call);
            }
            else
            {
                return e;
            }
        }
    }

    bool Parser::atStructLitBody() const
    {
        if (noStructLit_ || !check(Tok::LBrace))
            return false;
        Tok next = peekTok().kind;
        return next == Tok::RBrace || (next == Tok::Ident && peekTok(2).kind == Tok::Colon);
    }

    ExprPtr Parser::parseStructLit(TypeExpr type, const Token &at)
    {
        auto lit = make<StructLit>(at);
        lit->type = std::move(type);
        expect(Tok::LBrace, "");
        FlagGuard guard(noStructLit_, false);
        while (!check(Tok::RBrace))
        {
            FieldInit f;
            f.line = cur().line;
            f.col = cur().col;
            f.name = expect(Tok::Ident, "as a field name").text;
            expect(Tok::Colon, "after the field name");
            f.value = parseExpr();
            lit->fields.push_back(std::move(f));
            if (!accept(Tok::Comma))
                break;
        }
        expect(Tok::RBrace, "after the struct fields");
        return lit;
    }

    ExprPtr Parser::parsePrimary()
    {
        const Token &t = cur();

        switch (t.kind)
        {
        case Tok::Int:
        {
            auto lit = make<IntLit>(t);
            try
            {
                lit->value = std::stoll(t.text);
            }
            catch (const std::out_of_range &)
            {
                errorHere("integer literal is too large");
            }
            advance();
            return lit;
        }
        case Tok::Float:
        {
            auto lit = make<FloatLit>(t);
            lit->value = std::stod(t.text);
            advance();
            return lit;
        }
        case Tok::Char:
        {
            auto lit = make<CharLit>(t);
            lit->value = t.text[0];
            advance();
            return lit;
        }
        case Tok::KwTrue:
        case Tok::KwFalse:
        {
            auto lit = make<BoolLit>(t);
            lit->value = t.kind == Tok::KwTrue;
            advance();
            return lit;
        }
        case Tok::KwNil:
        {
            advance();
            return make<NilLit>(t);
        }
        case Tok::String:
        {
            auto lit = make<StringLit>(t);
            lit->value = t.text;
            advance();
            return lit;
        }
        case Tok::Ident:
        {
            if (t.text == "malloc" && peekTok().kind == Tok::LParen)
                return parseMalloc();
            if (t.text == "iota" && iota_ >= 0)
            {
                auto lit = make<IntLit>(t);
                lit->value = iota_;
                advance();
                return lit;
            }
            advance();
            if (atStructLitBody())
                return parseStructLit(TypeExpr::named(t.text, t.line, t.col), t);
            if (check(Tok::Dot) && peekTok().kind == Tok::Ident && !noStructLit_ &&
                peekTok(2).kind == Tok::LBrace &&
                (peekTok(3).kind == Tok::RBrace ||
                 (peekTok(3).kind == Tok::Ident && peekTok(4).kind == Tok::Colon)))
            {
                advance();
                TypeExpr type = TypeExpr::named(advance().text, t.line, t.col);
                type.qualifier = t.text;
                return parseStructLit(std::move(type), t);
            }
            auto ref = make<VarRef>(t);
            ref->name = t.text;
            return ref;
        }
        case Tok::LParen:
        {
            advance();
            FlagGuard guard(noStructLit_, false);
            ExprPtr inner = parseExpr();
            expect(Tok::RParen, "to close the parenthesized expression");
            return inner;
        }
        case Tok::KwMap:
        {
            auto lit = make<MapLit>(t);
            lit->type = parseType("as the map type");
            expect(Tok::LBrace, "in the map literal");
            FlagGuard guard(noStructLit_, false);
            while (!check(Tok::RBrace))
            {
                lit->keys.push_back(parseExpr());
                expect(Tok::Colon, "after the map key");
                lit->values.push_back(parseExpr());
                if (!accept(Tok::Comma))
                    break;
            }
            expect(Tok::RBrace, "after the map entries");
            return lit;
        }
        case Tok::LBracket:
        {
            auto lit = make<ArrayLit>(t);
            advance();
            FlagGuard guard(noStructLit_, false);
            while (!check(Tok::RBracket))
            {
                lit->elems.push_back(parseExpr());
                if (!accept(Tok::Comma))
                    break;
            }
            expect(Tok::RBracket, "after the array elements");
            return lit;
        }
        default:
            errorHere(std::string("expected an expression, found ") + tokName(t.kind));
        }
    }

}
