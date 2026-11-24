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

struct LinearTarget {
    const VarDecl *RootVD = nullptr;
    const FieldDecl *Field = nullptr; // nullptr = whole variable; non-null = specific member

    bool operator<(const LinearTarget &O) const {
        if (RootVD != O.RootVD) return RootVD < O.RootVD;
        return Field < O.Field;
    }
    bool operator==(const LinearTarget &O) const {
        return RootVD == O.RootVD && Field == O.Field;
    }
};

std::string formatLinearTargetName(const LinearTarget &T) {
    if (!T.RootVD) return "<unknown>";
    if (!T.Field) return T.RootVD->getNameAsString();
    return T.RootVD->getNameAsString() + "." + T.Field->getNameAsString();
}

LinearTarget extractLinearTarget(const Expr *E) {
    if (!E) return {};
    const Expr *Cur = E->IgnoreParenCasts();
    if (const auto *UO = dyn_cast<UnaryOperator>(Cur); UO && UO->getOpcode() == UO_AddrOf) {
        Cur = UO->getSubExpr()->IgnoreParenCasts();
    }
    if (const auto *DRE = dyn_cast<DeclRefExpr>(Cur)) {
        if (const auto *VD = dyn_cast<VarDecl>(DRE->getDecl())) {
            return { VD, nullptr };
        }
        return {};
    }
    if (const auto *ME = dyn_cast<MemberExpr>(Cur)) {
        const auto *FD = dyn_cast<FieldDecl>(ME->getMemberDecl());
        const Expr *Base = ME->getBase()->IgnoreParenCasts();
        while (Base) {
            if (const auto *AddrUO = dyn_cast<UnaryOperator>(Base);
                AddrUO && (AddrUO->getOpcode() == UO_AddrOf || AddrUO->getOpcode() == UO_Deref)) {
                Base = AddrUO->getSubExpr()->IgnoreParenCasts();
                continue;
            }
            if (const auto *SubME = dyn_cast<MemberExpr>(Base)) {
                Base = SubME->getBase()->IgnoreParenCasts();
                continue;
            }
            if (const auto *DRE = dyn_cast<DeclRefExpr>(Base)) {
                if (const auto *VD = dyn_cast<VarDecl>(DRE->getDecl())) {
                    return { VD, FD };
                }
            }
            break;
        }
    }
    return {};
}

void collectSubtreeStmts(const Stmt *S, llvm::DenseSet<const Stmt *> &Out) {
    if (!S) return;
    Out.insert(S);
    for (const Stmt *Child : S->children()) {
        collectSubtreeStmts(Child, Out);
    }
}

