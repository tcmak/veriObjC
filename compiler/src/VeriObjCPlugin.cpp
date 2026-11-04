#include <clang/AST/ASTConsumer.h>
#include <clang/AST/ASTContext.h>
#include <clang/AST/Attr.h>
#include <clang/AST/Decl.h>
#include <clang/AST/DeclGroup.h>
#include <clang/AST/DeclObjC.h>
#include <clang/AST/Expr.h>
#include <clang/AST/ExprObjC.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/AST/Stmt.h>
#include <clang/Basic/ParsedAttrInfo.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendPluginRegistry.h>
#include <clang/Sema/ParsedAttr.h>
#include <clang/Sema/Sema.h>
#include <llvm/ADT/DenseMap.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/ADT/StringMap.h>
#include <llvm/ADT/StringRef.h>
#include <string>
#include <vector>

using namespace clang;

namespace {

class VeriObjCVisitor : public RecursiveASTVisitor<VeriObjCVisitor> {
private:
    ASTContext *Context;
    llvm::StringMap<FunctionDecl *> DirectMethodDecls;
    std::vector<FunctionDecl *> SynthesizedDefs;

    void reportBannedId(SourceLocation Loc) {
        DiagnosticsEngine &Diag = Context->getDiagnostics();
        unsigned DiagID = Diag.getCustomDiagID(
            DiagnosticsEngine::Error,
            "VeriObjC Violation: Banned use of generic 'id' type. Use strict nominal object typing."
        );
        Diag.Report(Loc, DiagID);
    }

    static std::string mangleDirectMethodName(const ObjCInterfaceDecl *ID, Selector Sel) {
        std::string Name = "__veriobjc_direct_";
        Name += ID->getNameAsString();
        Name += "__";
        std::string SelStr = Sel.getAsString();
        for (char &c : SelStr) {
            if (c == ':') {
                c = '_';
            }
        }
        Name += SelStr;
        return Name;
    }

    FunctionDecl *getOrCreateDirectMethodDecl(ObjCInterfaceDecl *ID, ObjCMethodDecl *MD) {
        std::string Mangled = mangleDirectMethodName(ID, MD->getSelector());
        auto It = DirectMethodDecls.find(Mangled);
        if (It != DirectMethodDecls.end()) {
            return It->second;
        }

        QualType RetTy = MD->getReturnType().getNonReferenceType();
        QualType SelfTy = Context->getObjCObjectPointerType(Context->getObjCInterfaceType(ID));

        llvm::SmallVector<QualType, 8> ParamTypes;
        ParamTypes.push_back(SelfTy);
        for (const ParmVarDecl *PVD : MD->parameters()) {
            ParamTypes.push_back(PVD->getType());
        }

        FunctionProtoType::ExtProtoInfo EPI;
        QualType FnTy = Context->getFunctionType(RetTy, ParamTypes, EPI);
        IdentifierInfo &II = Context->Idents.get(Mangled);
        DeclContext *TU = Context->getTranslationUnitDecl();

        FunctionDecl *FD = FunctionDecl::Create(
            *Context, TU, MD->getLocation(), MD->getLocation(),
            &II, FnTy, nullptr, SC_None
        );

        llvm::SmallVector<ParmVarDecl *, 8> Params;
        IdentifierInfo &SelfII = Context->Idents.get("self");
        ParmVarDecl *SelfParam = ParmVarDecl::Create(
            *Context, FD, MD->getLocation(), MD->getLocation(),
            &SelfII, SelfTy, nullptr, SC_None, nullptr
        );
        Params.push_back(SelfParam);

        for (ParmVarDecl *OrigPVD : MD->parameters()) {
            ParmVarDecl *ClonedPVD = ParmVarDecl::Create(
                *Context, FD, OrigPVD->getBeginLoc(), OrigPVD->getLocation(),
                OrigPVD->getIdentifier(), OrigPVD->getType(),
                OrigPVD->getTypeSourceInfo(), SC_None, nullptr
            );
            Params.push_back(ClonedPVD);
        }
        FD->setParams(Params);
        TU->addDecl(FD);

        DirectMethodDecls[Mangled] = FD;
        return FD;
    }

