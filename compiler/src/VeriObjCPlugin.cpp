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
#include <bitset>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace clang;

namespace {

class VeriObjCVisitor : public RecursiveASTVisitor<VeriObjCVisitor> {
private:
    struct ErrFromReq {
        std::string TargetFrame;
        SourceLocation Loc;
    };

    ASTContext *Context;
    llvm::StringMap<FunctionDecl *> DirectMethodDecls;
    std::vector<FunctionDecl *> SynthesizedDefs;
    llvm::DenseMap<const FunctionDecl *, std::vector<ErrFromReq>> DirectRaisesFrom;
    llvm::DenseMap<const FunctionDecl *, llvm::SmallVector<std::pair<const FunctionDecl *, SourceLocation>, 4>> CallEdges;
    llvm::DenseMap<const VarDecl *, FunctionDecl *> MonomorphizedBlocks;
    llvm::DenseMap<const VarDecl *, llvm::SmallVector<VarDecl *, 4>> BlockCapturedVars;
    unsigned MonoBlockCounter = 0;
    std::map<std::pair<const FunctionDecl *, std::vector<int64_t>>, int64_t> VConstexprMemo;
    llvm::StringMap<VarDecl *> CompiledDFADecls;
    std::vector<Decl *> SynthesizedVarDefs;
    unsigned DFACounter = 0;

    struct CompiledDFA {
        uint32_t NumStates = 0;
        uint32_t StartState = 0;
        std::vector<uint8_t> Transitions; // NumStates * 256
        std::vector<uint8_t> AcceptMask;  // NumStates
    };

    struct NFAState {
        std::bitset<256> MatchBytes;
        int NextOnByte = -1;
        llvm::SmallVector<int, 2> Epsilons;
    };

    struct NFAFrag {
        int Start = -1;
        int Accept = -1;
    };

    static void addEpsilonClosure(
        const std::vector<NFAState> &NFA,
        std::set<int> &StateSet
    ) {
        llvm::SmallVector<int, 16> Work(StateSet.begin(), StateSet.end());
        while (!Work.empty()) {
            int s = Work.pop_back_val();
            for (int nxt : NFA[s].Epsilons) {
                if (StateSet.insert(nxt).second) {
                    Work.push_back(nxt);
                }
            }
        }
    }

