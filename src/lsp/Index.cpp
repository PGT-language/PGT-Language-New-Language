#include "lsp/Index.h"

#include "common/Error.h"
#include "lexer/Lexer.h"

#include <sstream>

namespace pgt::lsp
{
    namespace
    {

        std::vector<std::string> splitLines(const std::string &text)
        {
            std::vector<std::string> lines;
            std::string cur;
            for (char c : text)
            {
                if (c == '\n')
                {
                    if (!cur.empty() && cur.back() == '\r')
                        cur.pop_back();
                    lines.push_back(cur);
                    cur.clear();
                }
                else
                    cur += c;
            }
            lines.push_back(cur);
            return lines;
        }

        Pos posOf(const Token &t)
        {
            return Pos{t.line, t.col};
        }

        class Scanner
        {
        public:
            Scanner(FileIndex &f) : f_(f), t_(f.tokens)
            {
                match_.assign(t_.size(), -1);
                std::vector<size_t> stack;
                for (size_t i = 0; i < t_.size(); ++i)
                {
                    if (t_[i].kind == Tok::LBrace)
                        stack.push_back(i);
                    else if (t_[i].kind == Tok::RBrace && !stack.empty())
                    {
                        match_[stack.back()] = static_cast<long>(i);
                        match_[i] = static_cast<long>(stack.back());
                        stack.pop_back();
                    }
                }
            }

            void run()
            {
                size_t i = 0;
                while (i < t_.size() && t_[i].kind != Tok::Eof)
                {
                    switch (t_[i].kind)
                    {
                    case Tok::KwPackage:
                        if (is(i + 1, Tok::Ident))
                            f_.packageName = t_[i + 1].text;
                        i += 2;
                        break;
                    case Tok::KwUse:
                        i = imports(i + 1);
                        break;
                    case Tok::KwFn:
                        i = function(i);
                        break;
                    case Tok::KwExtern:
                        i = externFn(i);
                        break;
                    case Tok::KwStruct:
                        i = structDecl(i);
                        break;
                    case Tok::KwInterface:
                        i = ifaceDecl(i);
                        break;
                    case Tok::KwConst:
                        i = constDecl(i);
                        break;
                    case Tok::KwVar:
                        i = globalVar(i);
                        break;
                    case Tok::LBrace:
                        i = skipBlock(i);
                        break;
                    default:
                        ++i;
                    }
                }
            }

        private:
            bool is(size_t i, Tok k) const
            {
                return i < t_.size() && t_[i].kind == k;
            }

            size_t skipBlock(size_t i) const
            {
                return match_[i] >= 0 ? static_cast<size_t>(match_[i]) + 1 : t_.size() - 1;
            }

            Pos endOf(size_t closing) const
            {
                if (closing < t_.size())
                    return posOf(t_[closing]);
                return Pos{1 << 30, 0};
            }

            std::string join(size_t from, size_t to) const
            {
                std::string out;
                for (size_t k = from; k < to && k < t_.size(); ++k)
                {
                    const Token &tok = t_[k];
                    std::string text = tok.kind == Tok::String ? "\"" + tok.text + "\""
                                       : tok.kind == Tok::Char ? "'" + tok.text + "'"
                                                               : tok.text;
                    if (text.empty())
                        text = tokName(tok.kind);
                    if (!out.empty())
                    {
                        Tok prev = t_[k - 1].kind;
                        bool glue = tok.kind == Tok::LParen || tok.kind == Tok::RParen ||
                                    tok.kind == Tok::Comma || tok.kind == Tok::Dot ||
                                    tok.kind == Tok::RBracket || tok.kind == Tok::RBrace ||
                                    prev == Tok::LParen || prev == Tok::Dot || prev == Tok::LBracket ||
                                    prev == Tok::Star || prev == Tok::LBrace ||
                                    (prev == Tok::RBracket && tok.kind != Tok::LBrace);
                        if (tok.kind == Tok::LParen && prev == Tok::KwFn)
                            glue = false;
                        if (tok.kind == Tok::LBrace && prev == Tok::Plus)
                            glue = false;
                        if (!glue)
                            out += ' ';
                    }
                    out += text;
                }
                return out;
            }

