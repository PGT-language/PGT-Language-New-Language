#include "driver/Driver.h"

#include "codegen/CodeGen.h"
#include "codegen/ObjectEmitter.h"
#include "common/Error.h"
#include "common/Paths.h"
#include "debug/Dump.h"
#include "loader/Loader.h"

#include <llvm/Support/raw_ostream.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iostream>
#include <sys/wait.h>
#include <unistd.h>

namespace pgt
{
    namespace
    {

        namespace fs = std::filesystem;

        std::string shellQuote(const std::string &s)
        {
            std::string q = "'";
            for (char c : s)
                q += (c == '\'') ? std::string("'\\''") : std::string(1, c);
            return q + "'";
        }

        std::string outputName(const fs::path &input)
        {
            if (fs::is_directory(input))
                return fs::weakly_canonical(input).filename().string();
            return input.stem().string();
        }

    }

    namespace
    {

        // The files `--debug` shows: the given file, every .pgt file of the given directory, or stdin.
        std::vector<DebugSource> debugSources(const std::string &input)
        {
            std::vector<DebugSource> out;
            auto read = [&](const std::string &path, std::istream &in) {
                std::ostringstream ss;
                ss << in.rdbuf();
                out.push_back({path, ss.str()});
            };
            if (input == "-")
            {
                read("<stdin>", std::cin);
                return out;
            }
            std::vector<std::string> files =
                fs::is_directory(input) ? listSources(input) : std::vector<std::string>{input};
            for (const std::string &f : files)
            {
                std::ifstream in(f, std::ios::binary);
                if (!in)
                    throw CompileError("cannot read the file", 0, 0);
                read(f, in);
            }
            return out;
        }

    }

    int build(const BuildOptions &opts)
    {
        try
        {
            if (opts.debug != DebugMode::Off)
            {
                bool json = opts.debug == DebugMode::Json;
                if (opts.input != "-" && !fs::exists(opts.input))
                {
                    std::cerr << opts.input << ": error: no such file or directory\n";
                    return 1;
                }
                bool ok = dumpSources(debugSources(opts.input), json, std::cout);
                std::cout.flush();
                if (json || opts.input == "-")
                    return ok ? 0 : 1;
                if (!ok)
                    return 1;
            }

            Loader loader;
            Workspace workspace = loader.load(opts.input);

            std::string name = outputName(opts.input);
            bool isMain = workspace.root->name == "main";
            if (opts.run && !isMain)
            {
                std::cerr << "pgt: error: 'pgt run' needs package main\n";
                return 1;
            }

            std::string output = opts.output;
            if (opts.run)
            {
                output = (fs::temp_directory_path() /
                          ("pgt-run-" + std::to_string(getpid()) + "-" + name))
                             .string();
            }
            else if (output.empty())
            {
                std::string file = opts.emitIr ? name + ".ll" : (isMain ? name : name + ".o");
                fs::path base = fs::is_directory(opts.input) ? fs::path(opts.input)
                                                             : fs::path(opts.input).parent_path();
                output = (base / file).string();
            }

            CodeGen codegen(workspace.root->importPath);
            codegen.generate(workspace);

            if (opts.emitIr)
            {
                std::error_code ec;
                llvm::raw_fd_ostream out(output, ec);
                if (ec)
                {
                    std::cerr << "pgt: error: cannot write '" << output << "': " << ec.message()
                              << "\n";
                    return 1;
                }
                codegen.module().print(out, nullptr);
                return 0;
            }

            if (!isMain)
            {
                emitObjectFile(codegen.module(), output);
                return 0;
            }

            auto runtime = findRuntimeLib();
            if (!runtime)
            {
                std::cerr << "pgt: error: cannot find the runtime library libpgtrt.a (set PGT_RUNTIME)\n";
                return 1;
            }

            std::string objPath = output + ".o";
            emitObjectFile(codegen.module(), objPath);

            std::string cmd = "cc " + shellQuote(objPath) + " " + shellQuote(runtime->string()) +
                              " -lm -o " + shellQuote(output);
            int rc = std::system(cmd.c_str());
            std::remove(objPath.c_str());
            if (rc != 0)
            {
                std::cerr << "pgt: error: linking failed\n";
                return 1;
            }
            if (!opts.run)
                return 0;

            std::string runCmd = shellQuote(output);
            for (const std::string &a : opts.runArgs)
                runCmd += " " + shellQuote(a);
            int status = std::system(runCmd.c_str());
            std::remove(output.c_str());
            if (status == -1)
                return 1;
            if (WIFEXITED(status))
                return WEXITSTATUS(status);
            return 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0);
        }
        catch (const CompileError &e)
        {
            std::cerr << (e.file.empty() ? opts.input : e.file);
            if (e.line > 0)
                std::cerr << ":" << e.line << ":" << e.col;
            std::cerr << ": error: " << e.what() << "\n";
            return 1;
        }
        catch (const std::exception &e)
        {
            std::cerr << "pgt: error: " << e.what() << "\n";
            return 1;
        }
    }

}