    bool compileRegexToMinimizedDFA(
        llvm::StringRef Pattern,
        SourceLocation Loc,
        CompiledDFA &OutDFA
    ) {
        DiagnosticsEngine &Diag = Context->getDiagnostics();

        // 1. Check for banned backreferences (\1..\9) and lookaround ((?=, (?!, (?<=, (?<!)
        for (size_t i = 0; i < Pattern.size(); ++i) {
            if (Pattern[i] == '\\' && i + 1 < Pattern.size() &&
                Pattern[i + 1] >= '1' && Pattern[i + 1] <= '9') {
                unsigned DiagID = Diag.getCustomDiagID(
                    DiagnosticsEngine::Error,
                    "VeriObjC Violation: Banned backreference or lookaround in vregex pattern '%0'. Rewrite as a pure regular expression for O(1)-per-byte DFA execution."
                );
                Diag.Report(Loc, DiagID) << Pattern;
                return false;
            }
            if (Pattern[i] == '(' && i + 1 < Pattern.size() && Pattern[i + 1] == '?') {
                unsigned DiagID = Diag.getCustomDiagID(
                    DiagnosticsEngine::Error,
                    "VeriObjC Violation: Banned backreference or lookaround in vregex pattern '%0'. Rewrite as a pure regular expression for O(1)-per-byte DFA execution."
                );
                Diag.Report(Loc, DiagID) << Pattern;
                return false;
            }
        }

        // 2. Check group nesting depth <= 8 and balanced parentheses
        int Depth = 0;
        for (size_t i = 0; i < Pattern.size(); ++i) {
            if (Pattern[i] == '\\') {
                ++i;
                continue;
            }
            if (Pattern[i] == '(') {
                ++Depth;
                if (Depth > 8) {
                    unsigned DiagID = Diag.getCustomDiagID(
                        DiagnosticsEngine::Error,
                        "VeriObjC Violation: vregex pattern '%0' exceeds maximum group nesting depth (8) or has unbalanced parentheses."
                    );
                    Diag.Report(Loc, DiagID) << Pattern;
                    return false;
                }
            } else if (Pattern[i] == ')') {
                --Depth;
                if (Depth < 0) {
                    unsigned DiagID = Diag.getCustomDiagID(
                        DiagnosticsEngine::Error,
                        "VeriObjC Violation: vregex pattern '%0' exceeds maximum group nesting depth (8) or has unbalanced parentheses."
                    );
                    Diag.Report(Loc, DiagID) << Pattern;
                    return false;
                }
            }
        }
        if (Depth != 0) {
            unsigned DiagID = Diag.getCustomDiagID(
                DiagnosticsEngine::Error,
                "VeriObjC Violation: vregex pattern '%0' exceeds maximum group nesting depth (8) or has unbalanced parentheses."
            );
            Diag.Report(Loc, DiagID) << Pattern;
            return false;
        }

        // 3. Parse regex into Thompson NFA
        std::vector<NFAState> NFA;
        auto newState = [&]() -> int {
            NFA.push_back(NFAState{});
            return (int)NFA.size() - 1;
        };

        size_t Pos = 0;
        std::function<NFAFrag()> parseAlt;
        std::function<NFAFrag()> parseConcat;
        std::function<NFAFrag()> parseRepeat;
        std::function<NFAFrag()> parseAtom;

        parseAtom = [&]() -> NFAFrag {
            if (Pos < Pattern.size() && Pattern[Pos] == '(') {
                ++Pos; // '('
                NFAFrag Inner = parseAlt();
                if (Pos < Pattern.size() && Pattern[Pos] == ')') {
                    ++Pos;
                }
                return Inner;
            }

            std::bitset<256> CharSet;
            if (Pos < Pattern.size() && Pattern[Pos] == '[') {
                ++Pos;
                bool Negate = false;
                if (Pos < Pattern.size() && Pattern[Pos] == '^') {
                    Negate = true;
                    ++Pos;
                }
                while (Pos < Pattern.size() && Pattern[Pos] != ']') {
                    uint8_t Lo = (uint8_t)Pattern[Pos++];
                    if (Pos + 1 < Pattern.size() && Pattern[Pos] == '-' && Pattern[Pos + 1] != ']') {
                        ++Pos; // '-'
                        uint8_t Hi = (uint8_t)Pattern[Pos++];
                        for (uint32_t c = Lo; c <= Hi; ++c) {
                            CharSet.set(c);
                        }
                    } else {
                        CharSet.set(Lo);
                    }
                }
                if (Pos < Pattern.size() && Pattern[Pos] == ']') {
                    ++Pos;
                }
                if (Negate) {
                    CharSet.flip();
                }
            } else if (Pos < Pattern.size() && Pattern[Pos] == '.') {
                ++Pos;
                CharSet.set();
            } else if (Pos < Pattern.size() && Pattern[Pos] == '\\' && Pos + 1 < Pattern.size()) {
                ++Pos;
                char Esc = Pattern[Pos++];
                if (Esc == 'd') {
                    for (uint32_t c = '0'; c <= '9'; ++c) CharSet.set(c);
                } else if (Esc == 'w') {
                    for (uint32_t c = '0'; c <= '9'; ++c) CharSet.set(c);
                    for (uint32_t c = 'A'; c <= 'Z'; ++c) CharSet.set(c);
                    for (uint32_t c = 'a'; c <= 'z'; ++c) CharSet.set(c);
                    CharSet.set('_');
                } else {
                    CharSet.set((uint8_t)Esc);
                }
            } else if (Pos < Pattern.size()) {
                CharSet.set((uint8_t)Pattern[Pos++]);
            }

            int S = newState();
            int A = newState();
            NFA[S].MatchBytes = CharSet;
            NFA[S].NextOnByte = A;
            return {S, A};
        };

        parseRepeat = [&]() -> NFAFrag {
            NFAFrag Base = parseAtom();
            while (Pos < Pattern.size() &&
                   (Pattern[Pos] == '*' || Pattern[Pos] == '+' || Pattern[Pos] == '?')) {
                char Op = Pattern[Pos++];
                int S = newState();
                int A = newState();
                if (Op == '*') {
                    NFA[S].Epsilons.push_back(Base.Start);
                    NFA[S].Epsilons.push_back(A);
                    NFA[Base.Accept].Epsilons.push_back(Base.Start);
                    NFA[Base.Accept].Epsilons.push_back(A);
                } else if (Op == '+') {
                    NFA[S].Epsilons.push_back(Base.Start);
                    NFA[Base.Accept].Epsilons.push_back(Base.Start);
                    NFA[Base.Accept].Epsilons.push_back(A);
                } else if (Op == '?') {
                    NFA[S].Epsilons.push_back(Base.Start);
                    NFA[S].Epsilons.push_back(A);
                    NFA[Base.Accept].Epsilons.push_back(A);
                }
                Base = {S, A};
            }
            return Base;
        };

        parseConcat = [&]() -> NFAFrag {
            if (Pos >= Pattern.size() || Pattern[Pos] == ')' || Pattern[Pos] == '|') {
                int S = newState();
                return {S, S};
            }
            NFAFrag Cur = parseRepeat();
            while (Pos < Pattern.size() && Pattern[Pos] != ')' && Pattern[Pos] != '|') {
                NFAFrag Next = parseRepeat();
                NFA[Cur.Accept].Epsilons.push_back(Next.Start);
                Cur.Accept = Next.Accept;
            }
            return Cur;
        };

        parseAlt = [&]() -> NFAFrag {
            NFAFrag Left = parseConcat();
            while (Pos < Pattern.size() && Pattern[Pos] == '|') {
                ++Pos;
                NFAFrag Right = parseConcat();
                int S = newState();
                int A = newState();
                NFA[S].Epsilons.push_back(Left.Start);
                NFA[S].Epsilons.push_back(Right.Start);
                NFA[Left.Accept].Epsilons.push_back(A);
                NFA[Right.Accept].Epsilons.push_back(A);
                Left = {S, A};
            }
            return Left;
        };

        NFAFrag Root = parseAlt();

        // 4. Subset Construction (NFA -> DFA) with state budget
        static constexpr size_t MAX_DFA_STATES = 64;
        std::map<std::set<int>, uint32_t> SubsetToID;
        std::vector<std::set<int>> IDToSubset;
        std::vector<std::array<uint32_t, 256>> RawTrans;
        std::vector<uint8_t> RawAccept;

        auto getOrAddSubset = [&](const std::set<int> &Sub) -> int32_t {
            auto It = SubsetToID.find(Sub);
            if (It != SubsetToID.end()) {
                return (int32_t)It->second;
            }
            if (IDToSubset.size() >= MAX_DFA_STATES) {
                return -1;
            }
            uint32_t NewID = (uint32_t)IDToSubset.size();
            SubsetToID[Sub] = NewID;
            IDToSubset.push_back(Sub);
            RawTrans.emplace_back();
            RawTrans.back().fill(0);
            RawAccept.push_back(Sub.count(Root.Accept) ? 1u : 0u);
            return (int32_t)NewID;
        };

        // State 0 is dead/sink state (empty set) to keep DFA complete
        std::set<int> DeadSet;
        getOrAddSubset(DeadSet);

        std::set<int> StartSet = {Root.Start};
        addEpsilonClosure(NFA, StartSet);
        int32_t StartID = getOrAddSubset(StartSet);
        if (StartID < 0) return false;

        for (size_t idx = 0; idx < IDToSubset.size(); ++idx) {
            std::set<int> CurSet = IDToSubset[idx];
            if (CurSet.empty()) continue;
            for (uint32_t b = 0; b < 256u; ++b) {
                std::set<int> NextSet;
                for (int ns : CurSet) {
                    if (NFA[ns].NextOnByte >= 0 && NFA[ns].MatchBytes.test(b)) {
                        NextSet.insert(NFA[ns].NextOnByte);
                    }
                }
                if (!NextSet.empty()) {
                    addEpsilonClosure(NFA, NextSet);
                }
                int32_t NextID = getOrAddSubset(NextSet);
                if (NextID < 0) {
                    unsigned DiagID = Diag.getCustomDiagID(
                        DiagnosticsEngine::Error,
                        "VeriObjC Violation: vregex pattern '%0' exceeded compile-time DFA state/byte budget."
                    );
                    Diag.Report(Loc, DiagID) << Pattern;
                    return false;
                }
                RawTrans[idx][b] = (uint32_t)NextID;
            }
        }

        // 5. Hopcroft Minimization (equivalence class refinement)
        uint32_t NumRaw = (uint32_t)IDToSubset.size();
        std::vector<uint32_t> ClassOf(NumRaw, 0);
        uint32_t NumClasses = 1;
        bool HasAccept = false, HasNonAccept = false;
        for (uint32_t s = 0; s < NumRaw; ++s) {
            if (RawAccept[s]) HasAccept = true;
            else HasNonAccept = true;
        }
        if (HasAccept && HasNonAccept) {
            NumClasses = 2;
            for (uint32_t s = 0; s < NumRaw; ++s) {
                ClassOf[s] = RawAccept[s] ? 1u : 0u;
            }
        }

        bool Changed = true;
        while (Changed) {
            Changed = false;
            std::map<std::pair<uint32_t, std::array<uint32_t, 256>>, uint32_t> SigToNewClass;
            std::vector<uint32_t> NewClassOf(NumRaw, 0);
            uint32_t NextClass = 0;
            for (uint32_t s = 0; s < NumRaw; ++s) {
                std::array<uint32_t, 256> TransClasses;
                for (uint32_t b = 0; b < 256u; ++b) {
                    TransClasses[b] = ClassOf[RawTrans[s][b]];
                }
                auto Key = std::make_pair(ClassOf[s], TransClasses);
                auto It = SigToNewClass.find(Key);
                if (It == SigToNewClass.end()) {
                    SigToNewClass[Key] = NextClass;
                    NewClassOf[s] = NextClass++;
                } else {
                    NewClassOf[s] = It->second;
                }
            }
            if (NextClass != NumClasses) {
                NumClasses = NextClass;
                ClassOf = std::move(NewClassOf);
                Changed = true;
            }
        }

        OutDFA.NumStates = NumClasses;
        OutDFA.StartState = ClassOf[(uint32_t)StartID];
        OutDFA.Transitions.assign((size_t)NumClasses * 256u, 0u);
        OutDFA.AcceptMask.assign(NumClasses, 0u);

        for (uint32_t s = 0; s < NumRaw; ++s) {
            uint32_t c = ClassOf[s];
            OutDFA.AcceptMask[c] = RawAccept[s];
            for (uint32_t b = 0; b < 256u; ++b) {
                OutDFA.Transitions[(size_t)c * 256u + b] = (uint8_t)ClassOf[RawTrans[s][b]];
            }
        }
        return true;
    }

