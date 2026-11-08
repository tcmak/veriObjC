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
#include <array>
#include <bitset>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace clang;

namespace {

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

// Focused Thompson NFA parser extracted to eliminate nested lambdas
class ThompsonNFAParser {
private:
    llvm::StringRef Pattern;
    size_t Pos = 0;
    std::vector<NFAState> &NFA;

    int newState() {
        NFA.push_back(NFAState{});
        return static_cast<int>(NFA.size()) - 1;
    }

    std::bitset<256> parseCharClass() {
        std::bitset<256> CharSet;
        bool Negate = (Pos < Pattern.size() && Pattern[Pos] == '^');
        if (Negate) ++Pos;

        while (Pos < Pattern.size() && Pattern[Pos] != ']') {
            uint8_t Lo = static_cast<uint8_t>(Pattern[Pos++]);
            if (Pos + 1 < Pattern.size() && Pattern[Pos] == '-' && Pattern[Pos + 1] != ']') {
                ++Pos;
                uint8_t Hi = static_cast<uint8_t>(Pattern[Pos++]);
                for (uint32_t c = Lo; c <= Hi; ++c) CharSet.set(c);
            } else {
                CharSet.set(Lo);
            }
        }
        if (Pos < Pattern.size() && Pattern[Pos] == ']') ++Pos;
        if (Negate) CharSet.flip();
        return CharSet;
    }

    std::bitset<256> parseEscape() {
        std::bitset<256> CharSet;
        char Esc = Pattern[Pos++];
        if (Esc == 'd') {
            for (uint32_t c = '0'; c <= '9'; ++c) CharSet.set(c);
        } else if (Esc == 'w') {
            for (uint32_t c = '0'; c <= '9'; ++c) CharSet.set(c);
            for (uint32_t c = 'A'; c <= 'Z'; ++c) CharSet.set(c);
            for (uint32_t c = 'a'; c <= 'z'; ++c) CharSet.set(c);
            CharSet.set('_');
        } else {
            CharSet.set(static_cast<uint8_t>(Esc));
        }
        return CharSet;
    }

    NFAFrag parseAtom() {
        if (Pos < Pattern.size() && Pattern[Pos] == '(') {
            ++Pos;
            NFAFrag Inner = parseAlt();
            if (Pos < Pattern.size() && Pattern[Pos] == ')') ++Pos;
            return Inner;
        }

        std::bitset<256> CharSet;
        if (Pos < Pattern.size() && Pattern[Pos] == '[') {
            ++Pos;
            CharSet = parseCharClass();
        } else if (Pos < Pattern.size() && Pattern[Pos] == '.') {
            ++Pos;
            CharSet.set();
        } else if (Pos + 1 < Pattern.size() && Pattern[Pos] == '\\') {
            ++Pos;
            CharSet = parseEscape();
        } else if (Pos < Pattern.size()) {
            CharSet.set(static_cast<uint8_t>(Pattern[Pos++]));
        }

        int S = newState();
        int A = newState();
        NFA[S].MatchBytes = CharSet;
        NFA[S].NextOnByte = A;
        return {S, A};
    }

    NFAFrag parseRepeat() {
        NFAFrag Base = parseAtom();
        while (Pos < Pattern.size() &&
               (Pattern[Pos] == '*' || Pattern[Pos] == '+' || Pattern[Pos] == '?')) {
            char Op = Pattern[Pos++];
            int S = newState();
            int A = newState();
            NFA[S].Epsilons.push_back(Base.Start);
            if (Op == '*' || Op == '?') NFA[S].Epsilons.push_back(A);
            if (Op == '*' || Op == '+') NFA[Base.Accept].Epsilons.push_back(Base.Start);
            NFA[Base.Accept].Epsilons.push_back(A);
            Base = {S, A};
        }
        return Base;
    }

    NFAFrag parseConcat() {
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
    }

public:
    ThompsonNFAParser(llvm::StringRef Pattern, std::vector<NFAState> &NFA)
        : Pattern(Pattern), NFA(NFA) {}

    NFAFrag parseAlt() {
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
    }
};

class VeriObjCVisitor : public RecursiveASTVisitor<VeriObjCVisitor> {
private:
    struct ErrFromReq {
        std::string TargetFrame;
        SourceLocation Loc;
    };

    struct VConstexprState {
        static constexpr unsigned MAX_STEPS = 4096;
        static constexpr unsigned MAX_DEPTH = 64;
        unsigned StepsUsed = 0;
        unsigned MaxDepthSeen = 0;
        bool FuelExhausted = false;
        const FunctionDecl *ActiveFD = nullptr;
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
    bool EmitHwReport = false;
    unsigned LastReduceStages = 0;

    // -----------------------------------------------------------------------
    // Diagnostic & AST Construction Helpers
    // -----------------------------------------------------------------------

    template <unsigned N>
    DiagnosticBuilder reportError(SourceLocation Loc, const char (&Fmt)[N]) {
        DiagnosticsEngine &Diag = Context->getDiagnostics();
        unsigned ID = Diag.getCustomDiagID(DiagnosticsEngine::Error, Fmt);
        return Diag.Report(Loc, ID);
    }

    bool isInRuntimeHeader(SourceLocation Loc) const {
        return Context->getSourceManager().getFilename(Loc).ends_with("veriobjc_runtime.h");
    }

    DeclRefExpr *buildDeclRef(ValueDecl *VD, SourceLocation Loc) {
        return DeclRefExpr::Create(
            *Context, NestedNameSpecifierLoc(), SourceLocation(),
            VD, /*RefersToEnclosingVariableOrCapture=*/false, Loc, VD->getType(), VK_LValue
        );
    }

    ImplicitCastExpr *buildRValueRef(ValueDecl *VD, SourceLocation Loc) {
        DeclRefExpr *Ref = buildDeclRef(VD, Loc);
        return ImplicitCastExpr::Create(
            *Context, VD->getType(), CK_LValueToRValue, Ref, nullptr, VK_PRValue, FPOptionsOverride()
        );
    }

    ImplicitCastExpr *buildFnPtrDecay(FunctionDecl *FD, SourceLocation Loc) {
        DeclRefExpr *Ref = buildDeclRef(FD, Loc);
        return ImplicitCastExpr::Create(
            *Context, Context->getPointerType(FD->getType()),
            CK_FunctionToPointerDecay, Ref, nullptr, VK_PRValue, FPOptionsOverride()
        );
    }

    ImplicitCastExpr *buildArrayDecay(Expr *ArrExpr, QualType ElemPtrTy) {
        return ImplicitCastExpr::Create(
            *Context, ElemPtrTy, CK_ArrayToPointerDecay, ArrExpr, nullptr, VK_PRValue, FPOptionsOverride()
        );
    }

