#include "codegen/CodeGen.h"

#include "codegen/ObjectEmitter.h"
#include "common/Error.h"
#include "lexer/Lexer.h"

#include <llvm/IR/InlineAsm.h>

#include <cctype>

namespace pgt
{
    namespace
    {

        bool identStart(char c)
        {
            return std::isalpha(static_cast<unsigned char>(c)) || c == '_' || c == '.';
        }

        bool identChar(char c)
        {
            return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.' || c == '$';
        }

        std::set<std::string> asmLabels(const std::string &text)
        {
            std::set<std::string> labels;
            size_t i = 0, n = text.size();
            bool lineStart = true;
            while (i < n)
            {
                char c = text[i];
                if (c == ';')
                {
                    while (i < n && text[i] != '\n')
                        ++i;
                    continue;
                }
                if (c == '\n')
                {
                    lineStart = true;
                    ++i;
                    continue;
                }
                if (c == ' ' || c == '\t' || c == '\r')
                {
                    ++i;
                    continue;
                }
                if (identStart(c))
                {
                    size_t b = i;
                    while (i < n && identChar(text[i]))
                        ++i;
                    if (lineStart && i < n && text[i] == ':' && text[b] != '.')
                        labels.insert(text.substr(b, i - b));
                    lineStart = false;
                    continue;
                }
                lineStart = false;
                ++i;
            }
            return labels;
        }

        std::string ptrPrefix(int size)
        {
            switch (size)
            {
            case 1:
                return "byte ptr ";
            case 4:
                return "dword ptr ";
            default:
                return "qword ptr ";
            }
        }

        std::string renderAsm(const std::string &text, const std::vector<std::string> &names,
                              const std::vector<int> &sizes, bool real)
        {
            std::set<std::string> labels = real ? asmLabels(text) : std::set<std::string>{};
            std::string out;
            size_t i = 0, n = text.size();
            while (i < n)
            {
                char c = text[i];
                if (c == ';')
                {
                    size_t b = i;
                    while (i < n && text[i] != '\n')
                        ++i;
                    std::string cm = text.substr(b, i - b);
                    for (char ch : cm)
                    {
                        out += ch;
                        if (ch == '$' && real)
                            out += '$';
                    }
                    continue;
                }
                if (c == '{')
                {
                    size_t e = text.find('}', i);
                    std::string name = text.substr(i + 1, e - i - 1);
                    size_t idx = 0;
                    while (names[idx] != name)
                        ++idx;
                    out += ptrPrefix(sizes[idx]);
                    out += real ? "$" + std::to_string(idx) : std::string("[rsp]");
                    i = e + 1;
                    continue;
                }
                if (real && c == '$')
                {
                    out += "$$";
                    ++i;
                    continue;
                }
                if (identStart(c) || std::isdigit(static_cast<unsigned char>(c)))
                {
                    size_t b = i;
                    while (i < n && identChar(text[i]))
                        ++i;
                    std::string word = text.substr(b, i - b);
                    if (labels.count(word))
                        out += ".L" + word + "${:uid}";
                    else
                        out += word;
                    continue;
                }
                out += c;
                ++i;
            }
            return out;
        }

        std::string cTypeOf(const Type &t, bool &readOnly)
        {
            readOnly = false;
            switch (t.kind)
            {
            case Type::Kind::Int:
                return "int32_t";
            case Type::Kind::Int64:
                return "int64_t";
            case Type::Kind::Uint64:
                return "uint64_t";
            case Type::Kind::Float:
                return "double";
            case Type::Kind::Char:
                return "char";
            case Type::Kind::Byte:
                return "uint8_t";
            case Type::Kind::Bool:
                return "bool";
            case Type::Kind::Pointer:
                return "void *";
            case Type::Kind::String:
                readOnly = true;
                return "const char *";
            default:
                return "";
            }
        }

        std::string sanitize(const std::string &s)
        {
            std::string out;
            for (char c : s)
                out += std::isalnum(static_cast<unsigned char>(c)) ? c : '_';
            return out;
        }

        std::string lineDirective(int line, const std::string &file)
        {
            std::string f;
            for (char c : file)
            {
                if (c == '\\' || c == '"')
                    f += '\\';
                f += c;
            }
            return "#line " + std::to_string(line) + " \"" + f + "\"\n";
        }

    }

    CodeGen::CUnit &CodeGen::cUnit()
    {
        std::string key = pkg_->pkg->importPath;
        auto it = cIndex_.find(key);
        if (it != cIndex_.end())
            return cUnits_[it->second];
        cIndex_[key] = cUnits_.size();
        CUnit u;
        u.name = sanitize(key);
        u.top = "#include <stdint.h>\n#include <stddef.h>\n#include <stdbool.h>\n";
        cUnits_.push_back(std::move(u));
        return cUnits_.back();
    }

    void CodeGen::requireImport(const char *path, const char *shown, int line, int col)
    {
        if (pkg_->pkg->importPath == "c")
            return;
        for (const auto &[name, syms] : fileImports_)
            if (syms->pkg->importPath == path)
                return;
        fail(std::string(path == std::string("c") ? "c" : "asm") + " block needs #use \"pgt/std/" + shown + "\"", line, col);
    }

