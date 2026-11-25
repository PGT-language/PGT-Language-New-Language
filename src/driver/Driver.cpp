#include "driver/Driver.h"

#include "driver/BuildCache.h"

#include "codegen/CodeGen.h"
#include "codegen/ObjectEmitter.h"
#include "common/Error.h"
#include "common/Paths.h"
#include "modfile/ModFile.h"
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

        std::vector<std::string> compileCUnits(const std::vector<CodeGen::CUnit> &units, int level, bool kernel,
                                               const fs::path &extraSource, std::vector<std::string> &temps)
        {
            std::vector<std::string> objects;
            std::vector<std::pair<std::string, std::string>> jobs;
            fs::path tmp = fs::temp_directory_path();
            std::string tag = std::to_string(getpid());
            for (const CodeGen::CUnit &u : units)
            {
                std::string src = (tmp / ("pgt-c-" + tag + "-" + u.name + ".c")).string();
                std::ofstream(src, std::ios::binary) << u.source();
                temps.push_back(src);
                jobs.push_back({src, (tmp / ("pgt-c-" + tag + "-" + u.name + ".o")).string()});
            }
            if (!extraSource.empty())
                jobs.push_back({extraSource.string(), (tmp / ("pgt-krt-" + tag + ".o")).string()});
            for (const auto &[src, obj] : jobs)
            {
                std::string cmd = "clang -c -x c " + shellQuote(src) + " -o " + shellQuote(obj) + " -O" +
                                  std::to_string(level);
                if (kernel)
                    cmd += " -target x86_64-unknown-none-elf -ffreestanding -fno-stack-protector -mno-red-zone "
                           "-mcmodel=kernel -fno-pic -mgeneral-regs-only -fno-builtin";
                else
                    cmd += " -fPIC";
                if (std::system(cmd.c_str()) != 0)
                    throw std::runtime_error("C compilation failed");
                objects.push_back(obj);
                temps.push_back(obj);
            }
            return objects;
        }

        void removeAll(const std::vector<std::string> &files)
        {
            for (const std::string &f : files)
                std::remove(f.c_str());
        }

    }

    namespace
    {

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
            if (opts.debug == DebugMode::Project)
            {
                bool ok = dumpProject(opts.input, std::cout);
                std::cout.flush();
                return ok ? 0 : 1;
            }
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
            Workspace workspace = loader.load(opts.input, opts.freestanding);
            bool kernel = workspace.target == "kernel";
            if (kernel)
                setFreestanding(true);

            std::string name = outputName(opts.input);
            bool isMain = workspace.root->name == "main";
            if (kernel && opts.run)
            {
                std::cerr << "pgt: error: 'pgt run' cannot run a freestanding kernel; use pgt build\n";
                return 1;
            }
            if (kernel && !isMain)
            {
                std::cerr << "pgt: error: a freestanding build needs package main\n";
                return 1;
            }
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
            codegen.setFreestanding(kernel);
            codegen.setEntry(!opts.entry.empty() ? opts.entry : !workspace.entry.empty() ? workspace.entry : "kmain");
            codegen.generate(workspace);

            auto cacheDir = buildCacheDir();
            bool cached = isMain && !opts.emitIr && cacheDir && !kernel;
            if (!cached)
                optimizeModule(codegen.module(), opts.optLevel);

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
                if (!codegen.cUnits().empty())
                {
                    std::cerr << "pgt: error: c blocks are only supported when building package main\n";
                    return 1;
                }
                emitObjectFile(codegen.module(), output, opts.optLevel);
                return 0;
            }

            if (kernel)
            {
                auto kdir = findKernelDir();
                if (!kdir)
                {
                    std::cerr << "pgt: error: cannot find the kernel runtime directory (set PGT_KERNEL_DIR)\n";
                    return 1;
                }
                std::string script = opts.linkerScript;
                if (script.empty() && !workspace.linker.empty())
                {
                    fs::path l = workspace.linker;
                    script = (l.is_absolute() ? l : fs::path(workspace.modRoot) / l).string();
                }
                if (script.empty())
                    script = (*kdir / "kernel.ld").string();
                if (!fs::exists(script))
                {
                    std::cerr << "pgt: error: linker script '" << script << "' does not exist\n";
                    return 1;
                }
                std::vector<std::string> temps;
                std::string kobj = output + ".o";
                emitObjectFile(codegen.module(), kobj, opts.optLevel);
                temps.push_back(kobj);
                std::vector<std::string> objs;
                try
                {
                    objs = compileCUnits(codegen.cUnits(), opts.optLevel, true, *kdir / "pgt_kernel_rt.c", temps);
                }
                catch (const std::exception &e)
                {
                    removeAll(temps);
                    std::cerr << "pgt: error: " << e.what() << "\n";
                    return 1;
                }
                std::string lcmd = "ld.lld -nostdlib -static -z max-page-size=0x1000 -T " + shellQuote(script) + " " +
                                   shellQuote(kobj);
                for (const std::string &o : objs)
                    lcmd += " " + shellQuote(o);
                lcmd += " -o " + shellQuote(output);
                int lrc = std::system(lcmd.c_str());
                removeAll(temps);
                if (lrc != 0)
                {
                    std::cerr << "pgt: error: linking failed\n";
                    return 1;
                }
                return 0;
            }

            auto runtime = findRuntimeLib();
            if (!runtime)
            {
                std::cerr << "pgt: error: cannot find the runtime library libpgtrt.a (set PGT_RUNTIME)\n";
                return 1;
            }

            std::vector<std::string> objects;
            std::string objPath;
            if (cached)
                objects = emitCachedObjects(codegen.module(), workspace, opts.optLevel, *cacheDir);
            else
            {
                objPath = output + ".o";
                emitObjectFile(codegen.module(), objPath, opts.optLevel);
                objects.push_back(objPath);
            }

            std::vector<std::string> ctemps;
            if (!codegen.cUnits().empty())
            {
                try
                {
                    std::vector<std::string> cobjs = compileCUnits(codegen.cUnits(), opts.optLevel, false, {}, ctemps);
                    objects.insert(objects.end(), cobjs.begin(), cobjs.end());
                }
                catch (const std::exception &e)
                {
                    removeAll(ctemps);
                    if (!objPath.empty())
                        std::remove(objPath.c_str());
                    std::cerr << "pgt: error: " << e.what() << "\n";
                    return 1;
                }
            }
            std::string cmd = "cc";
            for (const std::string &o : objects)
                cmd += " " + shellQuote(o);
            cmd += " " + shellQuote(runtime->string()) + " -lm -o " + shellQuote(output);
            int rc = std::system(cmd.c_str());
            removeAll(ctemps);
            if (!objPath.empty())
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