    void remapMethodVarRefs(Stmt *S, const llvm::DenseMap<const ValueDecl *, ValueDecl *> &DeclMap) {
        if (!S) return;
        for (Stmt *Child : S->children()) {
            remapMethodVarRefs(Child, DeclMap);
        }
        if (auto *DRE = dyn_cast<DeclRefExpr>(S)) {
            auto It = DeclMap.find(DRE->getDecl());
            if (It != DeclMap.end()) {
                DRE->setDecl(It->second);
            }
        }
    }

    Expr *buildDevirtualizedCall(ObjCMessageExpr *E) {
        ObjCInterfaceDecl *ReceiverClass = E->getReceiverInterface();
        ObjCMethodDecl *MD = E->getMethodDecl();
        if (!ReceiverClass || !MD || !E->isInstanceMessage()) {
            return nullptr;
        }
        Expr *RecvExpr = E->getInstanceReceiver();
        if (!RecvExpr) {
            return nullptr;
        }

        ObjCInterfaceDecl *TargetClass = MD->getClassInterface();
        if (!TargetClass) {
            TargetClass = ReceiverClass;
        }

        FunctionDecl *DirectFD = getOrCreateDirectMethodDecl(TargetClass, MD);
        QualType RetTy = DirectFD->getReturnType();

        DeclRefExpr *CalleeRef = DeclRefExpr::Create(
            *Context, NestedNameSpecifierLoc(), SourceLocation(),
            DirectFD, false, E->getExprLoc(), DirectFD->getType(), VK_LValue
        );
        QualType FnPtrTy = Context->getPointerType(DirectFD->getType());
        ImplicitCastExpr *CalleeCast = ImplicitCastExpr::Create(
            *Context, FnPtrTy, CK_FunctionToPointerDecay, CalleeRef,
            nullptr, VK_PRValue, FPOptionsOverride()
        );

        llvm::SmallVector<Expr *, 8> CallArgs;
        QualType ExpectedSelfTy = DirectFD->getParamDecl(0)->getType();
        if (RecvExpr->getType().getCanonicalType() != ExpectedSelfTy.getCanonicalType()) {
            RecvExpr = ImplicitCastExpr::Create(
                *Context, ExpectedSelfTy, CK_BitCast, RecvExpr,
                nullptr, VK_PRValue, FPOptionsOverride()
            );
        }
        CallArgs.push_back(RecvExpr);

        for (unsigned i = 0; i < E->getNumArgs(); ++i) {
            CallArgs.push_back(E->getArg(i));
        }

        return CallExpr::Create(
            *Context, CalleeCast, CallArgs, RetTy, VK_PRValue,
            E->getRightLoc(), FPOptionsOverride()
        );
    }

    void rewriteStmtTree(Stmt *S) {
        if (!S) return;
        for (Stmt *&Child : S->children()) {
            if (!Child) continue;
            rewriteStmtTree(Child);
            if (auto *OME = dyn_cast<ObjCMessageExpr>(Child)) {
                if (Expr *DirectCall = buildDevirtualizedCall(OME)) {
                    Child = DirectCall;
                }
            }
        }
    }

public:
    explicit VeriObjCVisitor(ASTContext *Context) : Context(Context) {}

    std::vector<FunctionDecl *> &getSynthesizedDefs() { return SynthesizedDefs; }

    // RULE 1: Intercept and BAN the generic 'id' keyword across all declarations
    bool VisitVarDecl(VarDecl *D) {
        QualType T = D->getType();
        if (T->isObjCIdType()) {
            reportBannedId(D->getLocation());
        }
        return true;
    }

    bool VisitParmVarDecl(ParmVarDecl *D) {
        QualType T = D->getType();
        if (T->isObjCIdType()) {
            reportBannedId(D->getLocation());
        }
        return true;
    }

    bool VisitFieldDecl(FieldDecl *D) {
        QualType T = D->getType();
        if (T->isObjCIdType()) {
            reportBannedId(D->getLocation());
        }
        return true;
    }

    bool VisitTypedefNameDecl(TypedefNameDecl *D) {
        QualType T = D->getUnderlyingType();
        if (T->isObjCIdType()) {
            reportBannedId(D->getLocation());
        }
        return true;
    }