    void CodeGen::genTopBlocks(const Program &p)
    {
        for (const TopBlock &b : p.blocks)
        {
            requireImport(b.isC ? "c" : "asm", b.isC ? "c" : "asm", b.line, b.col);
            if (b.isC)
            {
                CUnit &u = cUnit();
                u.top += lineDirective(b.line, p.file) + b.text + "\n";
            }
            else
            {
                checkAssembly(b.text, b.line);
                module_->appendModuleInlineAsm(".intel_syntax noprefix\n" + b.text + "\n.att_syntax\n.text\n");
            }
        }
    }

    void CodeGen::genAsm(const AsmStmt &stmt)
    {
        requireImport("asm", "asm", stmt.line, stmt.col);
        std::vector<llvm::Value *> addrs;
        std::vector<llvm::Type *> elems;
        std::vector<int> sizes;
        for (const std::string &name : stmt.names)
        {
            auto var = lookupVar(name);
            if (!var)
                fail("undefined variable '" + name + "' in asm block", stmt.line, stmt.col);
            if (var->isConst)
                fail("constant '" + name + "' cannot be used in an asm block", stmt.line, stmt.col);
            int size = 0;
            switch (var->type->kind)
            {
            case Type::Kind::Int:
                size = 4;
                break;
            case Type::Kind::Int64:
            case Type::Kind::Uint64:
            case Type::Kind::Pointer:
                size = 8;
                break;
            case Type::Kind::Byte:
            case Type::Kind::Char:
            case Type::Kind::Bool:
                size = 1;
                break;
            default:
                fail("variable '" + name + "' has type " + var->type->name() +
                         "; asm blocks accept only int, int64, uint64, byte, char, bool and pointers (*T)",
                     stmt.line, stmt.col);
            }
            addrs.push_back(var->addr);
            elems.push_back(var->type->is(Type::Kind::Bool) ? static_cast<llvm::Type *>(builder_.getInt8Ty())
                                                            : llvmType(var->type));
            sizes.push_back(size);
        }

        checkAssembly(renderAsm(stmt.text, stmt.names, sizes, false), stmt.line);

        std::string constraints;
        for (size_t i = 0; i < addrs.size(); ++i)
            constraints += "=*m,";
        constraints += "~{memory},~{dirflag},~{fpsr},~{flags}";
        for (const char *r : {"rax", "rbx", "rcx", "rdx", "rsi", "rdi", "r8", "r9", "r10", "r11", "r12", "r13",
                              "r14", "r15"})
            constraints += std::string(",~{") + r + "}";

        std::vector<llvm::Type *> ptrs(addrs.size(), builder_.getPtrTy());
        auto *fty = llvm::FunctionType::get(builder_.getVoidTy(), ptrs, false);
        llvm::InlineAsm *ia = llvm::InlineAsm::get(fty, renderAsm(stmt.text, stmt.names, sizes, true), constraints,
                                                   true, false, llvm::InlineAsm::AD_Intel);
        llvm::CallInst *call = builder_.CreateCall(ia, addrs);
        for (size_t i = 0; i < addrs.size(); ++i)
            call->addParamAttr(i, llvm::Attribute::get(ctx_, llvm::Attribute::ElementType, elems[i]));
    }

    void CodeGen::genCBlock(const CStmt &stmt)
    {
        requireImport("c", "c", stmt.line, stmt.col);
        struct Slot
        {
            std::string name;
            llvm::Value *addr;
            std::string ctype;
            bool readOnly;
        };
        std::vector<Slot> slots;
        for (const std::string &name : stmt.names)
        {
            auto var = lookupVar(name);
            if (!var)
                continue;
            bool ro = false;
            std::string ct = cTypeOf(*var->type, ro);
            if (ct.empty())
                fail("variable '" + name + "' has type " + var->type->name() +
                         "; c blocks accept only int, int64, uint64, float, byte, char, bool, string and pointers (*T)",
                     stmt.line, stmt.col);
            if (var->isConst || var->isManual)
                ro = true;
            slots.push_back({name, var->addr, ct, ro});
        }

        std::string body;
        size_t last = 0;
        scanC(stmt.text, [&](size_t b, size_t e) {
            std::string name = stmt.text.substr(b + 1, e - b - 1);
            for (size_t i = 0; i < slots.size(); ++i)
                if (slots[i].name == name)
                {
                    body += stmt.text.substr(last, b - last) + "(*v" + std::to_string(i) + ")";
                    last = e + 1;
                    return;
                }
        });
        body += stmt.text.substr(last);

        CUnit &u = cUnit();
        std::string fname = "pgt_cblock_" + u.name + "_" + std::to_string(u.blocks++);
        std::string sig;
        for (size_t i = 0; i < slots.size(); ++i)
        {
            if (i)
                sig += ", ";
            sig += slots[i].ctype + (slots[i].readOnly ? " const" : "") + " *v";
            sig += std::to_string(i);
        }
        if (slots.empty())
            sig = "void";
        u.fns += "void " + fname + "(" + sig + ")\n{\n" + lineDirective(stmt.line, currentFile_) + body + "\n}\n";

        std::vector<llvm::Type *> ptrs(slots.size(), builder_.getPtrTy());
        auto *fty = llvm::FunctionType::get(builder_.getVoidTy(), ptrs, false);
        llvm::FunctionCallee callee = module_->getOrInsertFunction(fname, fty);
        std::vector<llvm::Value *> args;
        for (const Slot &s : slots)
            args.push_back(s.addr);
        builder_.CreateCall(callee, args);
    }

}
