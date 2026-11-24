#pragma once
#include "loader/Package.h"
#include "modfile/ModFile.h"

#include <filesystem>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace pgt
{

    class Loader
    {
    public:
        Workspace load(const std::string &input);

    private:
        Package *loadPackage(const std::filesystem::path &dir, std::vector<std::string> files,
                             bool isRoot, const std::string &importPath);
        void loadRuntime();
        std::string importPathFor(const std::filesystem::path &dir, const std::string &packageName) const;
        std::filesystem::path resolveImport(const Import &imp, const std::string &file) const;

        Workspace ws_;
        std::optional<ModFile> mod_;
        std::optional<std::filesystem::path> stdDir_;
        std::vector<std::string> stack_;
    };

    std::vector<std::string> listSources(const std::filesystem::path &dir);
    std::optional<std::filesystem::path> resolveImportDir(const std::string &importPath,
                                                          const std::optional<ModFile> &mod,
                                                          const std::optional<std::filesystem::path> &stdDir);

}
