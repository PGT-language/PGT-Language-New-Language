#pragma once
#include "ast/AST.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace pgt {

struct Package {
    std::string importPath;
    std::string name;
    std::string dir;
    std::vector<Program> programs;
};

struct Workspace {
    std::vector<std::unique_ptr<Package>> packages;
    std::map<std::string, Package*> byPath;
    Package* root = nullptr;
};

}