            std::string typeText(size_t from, size_t to) const
            {
                return join(from, to);
            }

            size_t typeEnd(size_t i) const
            {
                while (is(i, Tok::Star) || (is(i, Tok::LBracket) && is(i + 1, Tok::RBracket)))
                    i += is(i, Tok::Star) ? 1 : 2;
                if (is(i, Tok::Ident))
                {
                    ++i;
                    if (is(i, Tok::Dot) && is(i + 1, Tok::Ident))
                        i += 2;
                }
                return i;
            }

            void add(SymKind kind, const Token &name, std::string owner, std::string detail,
                     std::string type, Pos scopeBegin = {}, Pos scopeEnd = {})
            {
                Symbol s;
                s.kind = kind;
                s.name = name.text;
                s.owner = std::move(owner);
                s.detail = std::move(detail);
                s.type = std::move(type);
                s.file = f_.path;
                s.pos = posOf(name);
                s.scopeBegin = scopeBegin;
                s.scopeEnd = scopeEnd;
                f_.symbols.push_back(std::move(s));
            }

            size_t imports(size_t i)
            {
                auto one = [&](size_t k)
                {
                    const Token &t = t_[k];
                    int len = static_cast<int>(t.text.size()) + 2;
                    if (t.line - 1 < static_cast<int>(f_.lines.size()))
                    {
                        const std::string &ln = f_.lines[t.line - 1];
                        size_t close = ln.find('"', static_cast<size_t>(t.col));
                        if (close != std::string::npos)
                            len = static_cast<int>(close) - (t.col - 1) + 1;
                    }
                    f_.imports.push_back(ImportRef{normalizeImport(t.text), posOf(t), len});
                };
                if (is(i, Tok::String))
                {
                    one(i);
                    return i + 1;
                }
                if (is(i, Tok::LParen))
                {
                    ++i;
                    while (i < t_.size() && !is(i, Tok::RParen) && !is(i, Tok::Eof))
                    {
                        if (is(i, Tok::String))
                            one(i);
                        ++i;
                    }
                    return i + 1;
                }
                return i;
            }

            std::string paramsAndLocals(size_t open, size_t close, Pos scopeBegin, Pos scopeEnd)
            {
                size_t k = open + 1;
                bool expectName = true;
                int depth = 0;
                for (; k < close; ++k)
                {
                    if (t_[k].kind == Tok::LParen)
                        ++depth;
                    else if (t_[k].kind == Tok::RParen)
                        --depth;
                    else if (t_[k].kind == Tok::Comma && depth == 0)
                    {
                        expectName = true;
                        continue;
                    }
                    if (expectName && depth == 0 && t_[k].kind == Tok::Ident)
                    {
                        size_t te = typeEnd(k + 1);
                        std::string ty = te > k + 1 && te <= close ? typeText(k + 1, te) : "";
                        add(SymKind::Param, t_[k], "", t_[k].text + (ty.empty() ? "" : " " + ty), ty,
                            scopeBegin, scopeEnd);
                        expectName = false;
                    }
                }
                return "";
            }

            void locals(size_t open, size_t close)
            {
                for (size_t k = open + 1; k < close; ++k)
                {
                    Tok kind = t_[k].kind;
                    if (kind == Tok::KwVar || kind == Tok::KwVarManual)
                    {
                        size_t n = k + 1;
                        if (is(n, Tok::ColonAssign))
                            ++n;
                        if (!is(n, Tok::Ident))
                            continue;
                        size_t blockEnd = enclosingEnd(k, close);
                        std::string ty = declType(n + 1, kind == Tok::KwVarManual);
                        add(SymKind::Local, t_[n], "", lineText(k), ty, posOf(t_[n]), endOf(blockEnd));
                    }
                    else if (kind == Tok::KwFor)
                    {
                        size_t n = k + 1;
                        size_t blockEnd = n;
                        while (blockEnd < close && !is(blockEnd, Tok::LBrace))
                            ++blockEnd;
                        if (blockEnd >= close)
                            continue;
                        bool hasIn = false;
                        for (size_t q = n; q < blockEnd; ++q)
                            if (t_[q].kind == Tok::KwIn)
                                hasIn = true;
                        if (!hasIn)
                            continue;
                        size_t end = match_[blockEnd] >= 0 ? static_cast<size_t>(match_[blockEnd]) : close;
                        while (is(n, Tok::Ident) || is(n, Tok::Comma))
                        {
                            if (is(n, Tok::Ident))
                                add(SymKind::Local, t_[n], "", "for " + t_[n].text, "", posOf(t_[n]),
                                    endOf(end));
                            ++n;
                        }
                    }
                }
            }