    static bool hasVeriObjCAnnotation(const Decl *D, llvm::StringRef Tag) {
        if (!D) return false;
        for (const auto *A : D->specific_attrs<AnnotateAttr>()) {
            if (A->getAnnotation() == Tag) {
                return true;
            }
        }
        return false;
    }

    static bool isSealedClass(const ObjCInterfaceDecl *D) {
        return hasVeriObjCAnnotation(D, "objc_sealed");
    }

    static bool isFinalClass(const ObjCInterfaceDecl *D) {
        return hasVeriObjCAnnotation(D, "objc_final") ||
               (D && D->hasAttr<ObjCSubclassingRestrictedAttr>());
    }

    // RULE 2: Ban inheritance from NSObject and enforce sealed/final class hierarchies
    bool VisitObjCInterfaceDecl(ObjCInterfaceDecl *D) {
        for (const ObjCInterfaceDecl *Super = D->getSuperClass(); Super; Super = Super->getSuperClass()) {
            if (Super->getName() == "NSObject") {
                DiagnosticsEngine &Diag = Context->getDiagnostics();
                unsigned DiagID = Diag.getCustomDiagID(
                    DiagnosticsEngine::Error,
                    "VeriObjC Violation: Banned inheritance from 'NSObject'. VeriObjC classes are freestanding and must not inherit from the dynamic Apple runtime."
                );
                Diag.Report(D->getLocation(), DiagID);
                return true;
            }
        }

        if (const ObjCInterfaceDecl *Super = D->getSuperClass()) {
            DiagnosticsEngine &Diag = Context->getDiagnostics();
            if (isFinalClass(Super)) {
                unsigned DiagID = Diag.getCustomDiagID(
                    DiagnosticsEngine::Error,
                    "VeriObjC Violation: Cannot inherit from final class '%0' marked '__attribute__((objc_final))'."
                );
                Diag.Report(D->getLocation(), DiagID) << Super->getName();
                return true;
            }
            if (!isSealedClass(Super)) {
                unsigned DiagID = Diag.getCustomDiagID(
                    DiagnosticsEngine::Error,
                    "VeriObjC Violation: Polymorphic class '%0' in an inheritance hierarchy must be marked '__attribute__((objc_sealed))'."
                );
                Diag.Report(Super->getLocation(), DiagID) << Super->getName();
            }
            if (!isSealedClass(D) && !isFinalClass(D)) {
                unsigned DiagID = Diag.getCustomDiagID(
                    DiagnosticsEngine::Error,
                    "VeriObjC Violation: Polymorphic class '%0' in an inheritance hierarchy must be marked '__attribute__((objc_sealed))'."
                );
                Diag.Report(D->getLocation(), DiagID) << D->getName();
            }
        }

        for (const ObjCProtocolDecl *Proto : D->protocols()) {
            if (!hasVeriObjCAnnotation(Proto, "objc_sealed")) {
                DiagnosticsEngine &Diag = Context->getDiagnostics();
                unsigned DiagID = Diag.getCustomDiagID(
                    DiagnosticsEngine::Error,
                    "VeriObjC Violation: Protocol '%0' must be marked '__attribute__((objc_sealed))' for closed-world dispatch."
                );
                Diag.Report(Proto->getLocation(), DiagID) << Proto->getName();
            }
        }
        return true;
    }

    // RULE 3: Ban calls to dynamic runtime reflection and swizzling functions
    bool VisitCallExpr(CallExpr *E) {
        const FunctionDecl *FD = E->getDirectCallee();
        if (!FD) return true;

        llvm::StringRef Name = FD->getName();
        if (Name == "NSClassFromString" ||
            Name == "class_copyMethodList" ||
            Name == "objc_getClass" ||
            Name == "objc_msgSend" ||
            Name == "method_exchangeImplementations") {
            DiagnosticsEngine &Diag = Context->getDiagnostics();
            unsigned DiagID = Diag.getCustomDiagID(
                DiagnosticsEngine::Error,
                "VeriObjC Violation: Banned use of dynamic runtime reflection or swizzling function '%0'."
            );
            Diag.Report(E->getExprLoc(), DiagID) << Name;
        }
        return true;
    }

