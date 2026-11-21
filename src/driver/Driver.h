#pragma once
#include <string>
#include <vector>

namespace pgt
{

    enum class DebugMode
    {
        Off,
        Text, // --debug: print tokens and syntax trees, then build as usual
        Json  // --debug=json: print them as JSON and stop (for PGT Studio); input may be "-" (stdin)
    };

    struct BuildOptions
    {
        DebugMode debug = DebugMode::Off;
        std::string input;
        std::string output;
        bool emitIr = false;
        bool run = false;                  // build into a temporary file, run it, then delete it
        std::vector<std::string> runArgs;  // arguments for the program started by `pgt run`
    };

    int build(const BuildOptions &options);

}
