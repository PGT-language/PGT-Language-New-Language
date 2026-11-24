#pragma once
#include "loader/Package.h"

#include <llvm/IR/Module.h>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace pgt
{

    std::optional<std::filesystem::path> buildCacheDir();

    std::vector<std::string> emitCachedObjects(llvm::Module &module, const Workspace &workspace, int level,
                                               const std::filesystem::path &cacheDir);

    void trimCache(const std::filesystem::path &cacheDir, size_t keep = 500);

    size_t cleanCache(const std::filesystem::path &cacheDir);

}
