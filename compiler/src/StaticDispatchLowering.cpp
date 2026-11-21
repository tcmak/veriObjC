#include "veriobjc/VeriObjCVisitor.h"

#include <llvm/ADT/STLExtras.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Metadata.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/PassManager.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Support/raw_ostream.h>
#include <algorithm>
#include <set>

using namespace clang;

namespace veriobjc {
namespace {

uint16_t computeVeriObjCTypeTag(llvm::StringRef ClsName) {
    if (ClsName.empty()) return 0u;
    uint32_t H = 0x811c9dc5u;
    for (unsigned char C : ClsName) {
        H ^= static_cast<uint32_t>(C);
        H *= 0x01000193u;
    }
    return static_cast<uint16_t>(((H >> 16u) ^ (H & 0xFFFFu)) | 1u);
}

std::string mangleSelString(llvm::StringRef SelStr) {
    std::string Out = SelStr.str();
    for (char &C : Out) {
        if (C == ':') C = '_';
    }
    return Out;
}

struct SealedClassRecord {
    std::string Name;
    std::string SuperName;
    uint16_t TypeTag = 0;
    bool IsSealed = false;
    bool IsFinal = false;
    llvm::StringMap<std::string> ImplementedInstanceMethods;
    llvm::StringMap<std::string> ImplementedClassMethods;
};

struct MessageSiteHint {
    std::string ReceiverClass;
    bool IsInstance = true;
    bool IsFinalReceiver = false;
};

struct StaticDispatchModuleState {
    llvm::StringMap<SealedClassRecord> Classes;
    std::map<std::pair<std::string, std::string>, std::vector<MessageSiteHint>> FuncSelectorSites;
};

StaticDispatchModuleState &getGlobalDispatchState() {
    static StaticDispatchModuleState State;
    return State;
}

llvm::StringRef extractCStringFromConstant(const llvm::Constant *C) {
    if (!C) return {};
    C = C->stripPointerCasts();
    const auto *GV = llvm::dyn_cast<llvm::GlobalVariable>(C);
    if (!GV || !GV->hasInitializer()) return {};
    const auto *CDS = llvm::dyn_cast<llvm::ConstantDataSequential>(GV->getInitializer());
    if (!CDS || !CDS->isCString()) return {};
    return CDS->getAsCString();
}

llvm::StringRef extractSelectorFromLookupCall(const llvm::CallBase *LookupCB) {
    if (const llvm::MDNode *MD = LookupCB->getMetadata("GNUObjCMessageSend")) {
        if (MD->getNumOperands() >= 1) {
            if (const auto *MDS = llvm::dyn_cast<llvm::MDString>(MD->getOperand(0))) {
                return MDS->getString();
            }
        }
    }
    if (LookupCB->arg_size() >= 2) {
        const llvm::Constant *SelArg = llvm::dyn_cast<llvm::Constant>(LookupCB->getArgOperand(1)->stripPointerCasts());
        if (const auto *SelGV = llvm::dyn_cast_or_null<llvm::GlobalVariable>(SelArg)) {
            if (SelGV->hasInitializer()) {
                if (const auto *CA = llvm::dyn_cast<llvm::ConstantArray>(SelGV->getInitializer());
                    CA && CA->getNumOperands() >= 1) {
                    if (const auto *CS = llvm::dyn_cast<llvm::ConstantStruct>(CA->getOperand(0));
                        CS && CS->getNumOperands() >= 1) {
                        return extractCStringFromConstant(CS->getOperand(0));
                    }
                }
            }
        }
    }
    return {};
}

void populateClassesFromObjCMetadata(llvm::Module &M, llvm::StringMap<SealedClassRecord> &Classes) {
    for (llvm::GlobalVariable &GV : M.globals()) {
        llvm::StringRef GName = GV.getName();
        if (!GName.starts_with("_OBJC_CLASS_") || !GV.hasInitializer()) continue;
        const auto *CS = llvm::dyn_cast<llvm::ConstantStruct>(GV.getInitializer());
        if (!CS || CS->getNumOperands() < 8) continue;

        llvm::StringRef SuperStr = extractCStringFromConstant(CS->getOperand(1));
        llvm::StringRef ClsStr = extractCStringFromConstant(CS->getOperand(2));
        if (ClsStr.empty()) {
            ClsStr = GName.drop_front(12);
        }
        SealedClassRecord &Rec = Classes[ClsStr];
        if (Rec.Name.empty()) Rec.Name = ClsStr.str();
        if (Rec.SuperName.empty() && !SuperStr.empty()) Rec.SuperName = SuperStr.str();
        if (Rec.TypeTag == 0u) Rec.TypeTag = computeVeriObjCTypeTag(ClsStr);

        const llvm::Constant *MethodListConst = CS->getOperand(7)->stripPointerCasts();
        const auto *MethodListGV = llvm::dyn_cast<llvm::GlobalVariable>(MethodListConst);
        if (!MethodListGV || !MethodListGV->hasInitializer()) continue;
        const auto *MLCS = llvm::dyn_cast<llvm::ConstantStruct>(MethodListGV->getInitializer());
        if (!MLCS || MLCS->getNumOperands() < 3) continue;
        const auto *MethodsArr = llvm::dyn_cast<llvm::ConstantArray>(MLCS->getOperand(2));
        if (!MethodsArr) continue;

        for (unsigned i = 0; i < MethodsArr->getNumOperands(); ++i) {
            const auto *Entry = llvm::dyn_cast<llvm::ConstantStruct>(MethodsArr->getOperand(i));
            if (!Entry || Entry->getNumOperands() < 3) continue;
            llvm::StringRef SelName = extractCStringFromConstant(Entry->getOperand(0));
            const auto *ImpFn = llvm::dyn_cast<llvm::Function>(Entry->getOperand(2)->stripPointerCasts());
            if (!SelName.empty() && ImpFn) {
                Rec.ImplementedInstanceMethods[SelName] = ImpFn->getName().str();
            }
        }
    }
}

void filterGlobalArrayList(llvm::Module &M, llvm::StringRef GlobalName, bool IsCtors) {
    llvm::GlobalVariable *GV = M.getGlobalVariable(GlobalName);
    if (!GV || !GV->hasInitializer()) {
        if (GV && GV->use_empty()) GV->eraseFromParent();
        return;
    }
    const auto *CA = llvm::dyn_cast<llvm::ConstantArray>(GV->getInitializer());
    if (!CA) {
        if (IsCtors) GV->eraseFromParent();
        return;
    }

    llvm::SmallVector<llvm::Constant *, 4> Kept;
    for (unsigned i = 0; i < CA->getNumOperands(); ++i) {
        llvm::Constant *Elem = CA->getOperand(i);
        bool Drop = false;
        if (IsCtors) {
            if (const auto *CS = llvm::dyn_cast<llvm::ConstantStruct>(Elem); CS && CS->getNumOperands() >= 2) {
                const llvm::Value *FnVal = CS->getOperand(1)->stripPointerCasts();
                llvm::StringRef FnName = FnVal ? FnVal->getName() : llvm::StringRef();
                if (FnName == ".objc_load_function" || FnName.starts_with(".objc_")) {
                    Drop = true;
                }
            }
        } else {
            const llvm::Value *Target = Elem->stripPointerCasts();
            llvm::StringRef TName = Target ? Target->getName() : llvm::StringRef();
            if (TName.starts_with("__veriobjc_direct_") ||
                TName.starts_with("_OBJC_") ||
                TName.starts_with("__objc_") ||
                TName.starts_with(".objc_")) {
                Drop = true;
            }
        }
        if (!Drop) Kept.push_back(Elem);
    }

    if (Kept.size() == CA->getNumOperands()) return;

    llvm::Type *ElemTy = CA->getType()->getElementType();
    llvm::GlobalValue::LinkageTypes Linkage = GV->getLinkage();
    std::string Section = GV->getSection().str();
    GV->eraseFromParent();

    if (!Kept.empty()) {
        llvm::ArrayType *NewArrTy = llvm::ArrayType::get(ElemTy, Kept.size());
        auto *NewGV = new llvm::GlobalVariable(
            M, NewArrTy, false, Linkage, llvm::ConstantArray::get(NewArrTy, Kept), GlobalName
        );
        if (!Section.empty()) NewGV->setSection(Section);
    }
}

class VeriObjCStaticDispatchPass : public llvm::PassInfoMixin<VeriObjCStaticDispatchPass> {
public:
    llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &) {
        StaticDispatchModuleState &ASTState = getGlobalDispatchState();
        llvm::StringMap<SealedClassRecord> Classes = ASTState.Classes;
        populateClassesFromObjCMetadata(M, Classes);

        std::map<std::string, std::vector<std::string>> DirectChildren;
        for (const auto &KV : Classes) {
            const SealedClassRecord &Rec = KV.getValue();
            if (!Rec.SuperName.empty()) {
                DirectChildren[Rec.SuperName].push_back(Rec.Name);
            }
        }
        for (auto &[Parent, Kids] : DirectChildren) {
            std::sort(Kids.begin(), Kids.end());
            Kids.erase(std::unique(Kids.begin(), Kids.end()), Kids.end());
        }

        auto collectSubtree = [&](llvm::StringRef RootCls) {
            std::vector<std::string> Out;
            std::set<std::string> Visited;
            auto dfs = [&](auto &self, const std::string &Cur) -> void {
                if (Cur.empty() || !Visited.insert(Cur).second) return;
                Out.push_back(Cur);
                auto It = DirectChildren.find(Cur);
                if (It != DirectChildren.end()) {
                    for (const std::string &Child : It->second) {
                        self(self, Child);
                    }
                }
            };
            dfs(dfs, RootCls.str());
            return Out;
        };

        auto resolveMethodFn = [&](llvm::StringRef ClsName, llvm::StringRef SelStr, bool IsInstance) -> llvm::Function * {
            std::string Cur = ClsName.str();
            std::set<std::string> Seen;
            std::string MangledSel = mangleSelString(SelStr);
            while (!Cur.empty() && Seen.insert(Cur).second) {
                auto It = Classes.find(Cur);
                if (It != Classes.end()) {
                    const auto &Map = IsInstance ? It->second.ImplementedInstanceMethods
                                                 : It->second.ImplementedClassMethods;
                    auto MIt = Map.find(SelStr);
                    if (MIt != Map.end()) {
                        if (llvm::Function *F = M.getFunction(MIt->second)) return F;
                    }
                }
                std::string FallbackSym = (IsInstance ? "_i_" : "_c_") + Cur + "__" + MangledSel;
                if (llvm::Function *F = M.getFunction(FallbackSym)) {
                    return F;
                }
                Cur = (It != Classes.end()) ? It->second.SuperName : std::string();
            }
            return nullptr;
        };

        auto inferFallbackRootForSelector = [&](llvm::StringRef SelStr) -> std::string {
            std::string Best;
            for (const auto &KV : Classes) {
                const SealedClassRecord &Rec = KV.getValue();
                if (Rec.ImplementedInstanceMethods.count(SelStr) ||
                    M.getFunction("_i_" + Rec.Name + "__" + mangleSelString(SelStr))) {
                    std::string Root = Rec.Name;
                    std::set<std::string> Seen;
                    while (Seen.insert(Root).second) {
                        auto RIt = Classes.find(Root);
                        if (RIt == Classes.end() || RIt->second.SuperName.empty()) break;
                        Root = RIt->second.SuperName;
                    }
                    Best = Root;
                    break;
                }
            }
            return Best;
        };

        auto getOrBuildSealedDispatchFn = [&](llvm::StringRef StaticClass,
                                              llvm::StringRef SelStr,
                                              bool IsInstance,
                                              llvm::FunctionType *MsgFnTy,
                                              bool HasSRet) -> llvm::Function * {
            std::string DispName = "__veriobjc_dispatch_" + StaticClass.str() + "__" + mangleSelString(SelStr);
            if (llvm::Function *Existing = M.getFunction(DispName)) {
                return Existing;
            }

            auto *DispFn = llvm::Function::Create(
                MsgFnTy, llvm::GlobalValue::InternalLinkage, DispName, &M
            );
            llvm::LLVMContext &Ctx = M.getContext();
            llvm::Type *PtrTy = llvm::PointerType::getUnqual(Ctx);
            llvm::Type *I16Ty = llvm::Type::getInt16Ty(Ctx);
            llvm::Type *RetTy = MsgFnTy->getReturnType();

            unsigned SelfArgIdx = HasSRet ? 1u : 0u;
            unsigned CmdArgIdx = HasSRet ? 2u : 1u;

            llvm::Function *DefaultImp = resolveMethodFn(StaticClass, SelStr, IsInstance);
            if (DefaultImp) {
                DispFn->setCallingConv(DefaultImp->getCallingConv());
                DispFn->setAttributes(DefaultImp->getAttributes());
            }

            llvm::BasicBlock *EntryBB = llvm::BasicBlock::Create(Ctx, "entry", DispFn);
            llvm::BasicBlock *NullBB = llvm::BasicBlock::Create(Ctx, "ret.null", DispFn);
            llvm::BasicBlock *TagBB = llvm::BasicBlock::Create(Ctx, "read.tag", DispFn);
            llvm::BasicBlock *DefaultBB = llvm::BasicBlock::Create(Ctx, "sw.default", DispFn);

            auto emitReturnZeroOrVoid = [&](llvm::IRBuilder<> &B) {
                if (RetTy->isVoidTy()) {
                    B.CreateRetVoid();
                } else {
                    B.CreateRet(llvm::Constant::getNullValue(RetTy));
                }
            };

            auto emitForwardCallAndReturn = [&](llvm::IRBuilder<> &B, llvm::Function *TargetImp) {
                llvm::SmallVector<llvm::Value *, 8> ForwardArgs;
                unsigned Idx = 0;
                for (llvm::Argument &Arg : DispFn->args()) {
                    if (Idx == CmdArgIdx) {
                        ForwardArgs.push_back(llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(PtrTy)));
                    } else {
                        ForwardArgs.push_back(&Arg);
                    }
                    ++Idx;
                }
                llvm::CallInst *CI = B.CreateCall(TargetImp->getFunctionType(), TargetImp, ForwardArgs);
                CI->setCallingConv(TargetImp->getCallingConv());
                CI->setAttributes(TargetImp->getAttributes());
                if (RetTy->isVoidTy()) {
                    B.CreateRetVoid();
                } else {
                    B.CreateRet(CI);
                }
            };

            // Entry: guard null receiver before reading [-8] sealed header
            {
                llvm::IRBuilder<> B(EntryBB);
                llvm::Value *SelfVal = DispFn->getArg(SelfArgIdx);
                llvm::Value *IsNull = B.CreateICmpEQ(
                    SelfVal, llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(PtrTy))
                );
                B.CreateCondBr(IsNull, NullBB, TagBB);
            }
            {
                llvm::IRBuilder<> B(NullBB);
                emitReturnZeroOrVoid(B);
            }