struct CFGDataflowState {
    bool Initialized = false;
    llvm::DenseMap<const VarDecl *, LinearValState> Linear;
    std::map<LinearTarget, LinearValState> FieldLinear;
    llvm::DenseMap<const VarDecl *, LinearValState> PoolFreed;
    std::map<const VarDecl *, std::set<const VarDecl *>> AliasRoots;
    std::map<const VarDecl *, std::set<LinearTarget>> AliasTargets;
    llvm::DenseMap<const VarDecl *, StackProv> Prov;
    llvm::DenseMap<const VarDecl *, StackProv> PointeeProv;
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
    for (const auto &[Tgt, SrcFSt] : Src.FieldLinear) {
        auto It = Dst.FieldLinear.find(Tgt);
        LinearValState Old = (It != Dst.FieldLinear.end()) ? It->second : LinearValState::Unreached;
        LinearValState Joined = joinLinearState(Old, SrcFSt);
        if (Joined != Old) {
            Dst.FieldLinear[Tgt] = Joined;
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
    for (const auto &[VD, Tgts] : Src.AliasTargets) {
        std::set<LinearTarget> &DstTgts = Dst.AliasTargets[VD];
        for (const LinearTarget &T : Tgts) {
            if (DstTgts.insert(T).second) {
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
    for (const auto &[VD, SrcPP] : Src.PointeeProv) {
        auto It = Dst.PointeeProv.find(VD);
        if (It == Dst.PointeeProv.end() || SrcPP.Depth > It->second.Depth) {
            Dst.PointeeProv[VD] = SrcPP;
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
            const Expr *NextBase = ME->getBase()->IgnoreParenCasts();
            if (ME->isArrow()) {
                if (const auto *AddrUO = dyn_cast<UnaryOperator>(NextBase);
                    AddrUO && AddrUO->getOpcode() == UO_AddrOf) {
                    Cur = AddrUO->getSubExpr()->IgnoreParenCasts();
                    continue;
                }
                Info.DereferencedPointer = true;
            }
            Cur = NextBase;
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

VeriObjCVisitor::HelperParamSummary VeriObjCVisitor::getHelperParamSummary(
    const FunctionDecl *FD,
    llvm::DenseSet<const FunctionDecl *> &Visiting
) {
    HelperParamSummary Summary;
    if (!FD) return Summary;
    const FunctionDecl *CanonFD = FD->getCanonicalDecl();
    if (auto It = HelperSummaryCache.find(CanonFD); It != HelperSummaryCache.end()) {
        return It->second;
    }

    const FunctionDecl *DefFD = FD->getDefinition();
    if (!DefFD || !DefFD->hasBody()) {
        if (isInRuntimeHeader(FD->getLocation())) {
            if (canTypeCarryStackReference(FD->getReturnType())) {
                for (unsigned i = 0; i < FD->getNumParams(); ++i) {
                    if (canTypeCarryStackReference(FD->getParamDecl(i)->getType())) {
                        Summary.ReturnedParams.push_back(i);
                    }
                }
            }
            if (FD->getName() == "__vspan_init_raw" && FD->getNumParams() >= 3) {
                Summary.OutParamSinks.push_back({ 2u, 0u });
            }
        }
        HelperSummaryCache[CanonFD] = Summary;
        return Summary;
    }

    if (!Visiting.insert(CanonFD).second) {
        return Summary;
    }

    // Track local aliases inside helper body from parameters -> local vars -> return/global/out-param sinks
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
        if (const auto *CLE = dyn_cast<CompoundLiteralExpr>(E)) {
            self(self, CLE->getInitializer(), Out);
            return;
        }
        if (const auto *CE = dyn_cast<CallExpr>(E)) {
            if (const FunctionDecl *Callee = CE->getDirectCallee()) {
                HelperParamSummary Sub = getHelperParamSummary(Callee, Visiting);
                for (unsigned Idx : Sub.ReturnedParams) {
                    if (Idx < CE->getNumArgs()) {
                        self(self, CE->getArg(Idx), Out);
                    }
                }
            }
            return;
        }
    };

    std::set<unsigned> ReturnedParams;
    std::set<std::pair<unsigned, std::string>> GlobalSinks;
    std::set<std::pair<unsigned, unsigned>> OutParamSinks;

    auto recordSinkFromLHS = [&](const Expr *LhsExpr, const std::set<unsigned> &RhsTaint) {
        if (!LhsExpr || RhsTaint.empty()) return;
        SinkBaseInfo Sink = inspectAssignmentLHS(LhsExpr);
        if (!Sink.BaseVD) return;
        if (Sink.BaseVD->hasGlobalStorage()) {
            for (unsigned Idx : RhsTaint) {
                GlobalSinks.emplace(Idx, Sink.BaseVD->getNameAsString());
            }
        } else if (Sink.DereferencedPointer) {
            if (auto DstIt = VarParamTaint.find(Sink.BaseVD); DstIt != VarParamTaint.end()) {
                for (unsigned SrcIdx : RhsTaint) {
                    for (unsigned DstIdx : DstIt->second) {
                        if (SrcIdx != DstIdx) {
                            OutParamSinks.emplace(SrcIdx, DstIdx);
                        }
                    }
                }
            }
        } else {
            VarParamTaint[Sink.BaseVD].insert(RhsTaint.begin(), RhsTaint.end());
        }
    };

    auto walkHelperStmt = [&](auto &self, const Stmt *S) -> void {
        if (!S) return;
        if (const auto *DS = dyn_cast<DeclStmt>(S)) {
            for (const Decl *D : DS->decls()) {
                if (const auto *VD = dyn_cast<VarDecl>(D); VD && VD->getInit()) {
                    evalExprParamTaint(evalExprParamTaint, VD->getInit(), VarParamTaint[VD]);
                }
            }
        } else if (const auto *BO = dyn_cast<BinaryOperator>(S); BO && BO->isAssignmentOp()) {
            std::set<unsigned> RhsTaint;
            evalExprParamTaint(evalExprParamTaint, BO->getRHS(), RhsTaint);
            recordSinkFromLHS(BO->getLHS(), RhsTaint);
        } else if (const auto *CE = dyn_cast<CallExpr>(S)) {
            if (const FunctionDecl *Callee = CE->getDirectCallee()) {
                HelperParamSummary Sub = getHelperParamSummary(Callee, Visiting);
                for (const auto &[SubParam, GlobName] : Sub.GlobalSinks) {
                    if (SubParam < CE->getNumArgs()) {
                        std::set<unsigned> ArgTaint;
                        evalExprParamTaint(evalExprParamTaint, CE->getArg(SubParam), ArgTaint);
                        for (unsigned Idx : ArgTaint) {
                            GlobalSinks.emplace(Idx, GlobName);
                        }
                    }
                }
                for (const auto &[SubSrc, SubDst] : Sub.OutParamSinks) {
                    if (SubSrc < CE->getNumArgs() && SubDst < CE->getNumArgs()) {
                        std::set<unsigned> SrcTaint;
                        evalExprParamTaint(evalExprParamTaint, CE->getArg(SubSrc), SrcTaint);
                        if (SrcTaint.empty()) continue;
                        const Expr *DstArg = CE->getArg(SubDst)->IgnoreParenCasts();
                        if (const auto *AddrUO = dyn_cast<UnaryOperator>(DstArg);
                            AddrUO && AddrUO->getOpcode() == UO_AddrOf) {
                            recordSinkFromLHS(AddrUO->getSubExpr(), SrcTaint);
                        } else {
                            std::set<unsigned> DstTaint;
                            evalExprParamTaint(evalExprParamTaint, DstArg, DstTaint);
                            for (unsigned SIdx : SrcTaint) {
                                for (unsigned DIdx : DstTaint) {
                                    if (SIdx != DIdx) {
                                        OutParamSinks.emplace(SIdx, DIdx);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        } else if (const auto *RS = dyn_cast<ReturnStmt>(S)) {
            if (canTypeCarryStackReference(DefFD->getReturnType())) {
                evalExprParamTaint(evalExprParamTaint, RS->getRetValue(), ReturnedParams);
            }
        }
        for (const Stmt *Child : S->children()) {
            self(self, Child);
        }
    };

    walkHelperStmt(walkHelperStmt, DefFD->getBody());
    Visiting.erase(CanonFD);

    for (unsigned Idx : ReturnedParams) {
        Summary.ReturnedParams.push_back(Idx);
    }
    for (const auto &GS : GlobalSinks) {
        Summary.GlobalSinks.push_back(GS);
    }
    for (const auto &OPS : OutParamSinks) {
        Summary.OutParamSinks.push_back(OPS);
    }
    HelperSummaryCache[CanonFD] = Summary;
    return Summary;
}

void VeriObjCVisitor::analyzeFunctionCFGDataflow(Stmt *Body, DeclContext *DC) {
    if (!Body || !DC) return;
    auto *OwnerDecl = dyn_cast<Decl>(DC);
    if (!OwnerDecl) return;

    std::string FnName = getEnclosingCallableName(DC);

    // 1. Pre-pass: assign lexical scope depths and identify veriobjc_move() internal AST nodes
    llvm::DenseMap<const Stmt *, unsigned> StmtScopeDepth;
    llvm::DenseMap<const VarDecl *, unsigned> VarScopeDepth;
    std::map<const CallExpr *, LinearTarget> MoveMarkerCalls;
    llvm::DenseSet<const Stmt *> MoveInternalStmts;
    llvm::DenseSet<const DeclRefExpr *> DirectAssignLHSRefs;
    llvm::DenseSet<const MemberExpr *> DirectAssignLHSMembers;
    llvm::DenseSet<const DeclRefExpr *> MemberBaseRefs;

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

        if (const auto *ME = dyn_cast<MemberExpr>(S)) {
            if (const auto *BaseDRE = dyn_cast<DeclRefExpr>(ME->getBase()->IgnoreParenCasts())) {
                MemberBaseRefs.insert(BaseDRE);
            }
        }

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
                        LinearTarget MovedTgt = extractLinearTarget(CE->getArg(0));
                        if (MovedTgt.RootVD) {
                            MoveMarkerCalls[CE] = MovedTgt;
                            collectSubtreeStmts(CE->getArg(0), MoveInternalStmts);
                            collectSubtreeStmts(BO->getRHS(), MoveInternalStmts);
                        }
                    }
                }
            } else if (BO->getOpcode() == BO_Assign) {
                const Expr *LhsStripped = BO->getLHS()->IgnoreParenCasts();
                if (const auto *LhsDRE = dyn_cast<DeclRefExpr>(LhsStripped)) {
                    DirectAssignLHSRefs.insert(LhsDRE);
                } else if (const auto *LhsME = dyn_cast<MemberExpr>(LhsStripped)) {
                    DirectAssignLHSMembers.insert(LhsME);
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

    std::set<std::pair<unsigned, std::string>> EmittedMoveDiags;
    std::set<unsigned> EmittedEscapeDiags;

    auto emitConsumedOnce = [&](SourceLocation Loc, llvm::StringRef UsedName, llvm::StringRef RootName, LinearValState StVal) {
        unsigned RawLoc = Loc.getRawEncoding();
        if (EmittedMoveDiags.emplace(RawLoc, UsedName.str()).second) {
            reportConsumedVar(Loc, UsedName, RootName, linearStateName(StVal), FnName);
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
            if (ME->isArrow()) {
                const Expr *ArrowBase = ME->getBase()->IgnoreParenCasts();
                if (const auto *AddrUO = dyn_cast<UnaryOperator>(ArrowBase);
                    AddrUO && AddrUO->getOpcode() == UO_AddrOf) {
                    return self(self, AddrUO->getSubExpr(), HintRoot, CurDepth, St);
                }
                if (ME->getMemberDecl()->getType()->isArrayType()) {
                    return self(self, ME->getBase(), HintRoot, CurDepth, St);
                }
                if (const VarDecl *ArrowVD = extractVarDecl(ArrowBase)) {
                    return St.PointeeProv.lookup(ArrowVD);
                }
                return {};
            }
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

        if (const auto *CLE = dyn_cast<CompoundLiteralExpr>(Stripped)) {
            return self(self, CLE->getInitializer(), HintRoot, CurDepth, St);
        }

        if (const auto *CE = dyn_cast<CallExpr>(Stripped)) {
            if (const FunctionDecl *Callee = CE->getDirectCallee()) {
                llvm::DenseSet<const FunctionDecl *> Visiting;
                HelperParamSummary Sub = getHelperParamSummary(Callee, Visiting);
                StackProv Acc;
                for (unsigned Idx : Sub.ReturnedParams) {
                    if (Idx < CE->getNumArgs()) {
                        Acc = joinStackProv(Acc, self(self, CE->getArg(Idx), HintRoot, CurDepth, St));
                    }
                }
                return Acc;
            }
        }

        return {};
    };

    auto checkVarNotConsumed = [&](const VarDecl *VD, SourceLocation Loc, bool CheckFreedToo, bool WholeStructUse, const CFGDataflowState &St) -> bool {
        if (!VD) return true;
        if (auto TgtsIt = St.AliasTargets.find(VD); TgtsIt != St.AliasTargets.end() && !TgtsIt->second.empty()) {
            for (const LinearTarget &Tgt : TgtsIt->second) {
                if (!Tgt.RootVD) continue;
                LinearValState RSt = St.Linear.lookup(Tgt.RootVD);
                if (RSt == LinearValState::Moved || RSt == LinearValState::MaybeMoved) {
                    emitConsumedOnce(Loc, VD->getName(), Tgt.RootVD->getName(), RSt);
                    return false;
                }
                if (Tgt.Field) {
                    auto FIt = St.FieldLinear.find(Tgt);
                    LinearValState FSt = (FIt != St.FieldLinear.end()) ? FIt->second : LinearValState::Unreached;
                    if (FSt == LinearValState::Moved || FSt == LinearValState::MaybeMoved) {
                        emitConsumedOnce(Loc, VD->getName(), formatLinearTargetName(Tgt), FSt);
                        return false;
                    }
                }
            }
        }
        if (auto RootsIt = St.AliasRoots.find(VD); RootsIt != St.AliasRoots.end() && !RootsIt->second.empty()) {
            for (const VarDecl *R : RootsIt->second) {
                LinearValState RSt = St.Linear.lookup(R);
                if (RSt == LinearValState::Moved || RSt == LinearValState::MaybeMoved) {
                    emitConsumedOnce(Loc, VD->getName(), R->getName(), RSt);
                    return false;
                }
                if (CheckFreedToo) {
                    LinearValState FSt = St.PoolFreed.lookup(R);
                    if (FSt == LinearValState::Moved || FSt == LinearValState::MaybeMoved) {
                        emitConsumedOnce(Loc, VD->getName(), R->getName(), FSt);
                        return false;
                    }
                }
            }
        }
        LinearValState SelfSt = St.Linear.lookup(VD);
        if (SelfSt == LinearValState::Moved || SelfSt == LinearValState::MaybeMoved) {
            emitConsumedOnce(Loc, VD->getName(), VD->getName(), SelfSt);
            return false;
        }
        if (WholeStructUse) {
            for (const auto &[Tgt, FSt] : St.FieldLinear) {
                if (Tgt.RootVD == VD && (FSt == LinearValState::Moved || FSt == LinearValState::MaybeMoved)) {
                    emitConsumedOnce(Loc, VD->getName(), formatLinearTargetName(Tgt), FSt);
                    return false;
                }
            }
        }
        if (CheckFreedToo) {
            LinearValState SelfFr = St.PoolFreed.lookup(VD);
            if (SelfFr == LinearValState::Moved || SelfFr == LinearValState::MaybeMoved) {
                emitConsumedOnce(Loc, VD->getName(), VD->getName(), SelfFr);
                return false;
            }
        }
        return true;
    };

    auto checkTargetNotConsumed = [&](const LinearTarget &Tgt, SourceLocation Loc, const CFGDataflowState &St) -> bool {
        if (!Tgt.RootVD) return true;
        if (!Tgt.Field) {
            return checkVarNotConsumed(Tgt.RootVD, Loc, /*CheckFreedToo=*/true, /*WholeStructUse=*/true, St);
        }
        if (!checkVarNotConsumed(Tgt.RootVD, Loc, /*CheckFreedToo=*/false, /*WholeStructUse=*/false, St)) {
            return false;
        }
        auto FIt = St.FieldLinear.find(Tgt);
        LinearValState FSt = (FIt != St.FieldLinear.end()) ? FIt->second : LinearValState::Unreached;
        if (FSt == LinearValState::Moved || FSt == LinearValState::MaybeMoved) {
            std::string TgtStr = formatLinearTargetName(Tgt);
            emitConsumedOnce(Loc, TgtStr, TgtStr, FSt);
            return false;
        }
        return true;
    };

    auto consumeTargetAndRoots = [&](const LinearTarget &Tgt, CFGDataflowState &St) {
        if (!Tgt.RootVD) return;
        if (!Tgt.Field) {
            St.Linear[Tgt.RootVD] = LinearValState::Moved;
            auto It = St.AliasRoots.find(Tgt.RootVD);
            if (It != St.AliasRoots.end()) {
                for (const VarDecl *R : It->second) {
                    St.Linear[R] = LinearValState::Moved;
                }
            }
            if (auto TIt = St.AliasTargets.find(Tgt.RootVD); TIt != St.AliasTargets.end()) {
                for (const LinearTarget &AT : TIt->second) {
                    if (AT.Field) {
                        St.FieldLinear[AT] = LinearValState::Moved;
                    } else if (AT.RootVD) {
                        St.Linear[AT.RootVD] = LinearValState::Moved;
                    }
                }
            }
        } else {
            St.FieldLinear[Tgt] = LinearValState::Moved;
            if (auto It = St.AliasRoots.find(Tgt.RootVD); It != St.AliasRoots.end()) {
                for (const VarDecl *R : It->second) {
                    St.FieldLinear[{ R, Tgt.Field }] = LinearValState::Moved;
                }
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
        for (auto &[Tgt, FSt] : St.FieldLinear) {
            if (Tgt.RootVD == DstVD) {
                FSt = LinearValState::Live;
            }
        }
        St.PoolFreed[DstVD] = LinearValState::Live;
        St.AliasRoots[DstVD].clear();
        St.AliasTargets[DstVD].clear();

        LinearTarget InitTgt = InitExpr ? extractLinearTarget(InitExpr) : LinearTarget{};
        const VarDecl *SrcVD = InitExpr ? extractVarDecl(InitExpr) : nullptr;
        if (InitTgt.RootVD && (DstVD->getType()->isPointerType() || DstVD->getType()->isObjCObjectPointerType())) {
            if (!InitTgt.Field) {
                auto SrcRootsIt = St.AliasRoots.find(InitTgt.RootVD);
                if (SrcRootsIt != St.AliasRoots.end() && !SrcRootsIt->second.empty()) {
                    St.AliasRoots[DstVD] = SrcRootsIt->second;
                } else {
                    St.AliasRoots[DstVD].insert(InitTgt.RootVD);
                }
                auto SrcTgtsIt = St.AliasTargets.find(InitTgt.RootVD);
                if (SrcTgtsIt != St.AliasTargets.end() && !SrcTgtsIt->second.empty()) {
                    St.AliasTargets[DstVD] = SrcTgtsIt->second;
                } else {
                    St.AliasTargets[DstVD].insert(InitTgt);
                }
            } else {
                St.AliasRoots[DstVD].insert(DstVD);
                auto SrcRootsIt = St.AliasRoots.find(InitTgt.RootVD);
                if (SrcRootsIt != St.AliasRoots.end() && !SrcRootsIt->second.empty()) {
                    for (const VarDecl *R : SrcRootsIt->second) {
                        St.AliasTargets[DstVD].insert({ R, InitTgt.Field });
                    }
                } else {
                    St.AliasTargets[DstVD].insert(InitTgt);
                }
            }
            if (SrcVD) {
                StackProv SrcPP = St.PointeeProv.lookup(SrcVD);
                if (SrcPP.Depth > 0u) {
                    St.PointeeProv[DstVD] = SrcPP;
                } else {
                    St.PointeeProv.erase(DstVD);
                }
            } else {
                St.PointeeProv.erase(DstVD);
            }
        } else {
            St.AliasRoots[DstVD].insert(DstVD);
            St.AliasTargets[DstVD].insert({ DstVD, nullptr });
            St.PointeeProv.erase(DstVD);
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

    auto checkAndRecordLhsSink = [&](const Expr *LhsExpr, const StackProv &SrcP, SourceLocation DiagLoc, CFGDataflowState &St) {
        if (!LhsExpr || SrcP.Depth == 0u) return;
        SinkBaseInfo SinkInfo = inspectAssignmentLHS(LhsExpr);
        const VarDecl *BaseVD = SinkInfo.BaseVD;
        if (!BaseVD) return;

        if (BaseVD->hasGlobalStorage()) {
            std::string Sink = "global '" + BaseVD->getNameAsString() + "'";
            emitEscapeOnce(DiagLoc, SrcP, Sink);
        } else if (isa<ParmVarDecl>(BaseVD) && SinkInfo.DereferencedPointer) {
            std::string Sink = SinkInfo.FieldName.empty()
                ? ("out-parameter '*" + BaseVD->getNameAsString() + "'")
                : ("out-parameter field '" + BaseVD->getNameAsString() + "->" + SinkInfo.FieldName + "'");
            emitEscapeOnce(DiagLoc, SrcP, Sink);
        } else if (SinkInfo.DereferencedPointer) {
            unsigned TargetDepth = St.Prov.lookup(BaseVD).Depth;
            if (TargetDepth < SrcP.Depth) {
                std::string Sink = SinkInfo.FieldName.empty()
                    ? ("outer pointer '*" + BaseVD->getNameAsString() + "'")
                    : ("outer field '" + BaseVD->getNameAsString() + "->" + SinkInfo.FieldName + "'");
                emitEscapeOnce(DiagLoc, SrcP, Sink);
            } else {
                St.PointeeProv[BaseVD] = joinStackProv(St.PointeeProv.lookup(BaseVD), SrcP);
            }
        } else {
            unsigned HolderDepth = VarScopeDepth.lookup(BaseVD);
            if (HolderDepth < SrcP.Depth) {
                std::string Sink = SinkInfo.FieldName.empty()
                    ? ("outer variable '" + BaseVD->getNameAsString() + "'")
                    : ("outer field '" + BaseVD->getNameAsString() + "." + SinkInfo.FieldName + "'");
                emitEscapeOnce(DiagLoc, SrcP, Sink);
            } else {
                St.Prov[BaseVD] = joinStackProv(St.Prov.lookup(BaseVD), SrcP);
            }
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

                if (const auto *LhsME = dyn_cast<MemberExpr>(BO->getLHS()->IgnoreParenCasts())) {
                    LinearTarget LhsTgt = extractLinearTarget(LhsME);
                    if (LhsTgt.RootVD && LhsTgt.Field) {
                        St.FieldLinear[LhsTgt] = LinearValState::Live;
                    }
                }
                self(self, BO->getLHS(), CurDepth, St, Evaluated);
                checkAndRecordLhsSink(BO->getLHS(), SrcP, BO->getExprLoc(), St);
                return;
            }
        }

        if (const auto *CE = dyn_cast<CallExpr>(S)) {
            if (auto MoveIt = MoveMarkerCalls.find(CE); MoveIt != MoveMarkerCalls.end()) {
                const LinearTarget &MovedTgt = MoveIt->second;
                if (checkTargetNotConsumed(MovedTgt, CE->getExprLoc(), St)) {
                    consumeTargetAndRoots(MovedTgt, St);
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
                        if (checkVarNotConsumed(FreedVD, CE->getArg(1)->getExprLoc(), /*CheckFreedToo=*/true, /*WholeStructUse=*/true, St)) {
                            markFreedVarAndRoots(FreedVD, St);
                        }
                    }
                    return;
                }

                if (FnCallee == "__vspan_init_raw" && CE->getNumArgs() >= 3) {
                    for (unsigned i = 0; i < CE->getNumArgs(); ++i) {
                        self(self, CE->getArg(i), CurDepth, St, Evaluated);
                    }
                    StackProv SrcP = evalProvenance(evalProvenance, CE->getArg(2), "", CurDepth, St);
                    const Expr *OutPtrArg = CE->getArg(0)->IgnoreParenCasts();
                    if (const auto *AddrUO = dyn_cast<UnaryOperator>(OutPtrArg);
                        AddrUO && AddrUO->getOpcode() == UO_AddrOf) {
                        checkAndRecordLhsSink(AddrUO->getSubExpr(), SrcP, CE->getExprLoc(), St);
                    }
                    return;
                }

                // FFI cross-boundary check on bodyless external functions
                bool IsExternalFFI = !CalleeFD->hasBody() && !isInRuntimeHeader(CalleeFD->getLocation());
                llvm::SmallVector<StackProv, 4> ArgProvs(CE->getNumArgs());
                for (unsigned i = 0; i < CE->getNumArgs(); ++i) {
                    const Expr *Arg = CE->getArg(i);
                    self(self, Arg, CurDepth, St, Evaluated);
                    ArgProvs[i] = evalProvenance(evalProvenance, Arg, "", CurDepth, St);
                    if (IsExternalFFI) {
                        const StackProv &ArgP = ArgProvs[i];
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
                        LinearTarget ConsumedTgt = extractLinearTarget(Arg);
                        if (ConsumedTgt.RootVD) {
                            consumeTargetAndRoots(ConsumedTgt, St);
                        }
                    }
                }
                if (CalleeFD->hasBody() || isInRuntimeHeader(CalleeFD->getLocation())) {
                    llvm::DenseSet<const FunctionDecl *> Visiting;
                    HelperParamSummary HelperSum = getHelperParamSummary(CalleeFD, Visiting);

                    for (const auto &[ParamIdx, GlobName] : HelperSum.GlobalSinks) {
                        if (ParamIdx < CE->getNumArgs() && ArgProvs[ParamIdx].Depth > 0u) {
                            std::string SinkDesc = "global '" + GlobName + "' via helper '" + CalleeFD->getNameAsString() + "'";
                            emitEscapeOnce(CE->getArg(ParamIdx)->getExprLoc(), ArgProvs[ParamIdx], SinkDesc);
                        }
                    }
                    for (const auto &[SrcIdx, DstIdx] : HelperSum.OutParamSinks) {
                        if (SrcIdx >= CE->getNumArgs() || DstIdx >= CE->getNumArgs() || ArgProvs[SrcIdx].Depth == 0u) {
                            continue;
                        }
                        const StackProv &SrcP = ArgProvs[SrcIdx];
                        const Expr *DstArg = CE->getArg(DstIdx)->IgnoreParenCasts();
                        if (const auto *AddrUO = dyn_cast<UnaryOperator>(DstArg);
                            AddrUO && AddrUO->getOpcode() == UO_AddrOf) {
                            checkAndRecordLhsSink(AddrUO->getSubExpr(), SrcP, CE->getExprLoc(), St);
                        } else if (const VarDecl *DstPtrVD = extractVarDecl(DstArg)) {
                            if (DstPtrVD->hasGlobalStorage()) {
                                std::string SinkDesc = "global pointer '*" + DstPtrVD->getNameAsString() + "' via helper '" + CalleeFD->getNameAsString() + "'";
                                emitEscapeOnce(CE->getExprLoc(), SrcP, SinkDesc);
                            } else if (isa<ParmVarDecl>(DstPtrVD)) {
                                std::string SinkDesc = "out-parameter '*" + DstPtrVD->getNameAsString() + "' via helper '" + CalleeFD->getNameAsString() + "'";
                                emitEscapeOnce(CE->getExprLoc(), SrcP, SinkDesc);
                            } else {
                                unsigned TargetDepth = St.Prov.lookup(DstPtrVD).Depth;
                                if (TargetDepth < SrcP.Depth) {
                                    std::string SinkDesc = "outer pointer '*" + DstPtrVD->getNameAsString() + "' via helper '" + CalleeFD->getNameAsString() + "'";
                                    emitEscapeOnce(CE->getExprLoc(), SrcP, SinkDesc);
                                } else {
                                    St.PointeeProv[DstPtrVD] = joinStackProv(St.PointeeProv.lookup(DstPtrVD), SrcP);
                                }
                            }
                        }
                    }

                    if (CalleeFD->hasBody()) {
                        for (unsigned i = 0; i < CE->getNumArgs() && i < CalleeFD->getNumParams(); ++i) {
                            QualType PT = CalleeFD->getParamDecl(i)->getType();
                            if (!PT->isPointerType() || PT->getPointeeType().isConstQualified() ||
                                !canTypeCarryStackReference(PT->getPointeeType())) {
                                continue;
                            }
                            const VarDecl *OutPtrVD = extractVarDecl(CE->getArg(i));
                            if (!OutPtrVD) continue;
                            for (unsigned j = 0; j < CE->getNumArgs(); ++j) {
                                if (j == i || ArgProvs[j].Depth == 0u) continue;
                                St.PointeeProv[OutPtrVD] = joinStackProv(St.PointeeProv.lookup(OutPtrVD), ArgProvs[j]);
                            }
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

        if (const auto *ME = dyn_cast<MemberExpr>(S)) {
            if (!MoveInternalStmts.contains(ME) && !DirectAssignLHSMembers.contains(ME)) {
                LinearTarget Tgt = extractLinearTarget(ME);
                if (Tgt.RootVD && Tgt.Field) {
                    checkTargetNotConsumed(Tgt, ME->getMemberLoc(), St);
                }
            }
            for (const Stmt *Child : S->children()) {
                self(self, Child, CurDepth, St, Evaluated);
            }
            return;
        }

        if (const auto *DRE = dyn_cast<DeclRefExpr>(S)) {
            if (!MoveInternalStmts.contains(DRE) && !DirectAssignLHSRefs.contains(DRE)) {
                if (const auto *VD = dyn_cast<VarDecl>(DRE->getDecl())) {
                    bool WholeStruct = !MemberBaseRefs.contains(DRE);
                    checkVarNotConsumed(VD, DRE->getLocation(), /*CheckFreedToo=*/false, WholeStruct, St);
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
            InStates[EntryID].AliasTargets[VD].insert({ VD, nullptr });
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