    Expr *buildLoweredVRegexMatch(CallExpr *CE) {
        if (!CE || CE->getNumArgs() < 3) return nullptr;
        const auto *SL = dyn_cast<StringLiteral>(CE->getArg(0)->IgnoreParenImpCasts());
        if (!SL) return nullptr;

        llvm::StringRef Pattern = SL->getString();
        SourceLocation Loc = CE->getExprLoc();

        VarDecl *DFAObjVD = nullptr;
        auto MemoIt = CompiledDFADecls.find(Pattern);
        if (MemoIt != CompiledDFADecls.end()) {
            DFAObjVD = MemoIt->second;
        } else {
            CompiledDFA DFA;
            if (!compileRegexToMinimizedDFA(Pattern, Loc, DFA)) {
                return nullptr;
            }

            DeclContext *TU = Context->getTranslationUnitDecl();
            RecordDecl *DFAStructRD = nullptr;
            for (Decl *D : TU->decls()) {
                if (auto *RD = dyn_cast<RecordDecl>(D)) {
                    if (RD->getName() == "veriobjc_dfa" && RD->isCompleteDefinition()) {
                        DFAStructRD = RD;
                        break;
                    }
                }
            }
            if (!DFAStructRD) return nullptr;

            unsigned ID = DFACounter++;
            QualType ConstUInt8Ty = Context->UnsignedCharTy.withConst();

            // 1. static const uint8_t __veriobjc_dfa_trans_<ID>[NumStates * 256]
            llvm::APInt TransLen(64, (uint64_t)DFA.Transitions.size());
            QualType TransArrTy = Context->getConstantArrayType(
                ConstUInt8Ty, TransLen, nullptr, ArraySizeModifier::Normal, 0
            );
            std::string TransName = "__veriobjc_dfa_trans_" + std::to_string(ID);
            VarDecl *TransVD = VarDecl::Create(
                *Context, TU, Loc, Loc, &Context->Idents.get(TransName),
                TransArrTy, nullptr, SC_Static
            );
            TransVD->addAttr(UsedAttr::CreateImplicit(*Context));

            std::vector<Expr *> TransInits;
            TransInits.reserve(DFA.Transitions.size());
            for (uint8_t v : DFA.Transitions) {
                llvm::APInt Val(8, v);
                TransInits.push_back(IntegerLiteral::Create(*Context, Val, Context->UnsignedCharTy, Loc));
            }
            auto *TransILE = new (*Context) InitListExpr(*Context, Loc, TransInits, Loc, true);
            TransILE->setType(TransArrTy);
            TransVD->setInit(TransILE);
            TU->addDecl(TransVD);
            SynthesizedVarDefs.push_back(TransVD);

            // 2. static const uint8_t __veriobjc_dfa_accept_<ID>[NumStates]
            llvm::APInt AcceptLen(64, (uint64_t)DFA.AcceptMask.size());
            QualType AcceptArrTy = Context->getConstantArrayType(
                ConstUInt8Ty, AcceptLen, nullptr, ArraySizeModifier::Normal, 0
            );
            std::string AcceptName = "__veriobjc_dfa_accept_" + std::to_string(ID);
            VarDecl *AcceptVD = VarDecl::Create(
                *Context, TU, Loc, Loc, &Context->Idents.get(AcceptName),
                AcceptArrTy, nullptr, SC_Static
            );
            AcceptVD->addAttr(UsedAttr::CreateImplicit(*Context));

            std::vector<Expr *> AcceptInits;
            AcceptInits.reserve(DFA.AcceptMask.size());
            for (uint8_t v : DFA.AcceptMask) {
                llvm::APInt Val(8, v);
                AcceptInits.push_back(IntegerLiteral::Create(*Context, Val, Context->UnsignedCharTy, Loc));
            }
            auto *AcceptILE = new (*Context) InitListExpr(*Context, Loc, AcceptInits, Loc, true);
            AcceptILE->setType(AcceptArrTy);
            AcceptVD->setInit(AcceptILE);
            TU->addDecl(AcceptVD);
            SynthesizedVarDefs.push_back(AcceptVD);

            // 3. static const struct veriobjc_dfa __veriobjc_dfa_obj_<ID>
            QualType DFAStructTy = Context->getTypeDeclType(static_cast<const TypeDecl *>(DFAStructRD)).withConst();
            std::string ObjName = "__veriobjc_dfa_obj_" + std::to_string(ID);
            DFAObjVD = VarDecl::Create(
                *Context, TU, Loc, Loc, &Context->Idents.get(ObjName),
                DFAStructTy, nullptr, SC_Static
            );
            DFAObjVD->addAttr(UsedAttr::CreateImplicit(*Context));

            QualType ConstUInt8PtrTy = Context->getPointerType(ConstUInt8Ty);
            DeclRefExpr *TransRef = DeclRefExpr::Create(
                *Context, NestedNameSpecifierLoc(), SourceLocation(),
                TransVD, false, Loc, TransArrTy, VK_LValue
            );
            ImplicitCastExpr *TransDecay = ImplicitCastExpr::Create(
                *Context, ConstUInt8PtrTy, CK_ArrayToPointerDecay,
                TransRef, nullptr, VK_PRValue, FPOptionsOverride()
            );

            DeclRefExpr *AcceptRef = DeclRefExpr::Create(
                *Context, NestedNameSpecifierLoc(), SourceLocation(),
                AcceptVD, false, Loc, AcceptArrTy, VK_LValue
            );
            ImplicitCastExpr *AcceptDecay = ImplicitCastExpr::Create(
                *Context, ConstUInt8PtrTy, CK_ArrayToPointerDecay,
                AcceptRef, nullptr, VK_PRValue, FPOptionsOverride()
            );

            llvm::APInt NumStatesVal(32, DFA.NumStates);
            llvm::APInt StartStateVal(32, DFA.StartState);
            Expr *ObjFields[4] = {
                TransDecay,
                AcceptDecay,
                IntegerLiteral::Create(*Context, NumStatesVal, Context->UnsignedIntTy, Loc),
                IntegerLiteral::Create(*Context, StartStateVal, Context->UnsignedIntTy, Loc)
            };
            auto *ObjILE = new (*Context) InitListExpr(*Context, Loc, ObjFields, Loc, true);
            ObjILE->setType(DFAStructTy);
            DFAObjVD->setInit(ObjILE);
            TU->addDecl(DFAObjVD);
            SynthesizedVarDefs.push_back(DFAObjVD);

            CompiledDFADecls[Pattern] = DFAObjVD;
        }

        // Find veriobjc_dfa_match FunctionDecl
        FunctionDecl *MatchFD = nullptr;
        for (Decl *D : Context->getTranslationUnitDecl()->decls()) {
            if (auto *FD = dyn_cast<FunctionDecl>(D)) {
                if (FD->getName() == "veriobjc_dfa_match") {
                    MatchFD = FD;
                    break;
                }
            }
        }
        if (!MatchFD || !DFAObjVD) return nullptr;

        DeclRefExpr *MatchCalleeRef = DeclRefExpr::Create(
            *Context, NestedNameSpecifierLoc(), SourceLocation(),
            MatchFD, false, Loc, MatchFD->getType(), VK_LValue
        );
        ImplicitCastExpr *MatchCalleeCast = ImplicitCastExpr::Create(
            *Context, Context->getPointerType(MatchFD->getType()),
            CK_FunctionToPointerDecay, MatchCalleeRef, nullptr, VK_PRValue, FPOptionsOverride()
        );

        DeclRefExpr *DFAObjRef = DeclRefExpr::Create(
            *Context, NestedNameSpecifierLoc(), SourceLocation(),
            DFAObjVD, false, Loc, DFAObjVD->getType(), VK_LValue
        );
        UnaryOperator *DFAObjAddr = UnaryOperator::Create(
            *Context, DFAObjRef, UO_AddrOf,
            Context->getPointerType(DFAObjVD->getType()),
            VK_PRValue, OK_Ordinary, Loc, false, FPOptionsOverride()
        );

        Expr *CallArgs[3] = { DFAObjAddr, CE->getArg(1), CE->getArg(2) };
        return CallExpr::Create(
            *Context, MatchCalleeCast, CallArgs, MatchFD->getReturnType(),
            VK_PRValue, CE->getRParenLoc(), FPOptionsOverride()
        );
    }