            size_t enclosingEnd(size_t k, size_t fallback) const
            {
                int depth = 0;
                for (size_t q = k; q < fallback; ++q)
                {
                    if (t_[q].kind == Tok::LBrace)
                        ++depth;
                    else if (t_[q].kind == Tok::RBrace)
                    {
                        if (depth == 0)
                            return q;
                        --depth;
                    }
                }
                return fallback;
            }

            std::string lineText(size_t k) const
            {
                size_t e = k;
                while (e < t_.size() && t_[e].line == t_[k].line && t_[e].kind != Tok::Eof)
                    ++e;
                return join(k, e);
            }

            std::string declType(size_t n, bool manual) const
            {
                if (manual)
                {
                    for (size_t q = n; q < t_.size() && t_[q].line == t_[n - 1].line; ++q)
                        if (t_[q].kind == Tok::LBracket && is(q + 1, Tok::Ident) && is(q + 2, Tok::RBracket))
                            return "[]" + t_[q + 1].text;
                    return "";
                }
                if (is(n, Tok::Plus) && is(n + 1, Tok::LBrace))
                {
                    size_t te = typeEnd(n + 2);
                    return typeText(n + 2, te);
                }
                if (is(n, Tok::Ident) || is(n, Tok::Star) || is(n, Tok::LBracket))
                {
                    if (!is(n, Tok::Assign))
                    {
                        size_t te = typeEnd(n);
                        if (te > n && !is(te, Tok::LParen) && !is(te, Tok::LBrace))
                            return typeText(n, te);
                    }
                }
                if (!is(n, Tok::Assign))
                    return "";
                size_t e = n + 1;
                if (is(e, Tok::Amp))
                    ++e;
                if (is(e, Tok::Ident) && t_[e].text == "new" && is(e + 1, Tok::LParen))
                {
                    size_t te = typeEnd(e + 2);
                    return "*" + typeText(e + 2, te);
                }
                if (is(e, Tok::Ident))
                {
                    size_t te = e + 1;
                    if (is(te, Tok::Dot) && is(te + 1, Tok::Ident))
                        te += 2;
                    if (is(te, Tok::LBrace))
                        return typeText(e, te);
                    if (is(te, Tok::LParen))
                        return "call:" + typeText(e, te);
                    if (te == e + 1)
                        return "var:" + t_[e].text;
                }
                return "";
            }

