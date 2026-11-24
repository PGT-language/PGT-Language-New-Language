#pragma once
#include <ostream>
#include <string>
#include <vector>

namespace pgt
{

    struct DebugSource
    {
        std::string path;
        std::string text;
    };

    bool dumpSources(const std::vector<DebugSource> &sources, bool json, std::ostream &out);

    bool dumpProject(const std::string &input, std::ostream &out);

}
