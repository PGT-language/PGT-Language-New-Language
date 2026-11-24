#include "lsp/Server.h"

#include "codegen/CodeGen.h"
#include "common/Error.h"
#include "common/Paths.h"
#include "lexer/Lexer.h"
#include "loader/Loader.h"
#include "lsp/Index.h"
#include "lsp/Json.h"
#include "modfile/ModFile.h"
#include "parser/Parser.h"

#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

namespace pgt::lsp
{
    namespace
    {

        namespace fs = std::filesystem;

        const char *const kKeywords[] = {"package", "fn",       "var*",    "var&",   "return", "#use",
                                         "if",      "else",     "while",   "for",    "in",     "break",
                                         "continue", "struct",  "interface", "const", "switch", "case",
                                         "default", "extern",   "true",    "false",  "nil",    "malloc", "map",    "asm"};
        const char *const kTypes[] = {"int", "int64", "float", "char", "byte", "string", "bool", "void", "any"};

        bool identChar(char c)
        {
            return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
        }

        std::string readFile(const fs::path &p)
        {
            std::ifstream in(p, std::ios::binary);
            if (!in)
                return "";
            std::ostringstream ss;
            ss << in.rdbuf();
            return ss.str();
        }

        std::string uriToPath(const std::string &uri)
        {
            std::string s = uri.rfind("file://", 0) == 0 ? uri.substr(7) : uri;
            std::string out;
            for (size_t i = 0; i < s.size(); ++i)
            {
                if (s[i] == '%' && i + 2 < s.size())
                {
                    out += static_cast<char>(std::stoi(s.substr(i + 1, 2), nullptr, 16));
                    i += 2;
                }
                else
                    out += s[i];
            }
            return out;
        }

        std::string pathToUri(const std::string &path)
        {
            std::string out = "file://";
            for (unsigned char c : path)
            {
                if (std::isalnum(c) || c == '/' || c == '-' || c == '_' || c == '.' || c == '~')
                    out += static_cast<char>(c);
                else
                {
                    char buf[4];
                    std::snprintf(buf, sizeof buf, "%%%02X", c);
                    out += buf;
                }
            }
            return out;
        }

        std::string canonical(const std::string &p)
        {
            std::error_code ec;
            fs::path c = fs::weakly_canonical(fs::absolute(p, ec), ec);
            return ec ? p : c.string();
        }

        int byteToUtf16(const std::string &line, int byteCol)
        {
            int units = 0;
            for (int i = 0; i < byteCol && i < static_cast<int>(line.size()); ++i)
            {
                unsigned char c = static_cast<unsigned char>(line[i]);
                if ((c & 0xC0) == 0x80)
                    continue;
                units += (c >= 0xF0) ? 2 : 1;
            }
            return units;
        }

        int utf16ToByte(const std::string &line, int units)
        {
            int i = 0;
            int u = 0;
            while (i < static_cast<int>(line.size()) && u < units)
            {
                unsigned char c = static_cast<unsigned char>(line[i]);
                int len = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
                u += len == 4 ? 2 : 1;
                i += len;
            }
            return i;
        }

        struct Hit
        {
            const Symbol *sym = nullptr;
            const BuiltinDoc *builtin = nullptr;
            std::optional<fs::path> packageDir;
            std::string packageName;
        };

        class Server
        {
        public:
            int run()
            {
                int protoFd = dup(1);
                dup2(2, 1);
                out_ = fdopen(protoFd, "wb");
                stdDir_ = findStdDir();
                if (stdDir_)
                {
                    fs::path doc = *stdDir_ / "builtin" / "builtin.pgt";
                    builtinFile_ = canonical(doc.string());
                    builtins_ = parseBuiltinDocs(readFile(doc));
                }
                std::string body;
                while (readMessage(body))
                {
                    Json msg;
                    try
                    {
                        msg = Json::parse(body);
                    }
                    catch (const std::exception &)
                    {
                        continue;
                    }
                    if (!handle(msg))
                        break;
                }
                return shutdown_ ? 0 : 1;
            }

