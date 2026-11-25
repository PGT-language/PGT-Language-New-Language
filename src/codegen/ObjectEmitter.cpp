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

#include "common/Error.h"

#include <llvm/IR/DiagnosticInfo.h>
#include <llvm/IR/DiagnosticPrinter.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/SourceMgr.h>

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

        bool g_freestanding = false;

        std::unique_ptr<llvm::TargetMachine> makeMachine(llvm::Module &module, int level)
        {
            llvm::InitializeNativeTarget();
            llvm::InitializeNativeTargetAsmPrinter();

            llvm::Triple triple(g_freestanding ? std::string("x86_64-unknown-none-elf")
                                               : llvm::sys::getDefaultTargetTriple());
            std::string err;
            const llvm::Target *target = llvm::TargetRegistry::lookupTarget(triple, err);
            if (!target)
                throw std::runtime_error("cannot find LLVM target: " + err);

            llvm::TargetOptions opts;
            std::unique_ptr<llvm::TargetMachine> tm(target->createTargetMachine(
                triple, "generic", g_freestanding ? "-mmx,-sse,-sse2,-sse3,-ssse3,-sse4.1,-sse4.2,-avx,-avx2,+soft-float" : "",
                opts, g_freestanding ? llvm::Reloc::Static : llvm::Reloc::PIC_,
                g_freestanding ? std::optional<llvm::CodeModel::Model>(llvm::CodeModel::Kernel) : std::nullopt,
                codegenLevel(level)));
            if (!tm)
                throw std::runtime_error("cannot create LLVM target machine");

            module.setTargetTriple(triple);
            module.setDataLayout(tm->createDataLayout());
            return tm;
        }

    }

    void setFreestanding(bool on)
    {
        g_freestanding = on;
    }

    namespace
    {
        struct AsmDiag
        {
            bool failed = false;
            int line = 0;
            std::string message;
        };

        void asmDiagCallback(const llvm::DiagnosticInfo *di, void *ctx)
        {
            auto *out = static_cast<AsmDiag *>(ctx);
            if (di->getSeverity() != llvm::DS_Error || out->failed)
                return;
            out->failed = true;
            if (di->getKind() == llvm::DK_SrcMgr)
            {
                const llvm::SMDiagnostic &sm = static_cast<const llvm::DiagnosticInfoSrcMgr *>(di)->getSMDiag();
                out->line = sm.getLineNo();
                out->message = sm.getMessage().str();
            }
            else
            {
                llvm::raw_string_ostream os(out->message);
                llvm::DiagnosticPrinterRawOStream p(os);
                di->print(p);
                os.flush();
            }
        }
    }

    void checkAssembly(const std::string &text, int firstLine)
    {
        llvm::LLVMContext ctx;
        AsmDiag diag;
        ctx.setDiagnosticHandlerCallBack(asmDiagCallback, &diag);
        llvm::Module module("asmcheck", ctx);
        module.setModuleInlineAsm(".intel_syntax noprefix\n" + text + "\n.att_syntax\n");
        std::unique_ptr<llvm::TargetMachine> tm = makeMachine(module, 0);
        llvm::SmallVector<char, 0> buf;
        llvm::raw_svector_ostream os(buf);
        llvm::legacy::PassManager pass;
        if (tm->addPassesToEmitFile(pass, os, nullptr, llvm::CodeGenFileType::ObjectFile))
            throw std::runtime_error("target machine cannot emit an object file");
        pass.run(module);
        if (diag.failed)
        {
            int line = diag.line > 1 ? firstLine + diag.line - 2 : firstLine;
            std::string msg = diag.message;
            size_t nl = msg.find('\n');
            if (nl != std::string::npos)
                msg = msg.substr(0, nl);
            throw CompileError("assembler: " + msg, line, 1);
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