    struct VConstexprState {
        static constexpr unsigned MAX_STEPS = 4096;
        static constexpr unsigned MAX_DEPTH = 64;
        unsigned StepsUsed = 0;
        unsigned MaxDepthSeen = 0;
        bool FuelExhausted = false;
        const FunctionDecl *ActiveFD = nullptr;
    };

    bool consumeFuel(VConstexprState &State, unsigned Depth) {
        if (Depth > State.MaxDepthSeen) {
            State.MaxDepthSeen = Depth;
        }
        State.StepsUsed++;
        if (State.StepsUsed > VConstexprState::MAX_STEPS || Depth > VConstexprState::MAX_DEPTH) {
            State.FuelExhausted = true;
            return false;
        }
        return true;
    }

    bool evalVConstexprExpr(
        const Expr *E,
        llvm::DenseMap<const VarDecl *, int64_t> &Env,
        VConstexprState &State,
        unsigned Depth,
        int64_t &OutVal
    ) {
        if (!E || State.FuelExhausted) return false;
        if (!consumeFuel(State, Depth)) return false;

        E = E->IgnoreParenImpCasts();

        if (const auto *IL = dyn_cast<IntegerLiteral>(E)) {
            OutVal = IL->getValue().getSExtValue();
            return true;
        }
        if (const auto *DRE = dyn_cast<DeclRefExpr>(E)) {
            if (const auto *VD = dyn_cast<VarDecl>(DRE->getDecl())) {
                auto It = Env.find(VD);
                if (It != Env.end()) {
                    OutVal = It->second;
                    return true;
                }
            }
            return false;
        }
        if (const auto *UO = dyn_cast<UnaryOperator>(E)) {
            int64_t Sub = 0;
            if (!evalVConstexprExpr(UO->getSubExpr(), Env, State, Depth, Sub)) return false;
            switch (UO->getOpcode()) {
                case UO_Minus: OutVal = -Sub; return true;
                case UO_Plus:  OutVal = +Sub; return true;
                case UO_Not:   OutVal = ~Sub; return true;
                case UO_LNot:  OutVal = !Sub ? 1 : 0; return true;
                default: return false;
            }
        }
        if (const auto *BO = dyn_cast<BinaryOperator>(E)) {
            if (BO->getOpcode() == BO_Assign) {
                const auto *LHSRef = dyn_cast<DeclRefExpr>(BO->getLHS()->IgnoreParenImpCasts());
                const auto *VD = LHSRef ? dyn_cast<VarDecl>(LHSRef->getDecl()) : nullptr;
                if (!VD) return false;
                int64_t RHS = 0;
                if (!evalVConstexprExpr(BO->getRHS(), Env, State, Depth, RHS)) return false;
                Env[VD] = RHS;
                OutVal = RHS;
                return true;
            }
            int64_t L = 0, R = 0;
            if (!evalVConstexprExpr(BO->getLHS(), Env, State, Depth, L)) return false;
            if (!evalVConstexprExpr(BO->getRHS(), Env, State, Depth, R)) return false;
            switch (BO->getOpcode()) {
                case BO_Add: OutVal = L + R; return true;
                case BO_Sub: OutVal = L - R; return true;
                case BO_Mul: OutVal = L * R; return true;
                case BO_Div: if (R == 0) return false; OutVal = L / R; return true;
                case BO_Rem: if (R == 0) return false; OutVal = L % R; return true;
                case BO_Shl: OutVal = L << R; return true;
                case BO_Shr: OutVal = L >> R; return true;
                case BO_And: OutVal = L & R; return true;
                case BO_Xor: OutVal = L ^ R; return true;
                case BO_Or:  OutVal = L | R; return true;
                case BO_LT:  OutVal = (L < R) ? 1 : 0; return true;
                case BO_GT:  OutVal = (L > R) ? 1 : 0; return true;
                case BO_LE:  OutVal = (L <= R) ? 1 : 0; return true;
                case BO_GE:  OutVal = (L >= R) ? 1 : 0; return true;
                case BO_EQ:  OutVal = (L == R) ? 1 : 0; return true;
                case BO_NE:  OutVal = (L != R) ? 1 : 0; return true;
                case BO_LAnd: OutVal = (L && R) ? 1 : 0; return true;
                case BO_LOr:  OutVal = (L || R) ? 1 : 0; return true;
                default: return false;
            }
        }
        if (const auto *CE = dyn_cast<CallExpr>(E)) {
            if (const FunctionDecl *CalleeFD = CE->getDirectCallee()) {
                if (hasVeriObjCAnnotation(CalleeFD, "objc_vconstexpr") && CalleeFD->hasBody()) {
                    std::vector<int64_t> ArgVals;
                    for (unsigned i = 0; i < CE->getNumArgs(); ++i) {
                        int64_t V = 0;
                        if (!evalVConstexprExpr(CE->getArg(i), Env, State, Depth, V)) {
                            return false;
                        }
                        ArgVals.push_back(V);
                    }
                    return evalVConstexprCall(CalleeFD, ArgVals, State, Depth + 1, OutVal);
                }
            }
        }
        return false;
    }