        private:
            bool readMessage(std::string &body)
            {
                size_t length = 0;
                std::string line;
                while (true)
                {
                    line.clear();
                    int c;
                    while ((c = std::fgetc(stdin)) != EOF && c != '\n')
                        line += static_cast<char>(c);
                    if (c == EOF)
                        return false;
                    if (!line.empty() && line.back() == '\r')
                        line.pop_back();
                    if (line.empty())
                        break;
                    if (line.rfind("Content-Length:", 0) == 0)
                        length = std::stoul(line.substr(15));
                }
                body.assign(length, '\0');
                return std::fread(body.data(), 1, length, stdin) == length;
            }

            void send(const Json &msg)
            {
                std::string text = msg.dump();
                std::fprintf(out_, "Content-Length: %zu\r\n\r\n", text.size());
                std::fwrite(text.data(), 1, text.size(), out_);
                std::fflush(out_);
            }

            void reply(const Json &id, Json result)
            {
                Json m = Json::object();
                m["jsonrpc"] = "2.0";
                m["id"] = id;
                m["result"] = std::move(result);
                send(m);
            }

            void replyError(const Json &id, int code, const std::string &message)
            {
                Json m = Json::object();
                m["jsonrpc"] = "2.0";
                m["id"] = id;
                Json err = Json::object();
                err["code"] = code;
                err["message"] = message;
                m["error"] = err;
                send(m);
            }

            void notify(const std::string &method, Json params)
            {
                Json m = Json::object();
                m["jsonrpc"] = "2.0";
                m["method"] = method;
                m["params"] = std::move(params);
                send(m);
            }

            bool handle(const Json &msg)
            {
                const std::string &method = msg["method"].str();
                const Json &params = msg["params"];
                bool isRequest = msg.has("id");
                const Json &id = msg["id"];
                try
                {
                    if (method == "initialize")
                        reply(id, initialize());
                    else if (method == "shutdown")
                    {
                        shutdown_ = true;
                        reply(id, Json());
                    }
                    else if (method == "exit")
                        return false;
                    else if (method == "textDocument/didOpen")
                        didOpen(params["textDocument"]);
                    else if (method == "textDocument/didChange")
                        didChange(params);
                    else if (method == "textDocument/didSave")
                        fullCheck(uriToPath(params["textDocument"]["uri"].str()));
                    else if (method == "textDocument/didClose")
                        open_.erase(canonical(uriToPath(params["textDocument"]["uri"].str())));
                    else if (method == "textDocument/definition")
                        reply(id, definition(params));
                    else if (method == "textDocument/hover")
                        reply(id, hover(params));
                    else if (method == "textDocument/completion")
                        reply(id, completion(params));
                    else if (method == "textDocument/documentSymbol")
                        reply(id, documentSymbols(params));
                    else if (isRequest)
                        replyError(id, -32601, "method not supported: " + method);
                }
                catch (const std::exception &e)
                {
                    if (isRequest)
                        replyError(id, -32603, e.what());
                }
                return true;
            }

            Json initialize()
            {
                Json caps = Json::object();
                Json sync = Json::object();
                sync["openClose"] = true;
                sync["change"] = 1;
                Json save = Json::object();
                save["includeText"] = false;
                sync["save"] = save;
                caps["textDocumentSync"] = sync;
                Json comp = Json::object();
                Json triggers = Json::array();
                triggers.push(".");
                comp["triggerCharacters"] = triggers;
                caps["completionProvider"] = comp;
                caps["definitionProvider"] = true;
                caps["hoverProvider"] = true;
                caps["documentSymbolProvider"] = true;
                Json result = Json::object();
                result["capabilities"] = caps;
                Json info = Json::object();
                info["name"] = "pgt-lsp";
                info["version"] = PGT_VERSION;
                result["serverInfo"] = info;
                return result;
            }

            std::string textOf(const std::string &path)
            {
                auto it = open_.find(path);
                if (it != open_.end())
                    return it->second;
                return readFile(path);
            }

            const FileIndex &fileIndex(const std::string &path)
            {
                std::string text = textOf(path);
                auto it = cache_.find(path);
                if (it == cache_.end() || it->second.text != text)
                    it = cache_.insert_or_assign(path, indexFile(path, text)).first;
                return it->second;
            }

