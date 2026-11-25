#include "driver/BuildCache.h"

#include "codegen/ObjectEmitter.h"

#include <llvm/Config/llvm-config.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/Support/SHA1.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Transforms/Utils/Cloning.h>
#include <llvm/Transforms/Utils/ValueMapper.h>

#include <unistd.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <map>
#include <system_error>

namespace pgt
{
    namespace
    {

        namespace fs = std::filesystem;

        bool isCopied(const llvm::GlobalValue &gv)
        {
            auto *var = llvm::dyn_cast<llvm::GlobalVariable>(&gv);
            return var && var->isConstant() && var->hasPrivateLinkage();
        }

        class Partitioner
        {
        public:
            Partitioner(const Workspace &ws)
            {
                for (const auto &pkg : ws.packages)
                {
                    if (pkg.get() == ws.root)
                        continue;
                    paths_.push_back(pkg->importPath);
                }
            }

            size_t count() const
            {
                return paths_.size() + 1;
            }

            size_t ownerOf(const llvm::GlobalValue &gv) const
            {
                llvm::StringRef name = gv.getName();
                size_t best = rootIndex();
                size_t bestLen = 0;
                for (size_t i = 0; i < paths_.size(); ++i)
                {
                    std::string prefix = "pgt." + paths_[i] + ".";
                    if (prefix.size() > bestLen && name.starts_with(prefix))
                    {
                        best = i;
                        bestLen = prefix.size();
                    }
                }
                return best;
            }

            size_t rootIndex() const
            {
                return paths_.size();
            }

        private:
            std::vector<std::string> paths_;
        };

        void exposeShared(llvm::Module &module)
        {
            auto expose = [](llvm::GlobalValue &gv)
            {
                if (gv.isDeclaration() || isCopied(gv))
                    return;
                if (gv.hasLocalLinkage())
                {
                    gv.setLinkage(llvm::GlobalValue::ExternalLinkage);
                    gv.setVisibility(llvm::GlobalValue::HiddenVisibility);
                }
            };
            for (llvm::Function &f : module)
                expose(f);
            for (llvm::GlobalVariable &g : module.globals())
                expose(g);
        }

        void dropUnusedCopies(llvm::Module &module)
        {
            bool changed = true;
            while (changed)
            {
                changed = false;
                for (auto it = module.global_begin(); it != module.global_end();)
                {
                    llvm::GlobalVariable &g = *it++;
                    if ((g.hasPrivateLinkage() || g.isDeclaration()) && g.use_empty())
                    {
                        g.eraseFromParent();
                        changed = true;
                    }
                }
                for (auto it = module.begin(); it != module.end();)
                {
                    llvm::Function &f = *it++;
                    if (f.isDeclaration() && f.use_empty() && !f.isIntrinsic())
                    {
                        f.eraseFromParent();
                        changed = true;
                    }
                }
            }
            for (llvm::GlobalVariable &g : module.globals())
                if (g.hasPrivateLinkage())
                    g.setName("");
        }

        std::string hashOf(const std::string &text, int level)
        {
            llvm::SHA1 sha;
            sha.update(text);
            std::string salt = std::string(PGT_VERSION) + "|" + LLVM_VERSION_STRING + "|" + std::to_string(level) +
                               "|" + __DATE__ + " " + __TIME__;
            sha.update(salt);
            std::array<uint8_t, 20> digest = sha.final();
            static const char *hex = "0123456789abcdef";
            std::string out;
            for (uint8_t b : digest)
            {
                out += hex[b >> 4];
                out += hex[b & 15];
            }
            return out;
        }

    }

    std::optional<fs::path> buildCacheDir()
    {
        if (const char *env = std::getenv("PGT_CACHE"); env && *env)
        {
            std::string v = env;
            if (v == "off" || v == "0")
                return std::nullopt;
            return fs::path(v);
        }
        if (const char *xdg = std::getenv("XDG_CACHE_HOME"); xdg && *xdg)
            return fs::path(xdg) / "pgt";
        if (const char *home = std::getenv("HOME"); home && *home)
            return fs::path(home) / ".cache" / "pgt";
        return std::nullopt;
    }

    std::vector<std::string> emitCachedObjects(llvm::Module &module, const Workspace &workspace, int level,
                                               const fs::path &cacheDir)
    {
        std::error_code ec;
        fs::create_directories(cacheDir, ec);
        if (ec)
            throw std::runtime_error("cannot create the build cache '" + cacheDir.string() + "': " + ec.message());

        Partitioner parts(workspace);
        exposeShared(module);

        std::vector<std::string> objects;
        bool asmTaken = false;
        for (size_t p = 0; p < parts.count(); ++p)
        {
            llvm::ValueToValueMapTy vmap;
            std::unique_ptr<llvm::Module> part = llvm::CloneModule(
                module, vmap,
                [&](const llvm::GlobalValue *gv) { return isCopied(*gv) || parts.ownerOf(*gv) == p; });
            part->setModuleIdentifier("pgt");
            part->setSourceFileName("pgt");
            dropUnusedCopies(*part);

            bool empty = true;
            if (!part->getModuleInlineAsm().empty())
            {
                if (asmTaken)
                    part->setModuleInlineAsm(std::string());
                else
                {
                    asmTaken = true;
                    empty = false;
                }
            }
            for (const llvm::Function &f : *part)
                if (!f.isDeclaration())
                    empty = false;
            for (const llvm::GlobalVariable &g : part->globals())
                if (!g.isDeclaration() && !g.hasPrivateLinkage())
                    empty = false;
            if (empty)
                continue;

            std::string text;
            llvm::raw_string_ostream os(text);
            part->print(os, nullptr);
            os.flush();

            fs::path obj = cacheDir / (hashOf(text, level) + ".o");
            if (fs::exists(obj, ec))
                fs::last_write_time(obj, fs::file_time_type::clock::now(), ec);
            else
            {
                optimizeModule(*part, level);
                fs::path tmp = obj;
                tmp += ".tmp" + std::to_string(::getpid());
                emitObjectFile(*part, tmp.string(), level);
                fs::rename(tmp, obj, ec);
                if (ec)
                {
                    fs::remove(tmp, ec);
                    throw std::runtime_error("cannot write the build cache '" + obj.string() + "'");
                }
            }
            objects.push_back(obj.string());
        }
        trimCache(cacheDir);
        return objects;
    }

    void trimCache(const fs::path &cacheDir, size_t keep)
    {
        std::error_code ec;
        std::vector<std::pair<fs::file_time_type, fs::path>> files;
        for (const auto &e : fs::directory_iterator(cacheDir, ec))
            if (e.path().extension() == ".o")
                files.emplace_back(e.last_write_time(ec), e.path());
        if (files.size() <= keep * 2)
            return;
        std::sort(files.begin(), files.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
        for (size_t i = keep; i < files.size(); ++i)
            fs::remove(files[i].second, ec);
    }

    size_t cleanCache(const fs::path &cacheDir)
    {
        std::error_code ec;
        size_t n = 0;
        for (const auto &e : fs::directory_iterator(cacheDir, ec))
            if (e.path().extension() == ".o" && fs::remove(e.path(), ec))
                ++n;
        return n;
    }

}
