#pragma once

#include <clang/AST/ASTContext.h>
#include <clang/AST/Attr.h>
#include <clang/AST/Decl.h>
#include <clang/AST/DeclGroup.h>
#include <clang/AST/DeclObjC.h>
#include <clang/AST/Expr.h>
#include <clang/AST/ExprObjC.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/AST/Stmt.h>
#include <clang/AST/StmtObjC.h>
#include <clang/Basic/Diagnostic.h>
#include <clang/Basic/SourceManager.h>
#include <llvm/ADT/DenseMap.h>
#include <llvm/ADT/DenseSet.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/ADT/StringMap.h>
#include <llvm/ADT/StringRef.h>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace llvm {
class PassBuilder;
} // namespace llvm

namespace veriobjc {

void resetStaticDispatchModuleState();
void registerVeriObjCPassBuilderCallbacks(llvm::PassBuilder &PB);

struct VeriObjCPluginOptions {
    bool EmitEffectsReport = false;
    bool EmitBranchReport = false;
    bool EmitMonoReport = false;
    bool EmitExportHeader = false;
    bool EmitSmtReport = false;
    bool SmtVerify = false;
    uint32_t SmtTimeoutMs = 2000u;
    std::string Z3Path;
};

struct CompiledDFA {
    uint32_t NumStates = 0;
    uint32_t StartState = 0;
    std::vector<uint8_t> Transitions; // NumStates * 256
    std::vector<uint8_t> AcceptMask;  // NumStates
};

class VeriObjCVisitor : public clang::RecursiveASTVisitor<VeriObjCVisitor> {
private:
    struct ErrFromReq {
        std::string TargetFrame;
        clang::SourceLocation Loc;
    };

    struct VConstexprState {
        static constexpr unsigned MAX_STEPS = 4096;
        static constexpr unsigned MAX_DEPTH = 64;
        unsigned StepsUsed = 0;
        unsigned MaxDepthSeen = 0;
        bool FuelExhausted = false;
        const clang::FunctionDecl *ActiveFD = nullptr;
    };

    struct MonoGenericEntry {
        std::string Package;
        std::string GenericName;
        unsigned UniqueCount = 0;
        unsigned DedupedCount = 0;
        uint32_t CodeBytes = 0;
    };

    struct DAGEdgeRecord {
        std::string From;
        std::string To;
        uint32_t WCET = 0;
        clang::SourceLocation Loc;
    };

    struct DspOpSite {
        std::string OpStr;
        unsigned Line = 0;
        clang::SourceLocation Loc;
    };

    struct HwKernelRecord {
        const clang::Decl *KernelDecl = nullptr;
        std::string KernelName;
        clang::SourceLocation Loc;
    };

    clang::ASTContext *Context;
    VeriObjCPluginOptions Opts;
    llvm::StringMap<clang::FunctionDecl *> DirectMethodDecls;
    std::vector<clang::FunctionDecl *> SynthesizedDefs;
    llvm::DenseMap<const clang::FunctionDecl *, std::vector<ErrFromReq>> DirectRaisesFrom;
    llvm::DenseMap<const clang::FunctionDecl *, llvm::SmallVector<std::pair<const clang::FunctionDecl *, clang::SourceLocation>, 4>> CallEdges;
    llvm::DenseMap<const clang::Decl *, std::vector<DspOpSite>> DirectDspOps;
    llvm::DenseMap<const clang::Decl *, llvm::SmallVector<std::pair<const clang::Decl *, clang::SourceLocation>, 4>> DspCallEdges;
    std::vector<HwKernelRecord> HwKernels;
    llvm::DenseMap<const clang::FunctionDecl *, llvm::SmallVector<unsigned, 4>> ForwardedParamSummaryCache;
    llvm::DenseMap<const clang::VarDecl *, clang::FunctionDecl *> MonomorphizedBlocks;
    llvm::DenseMap<const clang::VarDecl *, llvm::SmallVector<clang::VarDecl *, 4>> BlockCapturedVars;
    llvm::StringMap<clang::FunctionDecl *> DedupBlockDecls;
    llvm::StringMap<MonoGenericEntry> MonoGenericStats;
    std::vector<DAGEdgeRecord> DAGEdges;
    std::vector<const clang::FunctionDecl *> ContractedFunctions;
    unsigned MonoBlockCounter = 0;
    std::map<std::pair<const clang::FunctionDecl *, std::vector<int64_t>>, int64_t> VConstexprMemo;
    llvm::StringMap<clang::VarDecl *> CompiledDFADecls;
    std::vector<clang::Decl *> SynthesizedVarDefs;
    llvm::DenseMap<const clang::VarDecl *, std::string> SPSCChannelProducer;
    unsigned DFACounter = 0;

