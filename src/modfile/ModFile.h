#pragma once
#include <filesystem>
#include <map>
#include <optional>
#include <string>

namespace pgt {

struct ModFile {
    std::filesystem::path path;
    std::filesystem::path root;
    std::string name;
    std::string version;
    std::map<std::string, std::string> dependencies;
};

ModFile parseModFile(const std::filesystem::path& path);

std::optional<std::filesystem::path> findModFile(const std::filesystem::path& startDir);

}