            std::vector<std::string> Subtree = collectSubtree(StaticClass);
            std::vector<std::pair<uint16_t, llvm::Function *>> SwitchTargets;
            std::set<uint16_t> AddedTags;
            for (const std::string &SubName : Subtree) {
                if (SubName == StaticClass) continue;
                llvm::Function *SubImp = resolveMethodFn(SubName, SelStr, IsInstance);
                if (!SubImp || SubImp == DefaultImp) continue;
                uint16_t Tag = 0u;
                auto CIt = Classes.find(SubName);
                Tag = (CIt != Classes.end() && CIt->second.TypeTag != 0u)
                    ? CIt->second.TypeTag
                    : computeVeriObjCTypeTag(SubName);
                if (Tag != 0u && AddedTags.insert(Tag).second) {
                    SwitchTargets.push_back({Tag, SubImp});
                }
            }

            {
                llvm::IRBuilder<> B(TagBB);
                llvm::Value *SelfVal = DispFn->getArg(SelfArgIdx);
                llvm::Value *HdrPtr = B.CreateInBoundsGEP(
                    llvm::Type::getInt8Ty(Ctx),
                    SelfVal,
                    llvm::ConstantInt::getSigned(llvm::Type::getInt64Ty(Ctx), -8)
                );
                llvm::LoadInst *TagLoad = B.CreateAlignedLoad(I16Ty, HdrPtr, llvm::Align(2), "isa_tag");
                llvm::SwitchInst *SI = B.CreateSwitch(TagLoad, DefaultBB, static_cast<unsigned>(SwitchTargets.size()));

                for (const auto &[TagVal, SubImp] : SwitchTargets) {
                    llvm::BasicBlock *CaseBB = llvm::BasicBlock::Create(
                        Ctx, "sw.tag." + std::to_string(TagVal), DispFn
                    );
                    SI->addCase(llvm::ConstantInt::get(llvm::cast<llvm::IntegerType>(I16Ty), TagVal), CaseBB);
                    llvm::IRBuilder<> CB(CaseBB);
                    emitForwardCallAndReturn(CB, SubImp);
                }
            }
            {
                llvm::IRBuilder<> B(DefaultBB);
                if (DefaultImp) {
                    emitForwardCallAndReturn(B, DefaultImp);
                } else {
                    emitReturnZeroOrVoid(B);
                }
            }
            return DispFn;
        };

