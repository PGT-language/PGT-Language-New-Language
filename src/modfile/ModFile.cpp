#include "modfile/ModFile.h"

#include "common/Error.h"

#include <cctype>
#include <fstream>
#include <sstream>
#include <vector>

namespace pgt {
namespace {

enum class MTok { Word, String, LBrace, RBrace, Assign, Eof };

struct MToken {
    MTok kind;
    std::string text;
    int line;
    int col;
};

class ModLexer {
public:
    ModLexer(const std::string& src, const std::string& file) : src_(src), file_(file) {}

    std::vector<MToken> run() {
        std::vector<MToken> out;
        while (true) {
            skipTrivia();
            if (pos_ >= src_.size()) break;
            int line = line_, col = col_;
            char c = src_[pos_];
            if (c == '{') {
                step();
                out.push_back({MTok::LBrace, "{", line, col});
            } else if (c == '}') {
                step();
                out.push_back({MTok::RBrace, "}", line, col});
            } else if (c == '=') {
                step();
                out.push_back({MTok::Assign, "=", line, col});
            } else if (c == '"') {
                step();
                std::string text;
                while (pos_ < src_.size() && src_[pos_] != '"' && src_[pos_] != '\n')
                    text += step();
                if (pos_ >= src_.size() || src_[pos_] != '"') fail("unterminated string", line, col);
                step();
                out.push_back({MTok::String, text, line, col});
            } else {
                std::string word;
                while (pos_ < src_.size() && !isStop(src_[pos_]) && !startsComment()) word += step();
                out.push_back({MTok::Word, word, line, col});
            }
        }
        out.push_back({MTok::Eof, "", line_, col_});
        return out;
    }

private:
    static bool isStop(char c) {
        return std::isspace(static_cast<unsigned char>(c)) || c == '{' || c == '}' || c == '=' ||
               c == '"';
    }
    bool startsComment() const {
        return src_.compare(pos_, 2, "//") == 0;
    }
    char step() {
        char c = src_[pos_++];
        if (c == '\n') {
            ++line_;
            col_ = 1;
        } else {
            ++col_;
        }
        return c;
    }
    void skipTrivia() {
        while (pos_ < src_.size()) {
            if (std::isspace(static_cast<unsigned char>(src_[pos_]))) {
                step();
            } else if (startsComment()) {
                while (pos_ < src_.size() && src_[pos_] != '\n') step();
            } else {
                break;
            }
        }
    }
    [[noreturn]] void fail(const std::string& msg, int line, int col) const {
        CompileError e(msg, line, col);
        e.file = file_;
        throw e;
    }

    const std::string& src_;
    std::string file_;
    size_t pos_ = 0;
    int line_ = 1;
    int col_ = 1;
};

}

ModFile parseModFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        CompileError e("cannot read the file", 0, 0);
        e.file = path.string();
        throw e;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    std::string src = ss.str();

    std::string file = path.string();
    auto fail = [&](const std::string& msg, const MToken& t) {
        CompileError e(msg, t.line, t.col);
        e.file = file;
        throw e;
    };

    std::vector<MToken> toks = ModLexer(src, file).run();
    size_t i = 0;

    ModFile mod;
    mod.path = path;
    mod.root = path.parent_path();

    if (toks[i].kind != MTok::Word || toks[i].text != "module")
        fail("pgt.mod must start with 'module <name>'", toks[i]);
    ++i;
    if (toks[i].kind != MTok::Word) fail("expected the module name after 'module'", toks[i]);
    mod.name = toks[i++].text;

    bool seenVersion = false, seenDeps = false;
    while (toks[i].kind != MTok::Eof) {
        const MToken& t = toks[i];
        if (t.kind != MTok::Word) fail("unexpected token in pgt.mod", t);

        if (t.text == "version") {
            if (seenVersion) fail("duplicate 'version' section", t);
            seenVersion = true;
            ++i;
            if (toks[i].kind != MTok::Word && toks[i].kind != MTok::String)
                fail("expected a version after 'version'", toks[i]);
            mod.version = toks[i++].text;
        } else if (t.text == "dependencies") {
            if (seenDeps) fail("duplicate 'dependencies' section", t);
            seenDeps = true;
            ++i;
            if (toks[i].kind != MTok::LBrace) fail("expected '{' after 'dependencies'", toks[i]);
            ++i;
            while (toks[i].kind != MTok::RBrace) {
                if (toks[i].kind != MTok::String) fail("expected a quoted dependency name", toks[i]);
                std::string dep = toks[i++].text;
                if (toks[i].kind != MTok::Assign) fail("expected '=' after the dependency name", toks[i]);
                ++i;
                if (toks[i].kind != MTok::String) fail("expected a quoted dependency version", toks[i]);
                mod.dependencies[dep] = toks[i++].text;
            }
            ++i;
        } else {
            fail("unknown section '" + t.text + "' in pgt.mod", t);
        }
    }
    return mod;
}

std::optional<std::filesystem::path> findModFile(const std::filesystem::path& startDir) {
    std::filesystem::path dir = std::filesystem::absolute(startDir).lexically_normal();
    while (true) {
        std::filesystem::path candidate = dir / "pgt.mod";
        if (std::filesystem::is_regular_file(candidate)) return candidate;
        if (!dir.has_parent_path() || dir.parent_path() == dir) return std::nullopt;
        dir = dir.parent_path();
    }
}

}