    // -----------------------------------------------------------------------
    // Shared Diagnostic & AST Construction Helpers
    // -----------------------------------------------------------------------

    template <unsigned N>
    clang::DiagnosticBuilder reportError(clang::SourceLocation Loc, const char (&Fmt)[N]) {
        clang::DiagnosticsEngine &Diag = Context->getDiagnostics();
        unsigned ID = Diag.getCustomDiagID(clang::DiagnosticsEngine::Error, Fmt);
        return Diag.Report(Loc, ID);
    }

    bool isInRuntimeHeader(clang::SourceLocation Loc) const {
        llvm::StringRef File = Context->getSourceManager().getFilename(Loc);
        return File.ends_with("veriobjc_runtime.h") ||
               File.contains("/rt/include/") ||
               File.contains("/vstd/include/") ||
               File.contains("/sys0/include/") ||
               File.contains("/hw/include/") ||
               File.contains("/host/include/");
    }

    clang::DeclRefExpr *buildDeclRef(clang::ValueDecl *VD, clang::SourceLocation Loc) {
        return clang::DeclRefExpr::Create(
            *Context, clang::NestedNameSpecifierLoc(), clang::SourceLocation(),
            VD, /*RefersToEnclosingVariableOrCapture=*/false, Loc, VD->getType(), clang::VK_LValue
        );
    }

    clang::ImplicitCastExpr *buildRValueRef(clang::ValueDecl *VD, clang::SourceLocation Loc) {
        clang::DeclRefExpr *Ref = buildDeclRef(VD, Loc);
        return clang::ImplicitCastExpr::Create(
            *Context, VD->getType(), clang::CK_LValueToRValue, Ref, nullptr, clang::VK_PRValue, clang::FPOptionsOverride()
        );
    }

    clang::ImplicitCastExpr *buildFnPtrDecay(clang::FunctionDecl *FD, clang::SourceLocation Loc) {
        clang::DeclRefExpr *Ref = buildDeclRef(FD, Loc);
        return clang::ImplicitCastExpr::Create(
            *Context, Context->getPointerType(FD->getType()),
            clang::CK_FunctionToPointerDecay, Ref, nullptr, clang::VK_PRValue, clang::FPOptionsOverride()
        );
    }

    clang::ImplicitCastExpr *buildArrayDecay(clang::Expr *ArrExpr, clang::QualType ElemPtrTy) {
        return clang::ImplicitCastExpr::Create(
            *Context, ElemPtrTy, clang::CK_ArrayToPointerDecay, ArrExpr, nullptr, clang::VK_PRValue, clang::FPOptionsOverride()
        );
    }

    static bool hasVeriObjCAnnotation(const clang::Decl *D, llvm::StringRef Tag) {
        if (!D) return false;
        for (const auto *A : D->specific_attrs<clang::AnnotateAttr>()) {
            if (A->getAnnotation() == Tag) return true;
        }
        return false;
    }

    static bool isSealedClass(const clang::ObjCInterfaceDecl *D) {
        return hasVeriObjCAnnotation(D, "objc_sealed");
    }

    static bool isFinalClass(const clang::ObjCInterfaceDecl *D) {
        return hasVeriObjCAnnotation(D, "objc_final") ||
               (D && D->hasAttr<clang::ObjCSubclassingRestrictedAttr>());
    }

    static bool isInferredSignatureType(clang::QualType T) {
        if (T.isNull()) return false;
        if (T->getAs<clang::AutoType>() != nullptr) return true;
        const auto *RT = T->getAs<clang::RecordType>();
        return RT && RT->getDecl()->getName() == "__veriobjc_inferred_auto";
    }

