#include "codegen/ObjectEmitter.h"

#include <llvm/IR/LegacyPassManager.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/TargetParser/Host.h>
#include <llvm/TargetParser/Triple.h>

#include <stdexcept>

namespace pgt {

void emitObjectFile(llvm::Module& module, const std::string& path) {
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();

    llvm::Triple triple(llvm::sys::getDefaultTargetTriple());
    std::string err;
    const llvm::Target* target = llvm::TargetRegistry::lookupTarget(triple, err);
    if (!target) throw std::runtime_error("cannot find LLVM target: " + err);

    llvm::TargetOptions opts;
    std::unique_ptr<llvm::TargetMachine> tm(
        target->createTargetMachine(triple, "generic", "", opts, llvm::Reloc::PIC_));
    if (!tm) throw std::runtime_error("cannot create LLVM target machine");

    module.setTargetTriple(triple);
    module.setDataLayout(tm->createDataLayout());

    std::error_code ec;
    llvm::raw_fd_ostream out(path, ec, llvm::sys::fs::OF_None);
    if (ec) throw std::runtime_error("cannot open '" + path + "': " + ec.message());

    llvm::legacy::PassManager pass;
    if (tm->addPassesToEmitFile(pass, out, nullptr, llvm::CodeGenFileType::ObjectFile))
        throw std::runtime_error("target machine cannot emit an object file");
    pass.run(module);
    out.flush();
}

}