            size_t function(size_t i)
            {
                size_t start = i;
                size_t k = i + 1;
                std::string owner;
                if (is(k, Tok::LParen))
                {
                    size_t q = k + 1;
                    while (q < t_.size() && !is(q, Tok::RParen) && !is(q, Tok::Eof))
                    {
                        if (is(q, Tok::Ident))
                            owner = t_[q].text;
                        ++q;
                    }
                    k = q + 1;
                }
                if (!is(k, Tok::Ident))
                    return k;
                size_t nameTok = k;
                size_t open = k + 1;
                if (!is(open, Tok::LParen))
                    return open;
                int depth = 0;
                size_t close = open;
                for (; close < t_.size(); ++close)
                {
                    if (t_[close].kind == Tok::LParen)
                        ++depth;
                    else if (t_[close].kind == Tok::RParen && --depth == 0)
                        break;
                }
                size_t body = close + 1;
                while (body < t_.size() && !is(body, Tok::LBrace) && !is(body, Tok::Eof) &&
                       t_[body].line == t_[close].line)
                    ++body;
                std::string retType;
                if (body > close + 1)
                    retType = typeText(close + 1, body);
                std::string sig = join(start, body);
                add(owner.empty() ? SymKind::Function : SymKind::Method, t_[nameTok], owner, sig, retType);
                if (!is(body, Tok::LBrace))
                    return body;
                size_t bodyEnd = skipBlock(body) - 1;
                Pos scopeBegin = posOf(t_[start]);
                Pos scopeEnd = endOf(bodyEnd);
                if (!owner.empty() && is(i + 2, Tok::Ident))
                {
                    size_t q = i + 3;
                    while (is(q, Tok::Star))
                        ++q;
                    std::string recvType = is(q, Tok::Ident) ? t_[q].text : owner;
                    add(SymKind::Param, t_[i + 2], "", t_[i + 2].text + " " + join(i + 3, q + 1), recvType,
                        scopeBegin, scopeEnd);
                }
                paramsAndLocals(open, close, scopeBegin, scopeEnd);
                locals(body, bodyEnd);
                return bodyEnd + 1;
            }

            size_t externFn(size_t i)
            {
                size_t k = i + 1;
                if (is(k, Tok::KwFn))
                    ++k;
                if (!is(k, Tok::Ident))
                    return k;
                size_t e = k;
                while (e < t_.size() && t_[e].line == t_[i].line && !is(e, Tok::Eof))
                    ++e;
                size_t close = e;
                for (size_t q = k; q < e; ++q)
                    if (t_[q].kind == Tok::RParen)
                        close = q;
                std::string retType = close + 1 < e ? typeText(close + 1, e) : "";
                add(SymKind::Extern, t_[k], "", join(i, e), retType);
                return e;
            }

            size_t structDecl(size_t i)
            {
                if (!is(i + 1, Tok::Ident))
                    return i + 1;
                const Token &name = t_[i + 1];
                add(SymKind::Struct, name, "", "struct " + name.text, name.text);
                size_t open = i + 2;
                if (!is(open, Tok::LBrace))
                    return open;
                size_t close = skipBlock(open) - 1;
                size_t k = open + 1;
                while (k < close)
                {
                    int line = t_[k].line;
                    size_t e = k;
                    while (e < close && t_[e].line == line)
                        ++e;
                    std::vector<size_t> names;
                    size_t q = k;
                    while (q < e && is(q, Tok::Ident))
                    {
                        names.push_back(q);
                        if (is(q + 1, Tok::Comma))
                            q += 2;
                        else
                        {
                            ++q;
                            break;
                        }
                    }
                    if (!names.empty() && q < e)
                    {
                        std::string ty = typeText(q, e);
                        for (size_t n : names)
                            add(SymKind::Field, t_[n], name.text, t_[n].text + " " + ty, ty);
                    }
                    k = e;
                }
                return close + 1;
            }

            size_t ifaceDecl(size_t i)
            {
                if (!is(i + 1, Tok::Ident))
                    return i + 1;
                const Token &name = t_[i + 1];
                add(SymKind::Interface, name, "", "interface " + name.text, name.text);
                size_t open = i + 2;
                if (!is(open, Tok::LBrace))
                    return open;
                size_t close = skipBlock(open) - 1;
                size_t k = open + 1;
                while (k < close)
                {
                    int line = t_[k].line;
                    size_t e = k;
                    while (e < close && t_[e].line == line)
                        ++e;
                    if (is(k, Tok::Ident))
                        add(SymKind::InterfaceMethod, t_[k], name.text, join(k, e), "");
                    k = e;
                }
                return close + 1;
            }