    static bool extractAnnotatedCapacity(const clang::Decl *D, llvm::StringRef Prefix, uint32_t &OutCap) {
        if (!D) return false;
        for (const auto *A : D->specific_attrs<clang::AnnotateAttr>()) {
            llvm::StringRef Ann = A->getAnnotation();
            if (Ann.starts_with(Prefix)) {
                unsigned Val = 0;
                if (!Ann.drop_front(Prefix.size()).getAsInteger(10, Val)) {
                    OutCap = Val;
                    return true;
                }
            }
        }
        return false;
    }

    static const clang::VarDecl *extractVarDecl(const clang::Expr *E) {
        if (!E) return nullptr;
        E = E->IgnoreParenCasts();
        if (const auto *UO = llvm::dyn_cast<clang::UnaryOperator>(E); UO && UO->getOpcode() == clang::UO_AddrOf) {
            E = UO->getSubExpr()->IgnoreParenCasts();
        }
        const auto *DRE = llvm::dyn_cast<clang::DeclRefExpr>(E);
        return DRE ? llvm::dyn_cast<clang::VarDecl>(DRE->getDecl()) : nullptr;
    }

    void reportBannedId(clang::SourceLocation Loc) {
        reportError(Loc, "VeriObjC Violation: Banned use of generic 'id' type. Use strict nominal object typing.");
    }

    void reportEscapingBlock(clang::SourceLocation Loc) {
        reportError(Loc, "VeriObjC Violation: Banned escaping function block: Escaping function block is banned. Function blocks must be non-escaping and monomorphized at compile time.");
    }

    void reportConsumedVar(
        clang::SourceLocation Loc,
        llvm::StringRef VarName,
        llvm::StringRef RootName,
        llvm::StringRef StateName,
        llvm::StringRef FnName
    ) {
        unsigned Line = Context->getSourceManager().getSpellingLineNumber(Loc);
        reportError(
            Loc,
            "VeriObjC Violation: Use of consumed linear variable '%0' (root '%1', state '%2') in function '%3' at line %4 after move or free."
        ) << VarName << RootName << StateName << FnName << Line;
    }

    void reportStackEscape(
        clang::SourceLocation Loc,
        llvm::StringRef RootName,
        unsigned RootDepth,
        llvm::StringRef SinkDesc,
        llvm::StringRef FnName
    ) {
        unsigned Line = Context->getSourceManager().getSpellingLineNumber(Loc);
        reportError(
            Loc,
            "VeriObjC Violation: Escaping data-stack reference '%0' (rooted at scope depth %1) escapes to %2 in function '%3' at line %4."
        ) << RootName << RootDepth << SinkDesc << FnName << Line;
    }

    // -----------------------------------------------------------------------
    // AstBanChecks.cpp
    // -----------------------------------------------------------------------
    bool validateRegexSyntax(llvm::StringRef Pattern, clang::SourceLocation Loc);
    bool compileRegexToMinimizedDFA(llvm::StringRef Pattern, clang::SourceLocation Loc, CompiledDFA &OutDFA);
    clang::VarDecl *synthesizeByteTableVar(llvm::StringRef Name, const std::vector<uint8_t> &Bytes, clang::SourceLocation Loc);
    clang::VarDecl *getOrCreateDFAObjectVar(llvm::StringRef Pattern, clang::SourceLocation Loc);
    clang::Expr *buildLoweredVRegexMatch(clang::CallExpr *CE);

    bool consumeFuel(VConstexprState &State, unsigned Depth);
    static bool evalBinaryIntOp(clang::BinaryOperator::Opcode Op, int64_t L, int64_t R, int64_t &OutVal);
    bool evalVConstexprBinaryOp(
        const clang::BinaryOperator *BO,
        llvm::DenseMap<const clang::VarDecl *, int64_t> &Env,
        VConstexprState &State,
        unsigned Depth,
        int64_t &OutVal
    );
    bool evalVConstexprExpr(
        const clang::Expr *E,
        llvm::DenseMap<const clang::VarDecl *, int64_t> &Env,
        VConstexprState &State,
        unsigned Depth,
        int64_t &OutVal
    );
    bool evalVConstexprStmt(
        const clang::Stmt *S,
        llvm::DenseMap<const clang::VarDecl *, int64_t> &Env,
        VConstexprState &State,
        unsigned Depth,
        bool &HasReturned,
        int64_t &RetVal
    );
    bool evalVConstexprCall(
        const clang::FunctionDecl *FD,
        const std::vector<int64_t> &Args,
        VConstexprState &State,
        unsigned Depth,
        int64_t &OutVal
    );
    bool evalConstPair(const clang::Expr *E1, const clang::Expr *E2, int64_t &Out1, int64_t &Out2);
    clang::Expr *tryFoldVConstexprCall(clang::CallExpr *CE, const clang::FunctionDecl *CalleeFD);
    clang::Expr *tryFoldReflectFieldCount(clang::CallExpr *CE);

