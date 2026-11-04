#pragma once
#include "loader/Package.h"
#include "modfile/ModFile.h"

#include <filesystem>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace pgt {

class Loader {
public:
    Workspace load(const std::string& input);

private:
    Package* loadPackage(const std::filesystem::path& dir, std::vector<std::string> files,
                         bool isRoot);
    std::string importPathFor(const std::filesystem::path& dir, const std::string& packageName) const;
    std::filesystem::path resolveImport(const Import& imp, const std::string& file) const;

    Workspace ws_;
    std::optional<ModFile> mod_;
    std::vector<std::string> stack_;
};

std::vector<std::string> listSources(const std::filesystem::path& dir);

}
