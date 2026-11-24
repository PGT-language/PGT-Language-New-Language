#include "loader/Loader.h"

#include "common/Error.h"
#include "common/Paths.h"
#include "lexer/Lexer.h"
#include "parser/Parser.h"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace pgt
{
    namespace
    {

        namespace fs = std::filesystem;

        [[noreturn]] void failAt(const std::string &msg, const std::string &file, int line, int col)
        {
            CompileError e(msg, line, col);
            e.file = file;
            throw e;
        }

        std::string displayPath(const fs::path &p)
        {
            std::error_code ec;
            fs::path rel = fs::relative(p, fs::current_path(), ec);
            if (!ec && !rel.empty() && rel.string().rfind("..", 0) != 0)
                return rel.string();
            return p.string();
        }

        Program parseFile(const std::string &path)
        {
            std::ifstream in(path, std::ios::binary);
            if (!in)
                failAt("cannot read the file", path, 0, 0);
            std::ostringstream ss;
            ss << in.rdbuf();
            try
            {
                Lexer lexer(ss.str());
                Parser parser(lexer.tokenize());
                Program program = parser.parseProgram();
                program.file = path;
                return program;
            }
            catch (CompileError &e)
            {
                e.file = path;
                throw;
            }
        }

    }

    std::vector<std::string> listSources(const fs::path &dir)
    {
        std::vector<std::string> files;
        for (const auto &entry : fs::directory_iterator(dir))
        {
            if (entry.is_regular_file() && entry.path().extension() == ".pgt")
                files.push_back(entry.path().string());
        }
        std::sort(files.begin(), files.end());
        return files;
    }

    Workspace Loader::load(const std::string &input)
    {
        fs::path in(input);
        if (!fs::exists(in))
            failAt("no such file or directory", input, 0, 0);

        bool isDir = fs::is_directory(in);
        fs::path dir = isDir ? in : in.parent_path();
        if (dir.empty())
            dir = ".";

        std::vector<std::string> files = isDir ? listSources(dir) : std::vector<std::string>{input};
        if (files.empty())
            failAt("no .pgt files in this directory", input, 0, 0);

        if (auto modPath = findModFile(dir))
            mod_ = parseModFile(*modPath);

        stdDir_ = findStdDir();
        loadImplicit("runtime");
        loadImplicit("maps");
        ws_.root = loadPackage(dir, std::move(files), true, "");
        checkUnusedImports();
        return std::move(ws_);
    }

    void Loader::loadImplicit(const std::string &name)
    {
        if (!stdDir_ || !fs::is_directory(*stdDir_ / name))
            failAt("the " + name + " package was not found in the standard library" +
                       std::string(stdDir_ ? "" : " (the standard library directory was not found; set PGT_STD)"),
                   name, 0, 0);
        fs::path dir = *stdDir_ / name;
        std::vector<std::string> files = listSources(dir);
        for (std::string &f : files)
            f = displayPath(f);
        loadPackage(dir, std::move(files), false, name);
    }

    void Loader::checkUnusedImports() const
    {
        for (const auto &pkg : ws_.packages)
        {
            for (const Program &p : pkg->programs)
            {
                std::set<std::string> names;
                for (const Import &imp : p.imports)
                {
                    auto it = ws_.byPath.find(imp.path);
                    if (it != ws_.byPath.end() && !names.insert(it->second->name).second)
                        failAt("package name '" + it->second->name + "' is imported more than once", p.file,
                               imp.line, imp.col);
                }
                for (const Import &imp : p.imports)
                {
                    auto it = ws_.byPath.find(imp.path);
                    if (it == ws_.byPath.end())
                        continue;
                    const Package *target = it->second;
                    bool used = p.qualifiers.count(target->name) ||
                                (target->importPath == "memory" && p.usesMalloc) ||
                                (target->importPath == "maps" && p.usesMaps) ||
                                (target->importPath == "asm" && p.usesAsm);
                    if (!used)
                        failAt("\"" + imp.path + "\" imported and not used", p.file, imp.line, imp.col);
                }
            }
        }
    }

    std::string Loader::importPathFor(const fs::path &dir, const std::string &packageName) const
    {
        if (!mod_)
            return packageName;
        fs::path rel = fs::weakly_canonical(dir).lexically_relative(fs::weakly_canonical(mod_->root));
        std::string relStr = rel.generic_string();
        if (relStr == "." || relStr.empty())
            return mod_->name;
        return mod_->name + "/" + relStr;
    }

    std::optional<fs::path> resolveImportDir(const std::string &importPath, const std::optional<ModFile> &mod,
                                             const std::optional<fs::path> &stdDir)
    {
        if (mod)
        {
            const std::string &name = mod->name;
            if (importPath == name)
                return mod->root;
            if (importPath.size() > name.size() && importPath.compare(0, name.size(), name) == 0 &&
                importPath[name.size()] == '/')
                return mod->root / importPath.substr(name.size() + 1);
        }

        if (stdDir && importPath.find("..") == std::string::npos)
        {
            fs::path dir = *stdDir / importPath;
            std::error_code ec;
            if (fs::is_directory(dir, ec))
                return dir;
        }
        return std::nullopt;
    }

    fs::path Loader::resolveImport(const Import &imp, const std::string &file) const
    {
        if (imp.path == "builtin")
            failAt("package \"builtin\" is documentation only and cannot be imported", file, imp.line,
                   imp.col);
        if (auto dir = resolveImportDir(imp.path, mod_, stdDir_))
            return *dir;

        std::string where = mod_ ? "in module '" + mod_->name + "' or " : "";
        std::string hint = mod_ ? "" : " (to import your own packages, create a pgt.mod)";
        failAt("cannot find package \"" + imp.path + "\" " + where + "in the standard library" +
                   (stdDir_ ? "" : " (the standard library directory was not found; set PGT_STD)") +
                   hint,
               file, imp.line, imp.col);
    }

    Package *Loader::loadPackage(const fs::path &dir, std::vector<std::string> files, bool isRoot,
                                 const std::string &importPath)
    {
        auto pkg = std::make_unique<Package>();
        pkg->dir = dir.string();

        for (const std::string &f : files)
            pkg->programs.push_back(parseFile(f));

        pkg->name = pkg->programs.front().package;
        for (const Program &p : pkg->programs)
        {
            if (p.package != pkg->name)
                failAt("package '" + p.package + "' conflicts with package '" + pkg->name +
                           "' of the same directory",
                       p.file, 0, 0);
        }
        if (!isRoot && pkg->name == "main")
            failAt("package 'main' cannot be imported", pkg->programs.front().file, 0, 0);

        pkg->importPath = importPath.empty() ? importPathFor(dir, pkg->name) : importPath;

        if (std::find(stack_.begin(), stack_.end(), pkg->importPath) != stack_.end())
        {
            std::string chain;
            for (const std::string &s : stack_)
                chain += s + " -> ";
            failAt("import cycle: " + chain + pkg->importPath, pkg->programs.front().file, 0, 0);
        }
        stack_.push_back(pkg->importPath);

        for (const Program &p : pkg->programs)
        {
            for (const Import &imp : p.imports)
            {
                if (ws_.byPath.count(imp.path))
                    continue;
                fs::path depDir = resolveImport(imp, p.file);
                if (!fs::is_directory(depDir))
                    failAt("cannot find package \"" + imp.path + "\"", p.file, imp.line, imp.col);
                std::vector<std::string> depFiles = listSources(depDir);
                if (depFiles.empty())
                    failAt("package \"" + imp.path + "\" contains no .pgt files", p.file, imp.line,
                           imp.col);
                for (std::string &f : depFiles)
                    f = displayPath(f);
                loadPackage(depDir, std::move(depFiles), false, imp.path);
            }
        }

        stack_.pop_back();

        Package *raw = pkg.get();
        ws_.byPath[raw->importPath] = raw;
        ws_.packages.push_back(std::move(pkg));
        return raw;
    }

}