    bool evalVConstexprStmt(
        const Stmt *S,
        llvm::DenseMap<const VarDecl *, int64_t> &Env,
        VConstexprState &State,
        unsigned Depth,
        bool &HasReturned,
        int64_t &RetVal
    ) {
        if (!S || State.FuelExhausted) return false;
        if (!consumeFuel(State, Depth)) return false;

        if (const auto *CS = dyn_cast<CompoundStmt>(S)) {
            for (const Stmt *Child : CS->body()) {
                if (!evalVConstexprStmt(Child, Env, State, Depth, HasReturned, RetVal)) {
                    return false;
                }
                if (HasReturned) return true;
            }
            return true;
        }
        if (const auto *DS = dyn_cast<DeclStmt>(S)) {
            for (const Decl *D : DS->decls()) {
                if (const auto *VD = dyn_cast<VarDecl>(D)) {
                    int64_t InitVal = 0;
                    if (VD->getInit()) {
                        if (!evalVConstexprExpr(VD->getInit(), Env, State, Depth, InitVal)) {
                            return false;
                        }
                    }
                    Env[VD] = InitVal;
                }
            }
            return true;
        }
        if (const auto *RS = dyn_cast<ReturnStmt>(S)) {
            if (RS->getRetValue()) {
                if (!evalVConstexprExpr(RS->getRetValue(), Env, State, Depth, RetVal)) {
                    return false;
                }
            }
            HasReturned = true;
            return true;
        }
        if (const auto *IS = dyn_cast<IfStmt>(S)) {
            int64_t Cond = 0;
            if (!evalVConstexprExpr(IS->getCond(), Env, State, Depth, Cond)) return false;
            if (Cond != 0) {
                return evalVConstexprStmt(IS->getThen(), Env, State, Depth, HasReturned, RetVal);
            } else if (IS->getElse()) {
                return evalVConstexprStmt(IS->getElse(), Env, State, Depth, HasReturned, RetVal);
            }
            return true;
        }
        if (const auto *WS = dyn_cast<WhileStmt>(S)) {
            while (true) {
                int64_t Cond = 0;
                if (!evalVConstexprExpr(WS->getCond(), Env, State, Depth, Cond)) return false;
                if (Cond == 0) break;
                if (!evalVConstexprStmt(WS->getBody(), Env, State, Depth, HasReturned, RetVal)) return false;
                if (HasReturned) return true;
            }
            return true;
        }
        if (const auto *E = dyn_cast<Expr>(S)) {
            int64_t Unused = 0;
            return evalVConstexprExpr(E, Env, State, Depth, Unused);
        }
        return false;
    }