            PackageIndex package(const fs::path &dir)
            {
                PackageIndex pkg;
                pkg.dir = dir;
                std::set<std::string> paths;
                std::error_code ec;
                if (fs::is_directory(dir, ec))
                    for (const std::string &f : listSources(dir))
                        paths.insert(canonical(f));
                for (const auto &[p, text] : open_)
                    if (fs::path(p).parent_path() == dir && fs::path(p).extension() == ".pgt")
                        paths.insert(p);
                for (const std::string &p : paths)
                {
                    const FileIndex &f = fileIndex(p);
                    if (pkg.name.empty())
                        pkg.name = f.packageName;
                    pkg.files.push_back(&f);
                }
                return pkg;
            }

            std::optional<fs::path> importDir(const std::string &file, const std::string &importPath)
            {
                fs::path dir = fs::path(file).parent_path();
                std::optional<ModFile> mod;
                try
                {
                    if (auto m = findModFile(dir))
                        mod = parseModFile(*m);
                }
                catch (const std::exception &)
                {
                }
                auto d = resolveImportDir(importPath, mod, stdDir_);
                if (!d)
                    return std::nullopt;
                return fs::path(canonical(d->string()));
            }

            std::map<std::string, fs::path> importMap(const FileIndex &f)
            {
                std::map<std::string, fs::path> out;
                for (const ImportRef &imp : f.imports)
                {
                    auto dir = importDir(f.path, imp.path);
                    if (!dir)
                        continue;
                    PackageIndex pkg = package(*dir);
                    std::string name = pkg.name.empty() ? fs::path(imp.path).filename().string() : pkg.name;
                    out[name] = *dir;
                }
                return out;
            }

            Pos toPos(const FileIndex &f, const Json &position)
            {
                int line = position["line"].integer();
                int ch = position["character"].integer();
                std::string text = line < static_cast<int>(f.lines.size()) ? f.lines[line] : "";
                return Pos{line + 1, utf16ToByte(text, ch) + 1};
            }

            Json range(const std::string &path, Pos p, int byteLen)
            {
                const FileIndex &f = fileIndex(path);
                int line = std::max(p.line, 1) - 1;
                std::string text = line < static_cast<int>(f.lines.size()) ? f.lines[line] : "";
                int startByte = std::max(p.col, 1) - 1;
                Json start = Json::object();
                start["line"] = line;
                start["character"] = byteToUtf16(text, startByte);
                Json end = Json::object();
                end["line"] = line;
                end["character"] = byteToUtf16(text, startByte + byteLen);
                Json r = Json::object();
                r["start"] = start;
                r["end"] = end;
                return r;
            }

            Json location(const std::string &path, Pos p, int len)
            {
                Json loc = Json::object();
                loc["uri"] = pathToUri(path);
                loc["range"] = range(path, p, len);
                return loc;
            }

            Json packageLocation(const fs::path &dir)
            {
                PackageIndex pkg = package(dir);
                if (pkg.files.empty())
                    return Json();
                const FileIndex *f = pkg.files.front();
                Pos p{1, 1};
                for (const Token &t : f->tokens)
                    if (t.kind == Tok::KwPackage)
                    {
                        p = Pos{t.line, t.col};
                        break;
                    }
                return location(f->path, p, 7);
            }

            const Symbol *visibleLocal(const FileIndex &f, const std::string &name, Pos at)
            {
                const Symbol *best = nullptr;
                for (const Symbol &s : f.symbols)
                {
                    if (s.name != name || (s.kind != SymKind::Local && s.kind != SymKind::Param))
                        continue;
                    if (!(s.scopeBegin <= at) || !(at <= s.scopeEnd))
                        continue;
                    if (s.kind == SymKind::Local && at < s.pos)
                        continue;
                    if (!best || best->pos < s.pos)
                        best = &s;
                }
                return best;
            }

            std::vector<const Symbol *> topLevel(const PackageIndex &pkg, const std::string &name)
            {
                std::vector<const Symbol *> out;
                for (const FileIndex *f : pkg.files)
                    for (const Symbol &s : f->symbols)
                        if (isTopLevel(s.kind) && s.name == name)
                            out.push_back(&s);
                return out;
            }

