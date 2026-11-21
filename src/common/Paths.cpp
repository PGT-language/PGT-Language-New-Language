#include "common/Paths.h"

#include <cstdlib>
#include <system_error>
#include <vector>

namespace pgt
{
    namespace
    {

        namespace fs = std::filesystem;

        fs::path exeDir()
        {
            std::error_code ec;
            fs::path exe = fs::read_symlink("/proc/self/exe", ec);
            return ec ? fs::path() : exe.parent_path();
        }

        std::optional<fs::path> firstExisting(const char *envVar, const std::vector<fs::path> &candidates,
                                              bool wantDir)
        {
            if (const char *env = std::getenv(envVar); env && *env)
                return fs::path(env);
            std::error_code ec;
            for (const fs::path &p : candidates)
            {
                if (p.empty())
                    continue;
                if (wantDir ? fs::is_directory(p, ec) : fs::is_regular_file(p, ec))
                    return fs::weakly_canonical(p, ec);
            }
            return std::nullopt;
        }

    }

    std::optional<fs::path> findStdDir()
    {
        fs::path dir = exeDir();
        std::vector<fs::path> candidates;
        if (!dir.empty())
        {
            candidates.push_back(dir / ".." / "lib" / "pgt" / "std");
            candidates.push_back(dir / "std");
        }
#ifdef PGT_STD_DIR
        candidates.push_back(PGT_STD_DIR);
#endif
        return firstExisting("PGT_STD", candidates, true);
    }

    std::optional<fs::path> findRuntimeLib()
    {
        fs::path dir = exeDir();
        std::vector<fs::path> candidates;
        if (!dir.empty())
        {
            candidates.push_back(dir / ".." / "lib" / "pgt" / "libpgtrt.a");
            candidates.push_back(dir / "libpgtrt.a");
        }
#ifdef PGT_RUNTIME_LIB
        candidates.push_back(PGT_RUNTIME_LIB);
#endif
        return firstExisting("PGT_RUNTIME", candidates, false);
    }

}