    // RULE 4: Dynamic message dispatch requires an explicitly typed receiver and resolved method
    bool VisitObjCMessageExpr(ObjCMessageExpr *E) {
        ObjCInterfaceDecl *ReceiverClass = E->getReceiverInterface();
        if (!ReceiverClass) {
            DiagnosticsEngine &Diag = Context->getDiagnostics();
            unsigned DiagID = Diag.getCustomDiagID(
                DiagnosticsEngine::Error,
                "VeriObjC Violation: Dynamic message dispatch requires an explicitly typed receiver."
            );
            Diag.Report(E->getExprLoc(), DiagID);
            return true;
        }

        if (!E->getMethodDecl()) {
            DiagnosticsEngine &Diag = Context->getDiagnostics();
            unsigned DiagID = Diag.getCustomDiagID(
                DiagnosticsEngine::Error,
                "VeriObjC Violation: Unresolved selector '%0' on class '%1'. Closed-world dispatch requires all methods to be statically declared."
            );
            Diag.Report(E->getSelectorStartLoc(), DiagID)
                << E->getSelector().getAsString() << ReceiverClass->getName();
            return true;
        }
        return true;
    }

    void lowerTopLevelDecl(Decl *D) {
        if (!D || Context->getDiagnostics().hasErrorOccurred()) {
            return;
        }

        if (auto *FD = dyn_cast<FunctionDecl>(D)) {
            if (FD->hasBody()) {
                rewriteStmtTree(FD->getBody());
            }
            return;
        }

        if (auto *ImplD = dyn_cast<ObjCImplementationDecl>(D)) {
            ObjCInterfaceDecl *ID = ImplD->getClassInterface();
            if (!ID) return;

            for (ObjCMethodDecl *MD : ImplD->instance_methods()) {
                if (!MD->hasBody()) continue;

                Stmt *OrigBody = MD->getBody();
                rewriteStmtTree(OrigBody);

                FunctionDecl *DirectFD = getOrCreateDirectMethodDecl(ID, MD);
                llvm::DenseMap<const ValueDecl *, ValueDecl *> DeclMap;
                if (MD->getSelfDecl()) {
                    DeclMap[MD->getSelfDecl()] = DirectFD->getParamDecl(0);
                }
                for (unsigned i = 0; i < MD->param_size(); ++i) {
                    DeclMap[MD->getParamDecl(i)] = DirectFD->getParamDecl(i + 1);
                }

                remapMethodVarRefs(OrigBody, DeclMap);
                DirectFD->setBody(OrigBody);
                SynthesizedDefs.push_back(DirectFD);

                // Replace ObjCMethodDecl body with a forwarding call to DirectFD
                DeclRefExpr *CalleeRef = DeclRefExpr::Create(
                    *Context, NestedNameSpecifierLoc(), SourceLocation(),
                    DirectFD, false, MD->getLocation(), DirectFD->getType(), VK_LValue
                );
                ImplicitCastExpr *CalleeCast = ImplicitCastExpr::Create(
                    *Context, Context->getPointerType(DirectFD->getType()),
                    CK_FunctionToPointerDecay, CalleeRef, nullptr, VK_PRValue, FPOptionsOverride()
                );

                llvm::SmallVector<Expr *, 8> FwdArgs;
                ImplicitParamDecl *SelfDecl = MD->getSelfDecl();
                DeclRefExpr *SelfRef = DeclRefExpr::Create(
                    *Context, NestedNameSpecifierLoc(), SourceLocation(),
                    SelfDecl, false, MD->getLocation(), SelfDecl->getType(), VK_LValue
                );
                ImplicitCastExpr *SelfRVal = ImplicitCastExpr::Create(
                    *Context, SelfDecl->getType(), CK_LValueToRValue,
                    SelfRef, nullptr, VK_PRValue, FPOptionsOverride()
                );
                FwdArgs.push_back(SelfRVal);

                for (ParmVarDecl *PVD : MD->parameters()) {
                    DeclRefExpr *PRef = DeclRefExpr::Create(
                        *Context, NestedNameSpecifierLoc(), SourceLocation(),
                        PVD, false, PVD->getLocation(), PVD->getType(), VK_LValue
                    );
                    ImplicitCastExpr *PRVal = ImplicitCastExpr::Create(
                        *Context, PVD->getType(), CK_LValueToRValue,
                        PRef, nullptr, VK_PRValue, FPOptionsOverride()
                    );
                    FwdArgs.push_back(PRVal);
                }

                CallExpr *FwdCall = CallExpr::Create(
                    *Context, CalleeCast, FwdArgs, DirectFD->getReturnType(),
                    VK_PRValue, MD->getLocation(), FPOptionsOverride()
                );

                Stmt *NewMethodStmt = FwdCall;
                if (!DirectFD->getReturnType()->isVoidType()) {
                    NewMethodStmt = ReturnStmt::Create(*Context, MD->getLocation(), FwdCall, nullptr);
                }
                Stmt *Stmts[1] = { NewMethodStmt };
                MD->setBody(CompoundStmt::Create(*Context, Stmts, FPOptionsOverride(), MD->getBeginLoc(), MD->getEndLoc()));
            }
        }
    }
};

class VeriObjCConsumer : public ASTConsumer {
private:
    CompilerInstance &CI;
    VeriObjCVisitor Visitor;
    bool EmittingSynthesized = false;

public:
    explicit VeriObjCConsumer(CompilerInstance &CI)
        : CI(CI), Visitor(&CI.getASTContext()) {}

