#include "veriobjc/VeriObjCVisitor.h"

#include <cstring>

using namespace clang;

namespace veriobjc {

const FieldDecl *VeriObjCVisitor::findNonFlatField(QualType T) const {
    if (T.isNull()) return nullptr;
    while (const auto *AT = Context->getAsArrayType(T)) T = AT->getElementType();

    const auto *RT = T->getAs<RecordType>();
    const RecordDecl *RD = RT ? RT->getDecl()->getDefinition() : nullptr;
    if (!RD) return nullptr;

    for (const FieldDecl *FD : RD->fields()) {
        QualType FT = FD->getType();
        while (const auto *FAT = Context->getAsArrayType(FT)) FT = FAT->getElementType();
        if (FT->isPointerType() || FT->isObjCObjectPointerType() ||
            FT->isBlockPointerType() || FT->isReferenceType()) {
            return FD;
        }
        if (const FieldDecl *NestedBad = findNonFlatField(FT)) return NestedBad;
    }
    return nullptr;
}

void VeriObjCVisitor::checkFlatPayloadType(QualType T, llvm::StringRef TypeName, SourceLocation Loc) {
    if (const FieldDecl *BadFD = findNonFlatField(T)) {
        reportError(Loc, "VeriObjC Violation: Non-flat channel payload type '%0' contains pointer or reference field '%1'. Cross-core channels require flat values.")
            << TypeName << BadFD->getName();
    }
}

const VarDecl *VeriObjCVisitor::extractVResultIsOkVar(const Expr *Cond) {
    if (!Cond) return nullptr;
    Cond = Cond->IgnoreParenImpCasts();
    if (const auto *ME = dyn_cast<MemberExpr>(Cond)) {
        if (ME->getMemberDecl()->getName() == "is_ok") {
            return extractVarDecl(ME->getBase());
        }
    }
    return nullptr;
}

bool VeriObjCVisitor::containsVAsyncReadyCall(const Stmt *S) {
    if (!S) return false;
    if (const auto *CE = dyn_cast<CallExpr>(S)) {
        if (const FunctionDecl *Callee = CE->getDirectCallee();
            Callee && Callee->getName() == "__vasync_task_is_ready_raw") {
            return true;
        }
    }
    for (const Stmt *Child : S->children()) {
        if (containsVAsyncReadyCall(Child)) return true;
    }
    return false;
}

bool VeriObjCVisitor::isRawSimdIntrinsicName(llvm::StringRef Name) {
    return Name.starts_with("_mm_") ||
           Name.starts_with("_mm256_") ||
           Name.starts_with("_mm512_") ||
           Name.starts_with("__builtin_ia32_") ||
           Name.starts_with("vaddq_");
}

void VeriObjCVisitor::analyzeVResultAndStackArrays(
    const Stmt *S,
    llvm::StringRef EnclosingFnName,
    bool IsNoLatencyFn,
    llvm::DenseSet<const VarDecl *> &ProvenOkVars,
    llvm::DenseMap<const VarDecl *, const VarDecl *> &ActiveStackArray,
    llvm::DenseMap<const VarDecl *, uint32_t> &VVectorPushCounts,
    llvm::DenseMap<const VarDecl *, uint32_t> &VHashInsertCounts,
    llvm::DenseMap<const VarDecl *, uint32_t> &VDmaRingCounts
) {
    if (!S) return;

    if (const auto *WS = dyn_cast<WhileStmt>(S)) {
        if (containsVAsyncReadyCall(WS->getCond())) {
            reportError(WS->getWhileLoc(), "VeriObjC Violation: Banned polling ring in vasync pipeline: wake-up must be interrupt-driven without busy-wait loops.");
        }
    }

    if (const auto *IS = dyn_cast<IfStmt>(S)) {
        analyzeVResultAndStackArrays(
            IS->getCond(), EnclosingFnName, IsNoLatencyFn, ProvenOkVars, ActiveStackArray,
            VVectorPushCounts, VHashInsertCounts, VDmaRingCounts);
        const VarDecl *ProvenVD = extractVResultIsOkVar(IS->getCond());
        if (ProvenVD) ProvenOkVars.insert(ProvenVD);
        analyzeVResultAndStackArrays(
            IS->getThen(), EnclosingFnName, IsNoLatencyFn, ProvenOkVars, ActiveStackArray,
            VVectorPushCounts, VHashInsertCounts, VDmaRingCounts);
        if (ProvenVD) ProvenOkVars.erase(ProvenVD);
        if (IS->getElse()) {
            analyzeVResultAndStackArrays(
                IS->getElse(), EnclosingFnName, IsNoLatencyFn, ProvenOkVars, ActiveStackArray,
                VVectorPushCounts, VHashInsertCounts, VDmaRingCounts);
        }
        return;
    }

    if (const auto *ME = dyn_cast<MemberExpr>(S)) {
        llvm::StringRef MemName = ME->getMemberDecl()->getName();
        if (MemName == "value") {
            const auto *RT = ME->getBase()->IgnoreParenImpCasts()->getType()->getAs<RecordType>();
            const RecordDecl *RD = RT ? RT->getDecl()->getDefinition() : nullptr;
            if (RD && hasVeriObjCAnnotation(RD, "objc_vresult")) {
                const VarDecl *BaseVD = extractVarDecl(ME->getBase());
                if (!BaseVD || !ProvenOkVars.contains(BaseVD)) {
                    reportError(ME->getMemberLoc(), "VeriObjC Violation: Unchecked vresult access on '%0': '.value' may only be accessed after proving '.is_ok' in an enclosing branch.")
                        << (BaseVD ? BaseVD->getName() : "vresult");
                }
            }
        } else if (MemName == "payload_DoNotAccessDirectly") {
            reportError(ME->getMemberLoc(), "VeriObjC Violation: Direct vatomic payload access without linear acquire guard.");
        }
    }

    if (const auto *OME = dyn_cast<ObjCMessageExpr>(S)) {
        if (OME->isInstanceMessage() && OME->getSelector().getAsString() == "push:") {
            if (const VarDecl *VecVD = extractVarDecl(OME->getInstanceReceiver())) {
                const ObjCInterfaceDecl *ID = OME->getReceiverInterface();
                uint32_t Cap = 0;
                if (extractAnnotatedCapacity(ID, "objc_vvector:", Cap)) {
                    uint32_t Count = ++VVectorPushCounts[VecVD];
                    if (Count > Cap) {
                        reportError(OME->getExprLoc(), "VeriObjC Violation: vvector push exceeds static capacity %0 on '%1'.")
                            << Cap << VecVD->getName();
                    }
                }
            }
        }
    }

    if (const auto *CE = dyn_cast<CallExpr>(S)) {
        const FunctionDecl *CalleeFD = CE->getDirectCallee();
        if (CalleeFD) {
            llvm::StringRef CalleeName = CalleeFD->getName();
            if (IsNoLatencyFn && isRawSimdIntrinsicName(CalleeName)) {
                reportError(CE->getExprLoc(), "VeriObjC Violation: Raw SIMD intrinsic in @nolatency code is banned: '%0'; use <vstd/simd.h>.")
                    << CalleeName;
            }
            if (CalleeName == "__vstack_array_init_raw" && CE->getNumArgs() >= 5) {
                const VarDecl *ArrVD = extractVarDecl(CE->getArg(0));
                const VarDecl *StackVD = extractVarDecl(CE->getArg(4));
                if (ArrVD && StackVD) {
                    auto It = ActiveStackArray.find(StackVD);
                    if (It != ActiveStackArray.end() && It->second != ArrVD) {
                        reportError(CE->getExprLoc(), "VeriObjC Violation: Second live vstack_array '%0' on data stack '%1' while '%2' is already active on top of the same stack.")
                            << ArrVD->getName() << StackVD->getName() << It->second->getName();
                    } else {
                        ActiveStackArray[StackVD] = ArrVD;
                    }
                }
            } else if (CalleeName == "__vvector_alloc_slot_raw" && CE->getNumArgs() >= 1) {
                if (const VarDecl *VecVD = extractVarDecl(CE->getArg(0))) {
                    const auto *RT = VecVD->getType()->getAs<RecordType>();
                    const RecordDecl *RD = RT ? RT->getDecl()->getDefinition() : nullptr;
                    uint32_t Cap = 0;
                    if (extractAnnotatedCapacity(RD, "objc_vvector:", Cap)) {
                        uint32_t Count = ++VVectorPushCounts[VecVD];
                        if (Count > Cap) {
                            reportError(CE->getExprLoc(), "VeriObjC Violation: vvector push exceeds static capacity %0 on '%1'.")
                                << Cap << VecVD->getName();
                        }
                    }
                }
            } else if (CalleeName == "__vhash_flat_insert_u64_i32" && CE->getNumArgs() >= 1) {
                if (const VarDecl *MapVD = extractVarDecl(CE->getArg(0))) {
                    const auto *RT = MapVD->getType()->getAs<RecordType>();
                    const RecordDecl *RD = RT ? RT->getDecl()->getDefinition() : nullptr;
                    uint32_t Cap = 0;
                    if (extractAnnotatedCapacity(RD, "objc_vhash_flat:", Cap)) {
                        uint32_t MaxLoad = (Cap * 3u) >> 2u;
                        uint32_t Count = ++VHashInsertCounts[MapVD];
                        if (Count > MaxLoad) {
                            reportError(CE->getExprLoc(), "VeriObjC Violation: vhash_flat load-factor ceiling exceeded on '%0' (max load %1 for capacity %2).")
                                << MapVD->getName() << MaxLoad << Cap;
                        }
                    }
                }
            } else if (CalleeName == "__vspsc_channel_send_raw" && CE->getNumArgs() >= 1) {
                if (const VarDecl *ChanVD = extractVarDecl(CE->getArg(0)); ChanVD && !EnclosingFnName.empty()) {
                    auto It = SPSCChannelProducer.find(ChanVD);
                    if (It != SPSCChannelProducer.end() && It->second != EnclosingFnName) {
                        reportError(CE->getExprLoc(), "VeriObjC Violation: SPSC invariant violated on channel '%0': multiple producers ('%1' and '%2') detected.")
                            << ChanVD->getName() << It->second << EnclosingFnName;
                    } else {
                        SPSCChannelProducer[ChanVD] = EnclosingFnName.str();
                    }
                }
            } else if (CalleeName == "__vstream_kv_append_raw" && CE->getNumArgs() >= 6) {
                if (const VarDecl *ArenaVD = extractVarDecl(CE->getArg(0))) {
                    const auto *RT = ArenaVD->getType()->getAs<RecordType>();
                    const RecordDecl *RD = RT ? RT->getDecl()->getDefinition() : nullptr;
                    uint32_t ContextCap = 0;
                    Expr::EvalResult Res;
                    if (extractAnnotatedCapacity(RD, "objc_kv_arena:", ContextCap) &&
                        CE->getArg(5)->EvaluateAsInt(Res, *Context) &&
                        Res.Val.getInt().getZExtValue() >= ContextCap) {
                        reportError(CE->getExprLoc(), "VeriObjC Violation: Context cap exceeded — compile error: token index %0 exceeds ContextCap %1 on '%2'.")
                            << static_cast<unsigned>(Res.Val.getInt().getZExtValue()) << ContextCap << ArenaVD->getName();
                    }
                }
            } else if (CalleeName == "__vdma_ring_submit_raw" && CE->getNumArgs() >= 1) {
                if (const VarDecl *RingVD = extractVarDecl(CE->getArg(0))) {
                    const auto *RT = RingVD->getType()->getAs<RecordType>();
                    const RecordDecl *RD = RT ? RT->getDecl()->getDefinition() : nullptr;
                    uint32_t RingCap = 0;
                    if (extractAnnotatedCapacity(RD, "objc_vdma_ring:", RingCap)) {
                        uint32_t Count = ++VDmaRingCounts[RingVD];
                        if (Count > RingCap) {
                            reportError(CE->getExprLoc(), "VeriObjC Violation: DMA ring capacity exceeded on '%0' (capacity %1).")
                                << RingVD->getName() << RingCap;
                        }
                    }
                }
            } else if (CalleeName == "__vdma_ring_poll_raw" && CE->getNumArgs() >= 1) {
                if (const VarDecl *RingVD = extractVarDecl(CE->getArg(0))) {
                    if (VDmaRingCounts[RingVD] > 0u) {
                        VDmaRingCounts[RingVD]--;
                    }
                }
            }
        }
    }

    for (const Stmt *Child : S->children()) {
        analyzeVResultAndStackArrays(
            Child, EnclosingFnName, IsNoLatencyFn, ProvenOkVars, ActiveStackArray,
            VVectorPushCounts, VHashInsertCounts, VDmaRingCounts);
    }
}

void VeriObjCVisitor::checkInterfaceByValueRecord(const RecordDecl *D, SourceLocation Loc) {
    if (!D) return;
    for (const auto *Ann : D->specific_attrs<AnnotateAttr>()) {
        StringRef A = Ann->getAnnotation();
        if (!A.starts_with("objc_interface_by_value:")) continue;
        StringRef IFaceName = A.drop_front(std::strlen("objc_interface_by_value:"));

        const ObjCInterfaceDecl *TargetID = nullptr;
        for (const Decl *TopD : Context->getTranslationUnitDecl()->decls()) {
            if (const auto *ID = dyn_cast<ObjCInterfaceDecl>(TopD)) {
                if (ID->getName() == IFaceName) {
                    TargetID = ID;
                    break;
                }
            }
        }

        bool Eligible = false;
        if (TargetID && isSealedClass(TargetID) && isFinalClass(TargetID)) {
            if (IFaceName == "VVector" || hasVeriObjCAnnotation(TargetID, "objc_flat")) {
                Eligible = true;
            }
        }

        if (!Eligible) {
            reportError(Loc, "VeriObjC Violation: Interface '%0' cannot be allocated or returned by value: only @flat @final @sealed interfaces support value semantics (VG-006).")
                << IFaceName;
        }
    }

    uint32_t VVecCap = 0;
    if (extractAnnotatedCapacity(D, "objc_vvector:", VVecCap)) {
        for (const FieldDecl *FD : D->fields()) {
            if (FD->getName() != "buf") continue;
            if (const ConstantArrayType *CAT = Context->getAsConstantArrayType(FD->getType())) {
                QualType ElemTy = CAT->getElementType();
                if (const auto *OPT = ElemTy->getAs<ObjCObjectPointerType>()) {
                    if (const ObjCInterfaceDecl *ElemID = OPT->getInterfaceDecl()) {
                        if (!isSealedClass(ElemID)) {
                            reportError(Loc, "VeriObjC Violation: Invalid vgeneric type argument '%0': type arguments to constrained generics must be @sealed classes.")
                                << ElemTy.getAsString();
                        }
                    }
                }
            }
        }
    }
}

} // namespace veriobjc
