#include "veriobjc/VeriObjCVisitor.h"

#include <llvm/Support/raw_ostream.h>

using namespace clang;

namespace veriobjc {

void VeriObjCVisitor::checkLightweightGenerics(QualType T, SourceLocation Loc) {
    if (T.isNull()) return;
    const auto *OPT = T->getAs<ObjCObjectPointerType>();
    if (!OPT || OPT->getTypeArgsAsWritten().empty()) return;
    const ObjCInterfaceDecl *ID = OPT->getInterfaceDecl();
    if (!ID) return;

    if (ID->getName() == "NSArray" || ID->getName() == "NSDictionary" ||
        ID->getName() == "NSSet" || (!isSealedClass(ID) && !isFinalClass(ID))) {
        reportError(Loc, "VeriObjC Violation: Banned erased generic hints: lightweight generic specialization on '%0' is type-erased; use constrained vgeneric.")
            << ID->getName();
        return;
    }

    for (QualType ArgTy : OPT->getTypeArgsAsWritten()) {
        const auto *ArgOPT = ArgTy->getAs<ObjCObjectPointerType>();
        const ObjCInterfaceDecl *ArgID = ArgOPT ? ArgOPT->getInterfaceDecl() : nullptr;
        if (ArgID && !isSealedClass(ArgID) && !isFinalClass(ArgID)) {
            reportError(Loc, "VeriObjC Violation: Invalid vgeneric type argument '%0': type arguments to constrained generics must be @sealed classes.")
                << ArgID->getName();
        }
    }
}

bool VeriObjCVisitor::hasValidSealedGenericBound(const ObjCTypeParamDecl *TPD) {
    if (!TPD || !TPD->hasExplicitBound()) return false;
    const auto *OPT = TPD->getUnderlyingType()->getAs<ObjCObjectPointerType>();
    const ObjCInterfaceDecl *BoundID = OPT ? OPT->getInterfaceDecl() : nullptr;
    return BoundID && BoundID->getName() != "NSObject" &&
           (isSealedClass(BoundID) || isFinalClass(BoundID));
}

bool VeriObjCVisitor::VisitObjCInterfaceDecl(ObjCInterfaceDecl *D) {
    for (const ObjCInterfaceDecl *Super = D->getSuperClass(); Super; Super = Super->getSuperClass()) {
        if (Super->getName() == "NSObject") {
            reportError(D->getLocation(), "VeriObjC Violation: Banned inheritance from 'NSObject'. VeriObjC classes are freestanding and must not inherit from the dynamic Apple runtime.");
            return true;
        }
    }

    if (const ObjCInterfaceDecl *Super = D->getSuperClass()) {
        if (isFinalClass(Super)) {
            reportError(D->getLocation(), "VeriObjC Violation: Cannot inherit from final class '%0' marked '__attribute__((objc_final))'.") << Super->getName();
            return true;
        }
        if (!isSealedClass(Super)) {
            reportError(Super->getLocation(), "VeriObjC Violation: Polymorphic class '%0' in an inheritance hierarchy must be marked '__attribute__((objc_sealed))'.") << Super->getName();
        }
        if (!isSealedClass(D) && !isFinalClass(D)) {
            reportError(D->getLocation(), "VeriObjC Violation: Polymorphic class '%0' in an inheritance hierarchy must be marked '__attribute__((objc_sealed))'.") << D->getName();
        }
    }

    for (const ObjCProtocolDecl *Proto : D->protocols()) {
        if (!hasVeriObjCAnnotation(Proto, "objc_sealed")) {
            reportError(Proto->getLocation(), "VeriObjC Violation: Protocol '%0' must be marked '__attribute__((objc_sealed))' for closed-world dispatch.") << Proto->getName();
        }
    }

    if (ObjCTypeParamList *TPL = D->getTypeParamListAsWritten()) {
        for (ObjCTypeParamDecl *TPD : *TPL) {
            if (!hasValidSealedGenericBound(TPD)) {
                reportError(TPD->getLocation(), "VeriObjC Violation: Unconstrained type parameter: Generic type parameter '%0' on '%1' must have an explicit sealed constraint bound (unconstrained generics are banned).")
                    << TPD->getName() << D->getName();
            }
        }
    }
    return true;
}

std::string VeriObjCVisitor::mangleDirectMethodName(const ObjCInterfaceDecl *ID, Selector Sel) {
    std::string Name = "__veriobjc_direct_" + ID->getNameAsString() + "__";
    std::string SelStr = Sel.getAsString();
    for (char &c : SelStr) {
        if (c == ':') c = '_';
    }
    return Name + SelStr;
}

FunctionDecl *VeriObjCVisitor::getOrCreateDirectMethodDecl(ObjCInterfaceDecl *ID, ObjCMethodDecl *MD) {
    std::string Mangled = mangleDirectMethodName(ID, MD->getSelector());
    if (auto It = DirectMethodDecls.find(Mangled); It != DirectMethodDecls.end()) {
        return It->second;
    }

    DeclContext *TU = Context->getTranslationUnitDecl();
    for (NamedDecl *ND : TU->lookup(&Context->Idents.get(Mangled))) {
        if (auto *ExistingFD = dyn_cast<FunctionDecl>(ND)) {
            DirectMethodDecls[Mangled] = ExistingFD;
            return ExistingFD;
        }
    }

    QualType RetTy = MD->getReturnType().getNonReferenceType();
    QualType SelfTy = Context->getObjCObjectPointerType(Context->getObjCInterfaceType(ID));

    llvm::SmallVector<QualType, 8> ParamTypes = {SelfTy};
    for (const ParmVarDecl *PVD : MD->parameters()) ParamTypes.push_back(PVD->getType());

    QualType FnTy = Context->getFunctionType(RetTy, ParamTypes, FunctionProtoType::ExtProtoInfo());
    FunctionDecl *FD = FunctionDecl::Create(
        *Context, TU, MD->getLocation(), MD->getLocation(),
        &Context->Idents.get(Mangled), FnTy, nullptr, SC_None
    );

    llvm::SmallVector<ParmVarDecl *, 8> Params;
    Params.push_back(ParmVarDecl::Create(
        *Context, FD, MD->getLocation(), MD->getLocation(),
        &Context->Idents.get("self"), SelfTy, nullptr, SC_None, nullptr
    ));
    for (ParmVarDecl *OrigPVD : MD->parameters()) {
        Params.push_back(ParmVarDecl::Create(
            *Context, FD, OrigPVD->getBeginLoc(), OrigPVD->getLocation(),
            OrigPVD->getIdentifier(), OrigPVD->getType(), OrigPVD->getTypeSourceInfo(), SC_None, nullptr
        ));
    }
    FD->setParams(Params);
    TU->addDecl(FD);
    DirectMethodDecls[Mangled] = FD;
    return FD;
}

void VeriObjCVisitor::remapMethodVarRefs(Stmt *S, const llvm::DenseMap<const ValueDecl *, ValueDecl *> &DeclMap) {
    if (!S) return;
    for (Stmt *&Child : S->children()) {
        if (!Child) continue;
        remapMethodVarRefs(Child, DeclMap);
        auto *DRE = dyn_cast<DeclRefExpr>(Child);
        if (!DRE) continue;
        if (auto It = DeclMap.find(DRE->getDecl()); It != DeclMap.end()) {
            Child = buildDeclRef(It->second, DRE->getLocation());
        }
    }
}

Expr *VeriObjCVisitor::buildDevirtualizedCall(ObjCMessageExpr *E) {
    ObjCInterfaceDecl *ReceiverClass = E->getReceiverInterface();
    ObjCMethodDecl *MD = E->getMethodDecl();
    Expr *RecvExpr = E ? E->getInstanceReceiver() : nullptr;
    if (!ReceiverClass || !MD || !E->isInstanceMessage() || !RecvExpr) return nullptr;

    ObjCInterfaceDecl *TargetClass = MD->getClassInterface() ? MD->getClassInterface() : ReceiverClass;
    FunctionDecl *DirectFD = getOrCreateDirectMethodDecl(TargetClass, MD);

    QualType ExpectedSelfTy = DirectFD->getParamDecl(0)->getType();
    if (RecvExpr->getType().getCanonicalType() != ExpectedSelfTy.getCanonicalType()) {
        RecvExpr = ImplicitCastExpr::Create(
            *Context, ExpectedSelfTy, CK_BitCast, RecvExpr, nullptr, VK_PRValue, FPOptionsOverride()
        );
    }

    llvm::SmallVector<Expr *, 8> CallArgs = {RecvExpr};
    unsigned ParamIdx = 1;
    for (Expr *Arg : E->arguments()) {
        if (ParamIdx < DirectFD->getNumParams()) {
            QualType ExpectedParamTy = DirectFD->getParamDecl(ParamIdx)->getType();
            if (Arg->getType().getCanonicalType() != ExpectedParamTy.getCanonicalType()) {
                Arg = ImplicitCastExpr::Create(
                    *Context, ExpectedParamTy, CK_BitCast, Arg, nullptr, VK_PRValue, FPOptionsOverride()
                );
            }
        }
        CallArgs.push_back(Arg);
        ++ParamIdx;
    }

    Expr *Call = CallExpr::Create(
        *Context, buildFnPtrDecay(DirectFD, E->getExprLoc()), CallArgs,
        DirectFD->getReturnType(), VK_PRValue, E->getRightLoc(), FPOptionsOverride()
    );
    if (!E->getType()->isVoidType() &&
        DirectFD->getReturnType().getCanonicalType() != E->getType().getCanonicalType()) {
        Call = ImplicitCastExpr::Create(
            *Context, E->getType(), CK_BitCast, Call, nullptr, VK_PRValue, FPOptionsOverride()
        );
    }
    return Call;
}

ObjCIvarDecl *VeriObjCVisitor::findBoundedArrayIvar(Expr *Coll, VarDecl *ElemVD, const ConstantArrayType *&OutCAT) {
    if (!Coll || !ElemVD) return nullptr;
    const auto *OPT = Coll->getType()->getAs<ObjCObjectPointerType>();
    const ObjCInterfaceDecl *ID = OPT ? OPT->getInterfaceDecl() : nullptr;
    for (const ObjCInterfaceDecl *CurID = ID; CurID; CurID = CurID->getSuperClass()) {
        for (ObjCIvarDecl *Ivar : CurID->ivars()) {
            const ConstantArrayType *CandCAT = Context->getAsConstantArrayType(Ivar->getType());
            if (!CandCAT) continue;
            QualType CandElemTy = CandCAT->getElementType();
            QualType LoopElemTy = ElemVD->getType();
            bool Matches = (CandElemTy.getCanonicalType() == LoopElemTy.getCanonicalType());
            if (!Matches) {
                const auto *CandOPT = CandElemTy->getAs<ObjCObjectPointerType>();
                const auto *LoopOPT = LoopElemTy->getAs<ObjCObjectPointerType>();
                if (CandOPT && LoopOPT && Context->canAssignObjCInterfaces(CandOPT, LoopOPT)) {
                    Matches = true;
                }
            }
            if (Matches) {
                OutCAT = CandCAT;
                return Ivar;
            }
        }
    }
    return nullptr;
}

Stmt *VeriObjCVisitor::buildLoweredForCollection(ObjCForCollectionStmt *OFS, DeclContext *DC) {
    if (!OFS || !DC) return nullptr;

    Expr *Coll = OFS->getCollection() ? OFS->getCollection()->IgnoreParenImpCasts() : nullptr;
    auto *ElemDS = dyn_cast_or_null<DeclStmt>(OFS->getElement());
    VarDecl *ElemVD = (ElemDS && ElemDS->isSingleDecl()) ? dyn_cast<VarDecl>(ElemDS->getSingleDecl()) : nullptr;

    const ConstantArrayType *CAT = nullptr;
    ObjCIvarDecl *FoundIvar = findBoundedArrayIvar(Coll, ElemVD, CAT);
    if (!FoundIvar || !CAT) {
        reportError(OFS->getForLoc(), "VeriObjC Violation: Range-based for loop requires a container with a statically bounded array field.");
        return nullptr;
    }

    rewriteStmtTree(OFS->getBody(), DC);

    SourceLocation Loc = OFS->getForLoc();
    QualType ElemTy = CAT->getElementType();
    QualType PtrTy = Context->getPointerType(ElemTy);

    auto *IvarRef = new (*Context) ObjCIvarRefExpr(FoundIvar, FoundIvar->getType(), Loc, Loc, Coll, true, false);
    VarDecl *IterVD = VarDecl::Create(*Context, DC, Loc, Loc, &Context->Idents.get("__veriobjc_it"), PtrTy, nullptr, SC_None);
    IterVD->setInit(buildArrayDecay(IvarRef, PtrTy));

    QualType SizeTy = Context->getSizeType();
    llvm::APInt BoundVal(Context->getTypeSize(SizeTy), CAT->getSize().getZExtValue());
    IntegerLiteral *BoundLit = IntegerLiteral::Create(*Context, BoundVal, SizeTy, Loc);
    BinaryOperator *EndPtr = BinaryOperator::Create(
        *Context, buildRValueRef(IterVD, Loc), BoundLit, BO_Add, PtrTy, VK_PRValue, OK_Ordinary, Loc, FPOptionsOverride()
    );
    VarDecl *EndVD = VarDecl::Create(*Context, DC, Loc, Loc, &Context->Idents.get("__veriobjc_end"), PtrTy, nullptr, SC_None);
    EndVD->setInit(EndPtr);

    Decl *InitDecls[2] = { IterVD, EndVD };
    auto *InitStmt = new (*Context) DeclStmt(DeclGroupRef::Create(*Context, InitDecls, 2), Loc, Loc);

    BinaryOperator *CondExpr = BinaryOperator::Create(
        *Context, buildRValueRef(IterVD, Loc), buildRValueRef(EndVD, Loc),
        BO_LT, Context->IntTy, VK_PRValue, OK_Ordinary, Loc, FPOptionsOverride()
    );
    UnaryOperator *IncExpr = UnaryOperator::Create(
        *Context, buildDeclRef(IterVD, Loc), UO_PreInc, PtrTy, VK_PRValue, OK_Ordinary, Loc, false, FPOptionsOverride()
    );
    UnaryOperator *DerefExpr = UnaryOperator::Create(
        *Context, buildRValueRef(IterVD, Loc), UO_Deref, ElemTy, VK_LValue, OK_Ordinary, Loc, false, FPOptionsOverride()
    );
    Expr *ElemInit = ImplicitCastExpr::Create(
        *Context, ElemTy, CK_LValueToRValue, DerefExpr, nullptr, VK_PRValue, FPOptionsOverride()
    );
    if (ElemTy.getCanonicalType() != ElemVD->getType().getCanonicalType()) {
        ElemInit = ImplicitCastExpr::Create(
            *Context, ElemVD->getType(), CK_BitCast, ElemInit, nullptr, VK_PRValue, FPOptionsOverride()
        );
    }
    ElemVD->setInit(ElemInit);

    Stmt *BodyStmts[2] = { OFS->getElement(), OFS->getBody() };
    CompoundStmt *NewBody = CompoundStmt::Create(*Context, BodyStmts, FPOptionsOverride(), Loc, OFS->getEndLoc());
    return new (*Context) ForStmt(
        *Context, InitStmt, CondExpr, nullptr, IncExpr, NewBody, OFS->getForLoc(), OFS->getForLoc(), OFS->getRParenLoc()
    );
}

FunctionDecl *VeriObjCVisitor::synthesizeMonomorphizedBlock(BlockExpr *BE, llvm::SmallVectorImpl<VarDecl *> &OutCaptures) {
    BlockDecl *BD = BE ? BE->getBlockDecl() : nullptr;
    const FunctionProtoType *FPT = BE ? BE->getFunctionType() : nullptr;
    if (!BD || !FPT) return nullptr;

    llvm::SmallVector<QualType, 8> ParamTypes;
    for (const ParmVarDecl *PVD : BD->parameters()) ParamTypes.push_back(PVD->getType());
    for (const BlockDecl::Capture &Cap : BD->captures()) {
        if (VarDecl *CapVD = Cap.getVariable()) {
            ParamTypes.push_back(CapVD->getType());
            OutCaptures.push_back(CapVD);
        }
    }

    QualType FnTy = Context->getFunctionType(FPT->getReturnType(), ParamTypes, FunctionProtoType::ExtProtoInfo());
    std::string DedupKey;
    if (OutCaptures.empty() && BD->getBody()) {
        llvm::raw_string_ostream OS(DedupKey);
        OS << FnTy.getCanonicalType().getAsString() << "::";
        BD->getBody()->printPretty(OS, nullptr, Context->getPrintingPolicy());
        if (auto It = DedupBlockDecls.find(DedupKey); It != DedupBlockDecls.end()) {
            return It->second;
        }
    }

    std::string Name = "__veriobjc_mono_block_" + std::to_string(MonoBlockCounter++);
    DeclContext *TU = Context->getTranslationUnitDecl();
    FunctionDecl *BlockFD = FunctionDecl::Create(
        *Context, TU, BD->getLocation(), BD->getLocation(), &Context->Idents.get(Name), FnTy, nullptr, SC_Static
    );

    llvm::SmallVector<ParmVarDecl *, 8> Params;
    llvm::DenseMap<const ValueDecl *, ValueDecl *> DeclMap;
    for (ParmVarDecl *OrigPVD : BD->parameters()) {
        ParmVarDecl *ClonedPVD = ParmVarDecl::Create(
            *Context, BlockFD, OrigPVD->getBeginLoc(), OrigPVD->getLocation(),
            OrigPVD->getIdentifier(), OrigPVD->getType(), OrigPVD->getTypeSourceInfo(), SC_None, nullptr
        );
        Params.push_back(ClonedPVD);
        DeclMap[OrigPVD] = ClonedPVD;
    }
    for (VarDecl *CapVD : OutCaptures) {
        ParmVarDecl *CapPVD = ParmVarDecl::Create(
            *Context, BlockFD, CapVD->getBeginLoc(), CapVD->getLocation(),
            CapVD->getIdentifier(), CapVD->getType(), nullptr, SC_None, nullptr
        );
        Params.push_back(CapPVD);
        DeclMap[CapVD] = CapPVD;
    }

    BlockFD->setParams(Params);
    TU->addDecl(BlockFD);

    if (Stmt *Body = BD->getBody()) {
        rewriteStmtTree(Body, BlockFD);
        remapMethodVarRefs(Body, DeclMap);
        BlockFD->setBody(Body);
        SynthesizedDefs.push_back(BlockFD);
    }
    if (!DedupKey.empty()) DedupBlockDecls[DedupKey] = BlockFD;
    return BlockFD;
}

Expr *VeriObjCVisitor::buildMonomorphizedBlockCall(CallExpr *CE, FunctionDecl *BlockFD, llvm::ArrayRef<VarDecl *> Captures) {
    if (!CE || !BlockFD) return nullptr;
    llvm::SmallVector<Expr *, 8> CallArgs(CE->arg_begin(), CE->arg_end());
    for (VarDecl *CapVD : Captures) CallArgs.push_back(buildRValueRef(CapVD, CE->getExprLoc()));

    return CallExpr::Create(
        *Context, buildFnPtrDecay(BlockFD, CE->getExprLoc()), CallArgs,
        BlockFD->getReturnType(), VK_PRValue, CE->getRParenLoc(), FPOptionsOverride()
    );
}

void VeriObjCVisitor::rewriteDeclStmtBlocks(DeclStmt *DS) {
    for (Decl *D : DS->decls()) {
        auto *VD = dyn_cast<VarDecl>(D);
        if (!VD || !VD->getType()->isBlockPointerType() || VD->hasGlobalStorage() || !VD->getInit()) continue;
        auto *BE = dyn_cast<BlockExpr>(VD->getInit()->IgnoreParenImpCasts());
        if (!BE) continue;

        llvm::SmallVector<VarDecl *, 4> Captures;
        if (FunctionDecl *BlockFD = synthesizeMonomorphizedBlock(BE, Captures)) {
            MonomorphizedBlocks[VD] = BlockFD;
            BlockCapturedVars[VD] = std::move(Captures);
            VD->setInit(nullptr);
            VD->setType(Context->IntTy);
        }
    }
}

Expr *VeriObjCVisitor::tryRewriteCallExpr(CallExpr *CE) {
    if (const FunctionDecl *CalleeFD = CE->getDirectCallee()) {
        llvm::StringRef Name = CalleeFD->getName();
        if (Name == "__veriobjc_return_err_from" && CE->getNumArgs() >= 2) {
            return CE->getArg(0);
        }
        if (Name == "__veriobjc_regex_match" && CE->getNumArgs() >= 3) {
            return buildLoweredVRegexMatch(CE);
        }
        if (Name == "__veriobjc_reflect_field_count") {
            return tryFoldReflectFieldCount(CE);
        }
        if (Name == "__veriobjc_dag_critical_path_wcet") {
            uint32_t WCET = 0;
            if (verifyTaskDAGAndComputeWCET(CE->getExprLoc(), WCET)) {
                return IntegerLiteral::Create(*Context, llvm::APInt(32, WCET), Context->UnsignedIntTy, CE->getExprLoc());
            }
            return nullptr;
        }
        if (hasVeriObjCAnnotation(CalleeFD, "objc_vconstexpr")) {
            return tryFoldVConstexprCall(CE, CalleeFD);
        }
        return nullptr;
    }

    Expr *Callee = CE->getCallee() ? CE->getCallee()->IgnoreParenImpCasts() : nullptr;
    if (!Callee) return nullptr;

    if (auto *DRE = dyn_cast<DeclRefExpr>(Callee)) {
        const auto *VD = dyn_cast<VarDecl>(DRE->getDecl());
        auto It = VD ? MonomorphizedBlocks.find(VD) : MonomorphizedBlocks.end();
        if (It != MonomorphizedBlocks.end()) {
            return buildMonomorphizedBlockCall(CE, It->second, BlockCapturedVars[VD]);
        }
    } else if (auto *BE = dyn_cast<BlockExpr>(Callee)) {
        llvm::SmallVector<VarDecl *, 4> Captures;
        if (FunctionDecl *BlockFD = synthesizeMonomorphizedBlock(BE, Captures)) {
            return buildMonomorphizedBlockCall(CE, BlockFD, Captures);
        }
    }
    return nullptr;
}

void VeriObjCVisitor::rewriteStmtTree(Stmt *S, DeclContext *DC) {
    if (!S) return;
    for (Stmt *&Child : S->children()) {
        if (!Child) continue;
        if (auto *DS = dyn_cast<DeclStmt>(Child)) {
            rewriteDeclStmtBlocks(DS);
        }
        if (auto *OFS = dyn_cast<ObjCForCollectionStmt>(Child)) {
            if (Stmt *LoweredFor = buildLoweredForCollection(OFS, DC)) Child = LoweredFor;
            continue;
        }
        rewriteStmtTree(Child, DC);
        if (auto *CE = dyn_cast<CallExpr>(Child)) {
            if (Expr *Rewritten = tryRewriteCallExpr(CE)) Child = Rewritten;
        } else if (auto *OME = dyn_cast<ObjCMessageExpr>(Child)) {
            if (Expr *DirectCall = buildDevirtualizedCall(OME)) Child = DirectCall;
        }
    }
}

bool VeriObjCVisitor::analyzeAndRewriteBody(Stmt *Body, DeclContext *DC) {
    llvm::DenseSet<const VarDecl *> MovedVars;
    llvm::DenseSet<const VarDecl *> FreedVars;
    analyzeLinearOwnership(Body, MovedVars, FreedVars);

    llvm::DenseMap<const VarDecl *, unsigned> VarDeclDepth;
    llvm::DenseMap<const VarDecl *, unsigned> StackRootDepth;
    analyzeScopeRootSafety(Body, 0, VarDeclDepth, StackRootDepth);

    llvm::StringRef FnName;
    bool IsNoLatencyFn = false;
    if (const auto *FD = dyn_cast_or_null<FunctionDecl>(DC)) {
        FnName = FD->getName();
        IsNoLatencyFn = hasVeriObjCAnnotation(FD, "objc_nolatency") || hasVeriObjCAnnotation(FD, "objc_pure");
    }
    llvm::DenseSet<const VarDecl *> ProvenOkVars;
    llvm::DenseMap<const VarDecl *, const VarDecl *> ActiveStackArray;
    llvm::DenseMap<const VarDecl *, uint32_t> VVectorPushCounts;
    llvm::DenseMap<const VarDecl *, uint32_t> VHashInsertCounts;
    llvm::DenseMap<const VarDecl *, uint32_t> VDmaRingCounts;
    analyzeVResultAndStackArrays(
        Body, FnName, IsNoLatencyFn, ProvenOkVars, ActiveStackArray,
        VVectorPushCounts, VHashInsertCounts, VDmaRingCounts);
    if (Context->getDiagnostics().hasErrorOccurred()) return false;

    rewriteStmtTree(Body, DC);
    return true;
}

void VeriObjCVisitor::replaceMethodWithDirectForwarding(ObjCInterfaceDecl *ID, ObjCMethodDecl *MD, Stmt *OrigBody) {
    FunctionDecl *DirectFD = getOrCreateDirectMethodDecl(ID, MD);
    llvm::DenseMap<const ValueDecl *, ValueDecl *> DeclMap;
    if (MD->getSelfDecl()) DeclMap[MD->getSelfDecl()] = DirectFD->getParamDecl(0);
    for (unsigned i = 0; i < MD->param_size(); ++i) {
        DeclMap[MD->getParamDecl(i)] = DirectFD->getParamDecl(i + 1);
    }

    remapMethodVarRefs(OrigBody, DeclMap);
    DirectFD->setBody(OrigBody);
    SynthesizedDefs.push_back(DirectFD);

    llvm::SmallVector<Expr *, 8> FwdArgs = {buildRValueRef(MD->getSelfDecl(), MD->getLocation())};
    for (ParmVarDecl *PVD : MD->parameters()) {
        FwdArgs.push_back(buildRValueRef(PVD, PVD->getLocation()));
    }

    CallExpr *FwdCall = CallExpr::Create(
        *Context, buildFnPtrDecay(DirectFD, MD->getLocation()), FwdArgs,
        DirectFD->getReturnType(), VK_PRValue, MD->getLocation(), FPOptionsOverride()
    );
    Stmt *NewMethodStmt = DirectFD->getReturnType()->isVoidType()
        ? static_cast<Stmt *>(FwdCall)
        : ReturnStmt::Create(*Context, MD->getLocation(), FwdCall, nullptr);
    Stmt *Stmts[1] = { NewMethodStmt };
    MD->setBody(CompoundStmt::Create(*Context, Stmts, FPOptionsOverride(), MD->getBeginLoc(), MD->getEndLoc()));
}

void VeriObjCVisitor::lowerTopLevelDecl(Decl *D) {
    if (!D || Context->getDiagnostics().hasErrorOccurred() || isInRuntimeHeader(D->getLocation())) {
        return;
    }

    if (auto *FD = dyn_cast<FunctionDecl>(D)) {
        if (!FD->hasBody()) return;
        auditFunctionEffectsAndReport(FD);
        collectErrorReturnGraph(FD->getBody(), FD->getCanonicalDecl());
        if (!analyzeAndRewriteBody(FD->getBody(), FD)) return;
        if (hasVeriObjCAnnotation(FD, "objc_hw_kernel")) {
            verifyHwKernelNoMultiply(FD->getBody(), FD->getName());
        }
        return;
    }

    auto *ImplD = dyn_cast<ObjCImplementationDecl>(D);
    ObjCInterfaceDecl *ID = ImplD ? ImplD->getClassInterface() : nullptr;
    if (!ID) return;

    for (ObjCMethodDecl *MD : ImplD->instance_methods()) {
        if (!MD->hasBody()) continue;
        Stmt *OrigBody = MD->getBody();
        if (!analyzeAndRewriteBody(OrigBody, MD)) continue;

        const ObjCMethodDecl *IfaceMD = ID->lookupMethod(MD->getSelector(), MD->isInstanceMethod());
        if (hasVeriObjCAnnotation(MD, "objc_hw_kernel") ||
            (IfaceMD && hasVeriObjCAnnotation(IfaceMD, "objc_hw_kernel"))) {
            std::string KernelName = (ID->getName() + "::" + MD->getSelector().getAsString()).str();
            verifyHwKernelNoMultiply(OrigBody, KernelName);
        }
        replaceMethodWithDirectForwarding(ID, MD, OrigBody);
    }
}

void VeriObjCVisitor::finalizeTranslationUnitAudits() {
    verifyMultiFrameErrorReturns();
    uint32_t UnusedWCET = 0;
    verifyTaskDAGAndComputeWCET(SourceLocation(), UnusedWCET);
    if (!Context->getDiagnostics().hasErrorOccurred() && Opts.EmitMonoReport) {
        for (const auto &KV : MonoGenericStats) {
            const MonoGenericEntry &E = KV.getValue();
            llvm::errs() << "[VeriObjC --mono-report] Package: " << E.Package
                         << " | Generic: " << E.GenericName
                         << " | Instantiations: " << E.UniqueCount
                         << " (Deduplicated: " << E.DedupedCount
                         << ") | Bytes: " << E.CodeBytes << " | Strategy: @mono\n";
        }
    }
}

} // namespace veriobjc