            std::vector<const Symbol *> members(const PackageIndex &pkg, const std::string &owner)
            {
                std::vector<const Symbol *> out;
                for (const FileIndex *f : pkg.files)
                    for (const Symbol &s : f->symbols)
                        if ((s.kind == SymKind::Field || s.kind == SymKind::Method ||
                             s.kind == SymKind::InterfaceMethod) &&
                            s.owner == owner)
                            out.push_back(&s);
                return out;
            }

            const BuiltinDoc *builtin(const std::string &name)
            {
                for (const BuiltinDoc &b : builtins_)
                    if (b.name == name)
                        return &b;
                return nullptr;
            }

            struct TypeRef
            {
                fs::path dir;
                std::string name;
            };

            std::optional<TypeRef> resolveType(const FileIndex &f, std::string ty, Pos at, int depth = 0)
            {
                if (depth > 8 || ty.empty())
                    return std::nullopt;
                if (ty.rfind("var:", 0) == 0)
                {
                    std::string name = ty.substr(4);
                    if (const Symbol *s = lookupValue(f, name, at))
                        return resolveType(fileIndex(s->file), s->type, s->pos, depth + 1);
                    return std::nullopt;
                }
                if (ty.rfind("call:", 0) == 0)
                {
                    std::string callee = ty.substr(5);
                    size_t dot = callee.find('.');
                    fs::path dir = fs::path(f.path).parent_path();
                    if (dot != std::string::npos)
                    {
                        auto imports = importMap(f);
                        auto it = imports.find(callee.substr(0, dot));
                        if (it == imports.end())
                            return std::nullopt;
                        dir = it->second;
                        callee = callee.substr(dot + 1);
                    }
                    for (const Symbol *s : topLevel(package(dir), callee))
                        if (s->kind == SymKind::Function || s->kind == SymKind::Extern)
                            return resolveType(fileIndex(s->file), s->type, s->pos, depth + 1);
                    return std::nullopt;
                }
                while (!ty.empty() && (ty[0] == '*' || ty[0] == '[' || ty[0] == ']'))
                    ty.erase(0, 1);
                fs::path dir = fs::path(f.path).parent_path();
                size_t dot = ty.find('.');
                if (dot != std::string::npos)
                {
                    auto imports = importMap(f);
                    auto it = imports.find(ty.substr(0, dot));
                    if (it == imports.end())
                        return std::nullopt;
                    dir = it->second;
                    ty = ty.substr(dot + 1);
                }
                for (const Symbol *s : topLevel(package(dir), ty))
                    if (s->kind == SymKind::Struct || s->kind == SymKind::Interface)
                        return TypeRef{dir, ty};
                return std::nullopt;
            }

            const Symbol *lookupValue(const FileIndex &f, const std::string &name, Pos at)
            {
                if (const Symbol *s = visibleLocal(f, name, at))
                    return s;
                for (const Symbol *s : topLevel(package(fs::path(f.path).parent_path()), name))
                    return s;
                return nullptr;
            }

            std::optional<TypeRef> resolveChain(const FileIndex &f, const std::vector<std::string> &chain,
                                                Pos at)
            {
                if (chain.empty())
                    return std::nullopt;
                std::optional<TypeRef> cur;
                size_t i = 0;
                const Symbol *first = lookupValue(f, chain[0], at);
                if (!first)
                {
                    auto imports = importMap(f);
                    auto it = imports.find(chain[0]);
                    if (it == imports.end() || chain.size() < 2)
                        return std::nullopt;
                    auto syms = topLevel(package(it->second), chain[1]);
                    if (syms.empty())
                        return std::nullopt;
                    cur = resolveType(fileIndex(syms[0]->file), syms[0]->type, syms[0]->pos);
                    i = 2;
                }
                else
                {
                    cur = resolveType(f, first->type, at);
                    i = 1;
                }
                for (; i < chain.size() && cur; ++i)
                {
                    const Symbol *next = nullptr;
                    for (const Symbol *m : members(package(cur->dir), cur->name))
                        if (m->name == chain[i])
                            next = m;
                    if (!next)
                        return std::nullopt;
                    cur = resolveType(fileIndex(next->file), next->type, next->pos);
                }
                return cur;
            }

            struct Word
            {
                std::string name;
                std::vector<std::string> chain;
                int start = 0;
            };

