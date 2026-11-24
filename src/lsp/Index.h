#pragma once
#include "lexer/Token.h"

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace pgt::lsp
{

    struct Pos
    {
        int line = 0;
        int col = 0;
        bool operator<(const Pos &o) const
        {
            return line != o.line ? line < o.line : col < o.col;
        }
        bool operator<=(const Pos &o) const
        {
            return !(o < *this);
        }
    };

    enum class SymKind
    {
        Function,
        Method,
        Extern,
        Struct,
        Interface,
        Field,
        InterfaceMethod,
        Global,
        Const,
        Param,
        Local
    };

    struct Symbol
    {
        SymKind kind = SymKind::Function;
        std::string name;
        std::string owner;
        std::string detail;
        std::string type;
        std::string file;
        Pos pos;
        Pos scopeBegin;
        Pos scopeEnd;
    };

    struct ImportRef
    {
        std::string path;
        Pos pos;
        int length = 0;
    };

    struct FileIndex
    {
        std::string path;
        std::string text;
        std::vector<std::string> lines;
        std::vector<Token> tokens;
        std::string packageName;
        std::vector<ImportRef> imports;
        std::vector<Symbol> symbols;
    };

    struct PackageIndex
    {
        std::filesystem::path dir;
        std::string name;
        std::vector<const FileIndex *> files;
    };

    struct BuiltinDoc
    {
        std::string name;
        std::string signature;
        std::string doc;
        Pos pos;
    };

    std::vector<Token> lenientTokens(const std::string &text);
    FileIndex indexFile(const std::string &path, const std::string &text);
    std::vector<BuiltinDoc> parseBuiltinDocs(const std::string &text);
    std::string normalizeImport(const std::string &path);
    bool isTopLevel(SymKind k);

}