    void verifyStreamZipCall(clang::CallExpr *E);
    void verifyReduceLatencyCall(clang::CallExpr *E);
    static llvm::StringRef classifyCallEffect(const clang::FunctionDecl *CalleeFD);
    void inspectCallSiteEffects(const clang::CallExpr *CE, const clang::FunctionDecl *CallerFD, std::vector<std::string> &Survivors);
    void walkFunctionEffects(const clang::Stmt *S, const clang::FunctionDecl *CallerFD, std::vector<std::string> &Survivors);
    static bool exprContainsBuiltinExpect(const clang::Stmt *S);
    void walkFunctionBranches(const clang::Stmt *S, unsigned &Proven, unsigned &Hinted, unsigned &Unproven);
    void auditFunctionEffectsAndReport(const clang::FunctionDecl *FD);
    bool isAllowedFFIBoundaryType(clang::QualType T) const;
    void verifyExportFunctionAndEmitHeader(const clang::FunctionDecl *D);
    const clang::EnumDecl *extractVVariantTagEnum(const clang::Expr *Cond, std::string &OutVariantName) const;
    void handleMonoInstantiateCall(clang::CallExpr *E);
    void recordDAGEdgeCall(clang::CallExpr *E);
    bool verifyTaskDAGAndComputeWCET(clang::SourceLocation DefaultLoc, uint32_t &OutWCET);

    // -----------------------------------------------------------------------
    // CfgDataflowChecks.cpp
    // -----------------------------------------------------------------------
    static bool isStackAllocExpr(const clang::Expr *E);
    bool canTypeCarryStackReference(clang::QualType T) const;
    llvm::SmallVector<unsigned, 4> getForwardedParamIndices(
        const clang::FunctionDecl *FD,
        llvm::DenseSet<const clang::FunctionDecl *> &Visiting
    );
    void analyzeFunctionCFGDataflow(clang::Stmt *Body, clang::DeclContext *DC);
    void recordDspGraphForBody(const clang::Decl *OwnerDecl, const clang::Stmt *Body);
    void registerHwKernelRoot(const clang::Decl *KernelDecl, llvm::StringRef KernelName, clang::SourceLocation Loc);
    void verifyAllHwKernelsZeroDsp();
    void collectErrorReturnGraph(const clang::Stmt *S, const clang::FunctionDecl *CallerFD);
    void verifyMultiFrameErrorReturns();

    // -----------------------------------------------------------------------
    // ContainerChecks.cpp
    // -----------------------------------------------------------------------
    bool extractVVectorCapacityFromRecord(const clang::RecordDecl *RD, uint32_t &OutCap) const;

    // -----------------------------------------------------------------------
    // SmtContractVerifier.cpp
    // -----------------------------------------------------------------------
    static bool hasContractAnnotations(const clang::FunctionDecl *FD);
    void verifyFunctionContractWithZ3(const clang::FunctionDecl *FD);
    void verifyAllContractedFunctions();
    const clang::FieldDecl *findNonFlatField(clang::QualType T) const;
    void checkFlatPayloadType(clang::QualType T, llvm::StringRef TypeName, clang::SourceLocation Loc);
    static const clang::VarDecl *extractVResultIsOkVar(const clang::Expr *Cond);
    static bool containsVAsyncReadyCall(const clang::Stmt *S);
    static bool isRawSimdIntrinsicName(llvm::StringRef Name);
    void analyzeVResultAndStackArrays(
        const clang::Stmt *S,
        llvm::StringRef EnclosingFnName,
        bool IsNoLatencyFn,
        llvm::DenseSet<const clang::VarDecl *> &ProvenOkVars,
        llvm::DenseMap<const clang::VarDecl *, const clang::VarDecl *> &ActiveStackArray,
        llvm::DenseMap<const clang::VarDecl *, uint32_t> &VVectorPushCounts,
        llvm::DenseMap<const clang::VarDecl *, uint32_t> &VHashInsertCounts,
        llvm::DenseMap<const clang::VarDecl *, uint32_t> &VDmaRingCounts
    );
    void checkInterfaceByValueRecord(const clang::RecordDecl *D, clang::SourceLocation Loc);

