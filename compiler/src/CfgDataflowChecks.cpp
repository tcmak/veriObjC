#include "veriobjc/VeriObjCVisitor.h"

using namespace clang;

namespace veriobjc {

const VarDecl *VeriObjCVisitor::extractMovedVarFromComma(const BinaryOperator *BO) {
    if (!BO || BO->getOpcode() != BO_Comma) return nullptr;
    const auto *CE = dyn_cast<CallExpr>(BO->getLHS()->IgnoreParenCasts());
    const FunctionDecl *FD = CE ? CE->getDirectCallee() : nullptr;
    if (!FD || FD->getName() != "__veriobjc_mark_moved" || CE->getNumArgs() != 1) return nullptr;
    return extractVarDecl(CE->getArg(0));
}

bool VeriObjCVisitor::handleLinearCallExpr(
    const CallExpr *CE,
    llvm::DenseSet<const VarDecl *> &MovedVars,
    llvm::DenseSet<const VarDecl *> &FreedVars
) {
    const FunctionDecl *FD = CE->getDirectCallee();
    if (!FD) return false;

    llvm::StringRef FnName = FD->getName();
    if ((FnName == "veriobjc_pool_free" || FnName == "veriobjc_gen_pool_free") && CE->getNumArgs() >= 2) {
        if (const VarDecl *VD = extractVarDecl(CE->getArg(1))) {
            analyzeLinearOwnership(CE->getArg(0), MovedVars, FreedVars);
            if (MovedVars.contains(VD) || FreedVars.contains(VD)) {
                reportConsumedVar(CE->getArg(1)->getExprLoc(), VD->getName());
            } else {
                FreedVars.insert(VD);
            }
            return true;
        }
    }

    for (unsigned i = 0; i < CE->getNumArgs(); ++i) {
        const Expr *Arg = CE->getArg(i);
        analyzeLinearOwnership(Arg, MovedVars, FreedVars);
        if (i < FD->getNumParams() && hasVeriObjCAnnotation(FD->getParamDecl(i), "objc_consumed")) {
            if (const VarDecl *VD = extractVarDecl(Arg)) MovedVars.insert(VD);
        }
    }
    return true;
}

void VeriObjCVisitor::analyzeLinearOwnership(
    const Stmt *S,
    llvm::DenseSet<const VarDecl *> &MovedVars,
    llvm::DenseSet<const VarDecl *> &FreedVars
) {
    if (!S) return;

    if (const auto *BO = dyn_cast<BinaryOperator>(S)) {
        if (const VarDecl *MovedVD = extractMovedVarFromComma(BO)) {
            if (MovedVars.contains(MovedVD) || FreedVars.contains(MovedVD)) {
                reportConsumedVar(BO->getExprLoc(), MovedVD->getName());
            } else {
                MovedVars.insert(MovedVD);
            }
            return;
        }
        if (BO->getOpcode() == BO_Assign) {
            analyzeLinearOwnership(BO->getRHS(), MovedVars, FreedVars);
            if (const auto *DRE = dyn_cast<DeclRefExpr>(BO->getLHS()->IgnoreParenCasts())) {
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

    if (const auto *CE = dyn_cast<CallExpr>(S); CE && handleLinearCallExpr(CE, MovedVars, FreedVars)) {
        return;
    }

    if (const auto *DRE = dyn_cast<DeclRefExpr>(S)) {
        const auto *VD = dyn_cast<VarDecl>(DRE->getDecl());
        if (VD && MovedVars.contains(VD)) reportConsumedVar(DRE->getLocation(), VD->getName());
        return;
    }

    for (const Stmt *Child : S->children()) {
        analyzeLinearOwnership(Child, MovedVars, FreedVars);
    }
}

bool VeriObjCVisitor::isStackAllocExpr(const Expr *E) {
    if (!E) return false;
    const auto *CE = dyn_cast<CallExpr>(E->IgnoreParenCasts());
    const FunctionDecl *FD = CE ? CE->getDirectCallee() : nullptr;
    if (!FD) return false;
    llvm::StringRef Name = FD->getName();
    return Name == "veriobjc_stack_alloc" || Name == "veriobjc_alloc_init";
}

void VeriObjCVisitor::recordDeclScopeDepth(
    const DeclStmt *DS,
    unsigned CurrentDepth,
    llvm::DenseMap<const VarDecl *, unsigned> &VarDeclDepth,
    llvm::DenseMap<const VarDecl *, unsigned> &StackRootDepth
) {
    for (const Decl *D : DS->decls()) {
        const auto *VD = dyn_cast<VarDecl>(D);
        if (!VD) continue;
        VarDeclDepth[VD] = CurrentDepth;
        const Expr *Init = VD->getInit();
        if (!Init) continue;
        if (isStackAllocExpr(Init)) {
            StackRootDepth[VD] = CurrentDepth;
        } else if (const VarDecl *SrcVD = extractVarDecl(Init)) {
            if (auto It = StackRootDepth.find(SrcVD); It != StackRootDepth.end()) {
                StackRootDepth[VD] = It->second;
            }
        } else if (const auto *CE = dyn_cast<CallExpr>(Init->IgnoreParenCasts())) {
            if (CE->getNumArgs() >= 1) {
                if (const VarDecl *ArgVD = extractVarDecl(CE->getArg(0))) {
                    if (auto It = StackRootDepth.find(ArgVD); It != StackRootDepth.end()) {
                        StackRootDepth[VD] = It->second;
                    }
                }
            }
        }
    }
}

void VeriObjCVisitor::checkAssignScopeEscape(
    const BinaryOperator *BO,
    unsigned CurrentDepth,
    const llvm::DenseMap<const VarDecl *, unsigned> &VarDeclDepth,
    const llvm::DenseMap<const VarDecl *, unsigned> &StackRootDepth
) {
    if (BO->getOpcode() != BO_Assign) return;
    const VarDecl *DstVD = extractVarDecl(BO->getLHS());
    if (!DstVD) return;

    unsigned DstDepth = VarDeclDepth.lookup(DstVD);
    unsigned SrcRootDepth = 0;
    llvm::StringRef SrcName = "veriobjc_stack_alloc";

    if (isStackAllocExpr(BO->getRHS())) {
        SrcRootDepth = CurrentDepth;
    } else if (const VarDecl *SrcVD = extractVarDecl(BO->getRHS())) {
        if (auto It = StackRootDepth.find(SrcVD); It != StackRootDepth.end()) {
            SrcRootDepth = It->second;
            SrcName = SrcVD->getName();
        }
    }

    if (SrcRootDepth > 0 && DstDepth > 0 && DstDepth < SrcRootDepth) {
        reportError(BO->getExprLoc(), "VeriObjC Violation: Escaping data-stack reference '%0' assigned to outer destination '%1' that outlives its root scope.")
            << SrcName << DstVD->getName();
    }
}

void VeriObjCVisitor::checkFFIStackArgEscape(const CallExpr *CE, const llvm::DenseMap<const VarDecl *, unsigned> &StackRootDepth) {
    const FunctionDecl *CalleeFD = CE->getDirectCallee();
    if (!CalleeFD || CalleeFD->hasBody() || isInRuntimeHeader(CalleeFD->getLocation())) return;

    for (const Expr *Arg : CE->arguments()) {
        if (const VarDecl *ArgVD = extractVarDecl(Arg); ArgVD && StackRootDepth.contains(ArgVD)) {
            reportError(Arg->getExprLoc(), "VeriObjC Violation: Banned cross-boundary ownership: data-stack reference '%0' cannot cross an FFI boundary.")
                << ArgVD->getName();
        } else if (isStackAllocExpr(Arg)) {
            reportError(Arg->getExprLoc(), "VeriObjC Violation: Banned cross-boundary ownership: data-stack reference cannot cross an FFI boundary.");
        }
    }
}

void VeriObjCVisitor::analyzeScopeRootSafety(
    const Stmt *S,
    unsigned CurrentDepth,
    llvm::DenseMap<const VarDecl *, unsigned> &VarDeclDepth,
    llvm::DenseMap<const VarDecl *, unsigned> &StackRootDepth
) {
    if (!S) return;
    if (const auto *CS = dyn_cast<CompoundStmt>(S)) {
        for (const Stmt *Child : CS->body()) {
            analyzeScopeRootSafety(Child, CurrentDepth + 1, VarDeclDepth, StackRootDepth);
        }
        return;
    }
    if (const auto *DS = dyn_cast<DeclStmt>(S)) {
        recordDeclScopeDepth(DS, CurrentDepth, VarDeclDepth, StackRootDepth);
    } else if (const auto *BO = dyn_cast<BinaryOperator>(S)) {
        checkAssignScopeEscape(BO, CurrentDepth, VarDeclDepth, StackRootDepth);
    } else if (const auto *CE = dyn_cast<CallExpr>(S)) {
        checkFFIStackArgEscape(CE, StackRootDepth);
    }
    for (const Stmt *Child : S->children()) {
        analyzeScopeRootSafety(Child, CurrentDepth, VarDeclDepth, StackRootDepth);
    }
}

void VeriObjCVisitor::verifyHwKernelNoMultiply(const Stmt *S, llvm::StringRef KernelName) {
    if (!S) return;
    if (const auto *BO = dyn_cast<BinaryOperator>(S)) {
        if (BO->getOpcode() == BO_Mul || BO->getOpcode() == BO_MulAssign) {
            reportError(BO->getOperatorLoc(), "VeriObjC Violation: Hardware kernel '%0' contains runtime multiplication (DSP instantiation); ternary datapaths must use 0 DSPs.")
                << KernelName;
        }
    }
    for (const Stmt *Child : S->children()) {
        verifyHwKernelNoMultiply(Child, KernelName);
    }
}

void VeriObjCVisitor::collectErrorReturnGraph(const Stmt *S, const FunctionDecl *CallerFD) {
    if (!S || !CallerFD) return;
    if (const auto *CE = dyn_cast<CallExpr>(S)) {
        if (const FunctionDecl *CalleeFD = CE->getDirectCallee()) {
            if (CalleeFD->getName() == "__veriobjc_return_err_from" && CE->getNumArgs() >= 2) {
                if (const auto *SL = dyn_cast<StringLiteral>(CE->getArg(1)->IgnoreParenImpCasts())) {
                    DirectRaisesFrom[CallerFD].push_back({SL->getString().str(), CE->getExprLoc()});
                }
            } else {
                CallEdges[CalleeFD->getCanonicalDecl()].push_back({CallerFD->getCanonicalDecl(), CE->getExprLoc()});
            }
        }
    }
    for (const Stmt *Child : S->children()) {
        collectErrorReturnGraph(Child, CallerFD);
    }
}

void VeriObjCVisitor::verifyMultiFrameErrorReturns() {
    for (const auto &[OriginFD, Reqs] : DirectRaisesFrom) {
        for (const ErrFromReq &Req : Reqs) {
            llvm::SmallVector<std::pair<const FunctionDecl *, SourceLocation>, 8> Worklist = {{OriginFD, Req.Loc}};
            llvm::DenseSet<const FunctionDecl *> Visited;

            while (!Worklist.empty()) {
                auto [CurFD, ReportLoc] = Worklist.pop_back_val();
                if (!Visited.insert(CurFD).second || CurFD->getName() == Req.TargetFrame) continue;

                auto EdgeIt = CallEdges.find(CurFD);
                if (EdgeIt == CallEdges.end() || EdgeIt->second.empty()) {
                    reportError(ReportLoc, "VeriObjC Violation: Uncaught multi-frame error return 'from %0' in function '%1' outside active '%0' frame.")
                        << Req.TargetFrame << CurFD->getName();
                } else {
                    for (const auto &Edge : EdgeIt->second) Worklist.push_back(Edge);
                }
            }
        }
    }
}

} // namespace veriobjc