        bool Modified = false;

        // 1. Rewrite every @objc_msg_lookup + indirect message call pair
        llvm::SmallVector<llvm::Function *, 32> WorkFns;
        for (llvm::Function &F : M) {
            if (!F.isDeclaration() && !F.getName().starts_with("__veriobjc_dispatch_") && F.getName() != ".objc_load_function") {
                WorkFns.push_back(&F);
            }
        }

        llvm::LLVMContext &Ctx = M.getContext();
        auto *NullPtr = llvm::ConstantPointerNull::get(llvm::PointerType::getUnqual(Ctx));

        for (llvm::Function *F : WorkFns) {
            llvm::StringMap<size_t> PerSelCursor;
            llvm::SmallVector<llvm::CallBase *, 16> Lookups;
            for (llvm::BasicBlock &BB : *F) {
                for (llvm::Instruction &I : BB) {
                    auto *CB = llvm::dyn_cast<llvm::CallBase>(&I);
                    if (!CB) continue;
                    llvm::Function *Callee = CB->getCalledFunction();
                    if (Callee && (Callee->getName() == "objc_msg_lookup" || Callee->getName() == "objc_msg_lookup_super")) {
                        Lookups.push_back(CB);
                    }
                }
            }

            for (llvm::CallBase *LookupCB : Lookups) {
                llvm::StringRef SelStr = extractSelectorFromLookupCall(LookupCB);
                size_t SiteIdx = PerSelCursor[SelStr]++;

                std::string StaticClass;
                bool IsInstance = true;
                bool IsFinalRecv = false;
                auto HintIt = ASTState.FuncSelectorSites.find({F->getName().str(), SelStr.str()});
                if (HintIt != ASTState.FuncSelectorSites.end() && SiteIdx < HintIt->second.size()) {
                    StaticClass = HintIt->second[SiteIdx].ReceiverClass;
                    IsInstance = HintIt->second[SiteIdx].IsInstance;
                    IsFinalRecv = HintIt->second[SiteIdx].IsFinalReceiver;
                } else {
                    StaticClass = inferFallbackRootForSelector(SelStr);
                }
                if (auto CIt = Classes.find(StaticClass); CIt != Classes.end() && CIt->second.IsFinal) {
                    IsFinalRecv = true;
                }

                std::string MangledSel = mangleSelString(SelStr);
                llvm::Function *HeaderDirectFn = M.getFunction("__veriobjc_direct_" + StaticClass + "__" + MangledSel);

                llvm::SmallVector<llvm::CallBase *, 2> MsgCalls;
                for (llvm::User *U : LookupCB->users()) {
                    if (auto *MsgCB = llvm::dyn_cast<llvm::CallBase>(U); MsgCB && MsgCB->getCalledOperand() == LookupCB) {
                        MsgCalls.push_back(MsgCB);
                    }
                }

                for (llvm::CallBase *MsgCB : MsgCalls) {
                    llvm::IRBuilder<> B(MsgCB);
                    bool HasSRet = MsgCB->hasStructRetAttr();
                    unsigned CmdArgIdx = HasSRet ? 2u : 1u;

                    llvm::CallInst *NewCall = nullptr;
                    if (HeaderDirectFn) {
                        // Header-only inline C helper (e.g. VVector in <vstd/vector.h>) omits _cmd parameter
                        llvm::SmallVector<llvm::Value *, 8> Args;
                        for (unsigned i = 0; i < MsgCB->arg_size(); ++i) {
                            if (i == CmdArgIdx) continue;
                            Args.push_back(MsgCB->getArgOperand(i));
                        }
                        NewCall = B.CreateCall(HeaderDirectFn->getFunctionType(), HeaderDirectFn, Args);
                        NewCall->setCallingConv(HeaderDirectFn->getCallingConv());
                        NewCall->setAttributes(HeaderDirectFn->getAttributes());
                    } else {
                        std::set<llvm::Function *> UniqueImps;
                        if (IsFinalRecv) {
                            if (llvm::Function *Single = resolveMethodFn(StaticClass, SelStr, IsInstance)) {
                                UniqueImps.insert(Single);
                            }
                        } else {
                            for (const std::string &SubName : collectSubtree(StaticClass)) {
                                if (llvm::Function *Imp = resolveMethodFn(SubName, SelStr, IsInstance)) {
                                    UniqueImps.insert(Imp);
                                }
                            }
                        }

                        llvm::Function *TargetCallee = nullptr;
                        if (UniqueImps.size() == 1u) {
                            TargetCallee = *UniqueImps.begin();
                        } else if (UniqueImps.size() >= 2u) {
                            TargetCallee = getOrBuildSealedDispatchFn(
                                StaticClass, SelStr, IsInstance, MsgCB->getFunctionType(), HasSRet
                            );
                        } else {
                            std::string ExtSym = (IsInstance ? "_i_" : "_c_") + StaticClass + "__" + MangledSel;
                            llvm::FunctionCallee FC = M.getOrInsertFunction(ExtSym, MsgCB->getFunctionType());
                            TargetCallee = llvm::dyn_cast<llvm::Function>(FC.getCallee()->stripPointerCasts());
                        }

                        llvm::SmallVector<llvm::Value *, 8> Args;
                        for (unsigned i = 0; i < MsgCB->arg_size(); ++i) {
                            Args.push_back(i == CmdArgIdx ? static_cast<llvm::Value *>(NullPtr) : MsgCB->getArgOperand(i));
                        }
                        NewCall = B.CreateCall(MsgCB->getFunctionType(), TargetCallee, Args);
                        if (TargetCallee) {
                            NewCall->setCallingConv(TargetCallee->getCallingConv());
                            NewCall->setAttributes(TargetCallee->getAttributes());
                        }
                    }

                    NewCall->setDebugLoc(MsgCB->getDebugLoc());
                    MsgCB->replaceAllUsesWith(NewCall);
                    MsgCB->eraseFromParent();
                    Modified = true;
                }

                if (LookupCB->use_empty()) {
                    LookupCB->eraseFromParent();
                    Modified = true;
                }
            }
        }