    // -----------------------------------------------------------------------
    // StaticDispatchLowering.cpp
    // -----------------------------------------------------------------------
    void checkLightweightGenerics(clang::QualType T, clang::SourceLocation Loc);
    static bool hasValidSealedGenericBound(const clang::ObjCTypeParamDecl *TPD);
    static std::string mangleSelectorForSymbol(clang::Selector Sel);
    void markHeaderDirectHelperIfPresent(const clang::ObjCInterfaceDecl *ID, clang::Selector Sel);
    void collectBodyMessageSendsPostOrder(const clang::Stmt *S, llvm::StringRef IRFuncName);
    void remapMethodVarRefs(clang::Stmt *S, const llvm::DenseMap<const clang::ValueDecl *, clang::ValueDecl *> &DeclMap);
    clang::ObjCIvarDecl *findBoundedArrayIvar(clang::Expr *Coll, clang::VarDecl *ElemVD, const clang::ConstantArrayType *&OutCAT);
    clang::Stmt *buildLoweredForCollection(clang::ObjCForCollectionStmt *OFS, clang::DeclContext *DC);
    clang::FunctionDecl *synthesizeMonomorphizedBlock(clang::BlockExpr *BE, llvm::SmallVectorImpl<clang::VarDecl *> &OutCaptures);
    clang::Expr *buildMonomorphizedBlockCall(clang::CallExpr *CE, clang::FunctionDecl *BlockFD, llvm::ArrayRef<clang::VarDecl *> Captures);
    void rewriteDeclStmtBlocks(clang::DeclStmt *DS);
    clang::Expr *tryRewriteCallExpr(clang::CallExpr *CE);
    void rewriteStmtTree(clang::Stmt *S, clang::DeclContext *DC);
    bool analyzeAndRewriteBody(clang::Stmt *Body, clang::DeclContext *DC);

public:
    explicit VeriObjCVisitor(clang::ASTContext *Context, VeriObjCPluginOptions Opts = {})
        : Context(Context), Opts(Opts) {}

    std::vector<clang::FunctionDecl *> &getSynthesizedDefs() { return SynthesizedDefs; }
    std::vector<clang::Decl *> &getSynthesizedVarDefs() { return SynthesizedVarDefs; }

    void finalizeTranslationUnitAudits();
    void lowerTopLevelDecl(clang::Decl *D);

    bool VisitVarDecl(clang::VarDecl *D);
    bool VisitSwitchStmt(clang::SwitchStmt *SS);
    bool VisitParmVarDecl(clang::ParmVarDecl *D);
    bool VisitFunctionDecl(clang::FunctionDecl *D);
    bool VisitObjCCategoryDecl(clang::ObjCCategoryDecl *D);
    bool VisitObjCPropertyImplDecl(clang::ObjCPropertyImplDecl *D);
    bool VisitObjCAtTryStmt(clang::ObjCAtTryStmt *S);
    bool VisitStmtExpr(clang::StmtExpr *E);
    bool VisitObjCMethodDecl(clang::ObjCMethodDecl *D);
    bool VisitFieldDecl(clang::FieldDecl *D);
    bool VisitReturnStmt(clang::ReturnStmt *S);
    bool VisitBinaryOperator(clang::BinaryOperator *BO);
    bool VisitTypedefNameDecl(clang::TypedefNameDecl *D);
    bool VisitRecordDecl(clang::RecordDecl *D);
    bool VisitObjCInterfaceDecl(clang::ObjCInterfaceDecl *D);
    bool VisitCallExpr(clang::CallExpr *E);
    bool VisitObjCMessageExpr(clang::ObjCMessageExpr *E);
};

} // namespace veriobjc
