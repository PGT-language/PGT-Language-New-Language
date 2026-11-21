#pragma once
#include <ostream>
#include <string>
#include <vector>

namespace pgt
{

    // One file prepared for `--debug`: its path and source text.
    struct DebugSource
    {
        std::string path;
        std::string text;
    };

    // Lexes and parses every source and prints its tokens and syntax tree.
    // Text mode is for people; JSON mode is for tools (PGT Studio).
    // Returns false if any file has a lexical or syntax error (it is reported in the dump).
    bool dumpSources(const std::vector<DebugSource> &sources, bool json, std::ostream &out);

    // `--debug=project`: walks the package given by `input` and every package it imports
    // (the standard library included) and prints, as JSON, each file with its imports, token
    // statistics, syntax tree and token stream. Unresolved imports are reported, not fatal.
    bool dumpProject(const std::string &input, std::ostream &out);

}