        // 2. Strip GNU Objective-C constructor & metadata tables so binaries link without -lobjc
        filterGlobalArrayList(M, "llvm.global_ctors", /*IsCtors=*/true);
        filterGlobalArrayList(M, "llvm.compiler.used", /*IsCtors=*/false);
        filterGlobalArrayList(M, "llvm.used", /*IsCtors=*/false);

        if (llvm::Function *LoadFn = M.getFunction(".objc_load_function")) {
            LoadFn->dropAllReferences();
            if (LoadFn->use_empty()) {
                LoadFn->eraseFromParent();
                Modified = true;
            }
        }

        for (llvm::StringRef RtFnName : {
                 "__objc_exec_class",
                 "objc_msg_lookup",
                 "objc_msg_lookup_super",
                 "objc_msgSend",
                 "objc_enumerationMutation"}) {
            if (llvm::Function *RtFn = M.getFunction(RtFnName)) {
                RtFn->removeDeadConstantUsers();
                if (RtFn->use_empty()) {
                    RtFn->eraseFromParent();
                    Modified = true;
                }
            }
        }

        bool ErasedGlobal = true;
        while (ErasedGlobal) {
            ErasedGlobal = false;
            for (llvm::GlobalVariable &GV : llvm::make_early_inc_range(M.globals())) {
                GV.removeDeadConstantUsers();
                llvm::StringRef GName = GV.getName();
                bool IsObjCGlobal = GName.starts_with("_OBJC_") ||
                                    GName.starts_with("__objc_") ||
                                    GName.starts_with(".objc_") ||
                                    GName.starts_with(".ivar.offsets") ||
                                    GName.starts_with(".class_name");
                if (GV.use_empty() &&
                    (IsObjCGlobal || GV.hasPrivateLinkage() || (GV.hasInternalLinkage() && GName.empty()))) {
                    GV.eraseFromParent();
                    ErasedGlobal = true;
                    Modified = true;
                }
            }
        }