            static std::vector<std::string> chainBefore(const std::string &line, int dotIndex)
            {
                std::vector<std::string> chain;
                int i = dotIndex;
                while (i >= 0 && line[i] == '.')
                {
                    int e = i;
                    int s = e;
                    while (s > 0 && identChar(line[s - 1]))
                        --s;
                    if (s == e)
                        break;
                    chain.insert(chain.begin(), line.substr(s, e - s));
                    i = s - 1;
                }
                return chain;
            }

            std::optional<Word> wordAt(const FileIndex &f, Pos p)
            {
                if (p.line - 1 >= static_cast<int>(f.lines.size()))
                    return std::nullopt;
                const std::string &line = f.lines[p.line - 1];
                int c = p.col - 1;
                if (c > static_cast<int>(line.size()))
                    return std::nullopt;
                if ((c >= static_cast<int>(line.size()) || !identChar(line[c])) && c > 0 && identChar(line[c - 1]))
                    --c;
                if (c < 0 || c >= static_cast<int>(line.size()) || !identChar(line[c]))
                    return std::nullopt;
                int s = c;
                int e = c;
                while (s > 0 && identChar(line[s - 1]))
                    --s;
                while (e < static_cast<int>(line.size()) && identChar(line[e]))
                    ++e;
                Word w;
                w.name = line.substr(s, e - s);
                w.start = s;
                if (s > 0)
                    w.chain = chainBefore(line, s - 1);
                return w;
            }

            const ImportRef *importAt(const FileIndex &f, Pos p)
            {
                for (const ImportRef &imp : f.imports)
                    if (imp.pos.line == p.line && imp.pos.col <= p.col && p.col <= imp.pos.col + imp.length)
                        return &imp;
                return nullptr;
            }

            std::vector<Hit> resolve(const FileIndex &f, Pos p)
            {
                std::vector<Hit> hits;
                if (const ImportRef *imp = importAt(f, p))
                {
                    if (auto dir = importDir(f.path, imp->path))
                    {
                        Hit h;
                        h.packageDir = dir;
                        h.packageName = package(*dir).name;
                        hits.push_back(h);
                    }
                    return hits;
                }
                auto w = wordAt(f, p);
                if (!w)
                    return hits;
                fs::path here = fs::path(f.path).parent_path();
                auto symHit = [&](const Symbol *s)
                {
                    Hit h;
                    h.sym = s;
                    hits.push_back(h);
                };
                if (w->chain.empty())
                {
                    if (const Symbol *s = visibleLocal(f, w->name, p))
                    {
                        symHit(s);
                        return hits;
                    }
                    for (const Symbol *s : topLevel(package(here), w->name))
                        symHit(s);
                    if (!hits.empty())
                        return hits;
                    auto imports = importMap(f);
                    if (auto it = imports.find(w->name); it != imports.end())
                    {
                        Hit h;
                        h.packageDir = it->second;
                        h.packageName = w->name;
                        hits.push_back(h);
                        return hits;
                    }
                    if (const BuiltinDoc *b = builtin(w->name))
                    {
                        Hit h;
                        h.builtin = b;
                        hits.push_back(h);
                        return hits;
                    }
                    for (const FileIndex *pf : package(here).files)
                        for (const Symbol &s : pf->symbols)
                            if (s.kind == SymKind::Field && s.name == w->name)
                                symHit(&s);
                    return hits;
                }
                if (w->chain.size() == 1 && !lookupValue(f, w->chain[0], p))
                {
                    auto imports = importMap(f);
                    if (auto it = imports.find(w->chain[0]); it != imports.end())
                    {
                        for (const Symbol *s : topLevel(package(it->second), w->name))
                            symHit(s);
                        return hits;
                    }
                }
                if (auto ty = resolveChain(f, w->chain, p))
                {
                    for (const Symbol *m : members(package(ty->dir), ty->name))
                        if (m->name == w->name)
                            symHit(m);
                    if (!hits.empty())
                        return hits;
                }
                std::vector<fs::path> dirs{here};
                for (const auto &[name, dir] : importMap(f))
                    dirs.push_back(dir);
                for (const fs::path &dir : dirs)
                    for (const FileIndex *pf : package(dir).files)
                        for (const Symbol &s : pf->symbols)
                            if ((s.kind == SymKind::Field || s.kind == SymKind::Method ||
                                 s.kind == SymKind::InterfaceMethod) &&
                                s.name == w->name)
                                symHit(&s);
                return hits;
            }