    bool evalVConstexprCall(
        const FunctionDecl *FD,
        const std::vector<int64_t> &Args,
        VConstexprState &State,
        unsigned Depth,
        int64_t &OutVal
    ) {
        State.ActiveFD = FD;
        if (!consumeFuel(State, Depth)) return false;

        auto MemoKey = std::make_pair(FD->getCanonicalDecl(), Args);
        auto MemoIt = VConstexprMemo.find(MemoKey);
        if (MemoIt != VConstexprMemo.end()) {
            OutVal = MemoIt->second;
            return true;
        }

        const FunctionDecl *DefFD = FD->getDefinition() ? FD->getDefinition() : FD;
        if (!DefFD->hasBody() || Args.size() != DefFD->getNumParams()) {
            return false;
        }

        llvm::DenseMap<const VarDecl *, int64_t> Env;
        for (size_t i = 0; i < Args.size(); ++i) {
            Env[DefFD->getParamDecl(i)] = Args[i];
        }

        bool HasReturned = false;
        int64_t RetVal = 0;
        if (!evalVConstexprStmt(DefFD->getBody(), Env, State, Depth, HasReturned, RetVal)) {
            return false;
        }
        if (!HasReturned) {
            return false;
        }
        VConstexprMemo[MemoKey] = RetVal;
        OutVal = RetVal;
        return true;
    }