        llvm::SmallVector<std::string, 8> DeadComdats;
        for (const auto &Entry : M.getComdatSymbolTable()) {
            if (Entry.first().starts_with(".objc_")) {
                DeadComdats.push_back(Entry.first().str());
            }
        }
        for (const std::string &CName : DeadComdats) {
            M.getComdatSymbolTable().erase(CName);
        }

        return Modified ? llvm::PreservedAnalyses::none() : llvm::PreservedAnalyses::all();
    }
};

} // namespace

void resetStaticDispatchModuleState() {
    StaticDispatchModuleState &S = getGlobalDispatchState();
    S.Classes.clear();
    S.FuncSelectorSites.clear();
}

void registerVeriObjCPassBuilderCallbacks(llvm::PassBuilder &PB) {
    PB.registerPipelineStartEPCallback(
        [](llvm::ModulePassManager &MPM, llvm::OptimizationLevel) {
            MPM.addPass(VeriObjCStaticDispatchPass());
        }
    );
    PB.registerPipelineParsingCallback(
        [](llvm::StringRef Name, llvm::ModulePassManager &MPM, llvm::ArrayRef<llvm::PassBuilder::PipelineElement>) {
            if (Name == "veriobjc-static-dispatch") {
                MPM.addPass(VeriObjCStaticDispatchPass());
                return true;
            }
            return false;
        }
    );
}

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

    // Record closed-world @interface hierarchy metadata & verify 16-bit tag uniqueness
    StaticDispatchModuleState &State = getGlobalDispatchState();
    SealedClassRecord &Rec = State.Classes[D->getName()];
    Rec.Name = D->getNameAsString();
    if (const ObjCInterfaceDecl *Super = D->getSuperClass()) {
        Rec.SuperName = Super->getNameAsString();
    }
    Rec.IsSealed = Rec.IsSealed || isSealedClass(D);
    Rec.IsFinal = Rec.IsFinal || isFinalClass(D);
    Rec.TypeTag = computeVeriObjCTypeTag(D->getName());

    const ObjCInterfaceDecl *Root = D;
    while (Root->getSuperClass()) {
        Root = Root->getSuperClass();
    }
    for (const auto &KV : State.Classes) {
        const SealedClassRecord &Other = KV.getValue();
        if (Other.Name == Rec.Name || Other.TypeTag != Rec.TypeTag) continue;
        // Check if Other shares the same root hierarchy
        std::string OtherRoot = Other.Name;
        std::set<std::string> Seen;
        while (Seen.insert(OtherRoot).second) {
            auto It = State.Classes.find(OtherRoot);
            if (It == State.Classes.end() || It->second.SuperName.empty()) break;
            OtherRoot = It->second.SuperName;
        }
        if (OtherRoot == Root->getName()) {
            reportError(D->getLocation(), "VeriObjC Violation: 16-bit @sealed type tag collision (%0) between '%1' and '%2' in hierarchy '%3'.")
                << static_cast<unsigned>(Rec.TypeTag) << Rec.Name << Other.Name << Root->getName();
        }
    }

    return true;
}