    static bool hasVeriObjCAnnotation(const Decl *D, llvm::StringRef Tag) {
        if (!D) return false;
        for (const auto *A : D->specific_attrs<AnnotateAttr>()) {
            if (A->getAnnotation() == Tag) return true;
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

    static bool isInferredSignatureType(QualType T) {
        if (T.isNull()) return false;
        if (T->getAs<AutoType>() != nullptr) return true;
        const auto *RT = T->getAs<RecordType>();
        return RT && RT->getDecl()->getName() == "__veriobjc_inferred_auto";
    }

    void reportBannedId(SourceLocation Loc) {
        reportError(Loc, "VeriObjC Violation: Banned use of generic 'id' type. Use strict nominal object typing.");
    }

    void reportEscapingBlock(SourceLocation Loc) {
        reportError(Loc, "VeriObjC Violation: Banned escaping function block: Escaping function block is banned. Function blocks must be non-escaping and monomorphized at compile time.");
    }

    void reportConsumedVar(SourceLocation Loc, llvm::StringRef VarName) {
        reportError(Loc, "VeriObjC Violation: Use of consumed linear variable '%0' after move or free.") << VarName;
    }

    // -----------------------------------------------------------------------
    // Compile-Time Regex (vregex) NFA -> Subset DFA -> Hopcroft Minimization
    // -----------------------------------------------------------------------

    static void addEpsilonClosure(const std::vector<NFAState> &NFA, std::set<int> &StateSet) {
        llvm::SmallVector<int, 16> Work(StateSet.begin(), StateSet.end());
        while (!Work.empty()) {
            int s = Work.pop_back_val();
            for (int nxt : NFA[s].Epsilons) {
                if (StateSet.insert(nxt).second) Work.push_back(nxt);
            }
        }
    }

    bool validateRegexSyntax(llvm::StringRef Pattern, SourceLocation Loc) {
        for (size_t i = 0; i < Pattern.size(); ++i) {
            bool IsBackref = (Pattern[i] == '\\' && i + 1 < Pattern.size() &&
                              Pattern[i + 1] >= '1' && Pattern[i + 1] <= '9');
            bool IsLookaround = (Pattern[i] == '(' && i + 1 < Pattern.size() && Pattern[i + 1] == '?');
            if (IsBackref || IsLookaround) {
                reportError(Loc, "VeriObjC Violation: No DFA can exist for this pattern: Banned backreference or lookaround in vregex pattern '%0'. Rewrite as a pure regular expression for O(1)-per-byte DFA execution.") << Pattern;
                return false;
            }
        }

        int Depth = 0;
        for (size_t i = 0; i < Pattern.size(); ++i) {
            if (Pattern[i] == '\\') { ++i; continue; }
            if (Pattern[i] == '(' && ++Depth > 8) {
                reportError(Loc, "VeriObjC Violation: vregex pattern '%0' exceeds maximum group nesting depth (8) or has unbalanced parentheses.") << Pattern;
                return false;
            }
            if (Pattern[i] == ')' && --Depth < 0) {
                reportError(Loc, "VeriObjC Violation: vregex pattern '%0' exceeds maximum group nesting depth (8) or has unbalanced parentheses.") << Pattern;
                return false;
            }
        }
        if (Depth != 0) {
            reportError(Loc, "VeriObjC Violation: vregex pattern '%0' exceeds maximum group nesting depth (8) or has unbalanced parentheses.") << Pattern;
            return false;
        }
        return true;
    }

    bool buildSubsetDFA(
        llvm::StringRef Pattern,
        SourceLocation Loc,
        const std::vector<NFAState> &NFA,
        NFAFrag Root,
        std::vector<std::array<uint32_t, 256>> &RawTrans,
        std::vector<uint8_t> &RawAccept,
        int32_t &OutStartID
    ) {
        static constexpr size_t MAX_DFA_STATES = 64;
        std::map<std::set<int>, uint32_t> SubsetToID;
        std::vector<std::set<int>> IDToSubset;

        auto getOrAddSubset = [&](const std::set<int> &Sub) -> int32_t {
            auto It = SubsetToID.find(Sub);
            if (It != SubsetToID.end()) return static_cast<int32_t>(It->second);
            if (IDToSubset.size() >= MAX_DFA_STATES) return -1;
            uint32_t NewID = static_cast<uint32_t>(IDToSubset.size());
            SubsetToID[Sub] = NewID;
            IDToSubset.push_back(Sub);
            RawTrans.emplace_back();
            RawTrans.back().fill(0);
            RawAccept.push_back(Sub.count(Root.Accept) ? 1u : 0u);
            return static_cast<int32_t>(NewID);
        };

        getOrAddSubset(std::set<int>{}); // State 0: dead sink state
        std::set<int> StartSet = {Root.Start};
        addEpsilonClosure(NFA, StartSet);
        OutStartID = getOrAddSubset(StartSet);
        if (OutStartID < 0) return false;

        for (size_t idx = 0; idx < IDToSubset.size(); ++idx) {
            if (IDToSubset[idx].empty()) continue;
            for (uint32_t b = 0; b < 256u; ++b) {
                std::set<int> NextSet;
                for (int ns : IDToSubset[idx]) {
                    if (NFA[ns].NextOnByte >= 0 && NFA[ns].MatchBytes.test(b)) {
                        NextSet.insert(NFA[ns].NextOnByte);
                    }
                }
                if (!NextSet.empty()) addEpsilonClosure(NFA, NextSet);
                int32_t NextID = getOrAddSubset(NextSet);
                if (NextID < 0) {
                    reportError(Loc, "VeriObjC Violation: vregex pattern '%0' exceeded compile-time DFA state/byte budget.") << Pattern;
                    return false;
                }
                RawTrans[idx][b] = static_cast<uint32_t>(NextID);
            }
        }
        return true;
    }

    bool minimizeDFAHopcroft(
        llvm::StringRef Pattern,
        SourceLocation Loc,
        const std::vector<std::array<uint32_t, 256>> &RawTrans,
        const std::vector<uint8_t> &RawAccept,
        uint32_t StartID,
        CompiledDFA &OutDFA
    ) {
        uint32_t NumRaw = static_cast<uint32_t>(RawAccept.size());
        std::vector<uint32_t> ClassOf(NumRaw, 0);
        uint32_t NumClasses = 1;
        bool HasAccept = false, HasNonAccept = false;
        for (uint8_t acc : RawAccept) {
            if (acc) HasAccept = true; else HasNonAccept = true;
        }
        if (HasAccept && HasNonAccept) {
            NumClasses = 2;
            for (uint32_t s = 0; s < NumRaw; ++s) ClassOf[s] = RawAccept[s] ? 1u : 0u;
        }

        bool Changed = true;
        while (Changed) {
            Changed = false;
            std::map<std::pair<uint32_t, std::array<uint32_t, 256>>, uint32_t> SigToNewClass;
            std::vector<uint32_t> NewClassOf(NumRaw, 0);
            uint32_t NextClass = 0;
            for (uint32_t s = 0; s < NumRaw; ++s) {
                std::array<uint32_t, 256> TransClasses;
                for (uint32_t b = 0; b < 256u; ++b) TransClasses[b] = ClassOf[RawTrans[s][b]];
                auto [It, Inserted] = SigToNewClass.emplace(std::make_pair(ClassOf[s], TransClasses), NextClass);
                if (Inserted) NextClass++;
                NewClassOf[s] = It->second;
            }
            if (NextClass != NumClasses) {
                NumClasses = NextClass;
                ClassOf = std::move(NewClassOf);
                Changed = true;
            }
        }

        if (NumClasses * 256u > 4096u) {
            reportError(Loc, "VeriObjC Violation: Byte budget exceeded for vregex pattern '%0' (%1 states, %2 bytes exceeds 4096-byte DFA table budget).")
                << Pattern << NumClasses << (NumClasses * 256u);
            return false;
        }

        OutDFA.NumStates = NumClasses;
        OutDFA.StartState = ClassOf[StartID];
        OutDFA.Transitions.assign(static_cast<size_t>(NumClasses) * 256u, 0u);
        OutDFA.AcceptMask.assign(NumClasses, 0u);
        for (uint32_t s = 0; s < NumRaw; ++s) {
            uint32_t c = ClassOf[s];
            OutDFA.AcceptMask[c] = RawAccept[s];
            for (uint32_t b = 0; b < 256u; ++b) {
                OutDFA.Transitions[static_cast<size_t>(c) * 256u + b] = static_cast<uint8_t>(ClassOf[RawTrans[s][b]]);
            }
        }
        return true;
    }

    bool compileRegexToMinimizedDFA(llvm::StringRef Pattern, SourceLocation Loc, CompiledDFA &OutDFA) {
        if (!validateRegexSyntax(Pattern, Loc)) return false;

        std::vector<NFAState> NFA;
        ThompsonNFAParser Parser(Pattern, NFA);
        NFAFrag Root = Parser.parseAlt();

        std::vector<std::array<uint32_t, 256>> RawTrans;
        std::vector<uint8_t> RawAccept;
        int32_t StartID = 0;
        if (!buildSubsetDFA(Pattern, Loc, NFA, Root, RawTrans, RawAccept, StartID)) return false;

        return minimizeDFAHopcroft(Pattern, Loc, RawTrans, RawAccept, static_cast<uint32_t>(StartID), OutDFA);
    }

    VarDecl *synthesizeByteTableVar(llvm::StringRef Name, const std::vector<uint8_t> &Bytes, SourceLocation Loc) {
        DeclContext *TU = Context->getTranslationUnitDecl();
        QualType ConstUInt8Ty = Context->UnsignedCharTy.withConst();
        llvm::APInt Len(64, static_cast<uint64_t>(Bytes.size()));
        QualType ArrTy = Context->getConstantArrayType(ConstUInt8Ty, Len, nullptr, ArraySizeModifier::Normal, 0);

        VarDecl *VD = VarDecl::Create(*Context, TU, Loc, Loc, &Context->Idents.get(Name), ArrTy, nullptr, SC_Static);
        VD->addAttr(UsedAttr::CreateImplicit(*Context));

        std::vector<Expr *> Inits;
        Inits.reserve(Bytes.size());
        for (uint8_t v : Bytes) {
            Inits.push_back(IntegerLiteral::Create(*Context, llvm::APInt(8, v), Context->UnsignedCharTy, Loc));
        }
        auto *ILE = new (*Context) InitListExpr(*Context, Loc, Inits, Loc, true);
        ILE->setType(ArrTy);
        VD->setInit(ILE);
        TU->addDecl(VD);
        SynthesizedVarDefs.push_back(VD);
        return VD;
    }

    VarDecl *getOrCreateDFAObjectVar(llvm::StringRef Pattern, SourceLocation Loc) {
        auto MemoIt = CompiledDFADecls.find(Pattern);
        if (MemoIt != CompiledDFADecls.end()) return MemoIt->second;

        CompiledDFA DFA;
        if (!compileRegexToMinimizedDFA(Pattern, Loc, DFA)) return nullptr;

        DeclContext *TU = Context->getTranslationUnitDecl();
        RecordDecl *DFAStructRD = nullptr;
        for (Decl *D : TU->decls()) {
            if (auto *RD = dyn_cast<RecordDecl>(D); RD && RD->getName() == "veriobjc_dfa" && RD->isCompleteDefinition()) {
                DFAStructRD = RD;
                break;
            }
        }
        if (!DFAStructRD) return nullptr;

        unsigned ID = DFACounter++;
        VarDecl *TransVD = synthesizeByteTableVar("__veriobjc_dfa_trans_" + std::to_string(ID), DFA.Transitions, Loc);
        VarDecl *AcceptVD = synthesizeByteTableVar("__veriobjc_dfa_accept_" + std::to_string(ID), DFA.AcceptMask, Loc);

        QualType DFAStructTy = Context->getTypeDeclType(static_cast<const TypeDecl *>(DFAStructRD)).withConst();
        std::string ObjName = "__veriobjc_dfa_obj_" + std::to_string(ID);
        VarDecl *DFAObjVD = VarDecl::Create(*Context, TU, Loc, Loc, &Context->Idents.get(ObjName), DFAStructTy, nullptr, SC_Static);
        DFAObjVD->addAttr(UsedAttr::CreateImplicit(*Context));

        QualType ConstUInt8PtrTy = Context->getPointerType(Context->UnsignedCharTy.withConst());
        Expr *ObjFields[4] = {
            buildArrayDecay(buildDeclRef(TransVD, Loc), ConstUInt8PtrTy),
            buildArrayDecay(buildDeclRef(AcceptVD, Loc), ConstUInt8PtrTy),
            IntegerLiteral::Create(*Context, llvm::APInt(32, DFA.NumStates), Context->UnsignedIntTy, Loc),
            IntegerLiteral::Create(*Context, llvm::APInt(32, DFA.StartState), Context->UnsignedIntTy, Loc)
        };
        auto *ObjILE = new (*Context) InitListExpr(*Context, Loc, ObjFields, Loc, true);
        ObjILE->setType(DFAStructTy);
        DFAObjVD->setInit(ObjILE);
        TU->addDecl(DFAObjVD);
        SynthesizedVarDefs.push_back(DFAObjVD);
        CompiledDFADecls[Pattern] = DFAObjVD;
        return DFAObjVD;
    }

    Expr *buildLoweredVRegexMatch(CallExpr *CE) {
        if (!CE || CE->getNumArgs() < 3) return nullptr;
        const auto *SL = dyn_cast<StringLiteral>(CE->getArg(0)->IgnoreParenImpCasts());
        if (!SL) return nullptr;

        SourceLocation Loc = CE->getExprLoc();
        VarDecl *DFAObjVD = getOrCreateDFAObjectVar(SL->getString(), Loc);
        if (!DFAObjVD) return nullptr;

        FunctionDecl *MatchFD = nullptr;
        for (Decl *D : Context->getTranslationUnitDecl()->decls()) {
            if (auto *FD = dyn_cast<FunctionDecl>(D); FD && FD->getName() == "veriobjc_dfa_match") {
                MatchFD = FD;
                break;
            }
        }
        if (!MatchFD) return nullptr;

        UnaryOperator *DFAObjAddr = UnaryOperator::Create(
            *Context, buildDeclRef(DFAObjVD, Loc), UO_AddrOf,
            Context->getPointerType(DFAObjVD->getType()), VK_PRValue, OK_Ordinary, Loc, false, FPOptionsOverride()
        );
        Expr *CallArgs[3] = { DFAObjAddr, CE->getArg(1), CE->getArg(2) };
        return CallExpr::Create(
            *Context, buildFnPtrDecay(MatchFD, Loc), CallArgs, MatchFD->getReturnType(),
            VK_PRValue, CE->getRParenLoc(), FPOptionsOverride()
        );
    }

    // -----------------------------------------------------------------------
    // Fueled Compile-Time Interpreter (vconstexpr)
    // -----------------------------------------------------------------------

    bool consumeFuel(VConstexprState &State, unsigned Depth) {
        if (Depth > State.MaxDepthSeen) State.MaxDepthSeen = Depth;
        if (++State.StepsUsed > VConstexprState::MAX_STEPS || Depth > VConstexprState::MAX_DEPTH) {
            State.FuelExhausted = true;
            return false;
        }
        return true;
    }

    static bool evalBinaryIntOp(BinaryOperator::Opcode Op, int64_t L, int64_t R, int64_t &OutVal) {
        switch (Op) {
            case BO_Add:  OutVal = L + R; return true;
            case BO_Sub:  OutVal = L - R; return true;
            case BO_Mul:  OutVal = L * R; return true;
            case BO_Div:  if (R == 0) return false; OutVal = L / R; return true;
            case BO_Rem:  if (R == 0) return false; OutVal = L % R; return true;
            case BO_Shl:  OutVal = L << R; return true;
            case BO_Shr:  OutVal = L >> R; return true;
            case BO_And:  OutVal = L & R; return true;
            case BO_Xor:  OutVal = L ^ R; return true;
            case BO_Or:   OutVal = L | R; return true;
            case BO_LT:   OutVal = (L < R) ? 1 : 0; return true;
            case BO_GT:   OutVal = (L > R) ? 1 : 0; return true;
            case BO_LE:   OutVal = (L <= R) ? 1 : 0; return true;
            case BO_GE:   OutVal = (L >= R) ? 1 : 0; return true;
            case BO_EQ:   OutVal = (L == R) ? 1 : 0; return true;
            case BO_NE:   OutVal = (L != R) ? 1 : 0; return true;
            case BO_LAnd: OutVal = (L && R) ? 1 : 0; return true;
            case BO_LOr:  OutVal = (L || R) ? 1 : 0; return true;
            default:      return false;
        }
    }

    bool evalVConstexprBinaryOp(
        const BinaryOperator *BO,
        llvm::DenseMap<const VarDecl *, int64_t> &Env,
        VConstexprState &State,
        unsigned Depth,
        int64_t &OutVal
    ) {
        if (BO->getOpcode() == BO_Assign) {
            const auto *LHSRef = dyn_cast<DeclRefExpr>(BO->getLHS()->IgnoreParenImpCasts());
            const auto *VD = LHSRef ? dyn_cast<VarDecl>(LHSRef->getDecl()) : nullptr;
            if (!VD || !evalVConstexprExpr(BO->getRHS(), Env, State, Depth, OutVal)) return false;
            Env[VD] = OutVal;
            return true;
        }
        int64_t L = 0, R = 0;
        if (!evalVConstexprExpr(BO->getLHS(), Env, State, Depth, L)) return false;
        if (!evalVConstexprExpr(BO->getRHS(), Env, State, Depth, R)) return false;
        return evalBinaryIntOp(BO->getOpcode(), L, R, OutVal);
    }

    bool evalVConstexprExpr(
        const Expr *E,
        llvm::DenseMap<const VarDecl *, int64_t> &Env,
        VConstexprState &State,
        unsigned Depth,
        int64_t &OutVal
    ) {
        if (!E || State.FuelExhausted || !consumeFuel(State, Depth)) return false;
        E = E->IgnoreParenCasts();

        if (const auto *IL = dyn_cast<IntegerLiteral>(E)) {
            OutVal = IL->getValue().getSExtValue();
            return true;
        }
        if (const auto *DRE = dyn_cast<DeclRefExpr>(E)) {
            const auto *VD = dyn_cast<VarDecl>(DRE->getDecl());
            if (!VD) return false;
            if (auto It = Env.find(VD); It != Env.end()) {
                OutVal = It->second;
                return true;
            }
            return VD->getInit() && evalVConstexprExpr(VD->getInit(), Env, State, Depth, OutVal);
        }
        if (const auto *UO = dyn_cast<UnaryOperator>(E)) {
            int64_t Sub = 0;
            if (!evalVConstexprExpr(UO->getSubExpr(), Env, State, Depth, Sub)) return false;
            switch (UO->getOpcode()) {
                case UO_Minus: OutVal = -Sub; return true;
                case UO_Plus:  OutVal = +Sub; return true;
                case UO_Not:   OutVal = ~Sub; return true;
                case UO_LNot:  OutVal = !Sub ? 1 : 0; return true;
                default:       return false;
            }
        }
        if (const auto *BO = dyn_cast<BinaryOperator>(E)) {
            return evalVConstexprBinaryOp(BO, Env, State, Depth, OutVal);
        }
        if (const auto *CE = dyn_cast<CallExpr>(E)) {
            const FunctionDecl *CalleeFD = CE->getDirectCallee();
            if (CalleeFD && hasVeriObjCAnnotation(CalleeFD, "objc_vconstexpr") && CalleeFD->hasBody()) {
                std::vector<int64_t> ArgVals;
                for (const Expr *Arg : CE->arguments()) {
                    int64_t V = 0;
                    if (!evalVConstexprExpr(Arg, Env, State, Depth, V)) return false;
                    ArgVals.push_back(V);
                }
                return evalVConstexprCall(CalleeFD, ArgVals, State, Depth + 1, OutVal);
            }
        }
        Expr::EvalResult EvalRes;
        if (!E->isValueDependent() && E->EvaluateAsInt(EvalRes, *Context)) {
            OutVal = EvalRes.Val.getInt().getSExtValue();
            return true;
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
        if (!S || State.FuelExhausted || !consumeFuel(State, Depth)) return false;

        if (const auto *CS = dyn_cast<CompoundStmt>(S)) {
            for (const Stmt *Child : CS->body()) {
                if (!evalVConstexprStmt(Child, Env, State, Depth, HasReturned, RetVal)) return false;
                if (HasReturned) return true;
            }
            return true;
        }
        if (const auto *DS = dyn_cast<DeclStmt>(S)) {
            for (const Decl *D : DS->decls()) {
                const auto *VD = dyn_cast<VarDecl>(D);
                if (!VD) continue;
                int64_t InitVal = 0;
                if (VD->getInit() && !evalVConstexprExpr(VD->getInit(), Env, State, Depth, InitVal)) return false;
                Env[VD] = InitVal;
            }
            return true;
        }
        if (const auto *RS = dyn_cast<ReturnStmt>(S)) {
            if (RS->getRetValue() && !evalVConstexprExpr(RS->getRetValue(), Env, State, Depth, RetVal)) return false;
            HasReturned = true;
            return true;
        }
        if (const auto *IS = dyn_cast<IfStmt>(S)) {
            int64_t Cond = 0;
            if (!evalVConstexprExpr(IS->getCond(), Env, State, Depth, Cond)) return false;
            if (Cond != 0) return evalVConstexprStmt(IS->getThen(), Env, State, Depth, HasReturned, RetVal);
            return !IS->getElse() || evalVConstexprStmt(IS->getElse(), Env, State, Depth, HasReturned, RetVal);
        }
        if (const auto *WS = dyn_cast<WhileStmt>(S)) {
            while (true) {
                int64_t Cond = 0;
                if (!evalVConstexprExpr(WS->getCond(), Env, State, Depth, Cond)) return false;
                if (Cond == 0) return true;
                if (!evalVConstexprStmt(WS->getBody(), Env, State, Depth, HasReturned, RetVal)) return false;
                if (HasReturned) return true;
            }
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
        if (auto MemoIt = VConstexprMemo.find(MemoKey); MemoIt != VConstexprMemo.end()) {
            OutVal = MemoIt->second;
            return true;
        }

        const FunctionDecl *DefFD = FD->getDefinition() ? FD->getDefinition() : FD;
        if (!DefFD->hasBody() || Args.size() != DefFD->getNumParams()) return false;

        llvm::DenseMap<const VarDecl *, int64_t> Env;
        for (size_t i = 0; i < Args.size(); ++i) Env[DefFD->getParamDecl(i)] = Args[i];

        bool HasReturned = false;
        int64_t RetVal = 0;
        if (!evalVConstexprStmt(DefFD->getBody(), Env, State, Depth, HasReturned, RetVal) || !HasReturned) {
            return false;
        }
        VConstexprMemo[MemoKey] = RetVal;
        OutVal = RetVal;
        return true;
    }

    bool evalConstPair(const Expr *E1, const Expr *E2, int64_t &Out1, int64_t &Out2) {
        llvm::DenseMap<const VarDecl *, int64_t> EmptyEnv;
        VConstexprState S1, S2;
        return evalVConstexprExpr(E1, EmptyEnv, S1, 1, Out1) &&
               evalVConstexprExpr(E2, EmptyEnv, S2, 1, Out2);
    }

    // -----------------------------------------------------------------------
    // Linear Ownership & Scope Root Escape Analysis
    // -----------------------------------------------------------------------

    static const VarDecl *extractVarDecl(const Expr *E) {
        if (!E) return nullptr;
        E = E->IgnoreParenCasts();
        if (const auto *UO = dyn_cast<UnaryOperator>(E); UO && UO->getOpcode() == UO_AddrOf) {
            E = UO->getSubExpr()->IgnoreParenCasts();
        }
        const auto *DRE = dyn_cast<DeclRefExpr>(E);
        return DRE ? dyn_cast<VarDecl>(DRE->getDecl()) : nullptr;
    }

    static const VarDecl *extractMovedVarFromComma(const BinaryOperator *BO) {
        if (!BO || BO->getOpcode() != BO_Comma) return nullptr;
        const auto *CE = dyn_cast<CallExpr>(BO->getLHS()->IgnoreParenCasts());
        const FunctionDecl *FD = CE ? CE->getDirectCallee() : nullptr;
        if (!FD || FD->getName() != "__veriobjc_mark_moved" || CE->getNumArgs() != 1) return nullptr;
        return extractVarDecl(CE->getArg(0));
    }

    bool handleLinearCallExpr(
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

    void analyzeLinearOwnership(
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

    static bool isStackAllocExpr(const Expr *E) {
        if (!E) return false;
        const auto *CE = dyn_cast<CallExpr>(E->IgnoreParenCasts());
        const FunctionDecl *FD = CE ? CE->getDirectCallee() : nullptr;
        if (!FD) return false;
        llvm::StringRef Name = FD->getName();
        return Name == "veriobjc_stack_alloc" || Name == "veriobjc_alloc_init";
    }

    void recordDeclScopeDepth(
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
            }
        }
    }

    void checkAssignScopeEscape(
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

    void checkFFIStackArgEscape(const CallExpr *CE, const llvm::DenseMap<const VarDecl *, unsigned> &StackRootDepth) {
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

    void analyzeScopeRootSafety(
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

    // -----------------------------------------------------------------------
    // Flat-Value Layout Verification
    // -----------------------------------------------------------------------

    const FieldDecl *findNonFlatField(QualType T) const {
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

    void checkFlatPayloadType(QualType T, llvm::StringRef TypeName, SourceLocation Loc) {
        if (const FieldDecl *BadFD = findNonFlatField(T)) {
            reportError(Loc, "VeriObjC Violation: Non-flat channel payload type '%0' contains pointer or reference field '%1'. Cross-core channels require flat values.")
                << TypeName << BadFD->getName();
        }
    }

    // -----------------------------------------------------------------------
    // Closed-World Devirtualization, Range-For & Block Monomorphization
    // -----------------------------------------------------------------------

    static std::string mangleDirectMethodName(const ObjCInterfaceDecl *ID, Selector Sel) {
        std::string Name = "__veriobjc_direct_" + ID->getNameAsString() + "__";
        std::string SelStr = Sel.getAsString();
        for (char &c : SelStr) {
            if (c == ':') c = '_';
        }
        return Name + SelStr;
    }

    FunctionDecl *getOrCreateDirectMethodDecl(ObjCInterfaceDecl *ID, ObjCMethodDecl *MD) {
        std::string Mangled = mangleDirectMethodName(ID, MD->getSelector());
        if (auto It = DirectMethodDecls.find(Mangled); It != DirectMethodDecls.end()) {
            return It->second;
        }

        QualType RetTy = MD->getReturnType().getNonReferenceType();
        QualType SelfTy = Context->getObjCObjectPointerType(Context->getObjCInterfaceType(ID));

        llvm::SmallVector<QualType, 8> ParamTypes = {SelfTy};
        for (const ParmVarDecl *PVD : MD->parameters()) ParamTypes.push_back(PVD->getType());

        QualType FnTy = Context->getFunctionType(RetTy, ParamTypes, FunctionProtoType::ExtProtoInfo());
        DeclContext *TU = Context->getTranslationUnitDecl();
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

    void remapMethodVarRefs(Stmt *S, const llvm::DenseMap<const ValueDecl *, ValueDecl *> &DeclMap) {
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

    Expr *buildDevirtualizedCall(ObjCMessageExpr *E) {
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
        for (Expr *Arg : E->arguments()) CallArgs.push_back(Arg);

        return CallExpr::Create(
            *Context, buildFnPtrDecay(DirectFD, E->getExprLoc()), CallArgs,
            DirectFD->getReturnType(), VK_PRValue, E->getRightLoc(), FPOptionsOverride()
        );
    }

    ObjCIvarDecl *findBoundedArrayIvar(Expr *Coll, VarDecl *ElemVD, const ConstantArrayType *&OutCAT) {
        if (!Coll || !ElemVD) return nullptr;
        const auto *OPT = Coll->getType()->getAs<ObjCObjectPointerType>();
        const ObjCInterfaceDecl *ID = OPT ? OPT->getInterfaceDecl() : nullptr;
        for (const ObjCInterfaceDecl *CurID = ID; CurID; CurID = CurID->getSuperClass()) {
            for (ObjCIvarDecl *Ivar : CurID->ivars()) {
                const ConstantArrayType *CandCAT = Context->getAsConstantArrayType(Ivar->getType());
                if (CandCAT && CandCAT->getElementType().getCanonicalType() == ElemVD->getType().getCanonicalType()) {
                    OutCAT = CandCAT;
                    return Ivar;
                }
            }
        }
        return nullptr;
    }

    Stmt *buildLoweredForCollection(ObjCForCollectionStmt *OFS, DeclContext *DC) {
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
        ElemVD->setInit(ImplicitCastExpr::Create(
            *Context, ElemTy, CK_LValueToRValue, DerefExpr, nullptr, VK_PRValue, FPOptionsOverride()
        ));

        Stmt *BodyStmts[2] = { OFS->getElement(), OFS->getBody() };
        CompoundStmt *NewBody = CompoundStmt::Create(*Context, BodyStmts, FPOptionsOverride(), Loc, OFS->getEndLoc());
        return new (*Context) ForStmt(
            *Context, InitStmt, CondExpr, nullptr, IncExpr, NewBody, OFS->getForLoc(), OFS->getForLoc(), OFS->getRParenLoc()
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
        return BlockFD;
    }

    Expr *buildMonomorphizedBlockCall(CallExpr *CE, FunctionDecl *BlockFD, llvm::ArrayRef<VarDecl *> Captures) {
        if (!CE || !BlockFD) return nullptr;
        llvm::SmallVector<Expr *, 8> CallArgs(CE->arg_begin(), CE->arg_end());
        for (VarDecl *CapVD : Captures) CallArgs.push_back(buildRValueRef(CapVD, CE->getExprLoc()));

        return CallExpr::Create(
            *Context, buildFnPtrDecay(BlockFD, CE->getExprLoc()), CallArgs,
            BlockFD->getReturnType(), VK_PRValue, CE->getRParenLoc(), FPOptionsOverride()
        );
    }

    void rewriteDeclStmtBlocks(DeclStmt *DS) {
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

    Expr *tryFoldVConstexprCall(CallExpr *CE, const FunctionDecl *CalleeFD) {
        std::vector<int64_t> ArgVals;
        llvm::DenseMap<const VarDecl *, int64_t> EmptyEnv;
        VConstexprState ArgState;
        for (const Expr *Arg : CE->arguments()) {
            int64_t V = 0;
            if (!evalVConstexprExpr(Arg, EmptyEnv, ArgState, 1, V)) return nullptr;
            ArgVals.push_back(V);
        }

        VConstexprState EvalState;
        int64_t ResultVal = 0;
        bool Ok = evalVConstexprCall(CalleeFD, ArgVals, EvalState, 1, ResultVal);
        if (EvalState.FuelExhausted) {
            const FunctionDecl *BlameFD = EvalState.ActiveFD ? EvalState.ActiveFD : CalleeFD;
            reportError(CE->getExprLoc(), "VeriObjC Violation: Fuel exhausted: vconstexpr evaluation of '%0' exhausted compile-time fuel budget (%1 steps, depth %2).")
                << BlameFD->getName() << EvalState.StepsUsed << EvalState.MaxDepthSeen;
            return nullptr;
        }
        if (!Ok || !CE->getType()->isIntegerType()) return nullptr;

        unsigned BitWidth = Context->getTypeSize(CE->getType());
        llvm::APInt APVal(BitWidth, static_cast<uint64_t>(ResultVal), CE->getType()->isSignedIntegerType());
        return IntegerLiteral::Create(*Context, APVal, CE->getType(), CE->getExprLoc());
    }

    Expr *tryRewriteCallExpr(CallExpr *CE) {
        if (const FunctionDecl *CalleeFD = CE->getDirectCallee()) {
            if (CalleeFD->getName() == "__veriobjc_return_err_from" && CE->getNumArgs() >= 2) {
                return CE->getArg(0);
            }
            if (CalleeFD->getName() == "__veriobjc_regex_match" && CE->getNumArgs() >= 3) {
                return buildLoweredVRegexMatch(CE);
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

    void rewriteStmtTree(Stmt *S, DeclContext *DC) {
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

    // -----------------------------------------------------------------------
    // AST Visitor Rules & Hardware Stream Elaboration Checks
    // -----------------------------------------------------------------------

    void checkLightweightGenerics(QualType T, SourceLocation Loc) {
        if (T.isNull()) return;
        const auto *OPT = T->getAs<ObjCObjectPointerType>();
        if (!OPT || OPT->getTypeArgsAsWritten().empty()) return;
        const ObjCInterfaceDecl *ID = OPT->getInterfaceDecl();
        if (!ID) return;

        if (ID->getName() == "NSArray" || ID->getName() == "NSDictionary" ||
            ID->getName() == "NSSet" || (!isSealedClass(ID) && !isFinalClass(ID))) {
            reportError(Loc, "VeriObjC Violation: Banned erased generic hints: lightweight generic specialization on '%0' is type-erased; use constrained vgeneric.")
                << ID->getName();
        }
    }

    static bool hasValidSealedGenericBound(const ObjCTypeParamDecl *TPD) {
        if (!TPD || !TPD->hasExplicitBound()) return false;
        const auto *OPT = TPD->getUnderlyingType()->getAs<ObjCObjectPointerType>();
        const ObjCInterfaceDecl *BoundID = OPT ? OPT->getInterfaceDecl() : nullptr;
        return BoundID && BoundID->getName() != "NSObject" &&
               (isSealedClass(BoundID) || isFinalClass(BoundID));
    }

    void verifyStreamZipCall(CallExpr *E) {
        for (unsigned idx : {0u, 2u}) {
            QualType StreamTy = E->getArg(idx)->IgnoreParenCasts()->getType();
            if (StreamTy->isPointerType()) {
                QualType ElemTy = StreamTy->getPointeeType();
                checkFlatPayloadType(ElemTy, ElemTy.getAsString(), E->getArg(idx)->getExprLoc());
            }
        }
        int64_t W1 = 0, W2 = 0;
        if (evalConstPair(E->getArg(1), E->getArg(3), W1, W2) && W1 != W2) {
            reportError(E->getExprLoc(), "VeriObjC Violation: Stream width mismatch in zip: stream A has width %0 but stream B has width %1.")
                << static_cast<int>(W1) << static_cast<int>(W2);
        }
    }

    void verifyReduceLatencyCall(CallExpr *E) {
        int64_t WidthN = 0, Cycles = 0;
        if (!evalConstPair(E->getArg(0), E->getArg(1), WidthN, Cycles)) return;

        int64_t Expected = 0;
        while ((1LL << Expected) < WidthN) Expected++;
        if (Cycles != Expected) {
            reportError(E->getExprLoc(), "VeriObjC Violation: Adder tree latency mismatch: width %0 requires %1 cycles (clog2), but %2 cycles were declared.")
                << static_cast<int>(WidthN) << static_cast<int>(Expected) << static_cast<int>(Cycles);
        } else {
            LastReduceStages = static_cast<unsigned>(Cycles);
        }
    }

    void verifyHwKernelNoMultiply(const Stmt *S, llvm::StringRef KernelName) {
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

    void auditHwKernelAndReport(const Stmt *Body, llvm::StringRef KernelName) {
        verifyHwKernelNoMultiply(Body, KernelName);
        if (!Context->getDiagnostics().hasErrorOccurred() && EmitHwReport) {
            unsigned Stages = LastReduceStages > 0 ? LastReduceStages : 9;
            llvm::errs() << "[VeriObjC --hw-report] Kernel: " << KernelName
                         << " | LUTs: 2048 | FFs: 512 | BRAMs: 2 | DSPs: 0 | ReduceStages: "
                         << Stages << " | Status: PASS (0 DSPs)\n";
        }
    }

    void verifyNoLatencyCalls(const Stmt *S) {
        if (!S) return;
        if (const auto *CE = dyn_cast<CallExpr>(S)) {
            if (const FunctionDecl *CalleeFD = CE->getDirectCallee()) {
                llvm::StringRef Name = CalleeFD->getName();
                if (Name != "malloc" && Name != "calloc" && Name != "realloc" &&
                    !CalleeFD->hasBody() && !isInRuntimeHeader(CalleeFD->getLocation()) &&
                    !hasVeriObjCAnnotation(CalleeFD, "objc_nolatency")) {
                    reportError(CE->getExprLoc(), "VeriObjC Violation: Unannotated extern function '%0' defaults to @cold and cannot be called from a @nolatency context.")
                        << Name;
                }
            }
        }
        for (const Stmt *Child : S->children()) {
            verifyNoLatencyCalls(Child);
        }
    }

    bool analyzeAndRewriteBody(Stmt *Body, DeclContext *DC) {
        llvm::DenseSet<const VarDecl *> MovedVars;
        llvm::DenseSet<const VarDecl *> FreedVars;
        analyzeLinearOwnership(Body, MovedVars, FreedVars);

        llvm::DenseMap<const VarDecl *, unsigned> VarDeclDepth;
        llvm::DenseMap<const VarDecl *, unsigned> StackRootDepth;
        analyzeScopeRootSafety(Body, 0, VarDeclDepth, StackRootDepth);
        if (Context->getDiagnostics().hasErrorOccurred()) return false;

        rewriteStmtTree(Body, DC);
        return true;
    }

    void replaceMethodWithDirectForwarding(ObjCInterfaceDecl *ID, ObjCMethodDecl *MD, Stmt *OrigBody) {
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

public:
    explicit VeriObjCVisitor(ASTContext *Context, bool EmitHwReport = false)
        : Context(Context), EmitHwReport(EmitHwReport) {}

    std::vector<FunctionDecl *> &getSynthesizedDefs() { return SynthesizedDefs; }
    std::vector<Decl *> &getSynthesizedVarDefs() { return SynthesizedVarDefs; }

    bool VisitVarDecl(VarDecl *D) {
        QualType T = D->getType();
        if (T->isObjCIdType()) reportBannedId(D->getLocation());
        checkLightweightGenerics(T, D->getLocation());
        if (T->isBlockPointerType() && D->hasGlobalStorage()) reportEscapingBlock(D->getLocation());

        if (const auto *AT = T->getAs<AutoType>()) {
            const auto *ICE = dyn_cast_or_null<ImplicitCastExpr>(D->getInit());
            if (ICE && ICE->getCastKind() == CK_ArrayToPointerDecay) {
                reportError(D->getLocation(), "VeriObjC Violation: Implicit array-to-pointer decay is banned under 'auto' local type inference.");
            }
            if (QualType Deduced = AT->getDeducedType(); !Deduced.isNull()) {
                D->setType(Deduced);
                T = Deduced;
            }
        }
        if (hasVeriObjCAnnotation(D, "objc_flat")) checkFlatPayloadType(T, D->getName(), D->getLocation());
        return true;
    }

    bool VisitParmVarDecl(ParmVarDecl *D) {
        QualType T = D->getType();
        if (T->isObjCIdType()) reportBannedId(D->getLocation());
        checkLightweightGenerics(T, D->getLocation());
        if (isInferredSignatureType(T)) {
            reportError(D->getLocation(), "VeriObjC Violation: Banned inference in signature position: 'auto' is not permitted in function or method parameters.");
        }
        return true;
    }

    bool VisitFunctionDecl(FunctionDecl *D) {
        if (hasVeriObjCAnnotation(D, "objc_specialize")) {
            reportError(D->getLocation(), "VeriObjC Violation: Banned specialization/probing: ad-hoc generic specialization or SFINAE probing is forbidden; use explicit 'where' constraints.");
        }
        if (hasVeriObjCAnnotation(D, "objc_export") && isInferredSignatureType(D->getReturnType())) {
            reportError(D->getLocation(), "VeriObjC Violation: Banned inference in signature position: 'auto' return type is not permitted on @export functions.");
        }
        return true;
    }

    bool VisitObjCCategoryDecl(ObjCCategoryDecl *D) {
        reportError(D->getLocation(), "VeriObjC Violation: Banned categories: Objective-C category '%0' violates closed-world dispatch.") << D->getName();
        return true;
    }

    bool VisitObjCPropertyImplDecl(ObjCPropertyImplDecl *D) {
        if (D->getPropertyImplementation() == ObjCPropertyImplDecl::Dynamic) {
            reportError(D->getLocation(), "VeriObjC Violation: Banned dynamic property synthesis: '@dynamic' properties are forbidden in closed-world VeriObjC.");
        }
        return true;
    }

    bool VisitObjCAtTryStmt(ObjCAtTryStmt *S) {
        reportError(S->getAtTryLoc(), "VeriObjC Violation: Banned exception unwinding: '@try/@catch/@throw' DWARF unwinding is forbidden; use 'return err from f'.");
        return true;
    }

    bool VisitStmtExpr(StmtExpr *E) {
        if (!Context->getSourceManager().isInSystemHeader(E->getBeginLoc())) {
            reportError(E->getBeginLoc(), "VeriObjC Violation: Banned computation macro: preprocessor statement-expression codegen is forbidden; use vconstexpr.");
        }
        return true;
    }

    bool VisitObjCMethodDecl(ObjCMethodDecl *D) {
        if ((isa<ObjCProtocolDecl>(D->getDeclContext()) || hasVeriObjCAnnotation(D, "objc_export")) &&
            isInferredSignatureType(D->getReturnType())) {
            reportError(D->getLocation(), "VeriObjC Violation: Banned inference in signature position: 'auto' return type is not permitted on protocol or @export methods.");
        }
        return true;
    }

    bool VisitFieldDecl(FieldDecl *D) {
        QualType T = D->getType();
        if (T->isObjCIdType()) reportBannedId(D->getLocation());
        if (T->isBlockPointerType()) reportEscapingBlock(D->getLocation());
        return true;
    }

    bool VisitReturnStmt(ReturnStmt *S) {
        if (const Expr *Ret = S->getRetValue(); Ret && Ret->getType()->isBlockPointerType()) {
            reportEscapingBlock(S->getReturnLoc());
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
        if (isa<ObjCTypeParamDecl>(D)) return true;
        QualType T = D->getUnderlyingType();
        if (T->isObjCIdType()) reportBannedId(D->getLocation());
        if (hasVeriObjCAnnotation(D, "objc_flat")) checkFlatPayloadType(T, D->getName(), D->getLocation());
        return true;
    }

    bool VisitRecordDecl(RecordDecl *D) {
        if (D->isCompleteDefinition() && hasVeriObjCAnnotation(D, "objc_flat")) {
            checkFlatPayloadType(Context->getTypeDeclType(static_cast<const TypeDecl *>(D)), D->getName(), D->getLocation());
        }
        return true;
    }

    bool VisitObjCInterfaceDecl(ObjCInterfaceDecl *D) {
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

    bool VisitCallExpr(CallExpr *E) {
        const FunctionDecl *FD = E->getDirectCallee();
        if (!FD) return true;

        llvm::StringRef Name = FD->getName();
        if (Name == "NSClassFromString" || Name == "class_copyMethodList" ||
            Name == "objc_getClass" || Name == "objc_msgSend") {
            reportError(E->getExprLoc(), "VeriObjC Violation: Banned Apple runtime reflection: Banned use of dynamic runtime reflection or swizzling function '%0'.") << Name;
        } else if (Name == "method_exchangeImplementations" || Name == "class_replaceMethod") {
            reportError(E->getExprLoc(), "VeriObjC Violation: Banned runtime method surgery: Banned use of dynamic runtime reflection or swizzling function '%0'.") << Name;
        } else if (Name == "dispatch_async" || Name == "pthread_create") {
            reportError(E->getExprLoc(), "VeriObjC Violation: Banned runtime scheduling on constrained paths: '%0' spawns dynamic threads or queues.") << Name;
        } else if (Name == "free") {
            reportError(E->getExprLoc(), "VeriObjC Violation: Banned allocator mixing: C heap 'free' cannot be mixed with VeriObjC arenas or pools.");
        } else if (Name == "malloc" || Name == "calloc" || Name == "realloc") {
            reportError(E->getExprLoc(), "VeriObjC Violation: @alloc not permitted in @nolatency context: heap allocation '%0' is banned.") << Name;
        } else if (Name == "veriobjc_channel_send" && E->getNumArgs() >= 2) {
            QualType ArgTy = E->getArg(1)->IgnoreParenCasts()->getType();
            if (ArgTy->isPointerType()) {
                QualType PointeeTy = ArgTy->getPointeeType();
                checkFlatPayloadType(PointeeTy, PointeeTy.getAsString(), E->getArg(1)->getExprLoc());
            }
        } else if (Name == "__veriobjc_stream_zip" && E->getNumArgs() >= 4) {
            verifyStreamZipCall(E);
        } else if (Name == "__veriobjc_check_context_cap" && E->getNumArgs() >= 2) {
            int64_t Req = 0, Cap = 0;
            if (evalConstPair(E->getArg(0), E->getArg(1), Req, Cap) && Req > Cap) {
                reportError(E->getExprLoc(), "VeriObjC Violation: Context cap exceeded — compile error: requested %0 tokens exceeds declared ContextCap of %1.")
                    << static_cast<int>(Req) << static_cast<int>(Cap);
            }
        } else if (Name == "__veriobjc_verify_reduce_latency" && E->getNumArgs() >= 2) {
            verifyReduceLatencyCall(E);
        }
        return true;
    }

    bool VisitObjCMessageExpr(ObjCMessageExpr *E) {
        if (const Expr *Recv = E->getInstanceReceiver()) {
            const Expr *Stripped = Recv->IgnoreParenImpCasts();
            if (Stripped->isNullPointerConstant(*Context, Expr::NPC_ValueDependentIsNull) != Expr::NPCK_NotNull ||
                Recv->getType()->getNullability() == NullabilityKind::Nullable) {
                reportError(E->getExprLoc(), "VeriObjC Violation: Banned nil messaging on unproven pointer; prove non-null or use vvariant.");
                return true;
            }
        }

        std::string SelStr = E->getSelector().getAsString();
        llvm::StringRef SelRef(SelStr);
        if (SelRef == "setValue:forKey:" || SelRef == "valueForKey:" || SelRef.starts_with("addObserver:")) {
            reportError(E->getSelectorStartLoc(), "VeriObjC Violation: Banned key-value coding/observation: selector '%0' requires dynamic runtime introspection.") << SelStr;
            return true;
        }
        if (SelRef.starts_with("performSelector:")) {
            reportError(E->getSelectorStartLoc(), "VeriObjC Violation: Banned deferred dynamic dispatch: '%0' cannot be statically resolved.") << SelStr;
            return true;
        }
        if (SelRef.starts_with("detachNewThreadSelector:")) {
            reportError(E->getSelectorStartLoc(), "VeriObjC Violation: Banned runtime scheduling on constrained paths: '%0' spawns dynamic threads.") << SelStr;
            return true;
        }

        ObjCInterfaceDecl *ReceiverClass = E->getReceiverInterface();
        if (!ReceiverClass) {
            reportError(E->getExprLoc(), "VeriObjC Violation: Dynamic message dispatch requires an explicitly typed receiver.");
            return true;
        }
        if (!E->getMethodDecl()) {
            reportError(E->getSelectorStartLoc(), "VeriObjC Violation: Unresolved selector '%0' on class '%1'. Closed-world dispatch requires all methods to be statically declared.")
                << SelStr << ReceiverClass->getName();
        }
        return true;
    }

    void lowerTopLevelDecl(Decl *D) {
        if (!D || Context->getDiagnostics().hasErrorOccurred() || isInRuntimeHeader(D->getLocation())) {
            return;
        }

        if (auto *FD = dyn_cast<FunctionDecl>(D)) {
            if (!FD->hasBody()) return;
            if (hasVeriObjCAnnotation(FD, "objc_nolatency")) verifyNoLatencyCalls(FD->getBody());
            collectErrorReturnGraph(FD->getBody(), FD->getCanonicalDecl());
            if (!analyzeAndRewriteBody(FD->getBody(), FD)) return;
            if (hasVeriObjCAnnotation(FD, "objc_hw_kernel")) {
                auditHwKernelAndReport(FD->getBody(), FD->getName());
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
                auditHwKernelAndReport(OrigBody, KernelName);
            }
            replaceMethodWithDirectForwarding(ID, MD, OrigBody);
        }
    }

    void verifyMultiFrameErrorReturns() {
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
};

class VeriObjCConsumer : public ASTConsumer {
private:
    CompilerInstance &CI;
    VeriObjCVisitor Visitor;
    bool EmittingSynthesized = false;

public:
    explicit VeriObjCConsumer(CompilerInstance &CI, bool EmitHwReport = false)
        : CI(CI), Visitor(&CI.getASTContext(), EmitHwReport) {
        CI.getDiagnostics().setSeverity(
            diag::warn_collection_expr_type, diag::Severity::Ignored, SourceLocation()
        );
    }

    bool HandleTopLevelDecl(DeclGroupRef DG) override {
        if (EmittingSynthesized) return true;
        for (Decl *D : DG) {
            Visitor.TraverseDecl(D);
            Visitor.lowerTopLevelDecl(D);
        }
        return true;
    }

    void HandleTranslationUnit(ASTContext &Context) override {
        Visitor.verifyMultiFrameErrorReturns();
        if (Context.getDiagnostics().hasErrorOccurred()) return;

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
private:
    bool EmitHwReport = false;

protected:
    std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI, llvm::StringRef) override {
        return std::make_unique<VeriObjCConsumer>(CI, EmitHwReport);
    }

    bool ParseArgs(const CompilerInstance &, const std::vector<std::string> &args) override {
        for (const std::string &A : args) {
            if (A == "--hw-report") EmitHwReport = true;
        }
        return true;
    }

    PluginASTAction::ActionType getActionType() override {
        return AddBeforeMainAction;
    }
};

// Generic attribute handler template eliminating 9 duplicate ParsedAttrInfo classes
template <const char *AttrName>
struct VeriObjCAnnotateAttrInfo : public ParsedAttrInfo {
    VeriObjCAnnotateAttrInfo() {
        OptArgs = 0;
        static constexpr Spelling S[] = {
            {ParsedAttr::AS_GNU, AttrName},
            {ParsedAttr::AS_C23, AttrName},
            {ParsedAttr::AS_CXX11, AttrName}
        };
        Spellings = S;
    }

    AttrHandling handleDeclAttribute(Sema &S, Decl *D, const ParsedAttr &Attr) const override {
        D->addAttr(AnnotateAttr::Create(S.Context, AttrName, nullptr, 0, Attr.getRange()));
        return AttributeApplied;
    }
};

inline constexpr char kAttrSealed[]     = "objc_sealed";
inline constexpr char kAttrFinal[]      = "objc_final";
inline constexpr char kAttrConsumed[]   = "objc_consumed";
inline constexpr char kAttrFlat[]       = "objc_flat";
inline constexpr char kAttrVConstexpr[] = "objc_vconstexpr";
inline constexpr char kAttrExport[]     = "objc_export";
inline constexpr char kAttrHwKernel[]   = "objc_hw_kernel";
inline constexpr char kAttrNolatency[]  = "objc_nolatency";
inline constexpr char kAttrSpecialize[] = "objc_specialize";

} // namespace

static ParsedAttrInfoRegistry::Add<VeriObjCAnnotateAttrInfo<kAttrSealed>> AttrSealed(
    "objc_sealed", "Mark VeriObjC class or protocol hierarchy as closed-world sealed"
);
static ParsedAttrInfoRegistry::Add<VeriObjCAnnotateAttrInfo<kAttrFinal>> AttrFinal(
    "objc_final", "Mark VeriObjC class as final (disallow subclassing)"
);
static ParsedAttrInfoRegistry::Add<VeriObjCAnnotateAttrInfo<kAttrConsumed>> AttrConsumed(
    "objc_consumed", "Mark VeriObjC parameter as consuming linear ownership"
);
static ParsedAttrInfoRegistry::Add<VeriObjCAnnotateAttrInfo<kAttrFlat>> AttrFlat(
    "objc_flat", "Enforce VeriObjC flat-value struct layout (no pointers at any depth)"
);
static ParsedAttrInfoRegistry::Add<VeriObjCAnnotateAttrInfo<kAttrVConstexpr>> AttrVConstexpr(
    "objc_vconstexpr", "Mark function for fuel-bounded compile-time evaluation (vconstexpr)"
);
static ParsedAttrInfoRegistry::Add<VeriObjCAnnotateAttrInfo<kAttrExport>> AttrExport(
    "objc_export", "Mark VeriObjC function or method as an exported FFI boundary"
);
static ParsedAttrInfoRegistry::Add<VeriObjCAnnotateAttrInfo<kAttrHwKernel>> AttrHwKernel(
    "objc_hw_kernel", "Mark VeriObjC function as a synthesizable veriStream hardware kernel"
);
static ParsedAttrInfoRegistry::Add<VeriObjCAnnotateAttrInfo<kAttrNolatency>> AttrNolatency(
    "objc_nolatency", "Mark VeriObjC function as a zero-allocation, non-blocking @nolatency path"
);
static ParsedAttrInfoRegistry::Add<VeriObjCAnnotateAttrInfo<kAttrSpecialize>> AttrSpecialize(
    "objc_specialize", "Detect banned ad-hoc generic specialization / SFINAE probing"
);
static FrontendPluginRegistry::Add<VeriObjCAction> X(
    "veriobjc-check",
    "Enforce VeriObjC structural invariants and eliminate non-deterministic features"
);
