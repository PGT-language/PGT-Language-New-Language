#include "codegen/ObjectEmitter.h"

#include <llvm/IR/LegacyPassManager.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Passes/OptimizationLevel.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/TargetParser/Host.h>
#include <llvm/TargetParser/Triple.h>

#include <stdexcept>

namespace pgt
{
    namespace
    {

        llvm::CodeGenOptLevel codegenLevel(int level)
        {
            switch (level)
            {
            case 0:
                return llvm::CodeGenOptLevel::None;
            case 1:
                return llvm::CodeGenOptLevel::Less;
            case 3:
                return llvm::CodeGenOptLevel::Aggressive;
            default:
                return llvm::CodeGenOptLevel::Default;
            }
        }

        llvm::OptimizationLevel passLevel(int level)
        {
            switch (level)
            {
            case 1:
                return llvm::OptimizationLevel::O1;
            case 3:
                return llvm::OptimizationLevel::O3;
            default:
                return llvm::OptimizationLevel::O2;
            }
        }

        std::unique_ptr<llvm::TargetMachine> makeMachine(llvm::Module &module, int level)
        {
            llvm::InitializeNativeTarget();
            llvm::InitializeNativeTargetAsmPrinter();

            llvm::Triple triple(llvm::sys::getDefaultTargetTriple());
            std::string err;
            const llvm::Target *target = llvm::TargetRegistry::lookupTarget(triple, err);
            if (!target)
                throw std::runtime_error("cannot find LLVM target: " + err);

            llvm::TargetOptions opts;
            std::unique_ptr<llvm::TargetMachine> tm(target->createTargetMachine(
                triple, "generic", "", opts, llvm::Reloc::PIC_, std::nullopt, codegenLevel(level)));
            if (!tm)
                throw std::runtime_error("cannot create LLVM target machine");

            module.setTargetTriple(triple);
            module.setDataLayout(tm->createDataLayout());
            return tm;
        }

    }

    void optimizeModule(llvm::Module &module, int level)
    {
        if (level <= 0)
            return;
        std::unique_ptr<llvm::TargetMachine> tm = makeMachine(module, level);

        llvm::LoopAnalysisManager lam;
        llvm::FunctionAnalysisManager fam;
        llvm::CGSCCAnalysisManager cgam;
        llvm::ModuleAnalysisManager mam;
        llvm::PassBuilder pb(tm.get());
        pb.registerModuleAnalyses(mam);
        pb.registerCGSCCAnalyses(cgam);
        pb.registerFunctionAnalyses(fam);
        pb.registerLoopAnalyses(lam);
        pb.crossRegisterProxies(lam, fam, cgam, mam);
        llvm::ModulePassManager mpm = pb.buildPerModuleDefaultPipeline(passLevel(level));
        mpm.run(module, mam);
    }

    void emitObjectFile(llvm::Module &module, const std::string &path, int level)
    {
        std::unique_ptr<llvm::TargetMachine> tm = makeMachine(module, level);

        std::error_code ec;
        llvm::raw_fd_ostream out(path, ec, llvm::sys::fs::OF_None);
        if (ec)
            throw std::runtime_error("cannot open '" + path + "': " + ec.message());

        llvm::legacy::PassManager pass;
        if (tm->addPassesToEmitFile(pass, out, nullptr, llvm::CodeGenFileType::ObjectFile))
            throw std::runtime_error("target machine cannot emit an object file");
        pass.run(module);
        out.flush();
    }

}