std::string VeriObjCVisitor::mangleSelectorForSymbol(Selector Sel) {
    return mangleSelString(Sel.getAsString());
}

void VeriObjCVisitor::markHeaderDirectHelperIfPresent(const ObjCInterfaceDecl *ID, Selector Sel) {
    if (!ID) return;
    std::string HelperName = "__veriobjc_direct_" + ID->getNameAsString() + "__" + mangleSelectorForSymbol(Sel);
    DeclContext *TU = Context->getTranslationUnitDecl();
    for (NamedDecl *ND : TU->lookup(&Context->Idents.get(HelperName))) {
        if (auto *FD = dyn_cast<FunctionDecl>(ND)) {
            if (!FD->hasAttr<UsedAttr>()) {
                FD->addAttr(UsedAttr::CreateImplicit(*Context));
            }
        }
    }
}

void VeriObjCVisitor::collectBodyMessageSendsPostOrder(const Stmt *S, llvm::StringRef IRFuncName) {
    if (!S || isa<BlockExpr>(S)) return;
    for (const Stmt *Child : S->children()) {
        if (Child) {
            collectBodyMessageSendsPostOrder(Child, IRFuncName);
        }
    }
    const auto *OME = dyn_cast<ObjCMessageExpr>(S);
    if (!OME) return;
    const ObjCInterfaceDecl *RecvCls = OME->getReceiverInterface();
    if (!RecvCls) return;

    markHeaderDirectHelperIfPresent(RecvCls, OME->getSelector());

    MessageSiteHint Hint;
    Hint.ReceiverClass = RecvCls->getNameAsString();
    Hint.IsInstance = OME->isInstanceMessage();
    Hint.IsFinalReceiver = isFinalClass(RecvCls) || (OME->getReceiverKind() == ObjCMessageExpr::SuperInstance);
    getGlobalDispatchState().FuncSelectorSites[{IRFuncName.str(), OME->getSelector().getAsString()}].push_back(std::move(Hint));
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
        collectBodyMessageSendsPostOrder(Body, BlockFD->getName());
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
        }
        // NOTE: ObjCMessageExpr nodes are intentionally left intact in the AST;
        // VeriObjCStaticDispatchPass lowers @objc_msg_lookup call sites in LLVM IR.
    }
}

