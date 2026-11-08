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
#include <clang/AST/StmtObjC.h>
#include <clang/Basic/DiagnosticSema.h>
#include <clang/Basic/ParsedAttrInfo.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendPluginRegistry.h>
#include <clang/Sema/ParsedAttr.h>
#include <clang/Sema/Sema.h>
#include <llvm/ADT/DenseMap.h>
#include <llvm/ADT/DenseSet.h>
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

    static const VarDecl *extractVarDecl(const Expr *E) {
        if (!E) return nullptr;
        E = E->IgnoreParenCasts();
        if (const auto *UO = dyn_cast<UnaryOperator>(E)) {
            if (UO->getOpcode() == UO_AddrOf) {
                E = UO->getSubExpr()->IgnoreParenCasts();
            }
        }
        if (const auto *DRE = dyn_cast<DeclRefExpr>(E)) {
            return dyn_cast<VarDecl>(DRE->getDecl());
        }
        return nullptr;
    }

    void reportConsumedVar(SourceLocation Loc, llvm::StringRef VarName) {
        DiagnosticsEngine &Diag = Context->getDiagnostics();
        unsigned DiagID = Diag.getCustomDiagID(
            DiagnosticsEngine::Error,
            "VeriObjC Violation: Use of consumed linear variable '%0' after move or free."
        );
        Diag.Report(Loc, DiagID) << VarName;
    }

    void analyzeLinearOwnership(const Stmt *S, llvm::DenseSet<const VarDecl *> &MovedVars, llvm::DenseSet<const VarDecl *> &FreedVars) {
        if (!S) return;

        if (const auto *BO = dyn_cast<BinaryOperator>(S)) {
            if (BO->getOpcode() == BO_Comma) {
                const Expr *LHS = BO->getLHS()->IgnoreParenCasts();
                if (const auto *CE = dyn_cast<CallExpr>(LHS)) {
                    if (const FunctionDecl *FD = CE->getDirectCallee()) {
                        if (FD->getName() == "__veriobjc_mark_moved" && CE->getNumArgs() == 1) {
                            if (const VarDecl *VD = extractVarDecl(CE->getArg(0))) {
                                if (MovedVars.contains(VD) || FreedVars.contains(VD)) {
                                    reportConsumedVar(BO->getExprLoc(), VD->getName());
                                } else {
                                    MovedVars.insert(VD);
                                }
                                return;
                            }
                        }
                    }
                }
            } else if (BO->getOpcode() == BO_Assign) {
                analyzeLinearOwnership(BO->getRHS(), MovedVars, FreedVars);
                const Expr *LHS = BO->getLHS()->IgnoreParenCasts();
                if (const auto *DRE = dyn_cast<DeclRefExpr>(LHS)) {
                    if (const auto *VD = dyn_cast<VarDecl>(DRE->getDecl())) {
                        MovedVars.erase(VD);
                        FreedVars.erase(VD);
                        return;
                    }
                }
                analyzeLinearOwnership(BO->getLHS(), MovedVars, FreedVars);
                return;
            }
        }

        if (const auto *CE = dyn_cast<CallExpr>(S)) {
            if (const FunctionDecl *FD = CE->getDirectCallee()) {
                llvm::StringRef FnName = FD->getName();
                if ((FnName == "veriobjc_pool_free" || FnName == "veriobjc_gen_pool_free") && CE->getNumArgs() >= 2) {
                    analyzeLinearOwnership(CE->getArg(0), MovedVars, FreedVars);
                    if (const VarDecl *VD = extractVarDecl(CE->getArg(1))) {
                        if (MovedVars.contains(VD) || FreedVars.contains(VD)) {
                            reportConsumedVar(CE->getArg(1)->getExprLoc(), VD->getName());
                        } else {
                            FreedVars.insert(VD);
                        }
                        return;
                    }
                }
                for (unsigned i = 0; i < CE->getNumArgs(); ++i) {
                    const Expr *Arg = CE->getArg(i);
                    analyzeLinearOwnership(Arg, MovedVars, FreedVars);
                    if (i < FD->getNumParams() && hasVeriObjCAnnotation(FD->getParamDecl(i), "objc_consumed")) {
                        if (const VarDecl *VD = extractVarDecl(Arg)) {
                            MovedVars.insert(VD);
                        }
                    }
                }
                return;
            }
        }

        if (const auto *DRE = dyn_cast<DeclRefExpr>(S)) {
            if (const auto *VD = dyn_cast<VarDecl>(DRE->getDecl())) {
                if (MovedVars.contains(VD)) {
                    reportConsumedVar(DRE->getLocation(), VD->getName());
                }
            }
            return;
        }

        for (const Stmt *Child : S->children()) {
            analyzeLinearOwnership(Child, MovedVars, FreedVars);
        }
    }

    static bool isStackAllocExpr(const Expr *E) {
        if (!E) return false;
        E = E->IgnoreParenCasts();
        if (const auto *CE = dyn_cast<CallExpr>(E)) {
            if (const FunctionDecl *FD = CE->getDirectCallee()) {
                llvm::StringRef Name = FD->getName();
                return Name == "veriobjc_stack_alloc" || Name == "veriobjc_alloc_init";
            }
        }
        return false;
    }

    void analyzeScopeRootSafety(
        const Stmt *S,
        unsigned CurrentDepth,
        llvm::DenseMap<const VarDecl *, unsigned> &VarDeclDepth,
        llvm::DenseMap<const VarDecl *, unsigned> &StackRootDepth
    ) {
        if (!S) return;
        if (const auto *CS = dyn_cast<CompoundStmt>(S)) {
            unsigned NextDepth = CurrentDepth + 1;
            for (const Stmt *Child : CS->body()) {
                analyzeScopeRootSafety(Child, NextDepth, VarDeclDepth, StackRootDepth);
            }
            return;
        }

        if (const auto *DS = dyn_cast<DeclStmt>(S)) {
            for (const Decl *D : DS->decls()) {
                if (const auto *VD = dyn_cast<VarDecl>(D)) {
                    VarDeclDepth[VD] = CurrentDepth;
                    if (const Expr *Init = VD->getInit()) {
                        if (isStackAllocExpr(Init)) {
                            StackRootDepth[VD] = CurrentDepth;
                        } else if (const VarDecl *SrcVD = extractVarDecl(Init)) {
                            auto It = StackRootDepth.find(SrcVD);
                            if (It != StackRootDepth.end()) {
                                StackRootDepth[VD] = It->second;
                            }
                        }
                    }
                }
            }
        }

        if (const auto *BO = dyn_cast<BinaryOperator>(S)) {
            if (BO->getOpcode() == BO_Assign) {
                if (const VarDecl *DstVD = extractVarDecl(BO->getLHS())) {
                    unsigned DstDepth = VarDeclDepth.lookup(DstVD);
                    unsigned SrcRootDepth = 0;
                    llvm::StringRef SrcName = "veriobjc_stack_alloc";

                    if (isStackAllocExpr(BO->getRHS())) {
                        SrcRootDepth = CurrentDepth;
                    } else if (const VarDecl *SrcVD = extractVarDecl(BO->getRHS())) {
                        auto It = StackRootDepth.find(SrcVD);
                        if (It != StackRootDepth.end()) {
                            SrcRootDepth = It->second;
                            SrcName = SrcVD->getName();
                        }
                    }

                    if (SrcRootDepth > 0 && DstDepth > 0 && DstDepth < SrcRootDepth) {
                        DiagnosticsEngine &Diag = Context->getDiagnostics();
                        unsigned DiagID = Diag.getCustomDiagID(
                            DiagnosticsEngine::Error,
                            "VeriObjC Violation: Escaping data-stack reference '%0' assigned to outer destination '%1' that outlives its root scope."
                        );
                        Diag.Report(BO->getExprLoc(), DiagID) << SrcName << DstVD->getName();
                    }
                }
            }
        }

        for (const Stmt *Child : S->children()) {
            analyzeScopeRootSafety(Child, CurrentDepth, VarDeclDepth, StackRootDepth);
        }
    }

    const FieldDecl *findNonFlatField(QualType T) const {
        if (T.isNull()) return nullptr;
        while (const auto *AT = Context->getAsArrayType(T)) {
            T = AT->getElementType();
        }
        if (const auto *RT = T->getAs<RecordType>()) {
            const RecordDecl *RD = RT->getDecl()->getDefinition();
            if (!RD) return nullptr;
            for (const FieldDecl *FD : RD->fields()) {
                QualType FT = FD->getType();
                while (const auto *FAT = Context->getAsArrayType(FT)) {
                    FT = FAT->getElementType();
                }
                if (FT->isPointerType() || FT->isObjCObjectPointerType() ||
                    FT->isBlockPointerType() || FT->isReferenceType()) {
                    return FD;
                }
                if (const FieldDecl *NestedBad = findNonFlatField(FT)) {
                    return NestedBad;
                }
            }
        }
        return nullptr;
    }

    void checkFlatPayloadType(QualType T, llvm::StringRef TypeName, SourceLocation Loc) {
        if (const FieldDecl *BadFD = findNonFlatField(T)) {
            DiagnosticsEngine &Diag = Context->getDiagnostics();
            unsigned DiagID = Diag.getCustomDiagID(
                DiagnosticsEngine::Error,
                "VeriObjC Violation: Non-flat channel payload type '%0' contains pointer or reference field '%1'. Cross-core channels require flat values."
            );
            Diag.Report(Loc, DiagID) << TypeName << BadFD->getName();
        }
    }


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

    Stmt *buildLoweredForCollection(ObjCForCollectionStmt *OFS, DeclContext *DC) {
        if (!OFS || !DC) return nullptr;

        Expr *Coll = OFS->getCollection() ? OFS->getCollection()->IgnoreParenImpCasts() : nullptr;
        auto *ElemDS = dyn_cast_or_null<DeclStmt>(OFS->getElement());
        VarDecl *ElemVD = (ElemDS && ElemDS->isSingleDecl()) ? dyn_cast<VarDecl>(ElemDS->getSingleDecl()) : nullptr;

        ObjCIvarDecl *FoundIvar = nullptr;
        const ConstantArrayType *CAT = nullptr;

        if (Coll && ElemVD) {
            if (const auto *OPT = Coll->getType()->getAs<ObjCObjectPointerType>()) {
                if (ObjCInterfaceDecl *ID = OPT->getInterfaceDecl()) {
                    for (const ObjCInterfaceDecl *CurID = ID; CurID && !FoundIvar; CurID = CurID->getSuperClass()) {
                        for (ObjCIvarDecl *Ivar : CurID->ivars()) {
                            if (const ConstantArrayType *CandCAT = Context->getAsConstantArrayType(Ivar->getType())) {
                                if (CandCAT->getElementType().getCanonicalType() == ElemVD->getType().getCanonicalType()) {
                                    FoundIvar = Ivar;
                                    CAT = CandCAT;
                                    break;
                                }
                            }
                        }
                    }
                }
            }
        }

        if (!FoundIvar || !CAT || !ElemVD) {
            DiagnosticsEngine &Diag = Context->getDiagnostics();
            unsigned DiagID = Diag.getCustomDiagID(
                DiagnosticsEngine::Error,
                "VeriObjC Violation: Range-based for loop requires a container with a statically bounded array field."
            );
            Diag.Report(OFS->getForLoc(), DiagID);
            return nullptr;
        }

        rewriteStmtTree(OFS->getBody(), DC);

        SourceLocation Loc = OFS->getForLoc();
        QualType ElemTy = CAT->getElementType();
        QualType PtrTy = Context->getPointerType(ElemTy);

        ObjCIvarRefExpr *IvarRef = new (*Context) ObjCIvarRefExpr(
            FoundIvar, FoundIvar->getType(), Loc, Loc, Coll, true, false
        );
        ImplicitCastExpr *BeginPtr = ImplicitCastExpr::Create(
            *Context, PtrTy, CK_ArrayToPointerDecay, IvarRef,
            nullptr, VK_PRValue, FPOptionsOverride()
        );

        VarDecl *IterVD = VarDecl::Create(
            *Context, DC, Loc, Loc, &Context->Idents.get("__veriobjc_it"),
            PtrTy, nullptr, SC_None
        );
        IterVD->setInit(BeginPtr);

        DeclRefExpr *IterRefForEnd = DeclRefExpr::Create(
            *Context, NestedNameSpecifierLoc(), SourceLocation(),
            IterVD, false, Loc, PtrTy, VK_LValue
        );
        ImplicitCastExpr *IterRValForEnd = ImplicitCastExpr::Create(
            *Context, PtrTy, CK_LValueToRValue, IterRefForEnd,
            nullptr, VK_PRValue, FPOptionsOverride()
        );

        QualType SizeTy = Context->getSizeType();
        llvm::APInt BoundVal(Context->getTypeSize(SizeTy), CAT->getSize().getZExtValue());
        IntegerLiteral *BoundLit = IntegerLiteral::Create(*Context, BoundVal, SizeTy, Loc);

        BinaryOperator *EndPtr = BinaryOperator::Create(
            *Context, IterRValForEnd, BoundLit, BO_Add, PtrTy,
            VK_PRValue, OK_Ordinary, Loc, FPOptionsOverride()
        );

        VarDecl *EndVD = VarDecl::Create(
            *Context, DC, Loc, Loc, &Context->Idents.get("__veriobjc_end"),
            PtrTy, nullptr, SC_None
        );
        EndVD->setInit(EndPtr);

        Decl *InitDecls[2] = { IterVD, EndVD };
        DeclStmt *InitStmt = new (*Context) DeclStmt(
            DeclGroupRef::Create(*Context, InitDecls, 2), Loc, Loc
        );

        DeclRefExpr *IterRefCond = DeclRefExpr::Create(
            *Context, NestedNameSpecifierLoc(), SourceLocation(),
            IterVD, false, Loc, PtrTy, VK_LValue
        );
        ImplicitCastExpr *IterRValCond = ImplicitCastExpr::Create(
            *Context, PtrTy, CK_LValueToRValue, IterRefCond,
            nullptr, VK_PRValue, FPOptionsOverride()
        );
        DeclRefExpr *EndRefCond = DeclRefExpr::Create(
            *Context, NestedNameSpecifierLoc(), SourceLocation(),
            EndVD, false, Loc, PtrTy, VK_LValue
        );
        ImplicitCastExpr *EndRValCond = ImplicitCastExpr::Create(
            *Context, PtrTy, CK_LValueToRValue, EndRefCond,
            nullptr, VK_PRValue, FPOptionsOverride()
        );
        BinaryOperator *CondExpr = BinaryOperator::Create(
            *Context, IterRValCond, EndRValCond, BO_LT, Context->IntTy,
            VK_PRValue, OK_Ordinary, Loc, FPOptionsOverride()
        );

        DeclRefExpr *IterRefInc = DeclRefExpr::Create(
            *Context, NestedNameSpecifierLoc(), SourceLocation(),
            IterVD, false, Loc, PtrTy, VK_LValue
        );
        UnaryOperator *IncExpr = UnaryOperator::Create(
            *Context, IterRefInc, UO_PreInc, PtrTy,
            VK_PRValue, OK_Ordinary, Loc, false, FPOptionsOverride()
        );

        DeclRefExpr *IterRefBody = DeclRefExpr::Create(
            *Context, NestedNameSpecifierLoc(), SourceLocation(),
            IterVD, false, Loc, PtrTy, VK_LValue
        );
        ImplicitCastExpr *IterRValBody = ImplicitCastExpr::Create(
            *Context, PtrTy, CK_LValueToRValue, IterRefBody,
            nullptr, VK_PRValue, FPOptionsOverride()
        );
        UnaryOperator *DerefExpr = UnaryOperator::Create(
            *Context, IterRValBody, UO_Deref, ElemTy,
            VK_LValue, OK_Ordinary, Loc, false, FPOptionsOverride()
        );
        ImplicitCastExpr *ElemInitRVal = ImplicitCastExpr::Create(
            *Context, ElemTy, CK_LValueToRValue, DerefExpr,
            nullptr, VK_PRValue, FPOptionsOverride()
        );
        ElemVD->setInit(ElemInitRVal);

        Stmt *BodyStmts[2] = { OFS->getElement(), OFS->getBody() };
        CompoundStmt *NewBody = CompoundStmt::Create(
            *Context, BodyStmts, FPOptionsOverride(), Loc, OFS->getEndLoc()
        );

        return new (*Context) ForStmt(
            *Context, InitStmt, CondExpr, nullptr, IncExpr, NewBody,
            OFS->getForLoc(), OFS->getForLoc(), OFS->getRParenLoc()
        );
    }

    void rewriteStmtTree(Stmt *S, DeclContext *DC) {
        if (!S) return;
        for (Stmt *&Child : S->children()) {
            if (!Child) continue;
            if (auto *OFS = dyn_cast<ObjCForCollectionStmt>(Child)) {
                if (Stmt *LoweredFor = buildLoweredForCollection(OFS, DC)) {
                    Child = LoweredFor;
                }
                continue;
            }
            rewriteStmtTree(Child, DC);
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

        if (hasVeriObjCAnnotation(D, "objc_flat")) {
            checkFlatPayloadType(T, D->getName(), D->getLocation());
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
        if (hasVeriObjCAnnotation(D, "objc_flat")) {
            checkFlatPayloadType(T, D->getName(), D->getLocation());
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


    bool VisitRecordDecl(RecordDecl *D) {
        if (D->isCompleteDefinition() && hasVeriObjCAnnotation(D, "objc_flat")) {
            checkFlatPayloadType(Context->getTypeDeclType(static_cast<const TypeDecl*>(D)), D->getName(), D->getLocation());
        }
        return true;
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
        } else if (Name == "veriobjc_channel_send" && E->getNumArgs() >= 2) {
            QualType ArgTy = E->getArg(1)->IgnoreParenCasts()->getType();
            if (ArgTy->isPointerType()) {
                QualType PointeeTy = ArgTy->getPointeeType();
                checkFlatPayloadType(PointeeTy, PointeeTy.getAsString(), E->getArg(1)->getExprLoc());
            }
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
        if (Context->getSourceManager().getFilename(D->getLocation()).ends_with("veriobjc_runtime.h")) {
            return;
        }

        if (auto *FD = dyn_cast<FunctionDecl>(D)) {
            if (FD->hasBody()) {
                llvm::DenseSet<const VarDecl *> MovedVars;
                llvm::DenseSet<const VarDecl *> FreedVars;
                analyzeLinearOwnership(FD->getBody(), MovedVars, FreedVars);
                llvm::DenseMap<const VarDecl *, unsigned> VarDeclDepth;
                llvm::DenseMap<const VarDecl *, unsigned> StackRootDepth;
                analyzeScopeRootSafety(FD->getBody(), 0, VarDeclDepth, StackRootDepth);
                if (Context->getDiagnostics().hasErrorOccurred()) return;
                rewriteStmtTree(FD->getBody(), FD);
            }
            return;
        }

        if (auto *ImplD = dyn_cast<ObjCImplementationDecl>(D)) {
            ObjCInterfaceDecl *ID = ImplD->getClassInterface();
            if (!ID) return;

            for (ObjCMethodDecl *MD : ImplD->instance_methods()) {
                if (!MD->hasBody()) continue;

                Stmt *OrigBody = MD->getBody();
                llvm::DenseSet<const VarDecl *> MovedVars;
                llvm::DenseSet<const VarDecl *> FreedVars;
                analyzeLinearOwnership(OrigBody, MovedVars, FreedVars);
                llvm::DenseMap<const VarDecl *, unsigned> VarDeclDepth;
                llvm::DenseMap<const VarDecl *, unsigned> StackRootDepth;
                analyzeScopeRootSafety(OrigBody, 0, VarDeclDepth, StackRootDepth);
                if (Context->getDiagnostics().hasErrorOccurred()) continue;
                rewriteStmtTree(OrigBody, MD);

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
        : CI(CI), Visitor(&CI.getASTContext()) {
        CI.getDiagnostics().setSeverity(
            diag::warn_collection_expr_type, diag::Severity::Ignored, SourceLocation()
        );
    }

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


struct ObjCConsumedAttrInfo : public ParsedAttrInfo {
    ObjCConsumedAttrInfo() {
        OptArgs = 0;
        static constexpr Spelling S[] = {
            {ParsedAttr::AS_GNU, "objc_consumed"},
            {ParsedAttr::AS_C23, "objc_consumed"},
            {ParsedAttr::AS_CXX11, "objc_consumed"}
        };
        Spellings = S;
    }
    AttrHandling handleDeclAttribute(Sema &S, Decl *D, const ParsedAttr &Attr) const override {
        D->addAttr(AnnotateAttr::Create(S.Context, "objc_consumed", nullptr, 0, Attr.getRange()));
        return AttributeApplied;
    }
};

struct ObjCFlatAttrInfo : public ParsedAttrInfo {
    ObjCFlatAttrInfo() {
        OptArgs = 0;
        static constexpr Spelling S[] = {
            {ParsedAttr::AS_GNU, "objc_flat"},
            {ParsedAttr::AS_C23, "objc_flat"},
            {ParsedAttr::AS_CXX11, "objc_flat"}
        };
        Spellings = S;
    }
    AttrHandling handleDeclAttribute(Sema &S, Decl *D, const ParsedAttr &Attr) const override {
        D->addAttr(AnnotateAttr::Create(S.Context, "objc_flat", nullptr, 0, Attr.getRange()));
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

static ParsedAttrInfoRegistry::Add<ObjCConsumedAttrInfo> AttrConsumed(
    "objc_consumed", "Mark VeriObjC parameter as consuming linear ownership"
);
static ParsedAttrInfoRegistry::Add<ObjCFlatAttrInfo> AttrFlat(
    "objc_flat", "Enforce VeriObjC flat-value struct layout (no pointers at any depth)"
);
static FrontendPluginRegistry::Add<VeriObjCAction> X(

    "veriobjc-check",
    "Enforce VeriObjC structural invariants and eliminate non-deterministic features"
);