            Json definition(const Json &params)
            {
                std::string path = canonical(uriToPath(params["textDocument"]["uri"].str()));
                const FileIndex &f = fileIndex(path);
                Json out = Json::array();
                for (const Hit &h : resolve(f, toPos(f, params["position"])))
                {
                    if (h.sym)
                        out.push(location(h.sym->file, h.sym->pos, static_cast<int>(h.sym->name.size())));
                    else if (h.builtin)
                        out.push(location(builtinFile_, h.builtin->pos, static_cast<int>(h.builtin->name.size())));
                    else if (h.packageDir)
                    {
                        Json loc = packageLocation(*h.packageDir);
                        if (!loc.isNull())
                            out.push(loc);
                    }
                }
                return out;
            }

            static std::string kindWord(SymKind k)
            {
                switch (k)
                {
                case SymKind::Field:
                    return "field";
                case SymKind::Param:
                    return "param";
                case SymKind::Local:
                    return "var";
                case SymKind::Global:
                    return "global";
                default:
                    return "";
                }
            }

            Json hover(const Json &params)
            {
                std::string path = canonical(uriToPath(params["textDocument"]["uri"].str()));
                const FileIndex &f = fileIndex(path);
                auto hits = resolve(f, toPos(f, params["position"]));
                if (hits.empty())
                    return Json();
                const Hit &h = hits.front();
                std::string md;
                if (h.sym)
                {
                    std::string detail = h.sym->detail;
                    std::string word = kindWord(h.sym->kind);
                    if (h.sym->kind == SymKind::Field)
                        detail = h.sym->owner + "." + detail;
                    md = "```pgt\n" + (word.empty() || h.sym->kind == SymKind::Local ? "" : word + " ") + detail +
                         "\n```";
                    std::string pkg = fileIndex(h.sym->file).packageName;
                    if (!pkg.empty() && h.sym->kind != SymKind::Local && h.sym->kind != SymKind::Param)
                        md += "\n\npackage `" + pkg + "`";
                }
                else if (h.builtin)
                    md = "```pgt\n" + h.builtin->signature + "\n```\n\n" + h.builtin->doc + "\nbuiltin";
                else if (h.packageDir)
                    md = "```pgt\npackage " + h.packageName + "\n```\n\n`" + h.packageDir->string() + "`";
                Json contents = Json::object();
                contents["kind"] = "markdown";
                contents["value"] = md;
                Json result = Json::object();
                result["contents"] = contents;
                return result;
            }

            static int completionKind(SymKind k)
            {
                switch (k)
                {
                case SymKind::Function:
                case SymKind::Extern:
                    return 3;
                case SymKind::Method:
                case SymKind::InterfaceMethod:
                    return 2;
                case SymKind::Field:
                    return 5;
                case SymKind::Struct:
                    return 22;
                case SymKind::Interface:
                    return 8;
                case SymKind::Const:
                    return 21;
                default:
                    return 6;
                }
            }

            static Json item(const std::string &label, int kind, const std::string &detail)
            {
                Json it = Json::object();
                it["label"] = label;
                it["kind"] = kind;
                if (!detail.empty())
                    it["detail"] = detail;
                return it;
            }