    void reportEscapingBlock(SourceLocation Loc) {
        DiagnosticsEngine &Diag = Context->getDiagnostics();
        unsigned DiagID = Diag.getCustomDiagID(
            DiagnosticsEngine::Error,
            "VeriObjC Violation: Escaping function block is banned. Function blocks must be non-escaping and monomorphized at compile time."
        );
        Diag.Report(Loc, DiagID);
    }

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
        for (Stmt *&Child : S->children()) {
            if (!Child) continue;
            remapMethodVarRefs(Child, DeclMap);
            if (auto *DRE = dyn_cast<DeclRefExpr>(Child)) {
                auto It = DeclMap.find(DRE->getDecl());
                if (It != DeclMap.end()) {
                    Child = DeclRefExpr::Create(
                        *Context, NestedNameSpecifierLoc(), SourceLocation(),
                        It->second, /*RefersToEnclosingVariableOrCapture=*/false,
                        DRE->getLocation(), It->second->getType(), VK_LValue
                    );
                }
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

    void collectErrorReturnGraph(const Stmt *S, const FunctionDecl *CallerFD) {
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

    FunctionDecl *synthesizeMonomorphizedBlock(BlockExpr *BE, llvm::SmallVectorImpl<VarDecl *> &OutCaptures) {
        if (!BE) return nullptr;
        BlockDecl *BD = BE->getBlockDecl();
        const FunctionProtoType *FPT = BE->getFunctionType();
        if (!BD || !FPT) return nullptr;

        QualType RetTy = FPT->getReturnType();
        llvm::SmallVector<QualType, 8> ParamTypes;
        for (const ParmVarDecl *PVD : BD->parameters()) {
            ParamTypes.push_back(PVD->getType());
        }
        for (const BlockDecl::Capture &Cap : BD->captures()) {
            if (VarDecl *CapVD = Cap.getVariable()) {
                ParamTypes.push_back(CapVD->getType());
                OutCaptures.push_back(CapVD);
            }
        }

        FunctionProtoType::ExtProtoInfo EPI;
        QualType FnTy = Context->getFunctionType(RetTy, ParamTypes, EPI);
        std::string Name = "__veriobjc_mono_block_" + std::to_string(MonoBlockCounter++);
        IdentifierInfo &II = Context->Idents.get(Name);
        DeclContext *TU = Context->getTranslationUnitDecl();

        FunctionDecl *BlockFD = FunctionDecl::Create(
            *Context, TU, BD->getLocation(), BD->getLocation(),
            &II, FnTy, nullptr, SC_Static
        );

        llvm::SmallVector<ParmVarDecl *, 8> Params;
        llvm::DenseMap<const ValueDecl *, ValueDecl *> DeclMap;

        for (ParmVarDecl *OrigPVD : BD->parameters()) {
            ParmVarDecl *ClonedPVD = ParmVarDecl::Create(
                *Context, BlockFD, OrigPVD->getBeginLoc(), OrigPVD->getLocation(),
                OrigPVD->getIdentifier(), OrigPVD->getType(),
                OrigPVD->getTypeSourceInfo(), SC_None, nullptr
            );
            Params.push_back(ClonedPVD);
            DeclMap[OrigPVD] = ClonedPVD;
        }
        for (VarDecl *CapVD : OutCaptures) {
            ParmVarDecl *CapPVD = ParmVarDecl::Create(
                *Context, BlockFD, CapVD->getBeginLoc(), CapVD->getLocation(),
                CapVD->getIdentifier(), CapVD->getType(),
                nullptr, SC_None, nullptr
            );
            Params.push_back(CapPVD);
            DeclMap[CapVD] = CapPVD;
        }

        BlockFD->setParams(Params);
        TU->addDecl(BlockFD);

        Stmt *Body = BD->getBody();
        if (Body) {
            rewriteStmtTree(Body, BlockFD);
            remapMethodVarRefs(Body, DeclMap);
            BlockFD->setBody(Body);
            SynthesizedDefs.push_back(BlockFD);
        }
        return BlockFD;
    }

    Expr *buildMonomorphizedBlockCall(CallExpr *CE, FunctionDecl *BlockFD, llvm::ArrayRef<VarDecl *> Captures) {
        if (!CE || !BlockFD) return nullptr;
        DeclRefExpr *CalleeRef = DeclRefExpr::Create(
            *Context, NestedNameSpecifierLoc(), SourceLocation(),
            BlockFD, false, CE->getExprLoc(), BlockFD->getType(), VK_LValue
        );
        ImplicitCastExpr *CalleeCast = ImplicitCastExpr::Create(
            *Context, Context->getPointerType(BlockFD->getType()),
            CK_FunctionToPointerDecay, CalleeRef, nullptr, VK_PRValue, FPOptionsOverride()
        );

        llvm::SmallVector<Expr *, 8> CallArgs;
        for (unsigned i = 0; i < CE->getNumArgs(); ++i) {
            CallArgs.push_back(CE->getArg(i));
        }
        for (VarDecl *CapVD : Captures) {
            DeclRefExpr *CapRef = DeclRefExpr::Create(
                *Context, NestedNameSpecifierLoc(), SourceLocation(),
                CapVD, false, CE->getExprLoc(), CapVD->getType(), VK_LValue
            );
            ImplicitCastExpr *CapRVal = ImplicitCastExpr::Create(
                *Context, CapVD->getType(), CK_LValueToRValue,
                CapRef, nullptr, VK_PRValue, FPOptionsOverride()
            );
            CallArgs.push_back(CapRVal);
        }

        return CallExpr::Create(
            *Context, CalleeCast, CallArgs, BlockFD->getReturnType(),
            VK_PRValue, CE->getRParenLoc(), FPOptionsOverride()
        );
    }

    void rewriteStmtTree(Stmt *S, DeclContext *DC) {
        if (!S) return;
        for (Stmt *&Child : S->children()) {
            if (!Child) continue;
            if (auto *DS = dyn_cast<DeclStmt>(Child)) {
                for (Decl *D : DS->decls()) {
                    if (auto *VD = dyn_cast<VarDecl>(D)) {
                        if (VD->getType()->isBlockPointerType() && !VD->hasGlobalStorage() && VD->getInit()) {
                            if (auto *BE = dyn_cast<BlockExpr>(VD->getInit()->IgnoreParenImpCasts())) {
                                llvm::SmallVector<VarDecl *, 4> Captures;
                                if (FunctionDecl *BlockFD = synthesizeMonomorphizedBlock(BE, Captures)) {
                                    MonomorphizedBlocks[VD] = BlockFD;
                                    BlockCapturedVars[VD] = std::move(Captures);
                                    VD->setInit(nullptr);
                                    VD->setType(Context->IntTy);
                                }
                            }
                        }
                    }
                }
            }
            if (auto *OFS = dyn_cast<ObjCForCollectionStmt>(Child)) {
                if (Stmt *LoweredFor = buildLoweredForCollection(OFS, DC)) {
                    Child = LoweredFor;
                }
                continue;
            }
            rewriteStmtTree(Child, DC);
            if (auto *CE = dyn_cast<CallExpr>(Child)) {
                if (const FunctionDecl *CalleeFD = CE->getDirectCallee()) {
                    if (CalleeFD->getName() == "__veriobjc_return_err_from" && CE->getNumArgs() >= 2) {
                        Child = CE->getArg(0);
                        continue;
                    }
                    if (CalleeFD->getName() == "__veriobjc_regex_match" && CE->getNumArgs() >= 3) {
                        if (Expr *LoweredDFA = buildLoweredVRegexMatch(CE)) {
                            Child = LoweredDFA;
                        }
                        continue;
                    }
                    if (hasVeriObjCAnnotation(CalleeFD, "objc_vconstexpr")) {
                        std::vector<int64_t> ArgVals;
                        bool AllConstArgs = true;
                        llvm::DenseMap<const VarDecl *, int64_t> EmptyEnv;
                        VConstexprState ArgState;
                        for (unsigned i = 0; i < CE->getNumArgs(); ++i) {
                            int64_t V = 0;
                            if (!evalVConstexprExpr(CE->getArg(i), EmptyEnv, ArgState, 1, V)) {
                                AllConstArgs = false;
                                break;
                            }
                            ArgVals.push_back(V);
                        }
                        if (AllConstArgs) {
                            VConstexprState EvalState;
                            int64_t ResultVal = 0;
                            bool Ok = evalVConstexprCall(CalleeFD, ArgVals, EvalState, 1, ResultVal);
                            if (EvalState.FuelExhausted) {
                                DiagnosticsEngine &Diag = Context->getDiagnostics();
                                unsigned DiagID = Diag.getCustomDiagID(
                                    DiagnosticsEngine::Error,
                                    "VeriObjC Violation: vconstexpr evaluation of '%0' exhausted compile-time fuel budget (%1 steps, depth %2)."
                                );
                                const FunctionDecl *BlameFD = EvalState.ActiveFD ? EvalState.ActiveFD : CalleeFD;
                                Diag.Report(CE->getExprLoc(), DiagID)
                                    << BlameFD->getName() << EvalState.StepsUsed << EvalState.MaxDepthSeen;
                                continue;
                            }
                            if (Ok && CE->getType()->isIntegerType()) {
                                unsigned BitWidth = Context->getTypeSize(CE->getType());
                                llvm::APInt APVal(BitWidth, (uint64_t)ResultVal, CE->getType()->isSignedIntegerType());
                                Child = IntegerLiteral::Create(*Context, APVal, CE->getType(), CE->getExprLoc());
                                continue;
                            }
                        }
                    }
                } else if (Expr *Callee = CE->getCallee()) {
                    Callee = Callee->IgnoreParenImpCasts();
                    if (auto *DRE = dyn_cast<DeclRefExpr>(Callee)) {
                        if (const auto *VD = dyn_cast<VarDecl>(DRE->getDecl())) {
                            auto It = MonomorphizedBlocks.find(VD);
                            if (It != MonomorphizedBlocks.end()) {
                                if (Expr *DirectBlockCall = buildMonomorphizedBlockCall(CE, It->second, BlockCapturedVars[VD])) {
                                    Child = DirectBlockCall;
                                    continue;
                                }
                            }
                        }
                    } else if (auto *BE = dyn_cast<BlockExpr>(Callee)) {
                        llvm::SmallVector<VarDecl *, 4> Captures;
                        if (FunctionDecl *BlockFD = synthesizeMonomorphizedBlock(BE, Captures)) {
                            if (Expr *DirectBlockCall = buildMonomorphizedBlockCall(CE, BlockFD, Captures)) {
                                Child = DirectBlockCall;
                                continue;
                            }
                        }
                    }
                }
            }
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
    std::vector<Decl *> &getSynthesizedVarDefs() { return SynthesizedVarDefs; }

    // RULE 1: Intercept and BAN the generic 'id' keyword across all declarations
    bool VisitVarDecl(VarDecl *D) {
        QualType T = D->getType();
        if (T->isObjCIdType()) {
            reportBannedId(D->getLocation());
        }
        if (T->isBlockPointerType() && D->hasGlobalStorage()) {
            reportEscapingBlock(D->getLocation());
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
        if (T->isBlockPointerType()) {
            reportEscapingBlock(D->getLocation());
        }
        return true;
    }

    bool VisitReturnStmt(ReturnStmt *S) {
        if (const Expr *Ret = S->getRetValue()) {
            if (Ret->getType()->isBlockPointerType()) {
                reportEscapingBlock(S->getReturnLoc());
            }
        }
        return true;
    }

    bool VisitBinaryOperator(BinaryOperator *BO) {
        if (BO->getOpcode() == BO_Assign && BO->getLHS()->getType()->isBlockPointerType()) {
            reportEscapingBlock(BO->getOperatorLoc());
        }
        return true;
    }

    bool VisitTypedefNameDecl(TypedefNameDecl *D) {
        if (isa<ObjCTypeParamDecl>(D)) {
            return true;
        }
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

        if (ObjCTypeParamList *TPL = D->getTypeParamListAsWritten()) {
            for (ObjCTypeParamDecl *TPD : *TPL) {
                bool ValidBound = false;
                if (TPD->hasExplicitBound()) {
                    QualType BoundTy = TPD->getUnderlyingType();
                    if (const auto *OPT = BoundTy->getAs<ObjCObjectPointerType>()) {
                        if (const ObjCInterfaceDecl *BoundID = OPT->getInterfaceDecl()) {
                            if (BoundID->getName() != "NSObject" &&
                                (isSealedClass(BoundID) || isFinalClass(BoundID))) {
                                ValidBound = true;
                            }
                        }
                    }
                }
                if (!ValidBound) {
                    DiagnosticsEngine &Diag = Context->getDiagnostics();
                    unsigned DiagID = Diag.getCustomDiagID(
                        DiagnosticsEngine::Error,
                        "VeriObjC Violation: Generic type parameter '%0' on '%1' must have an explicit sealed constraint bound (unconstrained generics are banned)."
                    );
                    Diag.Report(TPD->getLocation(), DiagID) << TPD->getName() << D->getName();
                }
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
                collectErrorReturnGraph(FD->getBody(), FD->getCanonicalDecl());
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

    void verifyMultiFrameErrorReturns() {
        DiagnosticsEngine &Diag = Context->getDiagnostics();
        unsigned DiagID = Diag.getCustomDiagID(
            DiagnosticsEngine::Error,
            "VeriObjC Violation: Uncaught multi-frame error return 'from %0' in function '%1' outside active '%0' frame."
        );

        for (const auto &Entry : DirectRaisesFrom) {
            const FunctionDecl *OriginFD = Entry.first;
            for (const ErrFromReq &Req : Entry.second) {
                llvm::SmallVector<std::pair<const FunctionDecl *, SourceLocation>, 8> Worklist;
                llvm::DenseSet<const FunctionDecl *> Visited;
                Worklist.push_back({OriginFD, Req.Loc});

                while (!Worklist.empty()) {
                    auto [CurFD, ReportLoc] = Worklist.pop_back_val();
                    if (!Visited.insert(CurFD).second) {
                        continue;
                    }
                    if (CurFD->getName() == Req.TargetFrame) {
                        continue;
                    }
                    auto EdgeIt = CallEdges.find(CurFD);
                    if (EdgeIt == CallEdges.end() || EdgeIt->second.empty()) {
                        Diag.Report(ReportLoc, DiagID) << Req.TargetFrame << CurFD->getName();
                    } else {
                        for (const auto &[CallerFD, CallLoc] : EdgeIt->second) {
                            Worklist.push_back({CallerFD, CallLoc});
                        }
                    }
                }
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
        Visitor.verifyMultiFrameErrorReturns();
        if (Context.getDiagnostics().hasErrorOccurred()) {
            return;
        }
        EmittingSynthesized = true;
        for (Decl *VD : Visitor.getSynthesizedVarDefs()) {
            CI.getASTConsumer().HandleTopLevelDecl(DeclGroupRef(VD));
        }
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

struct ObjCVConstexprAttrInfo : public ParsedAttrInfo {
    ObjCVConstexprAttrInfo() {
        OptArgs = 0;
        static constexpr Spelling S[] = {
            {ParsedAttr::AS_GNU, "objc_vconstexpr"},
            {ParsedAttr::AS_C23, "objc_vconstexpr"},
            {ParsedAttr::AS_CXX11, "objc_vconstexpr"}
        };
        Spellings = S;
    }
    AttrHandling handleDeclAttribute(Sema &S, Decl *D, const ParsedAttr &Attr) const override {
        D->addAttr(AnnotateAttr::Create(S.Context, "objc_vconstexpr", nullptr, 0, Attr.getRange()));
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
static ParsedAttrInfoRegistry::Add<ObjCVConstexprAttrInfo> AttrVConstexpr(
    "objc_vconstexpr", "Mark function for fuel-bounded compile-time evaluation (vconstexpr)"
);
static FrontendPluginRegistry::Add<VeriObjCAction> X(

    "veriobjc-check",
    "Enforce VeriObjC structural invariants and eliminate non-deterministic features"
);
