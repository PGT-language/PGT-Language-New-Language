#pragma once
#include <string>
#include <vector>

namespace pgt
{

    enum class DebugMode
    {
        Off,
        Text,
        Json,
        Project
    };

    struct BuildOptions
    {
        DebugMode debug = DebugMode::Off;
        std::string input;
        std::string output;
        bool emitIr = false;
        bool run = false;
        std::vector<std::string> runArgs;
    };

    int build(const BuildOptions &options);

}