            Json completion(const Json &params)
            {
                std::string path = canonical(uriToPath(params["textDocument"]["uri"].str()));
                const FileIndex &f = fileIndex(path);
                Pos p = toPos(f, params["position"]);
                std::string line = p.line - 1 < static_cast<int>(f.lines.size()) ? f.lines[p.line - 1] : "";
                int c = std::min(p.col - 1, static_cast<int>(line.size()));
                int s = c;
                while (s > 0 && identChar(line[s - 1]))
                    --s;
                Json items = Json::array();
                std::set<std::string> seen;
                auto addSym = [&](const Symbol *sym)
                {
                    if (!seen.insert(sym->name).second)
                        return;
                    items.push(item(sym->name, completionKind(sym->kind), sym->detail));
                };
                fs::path here = fs::path(f.path).parent_path();
                if (s > 0 && line[s - 1] == '.')
                {
                    std::vector<std::string> chain = chainBefore(line, s - 1);
                    if (chain.empty())
                        return items;
                    if (chain.size() == 1 && !lookupValue(f, chain[0], p))
                    {
                        auto imports = importMap(f);
                        if (auto it = imports.find(chain[0]); it != imports.end())
                        {
                            bool same = it->second == here;
                            for (const FileIndex *pf : package(it->second).files)
                                for (const Symbol &sym : pf->symbols)
                                    if (isTopLevel(sym.kind) &&
                                        (same || std::isupper(static_cast<unsigned char>(sym.name[0]))))
                                        addSym(&sym);
                            return items;
                        }
                    }
                    if (auto ty = resolveChain(f, chain, p))
                    {
                        bool same = ty->dir == here;
                        for (const Symbol *m : members(package(ty->dir), ty->name))
                            if (same || std::isupper(static_cast<unsigned char>(m->name[0])))
                                addSym(m);
                        return items;
                    }
                    for (const FileIndex *pf : package(here).files)
                        for (const Symbol &sym : pf->symbols)
                            if (sym.kind == SymKind::Field || sym.kind == SymKind::Method)
                                addSym(&sym);
                    return items;
                }
                for (const Symbol &sym : f.symbols)
                    if ((sym.kind == SymKind::Local || sym.kind == SymKind::Param) && sym.scopeBegin <= p &&
                        p <= sym.scopeEnd && (sym.kind == SymKind::Param || sym.pos < p))
                        addSym(&sym);
                for (const FileIndex *pf : package(here).files)
                    for (const Symbol &sym : pf->symbols)
                        if (isTopLevel(sym.kind))
                            addSym(&sym);
                for (const auto &[name, dir] : importMap(f))
                    if (seen.insert(name).second)
                        items.push(item(name, 9, "package " + name));
                for (const BuiltinDoc &b : builtins_)
                    if (seen.insert(b.name).second)
                        items.push(item(b.name, 3, b.signature));
                for (const char *t : kTypes)
                    if (seen.insert(t).second)
                        items.push(item(t, 25, "type"));
                for (const char *k : kKeywords)
                    if (seen.insert(k).second)
                        items.push(item(k, 14, "keyword"));
                return items;
            }

            static int symbolKind(SymKind k)
            {
                switch (k)
                {
                case SymKind::Function:
                case SymKind::Extern:
                    return 12;
                case SymKind::Method:
                case SymKind::InterfaceMethod:
                    return 6;
                case SymKind::Struct:
                    return 23;
                case SymKind::Interface:
                    return 11;
                case SymKind::Field:
                    return 8;
                case SymKind::Const:
                    return 14;
                default:
                    return 13;
                }
            }

            Json documentSymbols(const Json &params)
            {
                std::string path = canonical(uriToPath(params["textDocument"]["uri"].str()));
                const FileIndex &f = fileIndex(path);
                Json out = Json::array();
                for (const Symbol &s : f.symbols)
                {
                    if (s.kind == SymKind::Local || s.kind == SymKind::Param)
                        continue;
                    Json info = Json::object();
                    info["name"] = s.name;
                    info["kind"] = symbolKind(s.kind);
                    info["location"] = location(path, s.pos, static_cast<int>(s.name.size()));
                    if (!s.owner.empty())
                        info["containerName"] = s.owner;
                    out.push(info);
                }
                return out;
            }

            void didOpen(const Json &doc)
            {
                std::string path = canonical(uriToPath(doc["uri"].str()));
                open_[path] = doc["text"].str();
                fullCheck(path);
            }

            void didChange(const Json &params)
            {
                std::string path = canonical(uriToPath(params["textDocument"]["uri"].str()));
                const Json &changes = params["contentChanges"];
                if (changes.size() == 0)
                    return;
                open_[path] = changes.items().back()["text"].str();
                Json diags = Json::array();
                if (auto d = syntaxError(path, open_[path]))
                    diags.push(*d);
                else if (auto it = semantic_.find(path); it != semantic_.end())
                    diags = it->second;
                publish(path, diags);
            }