    bool HandleTopLevelDecl(DeclGroupRef DG) override {
        if (EmittingSynthesized) {
            return true;
        }
        for (Decl *D : DG) {
            Visitor.TraverseDecl(D);
            Visitor.lowerTopLevelDecl(D);
        }
        return true;
    }

    void HandleTranslationUnit(ASTContext &Context) override {
        if (Context.getDiagnostics().hasErrorOccurred()) {
            return;
        }
        EmittingSynthesized = true;
        for (FunctionDecl *FD : Visitor.getSynthesizedDefs()) {
            CI.getASTConsumer().HandleTopLevelDecl(DeclGroupRef(FD));
        }
        EmittingSynthesized = false;
    }
};

class VeriObjCAction : public PluginASTAction {
protected:
    std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI, llvm::StringRef) override {
        return std::make_unique<VeriObjCConsumer>(CI);
    }

    bool ParseArgs(const CompilerInstance &CI, const std::vector<std::string> &args) override {
        return true;
    }

    PluginASTAction::ActionType getActionType() override {
        return AddBeforeMainAction;
    }
};

struct ObjCSealedAttrInfo : public ParsedAttrInfo {
    ObjCSealedAttrInfo() {
        OptArgs = 0;
        static constexpr Spelling S[] = {
            {ParsedAttr::AS_GNU, "objc_sealed"},
            {ParsedAttr::AS_C23, "objc_sealed"},
            {ParsedAttr::AS_CXX11, "objc_sealed"}
        };
        Spellings = S;
    }

    AttrHandling handleDeclAttribute(Sema &S, Decl *D, const ParsedAttr &Attr) const override {
        D->addAttr(AnnotateAttr::Create(S.Context, "objc_sealed", nullptr, 0, Attr.getRange()));
        return AttributeApplied;
    }
};

struct ObjCFinalAttrInfo : public ParsedAttrInfo {
    ObjCFinalAttrInfo() {
        OptArgs = 0;
        static constexpr Spelling S[] = {
            {ParsedAttr::AS_GNU, "objc_final"},
            {ParsedAttr::AS_C23, "objc_final"},
            {ParsedAttr::AS_CXX11, "objc_final"}
        };
        Spellings = S;
    }

    AttrHandling handleDeclAttribute(Sema &S, Decl *D, const ParsedAttr &Attr) const override {
        D->addAttr(AnnotateAttr::Create(S.Context, "objc_final", nullptr, 0, Attr.getRange()));
        return AttributeApplied;
    }
};

} // namespace

// Register custom attributes and AST plugin with Clang's registries
static ParsedAttrInfoRegistry::Add<ObjCSealedAttrInfo> AttrSealed(
    "objc_sealed", "Mark VeriObjC class or protocol hierarchy as closed-world sealed"
);
static ParsedAttrInfoRegistry::Add<ObjCFinalAttrInfo> AttrFinal(
    "objc_final", "Mark VeriObjC class as final (disallow subclassing)"
);
static FrontendPluginRegistry::Add<VeriObjCAction> X(
    "veriobjc-check",
    "Enforce VeriObjC structural invariants and eliminate non-deterministic features"
);