bool VeriObjCVisitor::analyzeAndRewriteBody(Stmt *Body, DeclContext *DC) {
    analyzeFunctionCFGDataflow(Body, DC);

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

void VeriObjCVisitor::lowerTopLevelDecl(Decl *D) {
    if (!D || Context->getDiagnostics().hasErrorOccurred() || isInRuntimeHeader(D->getLocation())) {
        return;
    }

    if (auto *FD = dyn_cast<FunctionDecl>(D)) {
        if (!FD->hasBody()) return;
        auditFunctionEffectsAndReport(FD);
        if (hasContractAnnotations(FD)) {
            ContractedFunctions.push_back(FD);
        }
        collectErrorReturnGraph(FD->getBody(), FD->getCanonicalDecl());
        recordDspGraphForBody(FD, FD->getBody());
        if (hasVeriObjCAnnotation(FD, "objc_hw_kernel")) {
            registerHwKernelRoot(FD, FD->getName(), FD->getLocation());
        }
        if (!analyzeAndRewriteBody(FD->getBody(), FD)) return;
        collectBodyMessageSendsPostOrder(FD->getBody(), FD->getName());
        return;
    }

    auto *ImplD = dyn_cast<ObjCImplementationDecl>(D);
    ObjCInterfaceDecl *ID = ImplD ? ImplD->getClassInterface() : nullptr;
    if (!ID) return;

    SealedClassRecord &Rec = getGlobalDispatchState().Classes[ID->getName()];
    if (Rec.Name.empty()) Rec.Name = ID->getNameAsString();
    if (Rec.TypeTag == 0u) Rec.TypeTag = computeVeriObjCTypeTag(ID->getName());
    if (const ObjCInterfaceDecl *Super = ID->getSuperClass()) {
        Rec.SuperName = Super->getNameAsString();
    }
    Rec.IsSealed = Rec.IsSealed || isSealedClass(ID);
    Rec.IsFinal = Rec.IsFinal || isFinalClass(ID);

    for (ObjCMethodDecl *MD : ImplD->methods()) {
        if (!MD->hasBody()) continue;
        std::string IRMethodSym = (MD->isInstanceMethod() ? "_i_" : "_c_") +
                                  ID->getNameAsString() + "__" +
                                  mangleSelectorForSymbol(MD->getSelector());
        if (MD->isInstanceMethod()) {
            Rec.ImplementedInstanceMethods[MD->getSelector().getAsString()] = IRMethodSym;
        } else {
            Rec.ImplementedClassMethods[MD->getSelector().getAsString()] = IRMethodSym;
        }

        Stmt *OrigBody = MD->getBody();
        recordDspGraphForBody(MD, OrigBody);
        const ObjCMethodDecl *IfaceMD = ID->lookupMethod(MD->getSelector(), MD->isInstanceMethod());
        if (hasVeriObjCAnnotation(MD, "objc_hw_kernel") ||
            (IfaceMD && hasVeriObjCAnnotation(IfaceMD, "objc_hw_kernel"))) {
            std::string KernelName = (ID->getName() + "::" + MD->getSelector().getAsString()).str();
            registerHwKernelRoot(MD, KernelName, MD->getLocation());
        }
        if (!analyzeAndRewriteBody(OrigBody, MD)) continue;
        collectBodyMessageSendsPostOrder(OrigBody, IRMethodSym);
    }
}

void VeriObjCVisitor::finalizeTranslationUnitAudits() {
    verifyMultiFrameErrorReturns();
    verifyAllHwKernelsZeroDsp();
    verifyAllContractedFunctions();
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
