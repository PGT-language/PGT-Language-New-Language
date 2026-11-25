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
        struct CUnit
        {
            std::string name;
            std::string top;
            std::string fns;
            int blocks = 0;
            std::string source() const
            {
                return top + fns;
            }
        };
        const std::vector<CUnit> &cUnits() const
        {
            return cUnits_;
        }
        void setFreestanding(bool on)
        {
            freestanding_ = on;
        }
        void setEntry(const std::string &name)
        {
            entryName_ = name;
        }

    private:
        CUnit &cUnit();
        std::vector<CUnit> cUnits_;
        std::map<std::string, size_t> cIndex_;
        bool freestanding_ = false;
        std::string entryName_ = "kmain";
        void genKernelEntry();
        bool inStdFile() const;
        struct Value
        {
            llvm::Value *value = nullptr;
            TyP type = nullptr;
            bool owned = false;
        };
        struct Var
        {
            llvm::Value *addr = nullptr;
            TyP type = nullptr;
            bool isConst = false;
            bool isManual = false;
            bool indirect = false;
            llvm::Value *countSlot = nullptr;
        };
        using Wide = __int128;
        struct Global
        {
            llvm::GlobalVariable *gv = nullptr;
            TyP type = nullptr;
            bool isConst = false;
            bool untyped = false;
            Wide value = 0;
        };
        struct Override
        {
            llvm::Value *slot = nullptr;
            TyP type = nullptr;
            bool addr = false;
        };
        struct DeferSite
        {
            llvm::Value *flag = nullptr;
            const Call *call = nullptr;
            std::vector<std::pair<const Expr *, Override>> captures;
        };
        struct LoopTargets
        {
            llvm::BasicBlock *continueTo;
            llvm::BasicBlock *breakTo;
            size_t depth = 0;
        };
        struct OwnedLocal
        {
            llvm::Value *slot = nullptr;
            TyP type = nullptr;
            bool boxed = false;
        };
        struct Scope
        {
            std::map<std::string, Var> vars;
            std::vector<OwnedLocal> owned;
        };
        struct Temp
        {
            llvm::Value *value;
            TyP type;
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
        struct Addr
        {
            llvm::Value *addr = nullptr;
            TyP type = nullptr;
            bool isConst = false;
        };

        llvm::Type *llvmType(TyP t);
        llvm::Constant *sizeOf(TyP t);
        TyP resolveType(const TypeExpr &type, bool allowVoid = false);
        llvm::Value *zeroValue(TyP t);
        static bool isPublic(const std::string &name);
        static std::string mangle(const std::string &importPath, const std::string &name);
        [[noreturn]] void fail(const std::string &msg, int line, int col) const;

        llvm::AllocaInst *entryAlloca(llvm::Type *type, const std::string &name);
        llvm::Value *newVarSlot(TyP type, const std::string &name);
        void pushScope();
        void popScope();
        Var *lookupLocal(const std::string &name);
        std::optional<Var> lookupVar(const std::string &name);
        PackageSyms *lookupPackage(const std::string &name);
        void declare(const std::string &name, Var var, int line, int col);
        void startDeadBlock();

        bool isManaged(TyP t);
        void emitRetain(llvm::Value *v, TyP t);
        void emitRelease(llvm::Value *v, TyP t);
        llvm::Function *structHelper(TyP t, bool retain);
        llvm::Constant *dropFn(TyP t);
        llvm::Constant *retainFn(TyP t);
        Value owned(Value v);
        llvm::Value *take(const Value &v);
        void flushTemps(size_t downTo = 0);
        void storeValue(llvm::Value *addr, TyP t, Value v, bool initial);
        void holdValue(Value v);
        void emitScopeReleases(size_t downTo);
        bool isManualElement(const Expr &target);

        void checkExitCodes(const Package &pkg) const;
        void declareTypes(const Package &pkg);
        void resolveTypeBodies(const Package &pkg);
        void checkStructCycles();
        void declareFunctions(const Package &pkg);
        FuncInfo declareSignature(const Function &fn, TyP recvType, const std::string &symbol);
        void genGlobals(const Package &pkg);
        void bindImports(const Program &program);
        void genFunction(const Function &fn, const FuncInfo &info);
        void genBody(const std::vector<Param> &params, const Receiver *receiver, const Block &body,
                     const std::string &fullName, int line, int col, const FuncInfo &info);
        Value genFuncLit(const FuncLit &lit);
        Value funcValue(const FuncInfo &info, const std::string &name, int line, int col);
        std::optional<Value> packageFuncValue(const Selector &sel);
        Value callValue(Value fn, const Call &call, const std::string &name);
        void genEntryPoint(const Package &root, const Expr &exitCode);

        void genBlock(const Block &block);
        void genStmt(const Stmt &stmt);
        void genVarDecl(const VarDecl &decl);
        void genManualVarDecl(const VarDecl &decl);
        void handOverToManual(const VarDecl &decl, Var &var, size_t scopeIndex);
        const FuncInfo &runtimeFn(const std::string &name);
        const FuncInfo &memoryFn(const std::string &name);
        void genAssign(const Assign &assign);
        void genReturn(const Return &ret);
        void genIf(const If &node);
        void genLoop(const Loop &loop);
        void genForIn(const ForIn &loop);
        void genSwitch(const Switch &sw);
        void genAsm(const AsmStmt &stmt);
        void genCBlock(const CStmt &stmt);
        void requireImport(const char *path, const char *shown, int line, int col);
        void genTopBlocks(const Program &p);
        void genDefer(const Defer &stmt);
        void emitDefers();
        llvm::AllocaInst *zeroedEntrySlot(llvm::Type *type, const std::string &name);
        bool isConstExpr(const Expr &e);
        bool isStatelessExpr(const Expr &e);
        std::optional<Wide> constValue(const Expr &e);
        std::optional<Wide> constValueRaw(const Expr &e);
        const Global *untypedGlobal(const Expr &e);
        Value untypedConst(Wide v, TyP hint, int line, int col);
        Value genTernary(const Ternary &t, TyP hint);
        std::optional<std::vector<Value>> genMapGetOk(const Index &idx);
        void genMultiVarDecl(const MultiVarDecl &decl);
        void genMultiAssign(const MultiAssign &assign);
        void genMultiReturn(const Return &ret);
        void genTypeSwitch(const Switch &sw);
        std::vector<Value> expandTuple(Value v);
        std::vector<Value> genValueList(const std::vector<ExprPtr> &values, size_t want,
                                        const std::vector<TyP> &hints, int line, int col);
        Value genTypeAssert(const TypeAssert &ta);
        std::vector<Value> genAssertOk(const TypeAssert &ta);
        llvm::Value *dynTypeDesc(Value v);
        llvm::Function *ifaceAssertFn(TyP ifaceType);
        void finishAssertFns();
        llvm::Value *assertMatch(llvm::Value *desc, TyP target);
        llvm::Value *assertExtract(llvm::Value *data, llvm::Value *desc, TyP target);
        Value assertTo(Value src, TyP target, bool commaOk, llvm::Value **okOut);
        void checkAssertable(Value src, TyP target, int line, int col);

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
        Value genSlice(const SliceExpr &sl);
        Value genArrayLit(const ArrayLit &lit, TyP hint);
        Value genStructLit(const StructLit &lit);
        struct SpreadArgs
        {
            bool spread = false;
            std::vector<Value> values;
        };
        SpreadArgs genSpreadArgs(const Call &call, TyP hint);
        Value genCall(const Call &call, TyP hint);
        Value callFunction(const FuncInfo &info, const std::string &name, const Call &call,
                           std::vector<llvm::Value *> args, size_t firstParam,
                           llvm::Value *indirect = nullptr);
        Value genMethodCall(const Call &call, const Selector &sel);
        Value genIfaceCall(Value iface, const Call &call, const std::string &method);
        Value makeIface(Value v, TyP ifaceType, int line, int col);
        Value makeAny(Value v);
        llvm::Constant *typeDesc(TyP t);
        Value packArray(TyP elemTy, const std::vector<Value> &values);
        std::string missingMethod(TyP concrete, const IfaceInfo &iface) const;
        llvm::Value *vtableFor(TyP concrete, TyP ifaceType);
        llvm::Function *methodThunk(StructInfo *st, const FuncInfo &m);
        Value toStringValue(Value v, int line, int col);
        void nilCheck(llvm::Value *ptr);

        llvm::Value *rt(const std::string &name, llvm::Type *ret, std::vector<llvm::Value *> args,
                        bool noReturn = false);
        llvm::FunctionCallee rtDecl(const std::string &name, llvm::Type *ret,
                                    std::vector<llvm::Type *> params, bool noReturn = false);
        void panicIf(llvm::Value *cond, const std::string &fn, std::vector<llvm::Value *> args);
        llvm::Value *arrayToPtr(llvm::Value *arr);
        Value genIntrinsic(const FuncInfo &info, const std::vector<llvm::Value *> &args);
        std::optional<Value> genBuiltin(const Call &call, const std::string &name, TyP hint);
        Value genConversion(const Call &call, TyP target);
        Value genMath(const Call &call, const std::string &name);
        struct MapTarget
        {
            bool active = false;
            llvm::Value *map = nullptr;
            llvm::Value *key = nullptr;
            llvm::Value *val = nullptr;
            TyP valType = nullptr;
        };
        MapTarget mapTarget_;
        llvm::Function *mapDrop_ = nullptr;
        const FuncInfo &mapsFn(const std::string &name);
        llvm::Value *mapDropFn();
        llvm::Value *mapKeySlot(TyP mapType, const Expr &key);
        Value genMapNew(TyP mapType);
        Value genMapLit(const MapLit &lit);
        Value genMapGet(Value map, const Expr &key);
        Addr genMapAddr(Value map, const Expr &key, bool forWrite);
        void commitMapTarget(const MapTarget &mt);
        void genMapForIn(const ForIn &loop, Value seq);
        std::optional<Value> genMapBuiltin(const Call &call, const std::string &name);

        llvm::LLVMContext ctx_;
        std::unique_ptr<llvm::Module> module_;
        llvm::IRBuilder<> builder_;
        TypeTable types_;
        llvm::StructType *arrayTy_ = nullptr;
        llvm::StructType *ifaceTy_ = nullptr;
        llvm::StructType *descTy_ = nullptr;
        llvm::StructType *fieldDescTy_ = nullptr;
        std::map<TyP, llvm::GlobalVariable *> descs_;

        const Workspace *ws_ = nullptr;
        PackageSyms *pkg_ = nullptr;
        std::string currentFile_;
        TyP currentRet_ = nullptr;
        std::map<std::string, PackageSyms> syms_;
        std::deque<StructInfo> structStore_;
        std::deque<IfaceInfo> ifaceStore_;
        std::map<std::string, PackageSyms *> fileImports_;
        std::vector<Scope> scopes_;
        std::vector<Temp> temps_;
        std::map<TyP, bool> managed_;
        std::map<std::pair<TyP, int>, llvm::Function *> memFns_;
        std::vector<LoopTargets> loops_;
        std::vector<DeferSite> defers_;
        std::map<const Expr *, Override> overrides_;
        std::set<std::string> heapVars_;
        std::map<std::pair<TyP, TyP>, llvm::GlobalVariable *> vtables_;
        std::map<std::string, llvm::Function *> thunks_;
        std::map<TyP, llvm::Function *> assertFns_;
        int litCounter_ = 0;
    };

}
