#pragma once
#include <filesystem>
#include <optional>

namespace pgt {
    std::optional<std::filesystem::path> findStdDir();
    std::optional<std::filesystem::path> findRuntimeLib();
    std::optional<std::filesystem::path> findKernelDir();
}