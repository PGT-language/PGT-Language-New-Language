#pragma once
#include "codegen/Types.h"
#include "loader/Package.h"

#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace pgt
{

    class CodeGen
    {
    public:
        explicit CodeGen(const std::string &moduleName);

        void generate(const Workspace &workspace);
        llvm::Module &module()
        {
            return *module_;
        }

    private:
        struct Value
        {
            llvm::Value *value = nullptr;
            TyP type = nullptr;
        };
        struct Var
        {
            llvm::Value *addr = nullptr;
            TyP type = nullptr;
            bool isConst = false;
            bool isManual = false; // `var&`: memory is released by free(x); never tracked automatically
        };
        struct Global
        {
            llvm::GlobalVariable *gv = nullptr;
            TyP type = nullptr;
            bool isConst = false;
        };
        struct LoopTargets
        {
            llvm::BasicBlock *continueTo; // null for `switch`
            llvm::BasicBlock *breakTo;
        };
        struct PackageSyms
        {
            const Package *pkg = nullptr;
            std::map<std::string, FuncInfo> functions;
            std::map<std::string, StructInfo *> structs;
            std::map<std::string, IfaceInfo *> ifaces;
            std::map<std::string, Global> globals;
            llvm::Function *init = nullptr;
        };
        // The result of resolving a name or selector that is not a local variable.
        struct Addr
        {
            llvm::Value *addr = nullptr;
            TyP type = nullptr;
            bool isConst = false;
        };

        // ---- types (CodeGen.cpp)
        llvm::Type *llvmType(TyP t);
        llvm::Constant *sizeOf(TyP t);
        TyP resolveType(const TypeExpr &type, bool allowVoid = false);
        llvm::Value *zeroValue(TyP t);
        static bool isPublic(const std::string &name);
        static std::string mangle(const std::string &importPath, const std::string &name);
        [[noreturn]] void fail(const std::string &msg, int line, int col) const;

        // ---- scopes
        llvm::AllocaInst *entryAlloca(llvm::Type *type, const std::string &name);
        llvm::Value *newVarSlot(TyP type, const std::string &name);
        void pushScope();
        void popScope();
        Var *lookupLocal(const std::string &name);
        std::optional<Var> lookupVar(const std::string &name);
        PackageSyms *lookupPackage(const std::string &name);
        void declare(const std::string &name, Var var, int line, int col);
        void startDeadBlock();

        // ---- declarations
        void checkExitCodes(const Package &pkg) const;
        void declareTypes(const Package &pkg);
        void resolveTypeBodies(const Package &pkg);
        void checkStructCycles();
        void declareFunctions(const Package &pkg);
        FuncInfo declareSignature(const Function &fn, TyP recvType, const std::string &symbol);
        void genGlobals(const Package &pkg);
        void bindImports(const Program &program);
        void genFunction(const Function &fn, const FuncInfo &info);
        void genEntryPoint(const Package &root, const Expr &exitCode);

        // ---- statements (CodeGenStmt.cpp)
        void genBlock(const Block &block);
        void genStmt(const Stmt &stmt);
        void genVarDecl(const VarDecl &decl);
        void genManualVarDecl(const VarDecl &decl);
        void genAssign(const Assign &assign);
        void genReturn(const Return &ret);
        void genIf(const If &node);
        void genLoop(const Loop &loop);
        void genForIn(const ForIn &loop);
        void genSwitch(const Switch &sw);

        // ---- expressions (CodeGenExpr.cpp)
        Value genExpr(const Expr &expr, TyP hint = nullptr);
        Value genCond(const Expr &expr);
        Value coerce(Value v, TyP to, int line, int col, const std::string &what);
        Value convert(Value v, TyP to, int line, int col);
        Value genBinary(const Binary &bin, TyP hint);
        Value genLogical(const Binary &bin);
        Value genCompare(const Binary &bin);
        Value compareValues(BinOp op, Value l, Value r, int line, int col);
        Value genArith(BinOp op, Value l, Value r, int line, int col);
        Value genUnary(const Unary &un, TyP hint);
        void genOperands(const Expr &lhs, const Expr &rhs, TyP hint, Value &l, Value &r);
        std::optional<Addr> addressOf(const Expr &expr, bool forWrite);
        Addr requireAddress(const Expr &expr, bool forWrite);
        std::optional<Addr> packageMember(const Selector &sel);
        Value genSelector(const Selector &sel);
        Value genIndex(const Index &idx);
        llvm::Value *elementAddr(Value arr, const Expr &index, int line, int col);
        Value genArrayLit(const ArrayLit &lit, TyP hint);
        Value genStructLit(const StructLit &lit);
        Value genCall(const Call &call, TyP hint);
        Value callFunction(const FuncInfo &info, const std::string &name, const Call &call,
                           std::vector<llvm::Value *> args, size_t firstParam);
        Value genMethodCall(const Call &call, const Selector &sel);
        Value genIfaceCall(Value iface, const Call &call, const std::string &method);
        Value makeIface(Value v, TyP ifaceType, int line, int col);
        std::string missingMethod(TyP concrete, const IfaceInfo &iface) const;
        llvm::Value *vtableFor(TyP concrete, TyP ifaceType);
        llvm::Function *methodThunk(StructInfo *st, const FuncInfo &m);
        Value toStringValue(Value v, int line, int col);
        void nilCheck(llvm::Value *ptr);

        // ---- builtins and runtime (CodeGenBuiltins.cpp)
        llvm::Value *rt(const std::string &name, llvm::Type *ret, std::vector<llvm::Value *> args,
                        bool noReturn = false);
        llvm::FunctionCallee rtDecl(const std::string &name, llvm::Type *ret,
                                    std::vector<llvm::Type *> params, bool noReturn = false);
        void panicIf(llvm::Value *cond, const std::string &fn, std::vector<llvm::Value *> args);
        llvm::Value *arrayToPtr(llvm::Value *arr);
        std::optional<Value> genBuiltin(const Call &call, const std::string &name, TyP hint);
        Value genConversion(const Call &call, TyP target);
        void genPrint(const Call &call, bool newline);
        void genInput(const Call &call, bool wholeLine);
        void printValue(Value v, bool topLevel);
        llvm::Function *printFunction(TyP t);
        llvm::Function *printThunk(TyP concrete);
        Value genMath(const Call &call, const std::string &name);

        llvm::LLVMContext ctx_;
        std::unique_ptr<llvm::Module> module_;
        llvm::IRBuilder<> builder_;
        TypeTable types_;
        llvm::StructType *arrayTy_ = nullptr;
        llvm::StructType *ifaceTy_ = nullptr;

        const Workspace *ws_ = nullptr;
        PackageSyms *pkg_ = nullptr;
        std::string currentFile_;
        TyP currentRet_ = nullptr;
        std::map<std::string, PackageSyms> syms_;
        std::deque<StructInfo> structStore_;
        std::deque<IfaceInfo> ifaceStore_;
        std::map<std::string, PackageSyms *> fileImports_;
        std::vector<std::map<std::string, Var>> scopes_;
        std::vector<LoopTargets> loops_;
        std::set<std::string> heapVars_;
        std::map<std::pair<TyP, TyP>, llvm::GlobalVariable *> vtables_;
        std::map<std::string, llvm::Function *> thunks_;
        std::map<TyP, llvm::Function *> printFns_;
        std::map<TyP, llvm::Function *> printThunks_;
    };

} // namespace pgt