            Json diagnostic(const std::string &path, const CompileError &e)
            {
                const FileIndex &f = fileIndex(path);
                Pos p{std::max(e.line, 1), std::max(e.col, 1)};
                int len = 1;
                if (e.line > 0 && e.line - 1 < static_cast<int>(f.lines.size()))
                {
                    const std::string &ln = f.lines[e.line - 1];
                    int c = p.col - 1;
                    int k = c;
                    while (k < static_cast<int>(ln.size()) && identChar(ln[k]))
                        ++k;
                    len = std::max(1, k - c);
                    if (e.line <= 0 || c >= static_cast<int>(ln.size()))
                        len = 0;
                }
                if (e.line <= 0)
                {
                    p = Pos{1, 1};
                    len = f.lines.empty() ? 0 : static_cast<int>(f.lines[0].size());
                }
                Json d = Json::object();
                d["range"] = range(path, p, len);
                d["severity"] = 1;
                d["source"] = "pgt";
                d["message"] = std::string(e.what());
                return d;
            }

            std::optional<Json> syntaxError(const std::string &path, const std::string &text)
            {
                try
                {
                    Parser parser(Lexer(text).tokenize());
                    parser.parseProgram();
                }
                catch (const CompileError &e)
                {
                    return diagnostic(path, e);
                }
                catch (const std::exception &)
                {
                }
                return std::nullopt;
            }

            bool inStd(const std::string &path) const
            {
                if (!stdDir_)
                    return false;
                std::string root = canonical(stdDir_->string());
                return path.rfind(root + "/", 0) == 0;
            }

            std::optional<CompileError> compile(const fs::path &input)
            {
                try
                {
                    Loader loader;
                    Workspace ws = loader.load(input.string());
                    CodeGen cg(ws.root->importPath);
                    cg.generate(ws);
                }
                catch (const CompileError &e)
                {
                    return e;
                }
                catch (const std::exception &e)
                {
                    return CompileError(e.what(), 0, 0);
                }
                return std::nullopt;
            }

            void fullCheck(const std::string &rawPath)
            {
                std::string path = canonical(rawPath);
                std::map<std::string, Json> results;
                results[path] = Json::array();
                if (auto d = syntaxError(path, textOf(path)))
                    results[path].push(*d);
                else if (!inStd(path))
                {
                    fs::path dir = fs::path(path).parent_path();
                    fs::path root = dir;
                    try
                    {
                        if (auto m = findModFile(dir))
                            root = parseModFile(*m).root;
                    }
                    catch (const std::exception &)
                    {
                    }
                    std::optional<CompileError> err;
                    bool usedRoot = false;
                    if (root != dir && !listSources(root).empty())
                    {
                        err = compile(root);
                        usedRoot = true;
                    }
                    if (!usedRoot || (!err && !reaches(root, dir)))
                        err = compile(dir);
                    if (err)
                    {
                        std::string file = err->file.empty() ? path : canonical(err->file);
                        if (!results.count(file))
                            results[file] = Json::array();
                        results[file].push(diagnostic(file, *err));
                    }
                }
                for (const std::string &p : published_)
                    if (!results.count(p))
                        results[p] = Json::array();
                for (const auto &[p, diags] : results)
                {
                    semantic_[p] = diags;
                    publish(p, diags);
                }
            }

            bool reaches(const fs::path &root, const fs::path &dir)
            {
                try
                {
                    Loader loader;
                    Workspace ws = loader.load(root.string());
                    for (const auto &pkg : ws.packages)
                        if (canonical(pkg->dir) == dir.string())
                            return true;
                }
                catch (const std::exception &)
                {
                }
                return false;
            }

            void publish(const std::string &path, const Json &diags)
            {
                if (diags.size() > 0)
                    published_.insert(path);
                else
                    published_.erase(path);
                Json params = Json::object();
                params["uri"] = pathToUri(path);
                params["diagnostics"] = diags;
                notify("textDocument/publishDiagnostics", params);
            }

            FILE *out_ = nullptr;
            bool shutdown_ = false;
            std::optional<fs::path> stdDir_;
            std::string builtinFile_;
            std::vector<BuiltinDoc> builtins_;
            std::map<std::string, std::string> open_;
            std::map<std::string, FileIndex> cache_;
            std::map<std::string, Json> semantic_;
            std::set<std::string> published_;
        };

    }

    int runServer()
    {
        Server server;
        return server.run();
    }

}