            size_t constDecl(size_t i)
            {
                auto one = [&](size_t k) -> size_t
                {
                    size_t e = k;
                    while (e < t_.size() && t_[e].line == t_[k].line && !is(e, Tok::Eof) &&
                           !is(e, Tok::RParen))
                        ++e;
                    std::string ty;
                    if (is(k + 1, Tok::Ident) && !is(k + 2, Tok::Dot))
                        ty = t_[k + 1].text;
                    add(SymKind::Const, t_[k], "", "const " + join(k, e), ty);
                    return e;
                };
                size_t k = i + 1;
                if (is(k, Tok::Ident))
                    return one(k);
                if (!is(k, Tok::LParen))
                    return k;
                ++k;
                while (k < t_.size() && !is(k, Tok::RParen) && !is(k, Tok::Eof))
                {
                    if (is(k, Tok::Ident))
                        k = one(k);
                    else
                        ++k;
                }
                return k + 1;
            }

            size_t globalVar(size_t i)
            {
                if (!is(i + 1, Tok::Ident))
                    return i + 1;
                add(SymKind::Global, t_[i + 1], "", lineText(i), declType(i + 2, false));
                size_t e = i + 1;
                while (e < t_.size() && t_[e].line == t_[i].line && !is(e, Tok::Eof))
                    ++e;
                return e;
            }

            FileIndex &f_;
            const std::vector<Token> &t_;
            std::vector<long> match_;
        };

    }

    std::string normalizeImport(const std::string &path)
    {
        for (const char *prefix : {"pgt/std/", "std/"})
        {
            std::string p(prefix);
            if (path.size() > p.size() && path.compare(0, p.size(), p) == 0)
                return path.substr(p.size());
        }
        return path;
    }

    bool isTopLevel(SymKind k)
    {
        return k == SymKind::Function || k == SymKind::Extern || k == SymKind::Struct ||
               k == SymKind::Interface || k == SymKind::Global || k == SymKind::Const;
    }

    std::vector<Token> lenientTokens(const std::string &text)
    {
        std::vector<std::string> lines = splitLines(text);
        for (int attempt = 0; attempt < 64; ++attempt)
        {
            std::string src;
            for (size_t i = 0; i < lines.size(); ++i)
            {
                src += lines[i];
                if (i + 1 < lines.size())
                    src += '\n';
            }
            try
            {
                return Lexer(src).tokenize();
            }
            catch (const CompileError &e)
            {
                if (e.line <= 0 || e.line > static_cast<int>(lines.size()))
                    break;
                std::string &ln = lines[e.line - 1];
                size_t from = e.col > 0 ? static_cast<size_t>(e.col - 1) : 0;
                if (from >= ln.size() || ln.find_first_not_of(' ', from) == std::string::npos)
                    from = 0;
                bool changed = false;
                for (size_t c = from; c < ln.size(); ++c)
                    if (ln[c] != ' ')
                    {
                        ln[c] = ' ';
                        changed = true;
                    }
                if (!changed)
                    break;
            }
        }
        return {Token{Tok::Eof, "", 1, 1}};
    }

    FileIndex indexFile(const std::string &path, const std::string &text)
    {
        FileIndex f;
        f.path = path;
        f.text = text;
        f.lines = splitLines(text);
        f.tokens = lenientTokens(text);
        Scanner(f).run();
        return f;
    }

    std::vector<BuiltinDoc> parseBuiltinDocs(const std::string &text)
    {
        std::vector<BuiltinDoc> out;
        std::vector<std::string> lines = splitLines(text);
        for (size_t i = 0; i < lines.size(); ++i)
        {
            const std::string &ln = lines[i];
            if (ln.rfind("// fn ", 0) != 0)
                continue;
            BuiltinDoc d;
            d.signature = ln.substr(3);
            size_t nameStart = 6;
            size_t nameEnd = ln.find('(', nameStart);
            d.name = ln.substr(nameStart, nameEnd == std::string::npos ? std::string::npos : nameEnd - nameStart);
            d.pos = Pos{static_cast<int>(i) + 1, static_cast<int>(nameStart) + 1};
            std::ostringstream doc;
            for (size_t k = i + 1; k < lines.size() && lines[k].rfind("//", 0) == 0 &&
                                   lines[k].rfind("// fn ", 0) != 0;
                 ++k)
            {
                std::string body = lines[k].size() > 3 ? lines[k].substr(3) : "";
                doc << body << "\n";
            }
            d.doc = doc.str();
            out.push_back(std::move(d));
        }
        return out;
    }

}
