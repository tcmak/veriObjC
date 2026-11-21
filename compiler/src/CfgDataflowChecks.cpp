#include "veriobjc/VeriObjCVisitor.h"

#include <clang/Analysis/CFG.h>
#include <algorithm>
#include <deque>
#include <set>

using namespace clang;

namespace veriobjc {
namespace {

enum class LinearValState : uint8_t {
    Unreached  = 0,
    Live       = 1,
    Moved      = 2,
    MaybeMoved = 3
};

LinearValState joinLinearState(LinearValState A, LinearValState B) {
    if (A == LinearValState::Unreached) return B;
    if (B == LinearValState::Unreached) return A;
    if (A == B) return A;
    return LinearValState::MaybeMoved;
}

llvm::StringRef linearStateName(LinearValState S) {
    switch (S) {
        case LinearValState::Moved:      return "Moved";
        case LinearValState::MaybeMoved: return "MaybeMoved";
        case LinearValState::Live:       return "Live";
        case LinearValState::Unreached:  return "Unreached";
    }
    return "Unknown";
}

struct StackProv {
    unsigned Depth = 0; // 0 = non-stack / parameter / global; >= 1 = StackScoped(Depth)
    std::string RootName;
};

StackProv joinStackProv(const StackProv &A, const StackProv &B) {
    if (B.Depth > A.Depth) return B;
    return A;
}

struct CFGDataflowState {
    bool Initialized = false;
    llvm::DenseMap<const VarDecl *, LinearValState> Linear;
    llvm::DenseMap<const VarDecl *, LinearValState> PoolFreed;
    std::map<const VarDecl *, std::set<const VarDecl *>> AliasRoots;
    llvm::DenseMap<const VarDecl *, StackProv> Prov;
};

bool joinCFGState(CFGDataflowState &Dst, const CFGDataflowState &Src) {
    if (!Src.Initialized) return false;
    if (!Dst.Initialized) {
        Dst = Src;
        return true;
    }

    bool Changed = false;
    for (const auto &[VD, SrcSt] : Src.Linear) {
        LinearValState Old = Dst.Linear.lookup(VD);
        LinearValState Joined = joinLinearState(Old, SrcSt);
        if (Joined != Old) {
            Dst.Linear[VD] = Joined;
            Changed = true;
        }
    }
    for (const auto &[VD, SrcFr] : Src.PoolFreed) {
        LinearValState Old = Dst.PoolFreed.lookup(VD);
        LinearValState Joined = joinLinearState(Old, SrcFr);
        if (Joined != Old) {
            Dst.PoolFreed[VD] = Joined;
            Changed = true;
        }
    }
    for (const auto &[VD, Roots] : Src.AliasRoots) {
        std::set<const VarDecl *> &DstRoots = Dst.AliasRoots[VD];
        for (const VarDecl *R : Roots) {
            if (DstRoots.insert(R).second) {
                Changed = true;
            }
        }
    }
    for (const auto &[VD, SrcP] : Src.Prov) {
        auto It = Dst.Prov.find(VD);
        if (It == Dst.Prov.end() || SrcP.Depth > It->second.Depth) {
            Dst.Prov[VD] = SrcP;
            Changed = true;
        }
    }
    return Changed;
}

std::string getEnclosingCallableName(const DeclContext *DC) {
    if (const auto *FD = dyn_cast_or_null<FunctionDecl>(DC)) {
        return FD->getNameAsString();
    }
    if (const auto *MD = dyn_cast_or_null<ObjCMethodDecl>(DC)) {
        const ObjCInterfaceDecl *ID = MD->getClassInterface();
        std::string Cls = ID ? ID->getNameAsString() : "ObjCClass";
        return Cls + "::" + MD->getSelector().getAsString();
    }
    return "<anonymous>";
}

struct SinkBaseInfo {
    const VarDecl *BaseVD = nullptr;
    std::string FieldName;
    bool DereferencedPointer = false;
};

SinkBaseInfo inspectAssignmentLHS(const Expr *LHS) {
    SinkBaseInfo Info;
    if (!LHS) return Info;
    const Expr *Cur = LHS->IgnoreParenCasts();
    while (Cur) {
        if (const auto *DRE = dyn_cast<DeclRefExpr>(Cur)) {
            Info.BaseVD = dyn_cast<VarDecl>(DRE->getDecl());
            break;
        }
        if (const auto *ME = dyn_cast<MemberExpr>(Cur)) {
            if (Info.FieldName.empty()) {
                Info.FieldName = ME->getMemberDecl()->getNameAsString();
            }
            if (ME->isArrow()) {
                Info.DereferencedPointer = true;
            }
            Cur = ME->getBase()->IgnoreParenCasts();
            continue;
        }
        if (const auto *UO = dyn_cast<UnaryOperator>(Cur); UO && UO->getOpcode() == UO_Deref) {
            Info.DereferencedPointer = true;
            Cur = UO->getSubExpr()->IgnoreParenCasts();
            continue;
        }
        if (const auto *ASE = dyn_cast<ArraySubscriptExpr>(Cur)) {
            QualType BaseTy = ASE->getBase()->IgnoreParenImpCasts()->getType();
            if (BaseTy->isPointerType()) {
                Info.DereferencedPointer = true;
            }
            Cur = ASE->getBase()->IgnoreParenCasts();
            continue;
        }
        break;
    }
    return Info;
}

std::string getDeclDisplayName(const Decl *D) {
    if (const auto *FD = dyn_cast_or_null<FunctionDecl>(D)) {
        return FD->getNameAsString();
    }
    if (const auto *MD = dyn_cast_or_null<ObjCMethodDecl>(D)) {
        const ObjCInterfaceDecl *ID = MD->getClassInterface();
        return (ID ? ID->getNameAsString() : std::string("ObjC")) + "::" + MD->getSelector().getAsString();
    }
    return "<callee>";
}

} // namespace

bool VeriObjCVisitor::isStackAllocExpr(const Expr *E) {
    if (!E) return false;
    const auto *CE = dyn_cast<CallExpr>(E->IgnoreParenCasts());
    const FunctionDecl *FD = CE ? CE->getDirectCallee() : nullptr;
    if (!FD) return false;
    llvm::StringRef Name = FD->getName();
    return Name == "veriobjc_stack_alloc" ||
           Name == "veriobjc_alloc_init" ||
           Name == "veriobjc_sealed_alloc";
}

bool VeriObjCVisitor::canTypeCarryStackReference(QualType T) const {
    if (T.isNull() || T->isVoidType()) return false;
    if (T->isPointerType() || T->isObjCObjectPointerType() ||
        T->isBlockPointerType() || T->isReferenceType() || T->isArrayType()) {
        return true;
    }
    return findNonFlatField(T) != nullptr;
}

llvm::SmallVector<unsigned, 4> VeriObjCVisitor::getForwardedParamIndices(
    const FunctionDecl *FD,
    llvm::DenseSet<const FunctionDecl *> &Visiting
) {
    llvm::SmallVector<unsigned, 4> Result;
    if (!FD) return Result;
    const FunctionDecl *CanonFD = FD->getCanonicalDecl();
    if (auto It = ForwardedParamSummaryCache.find(CanonFD); It != ForwardedParamSummaryCache.end()) {
        return It->second;
    }
    if (!canTypeCarryStackReference(FD->getReturnType())) {
        ForwardedParamSummaryCache[CanonFD] = Result;
        return Result;
    }

    const FunctionDecl *DefFD = FD->getDefinition();
    if (!DefFD || !DefFD->hasBody()) {
        // Runtime/vstd header view/span constructors forward pointer-bearing arguments
        if (isInRuntimeHeader(FD->getLocation())) {
            for (unsigned i = 0; i < FD->getNumParams(); ++i) {
                if (canTypeCarryStackReference(FD->getParamDecl(i)->getType())) {
                    Result.push_back(i);
                }
            }
        }
        ForwardedParamSummaryCache[CanonFD] = Result;
        return Result;
    }

    if (!Visiting.insert(CanonFD).second) {
        return Result;
    }

    // Track local aliases inside helper body from parameters -> local vars -> return exprs
    std::map<const VarDecl *, std::set<unsigned>> VarParamTaint;
    for (unsigned i = 0; i < DefFD->getNumParams(); ++i) {
        if (canTypeCarryStackReference(DefFD->getParamDecl(i)->getType())) {
            VarParamTaint[DefFD->getParamDecl(i)].insert(i);
        }
    }

    auto evalExprParamTaint = [&](auto &self, const Expr *E, std::set<unsigned> &Out) -> void {
        if (!E) return;
        E = E->IgnoreParenCasts();
        if (!canTypeCarryStackReference(E->getType())) return;

        if (const auto *DRE = dyn_cast<DeclRefExpr>(E)) {
            if (const auto *VD = dyn_cast<VarDecl>(DRE->getDecl())) {
                if (auto It = VarParamTaint.find(VD); It != VarParamTaint.end()) {
                    Out.insert(It->second.begin(), It->second.end());
                }
            }
            return;
        }
        if (const auto *UO = dyn_cast<UnaryOperator>(E)) {
            if (UO->getOpcode() == UO_AddrOf) {
                const Expr *Sub = UO->getSubExpr()->IgnoreParenCasts();
                if (const auto *ASE = dyn_cast<ArraySubscriptExpr>(Sub)) {
                    self(self, ASE->getBase(), Out);
                    return;
                }
                if (const auto *ME = dyn_cast<MemberExpr>(Sub)) {
                    self(self, ME->getBase(), Out);
                    return;
                }
            }
            self(self, UO->getSubExpr(), Out);
            return;
        }
        if (const auto *ME = dyn_cast<MemberExpr>(E)) {
            self(self, ME->getBase(), Out);
            return;
        }
        if (const auto *ASE = dyn_cast<ArraySubscriptExpr>(E)) {
            self(self, ASE->getBase(), Out);
            return;
        }
        if (const auto *BO = dyn_cast<BinaryOperator>(E)) {
            if (BO->getOpcode() == BO_Add || BO->getOpcode() == BO_Sub) {
                self(self, BO->getLHS(), Out);
                self(self, BO->getRHS(), Out);
            } else if (BO->getOpcode() == BO_Comma) {
                self(self, BO->getRHS(), Out);
            }
            return;
        }
        if (const auto *CO = dyn_cast<ConditionalOperator>(E)) {
            self(self, CO->getTrueExpr(), Out);
            self(self, CO->getFalseExpr(), Out);
            return;
        }
        if (const auto *ILE = dyn_cast<InitListExpr>(E)) {
            for (const Expr *InitE : ILE->inits()) {
                self(self, InitE, Out);
            }
            return;
        }
        if (const auto *CE = dyn_cast<CallExpr>(E)) {
            if (const FunctionDecl *Callee = CE->getDirectCallee()) {
                auto Fwd = getForwardedParamIndices(Callee, Visiting);
                for (unsigned Idx : Fwd) {
                    if (Idx < CE->getNumArgs()) {
                        self(self, CE->getArg(Idx), Out);
                    }
                }
            }
            return;
        }
    };

    std::set<unsigned> ReturnedParams;
    auto walkHelperStmt = [&](auto &self, const Stmt *S) -> void {
        if (!S) return;
        if (const auto *DS = dyn_cast<DeclStmt>(S)) {
            for (const Decl *D : DS->decls()) {
                if (const auto *VD = dyn_cast<VarDecl>(D); VD && VD->getInit()) {
                    evalExprParamTaint(evalExprParamTaint, VD->getInit(), VarParamTaint[VD]);
                }
            }
        } else if (const auto *BO = dyn_cast<BinaryOperator>(S); BO && BO->getOpcode() == BO_Assign) {
            if (const VarDecl *LHS = extractVarDecl(BO->getLHS())) {
                evalExprParamTaint(evalExprParamTaint, BO->getRHS(), VarParamTaint[LHS]);
            }
        } else if (const auto *RS = dyn_cast<ReturnStmt>(S)) {
            evalExprParamTaint(evalExprParamTaint, RS->getRetValue(), ReturnedParams);
        }
        for (const Stmt *Child : S->children()) {
            self(self, Child);
        }
    };

    walkHelperStmt(walkHelperStmt, DefFD->getBody());
    Visiting.erase(CanonFD);

    for (unsigned Idx : ReturnedParams) {
        Result.push_back(Idx);
    }
    ForwardedParamSummaryCache[CanonFD] = Result;
    return Result;
}

void VeriObjCVisitor::analyzeFunctionCFGDataflow(Stmt *Body, DeclContext *DC) {
    if (!Body || !DC) return;
    auto *OwnerDecl = dyn_cast<Decl>(DC);
    if (!OwnerDecl) return;

    std::string FnName = getEnclosingCallableName(DC);

    // 1. Pre-pass: assign lexical scope depths and identify veriobjc_move() internal AST nodes
    llvm::DenseMap<const Stmt *, unsigned> StmtScopeDepth;
    llvm::DenseMap<const VarDecl *, unsigned> VarScopeDepth;
    llvm::DenseMap<const CallExpr *, const VarDecl *> MoveMarkerCalls;
    llvm::DenseSet<const DeclRefExpr *> MoveInternalRefs;
    llvm::DenseSet<const DeclRefExpr *> DirectAssignLHSRefs;

    if (const auto *FD = dyn_cast<FunctionDecl>(DC)) {
        for (const ParmVarDecl *PVD : FD->parameters()) {
            VarScopeDepth[PVD] = 0u;
        }
    } else if (const auto *MD = dyn_cast<ObjCMethodDecl>(DC)) {
        if (MD->getSelfDecl()) VarScopeDepth[MD->getSelfDecl()] = 0u;
        for (const ParmVarDecl *PVD : MD->parameters()) {
            VarScopeDepth[PVD] = 0u;
        }
    }

    auto preScanAST = [&](auto &self, const Stmt *S, unsigned Depth) -> void {
        if (!S) return;
        unsigned NextDepth = Depth;
        if (isa<CompoundStmt>(S)) {
            NextDepth = Depth + 1u;
        }
        StmtScopeDepth[S] = NextDepth;

        if (const auto *DS = dyn_cast<DeclStmt>(S)) {
            for (const Decl *D : DS->decls()) {
                if (const auto *VD = dyn_cast<VarDecl>(D)) {
                    VarScopeDepth[VD] = NextDepth;
                }
            }
        } else if (const auto *BO = dyn_cast<BinaryOperator>(S)) {
            if (BO->getOpcode() == BO_Comma) {
                if (const auto *CE = dyn_cast<CallExpr>(BO->getLHS()->IgnoreParenCasts())) {
                    const FunctionDecl *Callee = CE->getDirectCallee();
                    if (Callee && Callee->getName() == "__veriobjc_mark_moved" && CE->getNumArgs() == 1) {
                        const Expr *Arg0 = CE->getArg(0)->IgnoreParenCasts();
                        if (const auto *UO = dyn_cast<UnaryOperator>(Arg0); UO && UO->getOpcode() == UO_AddrOf) {
                            Arg0 = UO->getSubExpr()->IgnoreParenCasts();
                        }
                        if (const auto *ArgDRE = dyn_cast<DeclRefExpr>(Arg0)) {
                            if (const auto *MovedVD = dyn_cast<VarDecl>(ArgDRE->getDecl())) {
                                MoveMarkerCalls[CE] = MovedVD;
                                MoveInternalRefs.insert(ArgDRE);
                                if (const auto *RhsDRE = dyn_cast<DeclRefExpr>(BO->getRHS()->IgnoreParenCasts())) {
                                    MoveInternalRefs.insert(RhsDRE);
                                }
                            }
                        }
                    }
                }
            } else if (BO->getOpcode() == BO_Assign) {
                if (const auto *LhsDRE = dyn_cast<DeclRefExpr>(BO->getLHS()->IgnoreParenCasts())) {
                    DirectAssignLHSRefs.insert(LhsDRE);
                }
            }
        }

        for (const Stmt *Child : S->children()) {
            self(self, Child, NextDepth);
        }
    };
    preScanAST(preScanAST, Body, 0u);

    // 2. Build clang::CFG for path-sensitive forward monotone dataflow
    CFG::BuildOptions CfgBuildOpts;
    std::unique_ptr<CFG> Cfg = CFG::buildCFG(OwnerDecl, Body, Context, CfgBuildOpts);
    if (!Cfg) return;

    std::set<std::pair<unsigned, const VarDecl *>> EmittedMoveDiags;
    std::set<unsigned> EmittedEscapeDiags;

    auto emitConsumedOnce = [&](SourceLocation Loc, const VarDecl *UsedVD, const VarDecl *RootVD, LinearValState StVal) {
        unsigned RawLoc = Loc.getRawEncoding();
        if (EmittedMoveDiags.emplace(RawLoc, UsedVD).second) {
            reportConsumedVar(Loc, UsedVD->getName(), RootVD->getName(), linearStateName(StVal), FnName);
        }
    };

    auto emitEscapeOnce = [&](SourceLocation Loc, const StackProv &P, llvm::StringRef SinkDesc) {
        unsigned RawLoc = Loc.getRawEncoding();
        if (EmittedEscapeDiags.insert(RawLoc).second) {
            llvm::StringRef Root = P.RootName.empty() ? llvm::StringRef("veriobjc_stack_alloc") : llvm::StringRef(P.RootName);
            reportStackEscape(Loc, Root, P.Depth, SinkDesc, FnName);
        }
    };

    auto lookupScopeDepth = [&](const Stmt *S) -> unsigned {
        auto It = StmtScopeDepth.find(S);
        return (It != StmtScopeDepth.end() && It->second > 0u) ? It->second : 1u;
    };

    // Provenance evaluator over expressions in a CFG state
    auto evalProvenance = [&](auto &self, const Expr *E, llvm::StringRef HintRoot, unsigned CurDepth, const CFGDataflowState &St) -> StackProv {
        if (!E) return {};
        const Expr *Stripped = E->IgnoreParenCasts();
        if (!canTypeCarryStackReference(Stripped->getType()) && !canTypeCarryStackReference(E->getType())) {
            return {};
        }

        if (isStackAllocExpr(Stripped)) {
            std::string Name = HintRoot.empty() ? "veriobjc_stack_alloc" : HintRoot.str();
            return { CurDepth, Name };
        }

        if (const auto *DRE = dyn_cast<DeclRefExpr>(Stripped)) {
            if (const auto *VD = dyn_cast<VarDecl>(DRE->getDecl())) {
                if (auto It = St.Prov.find(VD); It != St.Prov.end() && It->second.Depth > 0u) {
                    return It->second;
                }
                if (VD->getType()->isArrayType() && VD->hasLocalStorage()) {
                    unsigned VDepth = VarScopeDepth.lookup(VD);
                    if (VDepth > 0u) return { VDepth, VD->getNameAsString() };
                }
            }
            return {};
        }

        if (const auto *UO = dyn_cast<UnaryOperator>(Stripped)) {
            if (UO->getOpcode() == UO_AddrOf) {
                const Expr *Sub = UO->getSubExpr()->IgnoreParenCasts();
                if (const auto *ASE = dyn_cast<ArraySubscriptExpr>(Sub)) {
                    return self(self, ASE->getBase(), HintRoot, CurDepth, St);
                }
                if (const auto *ME = dyn_cast<MemberExpr>(Sub)) {
                    if (ME->isArrow()) {
                        return self(self, ME->getBase(), HintRoot, CurDepth, St);
                    }
                    if (const VarDecl *BaseVD = extractVarDecl(ME->getBase())) {
                        auto BaseP = St.Prov.lookup(BaseVD);
                        unsigned VDepth = BaseVD->hasLocalStorage() ? VarScopeDepth.lookup(BaseVD) : 0u;
                        if (VDepth > BaseP.Depth) return { VDepth, BaseVD->getNameAsString() };
                        return BaseP;
                    }
                }
                if (const auto *SubDRE = dyn_cast<DeclRefExpr>(Sub)) {
                    if (const auto *VD = dyn_cast<VarDecl>(SubDRE->getDecl()); VD && VD->hasLocalStorage()) {
                        unsigned VDepth = VarScopeDepth.lookup(VD);
                        StackProv LocalP{ VDepth, VD->getNameAsString() };
                        return joinStackProv(St.Prov.lookup(VD), LocalP);
                    }
                }
                return self(self, Sub, HintRoot, CurDepth, St);
            }
            if (UO->getOpcode() == UO_Deref) {
                return self(self, UO->getSubExpr(), HintRoot, CurDepth, St);
            }
        }

        if (const auto *ASE = dyn_cast<ArraySubscriptExpr>(Stripped)) {
            return self(self, ASE->getBase(), HintRoot, CurDepth, St);
        }

        if (const auto *ME = dyn_cast<MemberExpr>(Stripped)) {
            return self(self, ME->getBase(), HintRoot, CurDepth, St);
        }

        if (const auto *BO = dyn_cast<BinaryOperator>(Stripped)) {
            if (BO->getOpcode() == BO_Add || BO->getOpcode() == BO_Sub) {
                return joinStackProv(
                    self(self, BO->getLHS(), HintRoot, CurDepth, St),
                    self(self, BO->getRHS(), HintRoot, CurDepth, St)
                );
            }
            if (BO->getOpcode() == BO_Comma) {
                return self(self, BO->getRHS(), HintRoot, CurDepth, St);
            }
        }

        if (const auto *CO = dyn_cast<ConditionalOperator>(Stripped)) {
            return joinStackProv(
                self(self, CO->getTrueExpr(), HintRoot, CurDepth, St),
                self(self, CO->getFalseExpr(), HintRoot, CurDepth, St)
            );
        }

        if (const auto *ILE = dyn_cast<InitListExpr>(Stripped)) {
            StackProv Acc;
            for (const Expr *InitE : ILE->inits()) {
                Acc = joinStackProv(Acc, self(self, InitE, HintRoot, CurDepth, St));
            }
            return Acc;
        }

        if (const auto *CE = dyn_cast<CallExpr>(Stripped)) {
            if (const FunctionDecl *Callee = CE->getDirectCallee()) {
                llvm::DenseSet<const FunctionDecl *> Visiting;
                auto FwdIndices = getForwardedParamIndices(Callee, Visiting);
                StackProv Acc;
                for (unsigned Idx : FwdIndices) {
                    if (Idx < CE->getNumArgs()) {
                        Acc = joinStackProv(Acc, self(self, CE->getArg(Idx), HintRoot, CurDepth, St));
                    }
                }
                return Acc;
            }
        }

        return {};
    };

    auto checkVarNotConsumed = [&](const VarDecl *VD, SourceLocation Loc, bool CheckFreedToo, const CFGDataflowState &St) -> bool {
        if (!VD) return true;
        if (auto RootsIt = St.AliasRoots.find(VD); RootsIt != St.AliasRoots.end() && !RootsIt->second.empty()) {
            for (const VarDecl *R : RootsIt->second) {
                LinearValState RSt = St.Linear.lookup(R);
                if (RSt == LinearValState::Moved || RSt == LinearValState::MaybeMoved) {
                    emitConsumedOnce(Loc, VD, R, RSt);
                    return false;
                }
                if (CheckFreedToo) {
                    LinearValState FSt = St.PoolFreed.lookup(R);
                    if (FSt == LinearValState::Moved || FSt == LinearValState::MaybeMoved) {
                        emitConsumedOnce(Loc, VD, R, FSt);
                        return false;
                    }
                }
            }
        }
        LinearValState SelfSt = St.Linear.lookup(VD);
        if (SelfSt == LinearValState::Moved || SelfSt == LinearValState::MaybeMoved) {
            emitConsumedOnce(Loc, VD, VD, SelfSt);
            return false;
        }
        if (CheckFreedToo) {
            LinearValState SelfFr = St.PoolFreed.lookup(VD);
            if (SelfFr == LinearValState::Moved || SelfFr == LinearValState::MaybeMoved) {
                emitConsumedOnce(Loc, VD, VD, SelfFr);
                return false;
            }
        }
        return true;
    };

    auto consumeVarAndRoots = [&](const VarDecl *VD, CFGDataflowState &St) {
        if (!VD) return;
        St.Linear[VD] = LinearValState::Moved;
        auto It = St.AliasRoots.find(VD);
        if (It != St.AliasRoots.end()) {
            for (const VarDecl *R : It->second) {
                St.Linear[R] = LinearValState::Moved;
            }
        }
    };

    auto markFreedVarAndRoots = [&](const VarDecl *VD, CFGDataflowState &St) {
        if (!VD) return;
        St.PoolFreed[VD] = LinearValState::Moved;
        auto It = St.AliasRoots.find(VD);
        if (It != St.AliasRoots.end()) {
            for (const VarDecl *R : It->second) {
                St.PoolFreed[R] = LinearValState::Moved;
            }
        }
    };

    auto bindVarValue = [&](const VarDecl *DstVD, const Expr *InitExpr, unsigned CurDepth, CFGDataflowState &St) {
        St.Linear[DstVD] = LinearValState::Live;
        St.PoolFreed[DstVD] = LinearValState::Live;
        St.AliasRoots[DstVD].clear();
        const VarDecl *SrcVD = InitExpr ? extractVarDecl(InitExpr) : nullptr;
        if (SrcVD && (DstVD->getType()->isPointerType() || DstVD->getType()->isObjCObjectPointerType())) {
            auto SrcRootsIt = St.AliasRoots.find(SrcVD);
            if (SrcRootsIt != St.AliasRoots.end() && !SrcRootsIt->second.empty()) {
                St.AliasRoots[DstVD] = SrcRootsIt->second;
            } else {
                St.AliasRoots[DstVD].insert(SrcVD);
            }
        } else {
            St.AliasRoots[DstVD].insert(DstVD);
        }

        if (InitExpr) {
            StackProv P = evalProvenance(evalProvenance, InitExpr, DstVD->getName(), CurDepth, St);
            if (P.Depth > 0u) {
                St.Prov[DstVD] = P;
            } else {
                St.Prov.erase(DstVD);
            }
        } else {
            St.Prov.erase(DstVD);
        }
    };

    // Statement transfer function (tracks evaluated Stmts within each basic block)
    auto transferStmt = [&](auto &self, const Stmt *S, unsigned BlockScopeFallback, CFGDataflowState &St, llvm::DenseSet<const Stmt *> &Evaluated) -> void {
        if (!S || !Evaluated.insert(S).second) return;
        unsigned CurDepth = StmtScopeDepth.count(S) ? StmtScopeDepth.lookup(S) : BlockScopeFallback;

        if (const auto *DS = dyn_cast<DeclStmt>(S)) {
            for (const Decl *D : DS->decls()) {
                const auto *VD = dyn_cast<VarDecl>(D);
                if (!VD) continue;
                if (const Expr *Init = VD->getInit()) {
                    self(self, Init, CurDepth, St, Evaluated);
                }
                bindVarValue(VD, VD->getInit(), CurDepth, St);
            }
            return;
        }

        if (const auto *BO = dyn_cast<BinaryOperator>(S)) {
            if (BO->getOpcode() == BO_Assign) {
                self(self, BO->getRHS(), CurDepth, St, Evaluated);
                StackProv SrcP = evalProvenance(evalProvenance, BO->getRHS(), "", CurDepth, St);

                if (const auto *LhsDRE = dyn_cast<DeclRefExpr>(BO->getLHS()->IgnoreParenCasts())) {
                    if (const auto *DstVD = dyn_cast<VarDecl>(LhsDRE->getDecl())) {
                        Evaluated.insert(LhsDRE);
                        if (SrcP.Depth > 0u) {
                            if (DstVD->hasGlobalStorage()) {
                                std::string Sink = "global '" + DstVD->getNameAsString() + "'";
                                emitEscapeOnce(BO->getExprLoc(), SrcP, Sink);
                            } else {
                                unsigned DstDepth = VarScopeDepth.lookup(DstVD);
                                if (DstDepth < SrcP.Depth) {
                                    std::string Sink = "outer destination '" + DstVD->getNameAsString() + "'";
                                    emitEscapeOnce(BO->getExprLoc(), SrcP, Sink);
                                }
                            }
                        }
                        bindVarValue(DstVD, BO->getRHS(), CurDepth, St);
                        return;
                    }
                }

                self(self, BO->getLHS(), CurDepth, St, Evaluated);
                if (SrcP.Depth > 0u) {
                    SinkBaseInfo SinkInfo = inspectAssignmentLHS(BO->getLHS());
                    if (const VarDecl *BaseVD = SinkInfo.BaseVD) {
                        if (BaseVD->hasGlobalStorage()) {
                            std::string Sink = "global '" + BaseVD->getNameAsString() + "'";
                            emitEscapeOnce(BO->getExprLoc(), SrcP, Sink);
                        } else if (isa<ParmVarDecl>(BaseVD) && SinkInfo.DereferencedPointer) {
                            std::string Sink = SinkInfo.FieldName.empty()
                                ? ("out-parameter '*" + BaseVD->getNameAsString() + "'")
                                : ("out-parameter field '" + BaseVD->getNameAsString() + "->" + SinkInfo.FieldName + "'");
                            emitEscapeOnce(BO->getExprLoc(), SrcP, Sink);
                        } else if (SinkInfo.DereferencedPointer) {
                            unsigned TargetDepth = St.Prov.lookup(BaseVD).Depth;
                            if (TargetDepth < SrcP.Depth) {
                                std::string Sink = SinkInfo.FieldName.empty()
                                    ? ("outer pointer '*" + BaseVD->getNameAsString() + "'")
                                    : ("outer field '" + BaseVD->getNameAsString() + "->" + SinkInfo.FieldName + "'");
                                emitEscapeOnce(BO->getExprLoc(), SrcP, Sink);
                            }
                        } else {
                            unsigned HolderDepth = VarScopeDepth.lookup(BaseVD);
                            if (HolderDepth < SrcP.Depth) {
                                std::string Sink = SinkInfo.FieldName.empty()
                                    ? ("outer variable '" + BaseVD->getNameAsString() + "'")
                                    : ("outer field '" + BaseVD->getNameAsString() + "." + SinkInfo.FieldName + "'");
                                emitEscapeOnce(BO->getExprLoc(), SrcP, Sink);
                            } else {
                                St.Prov[BaseVD] = joinStackProv(St.Prov.lookup(BaseVD), SrcP);
                            }
                        }
                    }
                }
                return;
            }
        }

        if (const auto *CE = dyn_cast<CallExpr>(S)) {
            if (auto MoveIt = MoveMarkerCalls.find(CE); MoveIt != MoveMarkerCalls.end()) {
                const VarDecl *MovedVD = MoveIt->second;
                if (checkVarNotConsumed(MovedVD, CE->getExprLoc(), /*CheckFreedToo=*/true, St)) {
                    consumeVarAndRoots(MovedVD, St);
                }
                return;
            }

            const FunctionDecl *CalleeFD = CE->getDirectCallee();
            if (CalleeFD) {
                llvm::StringRef FnCallee = CalleeFD->getName();
                if ((FnCallee == "veriobjc_pool_free" || FnCallee == "veriobjc_gen_pool_free") && CE->getNumArgs() >= 2) {
                    self(self, CE->getArg(0), CurDepth, St, Evaluated);
                    self(self, CE->getArg(1), CurDepth, St, Evaluated);
                    if (const VarDecl *FreedVD = extractVarDecl(CE->getArg(1))) {
                        if (checkVarNotConsumed(FreedVD, CE->getArg(1)->getExprLoc(), /*CheckFreedToo=*/true, St)) {
                            markFreedVarAndRoots(FreedVD, St);
                        }
                    }
                    return;
                }

                // FFI cross-boundary check on bodyless external functions
                bool IsExternalFFI = !CalleeFD->hasBody() && !isInRuntimeHeader(CalleeFD->getLocation());
                for (unsigned i = 0; i < CE->getNumArgs(); ++i) {
                    const Expr *Arg = CE->getArg(i);
                    self(self, Arg, CurDepth, St, Evaluated);
                    if (IsExternalFFI) {
                        StackProv ArgP = evalProvenance(evalProvenance, Arg, "", CurDepth, St);
                        if (ArgP.Depth > 0u) {
                            unsigned RawLoc = Arg->getExprLoc().getRawEncoding();
                            if (EmittedEscapeDiags.insert(RawLoc).second) {
                                if (!ArgP.RootName.empty() && ArgP.RootName != "veriobjc_stack_alloc") {
                                    reportError(Arg->getExprLoc(), "VeriObjC Violation: Banned cross-boundary ownership: data-stack reference '%0' cannot cross an FFI boundary.")
                                        << ArgP.RootName;
                                } else {
                                    reportError(Arg->getExprLoc(), "VeriObjC Violation: Banned cross-boundary ownership: data-stack reference cannot cross an FFI boundary.");
                                }
                            }
                        }
                    }
                    if (i < CalleeFD->getNumParams() && hasVeriObjCAnnotation(CalleeFD->getParamDecl(i), "objc_consumed")) {
                        if (const VarDecl *ConsumedVD = extractVarDecl(Arg)) {
                            consumeVarAndRoots(ConsumedVD, St);
                        }
                    }
                }
                return;
            }
        }

        if (const auto *RS = dyn_cast<ReturnStmt>(S)) {
            if (const Expr *RetE = RS->getRetValue()) {
                self(self, RetE, CurDepth, St, Evaluated);
                StackProv RetP = evalProvenance(evalProvenance, RetE, "", CurDepth, St);
                if (RetP.Depth > 0u) {
                    emitEscapeOnce(RS->getReturnLoc(), RetP, "caller return value");
                }
            }
            return;
        }

        if (const auto *DRE = dyn_cast<DeclRefExpr>(S)) {
            if (!MoveInternalRefs.contains(DRE) && !DirectAssignLHSRefs.contains(DRE)) {
                if (const auto *VD = dyn_cast<VarDecl>(DRE->getDecl())) {
                    checkVarNotConsumed(VD, DRE->getLocation(), /*CheckFreedToo=*/false, St);
                }
            }
            return;
        }

        for (const Stmt *Child : S->children()) {
            self(self, Child, CurDepth, St, Evaluated);
        }
    };

    // 3. Monotone forward worklist iteration over clang::CFG
    unsigned NumBlocks = Cfg->getNumBlockIDs();
    std::vector<CFGDataflowState> InStates(NumBlocks);
    std::vector<CFGDataflowState> OutStates(NumBlocks);

    unsigned EntryID = Cfg->getEntry().getBlockID();
    InStates[EntryID].Initialized = true;
    for (const auto &[VD, Depth] : VarScopeDepth) {
        if (Depth == 0u) {
            InStates[EntryID].Linear[VD] = LinearValState::Live;
            InStates[EntryID].AliasRoots[VD].insert(VD);
        }
    }

    std::deque<const CFGBlock *> Worklist;
    std::vector<uint8_t> InQueue(NumBlocks, 0u);
    Worklist.push_back(&Cfg->getEntry());
    InQueue[EntryID] = 1u;

    while (!Worklist.empty()) {
        const CFGBlock *B = Worklist.front();
        Worklist.pop_front();
        unsigned BID = B->getBlockID();
        InQueue[BID] = 0u;

        CFGDataflowState Cur = InStates[BID];
        if (!Cur.Initialized) continue;

        llvm::DenseSet<const Stmt *> EvaluatedInBlock;
        for (const CFGElement &Elem : *B) {
            if (auto CS = Elem.getAs<CFGStmt>()) {
                const Stmt *S = CS->getStmt();
                transferStmt(transferStmt, S, lookupScopeDepth(S), Cur, EvaluatedInBlock);
            }
        }

        OutStates[BID] = Cur;
        for (auto SuccIt = B->succ_begin(); SuccIt != B->succ_end(); ++SuccIt) {
            const CFGBlock *Succ = *SuccIt;
            if (!Succ) continue;
            unsigned SuccID = Succ->getBlockID();
            if (joinCFGState(InStates[SuccID], Cur) && !InQueue[SuccID]) {
                Worklist.push_back(Succ);
                InQueue[SuccID] = 1u;
            }
        }
    }
}

void VeriObjCVisitor::recordDspGraphForBody(const Decl *OwnerDecl, const Stmt *Body) {
    if (!OwnerDecl || !Body) return;
    const Decl *CanonOwner = OwnerDecl->getCanonicalDecl();

    auto walkDsp = [&](auto &self, const Stmt *S) -> void {
        if (!S) return;
        if (const auto *BO = dyn_cast<BinaryOperator>(S)) {
            BinaryOperator::Opcode Op = BO->getOpcode();
            if (Op == BO_Mul || Op == BO_MulAssign ||
                Op == BO_Div || Op == BO_DivAssign ||
                Op == BO_Rem || Op == BO_RemAssign) {
                Expr::EvalResult ConstRes;
                bool IsConstExpr = !BO->isValueDependent() && BO->EvaluateAsInt(ConstRes, *Context);
                if (!IsConstExpr) {
                    unsigned Line = Context->getSourceManager().getSpellingLineNumber(BO->getOperatorLoc());
                    DirectDspOps[CanonOwner].push_back({
                        BO->getOpcodeStr().str(),
                        Line,
                        BO->getOperatorLoc()
                    });
                }
            }
        } else if (const auto *CE = dyn_cast<CallExpr>(S)) {
            if (const FunctionDecl *CalleeFD = CE->getDirectCallee()) {
                if (!isInRuntimeHeader(CalleeFD->getLocation()) &&
                    !Context->getSourceManager().isInSystemHeader(CalleeFD->getLocation()) &&
                    !hasVeriObjCAnnotation(CalleeFD, "objc_vconstexpr")) {
                    DspCallEdges[CanonOwner].push_back({
                        CalleeFD->getCanonicalDecl(),
                        CE->getExprLoc()
                    });
                }
            }
        } else if (const auto *OME = dyn_cast<ObjCMessageExpr>(S)) {
            if (const ObjCMethodDecl *CalleeMD = OME->getMethodDecl()) {
                if (!isInRuntimeHeader(CalleeMD->getLocation()) &&
                    !Context->getSourceManager().isInSystemHeader(CalleeMD->getLocation())) {
                    DspCallEdges[CanonOwner].push_back({
                        CalleeMD->getCanonicalDecl(),
                        OME->getExprLoc()
                    });
                }
            }
        }
        for (const Stmt *Child : S->children()) {
            self(self, Child);
        }
    };
    walkDsp(walkDsp, Body);
}

void VeriObjCVisitor::registerHwKernelRoot(const Decl *KernelDecl, llvm::StringRef KernelName, SourceLocation Loc) {
    if (!KernelDecl) return;
    HwKernels.push_back({ KernelDecl->getCanonicalDecl(), KernelName.str(), Loc });
}

void VeriObjCVisitor::verifyAllHwKernelsZeroDsp() {
    for (const HwKernelRecord &Kernel : HwKernels) {
        // 1. Direct DSP arithmetic in the kernel itself
        if (auto DirectIt = DirectDspOps.find(Kernel.KernelDecl);
            DirectIt != DirectDspOps.end() && !DirectIt->second.empty()) {
            for (const DspOpSite &Site : DirectIt->second) {
                reportError(
                    Site.Loc,
                    "VeriObjC Violation: Hardware kernel '%0' contains runtime multiplication or division ('%1' at line %2); ternary datapaths must use 0 DSPs."
                ) << Kernel.KernelName << Site.OpStr << Site.Line;
            }
            continue;
        }

        // 2. Transitive call-graph BFS for DSP arithmetic in helper functions/methods
        struct QueueEntry {
            const Decl *Callee = nullptr;
            SourceLocation RootCallLoc;
            std::string Chain;
        };

        std::deque<QueueEntry> Queue;
        llvm::DenseSet<const Decl *> Visited;
        Visited.insert(Kernel.KernelDecl);

        if (auto EdgeIt = DspCallEdges.find(Kernel.KernelDecl); EdgeIt != DspCallEdges.end()) {
            for (const auto &[Callee, CallLoc] : EdgeIt->second) {
                if (Visited.insert(Callee).second) {
                    Queue.push_back({ Callee, CallLoc, getDeclDisplayName(Callee) });
                }
            }
        }

        while (!Queue.empty()) {
            QueueEntry Cur = std::move(Queue.front());
            Queue.pop_front();

            if (auto OpIt = DirectDspOps.find(Cur.Callee); OpIt != DirectDspOps.end() && !OpIt->second.empty()) {
                const DspOpSite &Site = OpIt->second.front();
                reportError(
                    Cur.RootCallLoc,
                    "VeriObjC Violation: Hardware kernel '%0' transitively invokes DSP arithmetic ('%1' in '%2' at line %3 via '%4'); ternary datapaths must use 0 DSPs."
                ) << Kernel.KernelName << Site.OpStr << getDeclDisplayName(Cur.Callee) << Site.Line << Cur.Chain;
                break;
            }

            if (auto NextIt = DspCallEdges.find(Cur.Callee); NextIt != DspCallEdges.end()) {
                for (const auto &[NextCallee, _] : NextIt->second) {
                    if (Visited.insert(NextCallee).second) {
                        Queue.push_back({
                            NextCallee,
                            Cur.RootCallLoc,
                            Cur.Chain + " -> " + getDeclDisplayName(NextCallee)
                        });
                    }
                }
            }
        }
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
