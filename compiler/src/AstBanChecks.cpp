#include "veriobjc/VeriObjCVisitor.h"

#include <llvm/Support/raw_ostream.h>
#include <algorithm>
#include <array>
#include <bitset>
#include <set>

using namespace clang;

namespace veriobjc {
namespace {

bool isKnownVeriObjCAnnotation(llvm::StringRef Ann) {
    static constexpr llvm::StringRef kExactAllowed[] = {
        "objc_sealed",
        "objc_final",
        "objc_consumed",
        "objc_flat",
        "objc_vconstexpr",
        "objc_export",
        "objc_hw_kernel",
        "objc_nolatency",
        "objc_pure",
        "objc_cold",
        "objc_effect_alloc",
        "objc_effect_io",
        "objc_effect_blocking",
        "objc_effect_fence",
        "objc_effect_shared_retain",
        "objc_soa",
        "objc_vvariant",
        "objc_variant",
        "objc_vtrait",
        "objc_inplace_only",
        "objc_specialize",
        "objc_vresult",
        "objc_vasync_task",
        "objc_vatomic",
        "veriobjc_vvector"
    };
    for (llvm::StringRef Allowed : kExactAllowed) {
        if (Ann == Allowed) return true;
    }

    static constexpr llvm::StringRef kPrefixAllowed[] = {
        "objc_pre:",
        "objc_post:",
        "objc_vflat_map:",
        "objc_vhash_flat:",
        "objc_interface_by_value:",
        "objc_vvector:",
        "objc_vbits:",
        "objc_vstream:",
        "objc_token_shift:",
        "objc_kv_arena:",
        "objc_spsc_channel:",
        "objc_vdma_ring:"
    };
    for (llvm::StringRef Prefix : kPrefixAllowed) {
        if (Ann.starts_with(Prefix) && Ann.size() > Prefix.size()) {
            return true;
        }
    }
    return false;
}

llvm::StringRef suggestCanonicalVeriObjCMacro(llvm::StringRef Raw) {
    if (Raw.contains_insensitive("cold")) return "macro 'veriobjc_cold' (or '__attribute__((objc_cold))')";
    if (Raw.contains_insensitive("pur")) return "macro 'veriobjc_pure' (or '__attribute__((objc_pure))')";
    if (Raw.contains_insensitive("nolatency")) return "macro 'veriobjc_nolatency' (or '__attribute__((objc_nolatency))')";
    if (Raw.contains_insensitive("export")) return "macro 'veriobjc_export'";
    if (Raw.contains_insensitive("effect_io")) return "macro 'veriobjc_effect_io'";
    if (Raw.contains_insensitive("effect_alloc")) return "macro 'veriobjc_effect_alloc'";
    if (Raw.contains_insensitive("effect_blocking")) return "macro 'veriobjc_effect_blocking'";
    if (Raw.contains_insensitive("effect_fence")) return "macro 'veriobjc_effect_fence'";
    if (Raw.contains_insensitive("shared")) return "macro 'veriobjc_effect_shared_retain'";
    if (Raw.contains_insensitive("seal")) return "attribute '__attribute__((objc_sealed))'";
    if (Raw.contains_insensitive("final")) return "attribute '__attribute__((objc_final))'";
    if (Raw.contains_insensitive("flat")) return "attribute '__attribute__((objc_flat))'";
    if (Raw.contains_insensitive("consumed")) return "attribute '__attribute__((objc_consumed))'";
    if (Raw.contains_insensitive("constexpr")) return "macro 'vconstexpr'";
    if (Raw.contains_insensitive("hw_kernel")) return "attribute '__attribute__((objc_hw_kernel))'";
    if (Raw.contains_insensitive("pre")) return "macro 'vcontract_pre(\"...\")'";
    if (Raw.contains_insensitive("post")) return "macro 'vcontract_post(\"...\")'";
    return "macros ('veriobjc_cold', 'veriobjc_pure', 'veriobjc_nolatency', 'veriobjc_export')";
}

} // namespace

bool VeriObjCVisitor::VisitDecl(Decl *D) {
    if (!D || D->isImplicit()) return true;
    SourceManager &SM = Context->getSourceManager();
    for (const auto *A : D->specific_attrs<AnnotateAttr>()) {
        llvm::StringRef Ann = A->getAnnotation();
        if (!Ann.starts_with("veriobjc_") && !Ann.starts_with("objc_")) {
            continue;
        }
        if (isKnownVeriObjCAnnotation(Ann)) {
            continue;
        }
        std::string DeclName = "<anonymous>";
        if (const auto *ND = dyn_cast<NamedDecl>(D); ND && !ND->getNameAsString().empty()) {
            DeclName = ND->getNameAsString();
        }
        SourceLocation Loc = A->getLocation().isValid() ? A->getLocation() : D->getLocation();
        unsigned Line = SM.getSpellingLineNumber(Loc);
        unsigned Col = SM.getSpellingColumnNumber(Loc);
        reportError(
            Loc,
            "VeriObjC Violation: Unknown VeriObjC annotation '%0' on '%1' at %2:%3; use canonical %4 instead of raw annotate(\"...\")."
        ) << Ann << DeclName << Line << Col << suggestCanonicalVeriObjCMacro(Ann);
    }
    return true;
}
namespace {

struct NFAState {
    std::bitset<256> MatchBytes;
    int NextOnByte = -1;
    llvm::SmallVector<int, 2> Epsilons;
};

struct NFAFrag {
    int Start = -1;
    int Accept = -1;
};

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

void addEpsilonClosure(const std::vector<NFAState> &NFA, std::set<int> &StateSet) {
    llvm::SmallVector<int, 16> Work(StateSet.begin(), StateSet.end());
    while (!Work.empty()) {
        int s = Work.pop_back_val();
        for (int nxt : NFA[s].Epsilons) {
            if (StateSet.insert(nxt).second) Work.push_back(nxt);
        }
    }
}

} // namespace

bool VeriObjCVisitor::validateRegexSyntax(llvm::StringRef Pattern, SourceLocation Loc) {
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

bool VeriObjCVisitor::compileRegexToMinimizedDFA(llvm::StringRef Pattern, SourceLocation Loc, CompiledDFA &OutDFA) {
    if (!validateRegexSyntax(Pattern, Loc)) return false;

    std::vector<NFAState> NFA;
    ThompsonNFAParser Parser(Pattern, NFA);
    NFAFrag Root = Parser.parseAlt();

    static constexpr size_t MAX_DFA_STATES = 64;
    std::vector<std::array<uint32_t, 256>> RawTrans;
    std::vector<uint8_t> RawAccept;
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
    int32_t StartID = getOrAddSubset(StartSet);
    if (StartID < 0) return false;

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
    OutDFA.StartState = ClassOf[static_cast<uint32_t>(StartID)];
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

VarDecl *VeriObjCVisitor::synthesizeByteTableVar(llvm::StringRef Name, const std::vector<uint8_t> &Bytes, SourceLocation Loc) {
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

VarDecl *VeriObjCVisitor::getOrCreateDFAObjectVar(llvm::StringRef Pattern, SourceLocation Loc) {
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

Expr *VeriObjCVisitor::buildLoweredVRegexMatch(CallExpr *CE) {
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

bool VeriObjCVisitor::consumeFuel(VConstexprState &State, unsigned Depth) {
    if (Depth > State.MaxDepthSeen) State.MaxDepthSeen = Depth;
    if (++State.StepsUsed > VConstexprState::MAX_STEPS || Depth > VConstexprState::MAX_DEPTH) {
        State.FuelExhausted = true;
        return false;
    }
    return true;
}

bool VeriObjCVisitor::evalBinaryIntOp(BinaryOperator::Opcode Op, int64_t L, int64_t R, int64_t &OutVal) {
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

bool VeriObjCVisitor::evalVConstexprBinaryOp(
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

bool VeriObjCVisitor::evalVConstexprExpr(
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

bool VeriObjCVisitor::evalVConstexprStmt(
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

bool VeriObjCVisitor::evalVConstexprCall(
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

bool VeriObjCVisitor::evalConstPair(const Expr *E1, const Expr *E2, int64_t &Out1, int64_t &Out2) {
    llvm::DenseMap<const VarDecl *, int64_t> EmptyEnv;
    VConstexprState S1, S2;
    return evalVConstexprExpr(E1, EmptyEnv, S1, 1, Out1) &&
           evalVConstexprExpr(E2, EmptyEnv, S2, 1, Out2);
}

Expr *VeriObjCVisitor::tryFoldVConstexprCall(CallExpr *CE, const FunctionDecl *CalleeFD) {
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

Expr *VeriObjCVisitor::tryFoldReflectFieldCount(CallExpr *CE) {
    if (!CE || CE->getNumArgs() < 1) return nullptr;
    QualType ArgTy = CE->getArg(0)->getType();
    if (!ArgTy->isPointerType()) return nullptr;
    const auto *RT = ArgTy->getPointeeType()->getAs<RecordType>();
    const RecordDecl *RD = RT ? RT->getDecl()->getDefinition() : nullptr;
    if (!RD) return nullptr;

    uint32_t Count = 0;
    for (const FieldDecl *FD : RD->fields()) {
        (void)FD;
        Count++;
    }
    return IntegerLiteral::Create(*Context, llvm::APInt(32, Count), Context->UnsignedIntTy, CE->getExprLoc());
}

void VeriObjCVisitor::verifyStreamZipCall(CallExpr *E) {
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

void VeriObjCVisitor::verifyReduceLatencyCall(CallExpr *E) {
    int64_t WidthN = 0, Cycles = 0;
    if (!evalConstPair(E->getArg(0), E->getArg(1), WidthN, Cycles)) return;

    int64_t Expected = 0;
    while ((1LL << Expected) < WidthN) Expected++;
    if (Cycles != Expected) {
        reportError(E->getExprLoc(), "VeriObjC Violation: Adder tree latency mismatch: width %0 requires %1 cycles (clog2), but %2 cycles were declared.")
            << static_cast<int>(WidthN) << static_cast<int>(Expected) << static_cast<int>(Cycles);
    }
}

llvm::StringRef VeriObjCVisitor::classifyCallEffect(const FunctionDecl *CalleeFD) {
    if (!CalleeFD) return "";
    llvm::StringRef Name = CalleeFD->getName();
    if (hasVeriObjCAnnotation(CalleeFD, "objc_effect_alloc") ||
        Name == "malloc" || Name == "calloc" || Name == "realloc") {
        return "@alloc";
    }
    if (hasVeriObjCAnnotation(CalleeFD, "objc_effect_blocking") ||
        Name == "read" || Name == "write" || Name == "recv" ||
        Name == "send" || Name == "accept" || Name == "sleep" || Name == "usleep") {
        return "@blocking";
    }
    if (hasVeriObjCAnnotation(CalleeFD, "objc_effect_shared_retain") ||
        Name == "veriobjc_shared_retain" || Name == "objc_retain") {
        return "@shared-retain";
    }
    if (hasVeriObjCAnnotation(CalleeFD, "objc_effect_io")) return "@io";
    if (hasVeriObjCAnnotation(CalleeFD, "objc_effect_fence")) return "@fence";
    return "";
}

void VeriObjCVisitor::inspectCallSiteEffects(const CallExpr *CE, const FunctionDecl *CallerFD, std::vector<std::string> &Survivors) {
    const FunctionDecl *CalleeFD = CE->getDirectCallee();
    if (!CalleeFD) return;

    llvm::StringRef Name = CalleeFD->getName();
    llvm::StringRef Eff = classifyCallEffect(CalleeFD);
    if (!Eff.empty()) {
        unsigned Line = Context->getSourceManager().getSpellingLineNumber(CE->getExprLoc());
        Survivors.push_back("Line " + std::to_string(Line) + ": " + Eff.str() + " (" + Name.str() + ")");
    }

    bool IsColdCaller = hasVeriObjCAnnotation(CallerFD, "objc_cold");
    bool IsNolatency  = hasVeriObjCAnnotation(CallerFD, "objc_nolatency");
    bool IsPure       = hasVeriObjCAnnotation(CallerFD, "objc_pure");

    if (Eff == "@shared-retain" && !IsColdCaller) {
        reportError(CE->getExprLoc(), "VeriObjC Violation: @shared retain permitted only in @cold quarantine context (called '%0' outside @cold).")
            << Name;
        return;
    }
    if (!IsNolatency && !IsPure) return;

    if (Eff == "@blocking") {
        reportError(CE->getExprLoc(), "VeriObjC Violation: Banned blocking I/O in @nolatency context: '%0' disrupts deterministic execution.")
            << Name;
    } else if (IsPure && !Eff.empty()) {
        reportError(CE->getExprLoc(), "VeriObjC Violation: Effectful call '%0' (%1) is not permitted in a @pure context.")
            << Name << Eff;
    } else if (Eff.empty() && !CalleeFD->hasBody() && CalleeFD->getBuiltinID() == 0 &&
               !isInRuntimeHeader(CalleeFD->getLocation()) &&
               !hasVeriObjCAnnotation(CalleeFD, "objc_nolatency") &&
               !hasVeriObjCAnnotation(CalleeFD, "objc_pure") &&
               !hasVeriObjCAnnotation(CalleeFD, "objc_vconstexpr")) {
        reportError(CE->getExprLoc(), "VeriObjC Violation: Unannotated extern function '%0' defaults to @cold and cannot be called from a @nolatency context.")
            << Name;
    }
}

void VeriObjCVisitor::walkFunctionEffects(const Stmt *S, const FunctionDecl *CallerFD, std::vector<std::string> &Survivors) {
    if (!S) return;
    if (const auto *CE = dyn_cast<CallExpr>(S)) {
        inspectCallSiteEffects(CE, CallerFD, Survivors);
    }
    for (const Stmt *Child : S->children()) {
        walkFunctionEffects(Child, CallerFD, Survivors);
    }
}

bool VeriObjCVisitor::exprContainsBuiltinExpect(const Stmt *S) {
    if (!S) return false;
    if (const auto *CE = dyn_cast<CallExpr>(S)) {
        if (const FunctionDecl *Callee = CE->getDirectCallee()) {
            if (Callee->getName() == "__builtin_expect") return true;
        }
    }
    for (const Stmt *Child : S->children()) {
        if (exprContainsBuiltinExpect(Child)) return true;
    }
    return false;
}

void VeriObjCVisitor::walkFunctionBranches(const Stmt *S, unsigned &Proven, unsigned &Hinted, unsigned &Unproven) {
    if (!S) return;
    if (const auto *IS = dyn_cast<IfStmt>(S)) {
        const Expr *Cond = IS->getCond();
        bool BoolVal = false;
        if (Cond && Cond->IgnoreParenImpCasts()->EvaluateAsBooleanCondition(BoolVal, *Context)) {
            ++Proven;
        } else if (exprContainsBuiltinExpect(Cond)) {
            ++Hinted;
        } else {
            ++Unproven;
        }
    }
    for (const Stmt *Child : S->children()) {
        walkFunctionBranches(Child, Proven, Hinted, Unproven);
    }
}

void VeriObjCVisitor::auditFunctionEffectsAndReport(const FunctionDecl *FD) {
    if (!FD || !FD->hasBody()) return;
    std::vector<std::string> Survivors;
    walkFunctionEffects(FD->getBody(), FD, Survivors);
    if (Context->getDiagnostics().hasErrorOccurred()) return;

    if (Opts.EmitEffectsReport) {
        if (Survivors.empty()) {
            llvm::errs() << "[VeriObjC --effects-report] Function: " << FD->getName()
                         << " | Survivors: 0 (effect-free)\n";
        } else {
            for (const std::string &Entry : Survivors) {
                llvm::errs() << "[VeriObjC --effects-report] Function: " << FD->getName()
                             << " | " << Entry << "\n";
            }
        }
    }

    if (Opts.EmitBranchReport &&
        (hasVeriObjCAnnotation(FD, "objc_nolatency") || hasVeriObjCAnnotation(FD, "objc_pure"))) {
        unsigned Proven = 0, Hinted = 0, Unproven = 0;
        walkFunctionBranches(FD->getBody(), Proven, Hinted, Unproven);
        llvm::errs() << "[VeriObjC --branch-report] Function: " << FD->getName()
                     << " | Proven: " << Proven
                     << " | Hinted: " << Hinted
                     << " | Unproven: " << Unproven << "\n";
    }
}

bool VeriObjCVisitor::isAllowedFFIBoundaryType(QualType T) const {
    if (T.isNull() || T->isVoidType()) return true;
    if (T->isPointerType() || T->isObjCObjectPointerType() || T->isBlockPointerType() || T->isReferenceType()) {
        return false;
    }
    return findNonFlatField(T) == nullptr;
}

void VeriObjCVisitor::verifyExportFunctionAndEmitHeader(const FunctionDecl *D) {
    if (!isAllowedFFIBoundaryType(D->getReturnType())) {
        reportError(D->getLocation(), "VeriObjC Violation: Banned cross-boundary ownership: @export function '%0' cannot return non-flat type across FFI boundary.")
            << D->getName();
        return;
    }
    for (const ParmVarDecl *PVD : D->parameters()) {
        if (!isAllowedFFIBoundaryType(PVD->getType())) {
            reportError(PVD->getLocation(), "VeriObjC Violation: Banned cross-boundary ownership: @export function '%0' parameter '%1' is non-flat across FFI boundary.")
                << D->getName() << PVD->getName();
            return;
        }
    }
    for (const auto *A : D->specific_attrs<AnnotateAttr>()) {
        llvm::StringRef Ann = A->getAnnotation();
        if (Ann.starts_with("objc_pre:")) {
            if (Opts.EmitExportHeader) llvm::errs() << "// @pre(" << Ann.drop_front(9) << ")\n";
        } else if (Ann.starts_with("objc_post:")) {
            if (Opts.EmitExportHeader) llvm::errs() << "// @post(" << Ann.drop_front(10) << ")\n";
        }
    }
    if (!Opts.EmitExportHeader) return;
    llvm::errs() << "extern " << D->getReturnType().getAsString() << " " << D->getName() << "(";
    for (unsigned i = 0; i < D->getNumParams(); ++i) {
        if (i > 0) llvm::errs() << ", ";
        llvm::errs() << D->getParamDecl(i)->getType().getAsString() << " " << D->getParamDecl(i)->getName();
    }
    llvm::errs() << ");\n";
}

const EnumDecl *VeriObjCVisitor::extractVVariantTagEnum(const Expr *Cond, std::string &OutVariantName) const {
    if (!Cond) return nullptr;
    Cond = Cond->IgnoreParenCasts();
    if (const auto *CE = dyn_cast<CallExpr>(Cond)) {
        const FunctionDecl *Callee = CE->getDirectCallee();
        if (Callee && Callee->getName() == "__vvariant_tag_id" && CE->getNumArgs() >= 1) {
            Cond = CE->getArg(0)->IgnoreParenCasts();
        }
    }
    const auto *ME = dyn_cast<MemberExpr>(Cond);
    if (!ME) return nullptr;
    QualType BaseTy = ME->getBase()->IgnoreParenImpCasts()->getType();
    const auto *RT = BaseTy->getAs<RecordType>();
    const RecordDecl *RD = RT ? RT->getDecl()->getDefinition() : nullptr;
    if (!RD || (!hasVeriObjCAnnotation(RD, "objc_vvariant") && !hasVeriObjCAnnotation(RD, "objc_variant"))) {
        return nullptr;
    }

    const auto *ET = ME->getType()->getAs<EnumType>();
    if (!ET) return nullptr;
    OutVariantName = !RD->getName().empty() ? RD->getName().str() : BaseTy.getAsString();
    return ET->getDecl();
}

void VeriObjCVisitor::handleMonoInstantiateCall(CallExpr *E) {
    if (E->getNumArgs() < 4) return;
    const auto *PkgSL = dyn_cast<StringLiteral>(E->getArg(0)->IgnoreParenImpCasts());
    const auto *GenSL = dyn_cast<StringLiteral>(E->getArg(1)->IgnoreParenImpCasts());
    int64_t Bytes = 0, Budget = 0;
    if (!PkgSL || !GenSL || !evalConstPair(E->getArg(2), E->getArg(3), Bytes, Budget)) return;

    llvm::StringRef Pkg = PkgSL->getString();
    llvm::StringRef Gen = GenSL->getString();
    std::string Key = (Pkg + "::" + Gen).str();
    MonoGenericEntry &Entry = MonoGenericStats[Key];
    Entry.Package = Pkg.str();
    Entry.GenericName = Gen.str();

    bool IsSpecializedKey = Gen.contains('<');
    if (IsSpecializedKey && Entry.UniqueCount > 0) {
        Entry.DedupedCount++;
        return;
    }
    Entry.UniqueCount++;
    Entry.CodeBytes += static_cast<uint32_t>(Bytes);
    if (Entry.UniqueCount > static_cast<unsigned>(Budget)) {
        reportError(E->getExprLoc(), "VeriObjC Violation: Monomorphization budget exceeded in package '%0' for generic '%1' (%2 instantiations exceeds limit %3).")
            << Pkg << Gen << Entry.UniqueCount << static_cast<unsigned>(Budget);
    }
}

void VeriObjCVisitor::recordDAGEdgeCall(CallExpr *E) {
    if (E->getNumArgs() < 3) return;
    const auto *FromSL = dyn_cast<StringLiteral>(E->getArg(0)->IgnoreParenImpCasts());
    const auto *ToSL = dyn_cast<StringLiteral>(E->getArg(1)->IgnoreParenImpCasts());
    int64_t WCET = 0, Dummy = 0;
    if (!FromSL || !ToSL || !evalConstPair(E->getArg(2), E->getArg(2), WCET, Dummy)) return;
    DAGEdges.push_back({FromSL->getString().str(), ToSL->getString().str(), static_cast<uint32_t>(WCET), E->getExprLoc()});
}

bool VeriObjCVisitor::verifyTaskDAGAndComputeWCET(SourceLocation DefaultLoc, uint32_t &OutWCET) {
    OutWCET = 0;
    if (DAGEdges.empty()) return true;

    llvm::StringMap<llvm::SmallVector<std::pair<std::string, uint32_t>, 4>> Adj;
    std::set<std::string> Nodes;
    for (const auto &E : DAGEdges) {
        Adj[E.From].push_back({E.To, E.WCET});
        Nodes.insert(E.From);
        Nodes.insert(E.To);
    }

    llvm::StringMap<int> Color; // 0 = unvisited, 1 = visiting, 2 = done
    llvm::StringMap<uint32_t> LongestFrom;
    std::string CycleNode;

    auto dfs = [&](auto &self, const std::string &U) -> bool {
        Color[U] = 1;
        uint32_t Best = 0;
        for (const auto &[V, W] : Adj[U]) {
            if (Color[V] == 1) {
                CycleNode = V;
                return false;
            }
            if (Color[V] == 0 && !self(self, V)) return false;
            Best = std::max(Best, W + LongestFrom[V]);
        }
        Color[U] = 2;
        LongestFrom[U] = Best;
        return true;
    };

    for (const std::string &N : Nodes) {
        if (Color[N] == 0 && !dfs(dfs, N)) {
            SourceLocation Loc = DAGEdges.back().Loc.isValid() ? DAGEdges.back().Loc : DefaultLoc;
            reportError(Loc, "VeriObjC Violation: Cycle detected in static task DAG involving node '%0' — deadlock-freedom proof failed.")
                << CycleNode;
            return false;
        }
        OutWCET = std::max(OutWCET, LongestFrom[N]);
    }
    return true;
}

bool VeriObjCVisitor::VisitVarDecl(VarDecl *D) {
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
    if (const auto *RT = T->getAs<RecordType>()) {
        const RecordDecl *RD = RT->getDecl()->getDefinition();
        if (RD && hasVeriObjCAnnotation(RD, "objc_inplace_only") && D->getInit()) {
            if (isa<DeclRefExpr>(D->getInit()->IgnoreParenImpCasts())) {
                reportError(D->getLocation(), "VeriObjC Violation: Intermediate copy of built-in-place type '%0' is banned; thread destination slot via veriobjc_push_inplace.")
                    << RD->getName();
            }
        }
    }
    if (hasVeriObjCAnnotation(D, "objc_flat")) checkFlatPayloadType(T, D->getName(), D->getLocation());
    return true;
}

bool VeriObjCVisitor::VisitSwitchStmt(SwitchStmt *SS) {
    std::string VariantName;
    const EnumDecl *ED = extractVVariantTagEnum(SS->getCond(), VariantName);
    if (!ED) return true;

    llvm::DenseSet<int64_t> HandledValues;
    for (const SwitchCase *SC = SS->getSwitchCaseList(); SC; SC = SC->getNextSwitchCase()) {
        if (const auto *CS = dyn_cast<CaseStmt>(SC)) {
            Expr::EvalResult Res;
            if (CS->getLHS() && CS->getLHS()->EvaluateAsInt(Res, *Context)) {
                HandledValues.insert(Res.Val.getInt().getSExtValue());
            }
        }
    }
    for (const EnumConstantDecl *ECD : ED->enumerators()) {
        if (!HandledValues.contains(ECD->getInitVal().getSExtValue())) {
            reportError(SS->getSwitchLoc(), "VeriObjC Violation: Non-exhaustive vmatch on vvariant '%0': missing case '%1'.")
                << VariantName << ECD->getName();
        }
    }
    return true;
}

bool VeriObjCVisitor::VisitParmVarDecl(ParmVarDecl *D) {
    QualType T = D->getType();
    if (T->isObjCIdType()) reportBannedId(D->getLocation());
    checkLightweightGenerics(T, D->getLocation());
    if (isInferredSignatureType(T)) {
        reportError(D->getLocation(), "VeriObjC Violation: Banned inference in signature position: 'auto' is not permitted in function or method parameters.");
    }
    return true;
}

bool VeriObjCVisitor::VisitFunctionDecl(FunctionDecl *D) {
    if (hasVeriObjCAnnotation(D, "objc_specialize")) {
        reportError(D->getLocation(), "VeriObjC Violation: Banned specialization/probing: ad-hoc generic specialization or SFINAE probing is forbidden; use explicit 'where' constraints.");
    }
    if (hasVeriObjCAnnotation(D, "objc_export")) {
        if (isInferredSignatureType(D->getReturnType())) {
            reportError(D->getLocation(), "VeriObjC Violation: Banned inference in signature position: 'auto' return type is not permitted on @export functions.");
        } else {
            verifyExportFunctionAndEmitHeader(D);
        }
    }
    return true;
}

bool VeriObjCVisitor::VisitObjCCategoryDecl(ObjCCategoryDecl *D) {
    reportError(D->getLocation(), "VeriObjC Violation: Banned categories: Objective-C category '%0' violates closed-world dispatch.") << D->getName();
    return true;
}

bool VeriObjCVisitor::VisitObjCPropertyImplDecl(ObjCPropertyImplDecl *D) {
    if (D->getPropertyImplementation() == ObjCPropertyImplDecl::Dynamic) {
        reportError(D->getLocation(), "VeriObjC Violation: Banned dynamic property synthesis: '@dynamic' properties are forbidden in closed-world VeriObjC.");
    }
    return true;
}

bool VeriObjCVisitor::VisitObjCAtTryStmt(ObjCAtTryStmt *S) {
    reportError(S->getAtTryLoc(), "VeriObjC Violation: Banned exception unwinding: '@try/@catch/@throw' DWARF unwinding is forbidden; use 'return err from f'.");
    return true;
}

bool VeriObjCVisitor::VisitStmtExpr(StmtExpr *E) {
    if (!Context->getSourceManager().isInSystemHeader(E->getBeginLoc()) &&
        !isInRuntimeHeader(E->getBeginLoc())) {
        reportError(E->getBeginLoc(), "VeriObjC Violation: Banned computation macro: preprocessor statement-expression codegen is forbidden; use vconstexpr.");
    }
    return true;
}

bool VeriObjCVisitor::VisitObjCMethodDecl(ObjCMethodDecl *D) {
    if ((isa<ObjCProtocolDecl>(D->getDeclContext()) || hasVeriObjCAnnotation(D, "objc_export")) &&
        isInferredSignatureType(D->getReturnType())) {
        reportError(D->getLocation(), "VeriObjC Violation: Banned inference in signature position: 'auto' return type is not permitted on protocol or @export methods.");
    }
    return true;
}

bool VeriObjCVisitor::VisitFieldDecl(FieldDecl *D) {
    QualType T = D->getType();
    if (T->isObjCIdType()) reportBannedId(D->getLocation());
    if (T->isBlockPointerType()) reportEscapingBlock(D->getLocation());
    return true;
}

bool VeriObjCVisitor::VisitReturnStmt(ReturnStmt *S) {
    if (const Expr *Ret = S->getRetValue(); Ret && Ret->getType()->isBlockPointerType()) {
        reportEscapingBlock(S->getReturnLoc());
    }
    return true;
}

bool VeriObjCVisitor::VisitBinaryOperator(BinaryOperator *BO) {
    if (BO->getOpcode() == BO_Assign && BO->getLHS()->getType()->isBlockPointerType()) {
        reportEscapingBlock(BO->getOperatorLoc());
    }
    return true;
}

bool VeriObjCVisitor::VisitTypedefNameDecl(TypedefNameDecl *D) {
    if (isa<ObjCTypeParamDecl>(D)) return true;
    QualType T = D->getUnderlyingType();
    if (T->isObjCIdType()) reportBannedId(D->getLocation());
    if (hasVeriObjCAnnotation(D, "objc_flat")) checkFlatPayloadType(T, D->getName(), D->getLocation());
    return true;
}

bool VeriObjCVisitor::VisitRecordDecl(RecordDecl *D) {
    if (D->isCompleteDefinition()) {
        if (hasVeriObjCAnnotation(D, "objc_flat")) {
            checkFlatPayloadType(Context->getTypeDeclType(static_cast<const TypeDecl *>(D)), D->getName(), D->getLocation());
        }
        checkInterfaceByValueRecord(D, D->getLocation());
    }
    return true;
}

bool VeriObjCVisitor::VisitCallExpr(CallExpr *E) {
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
    } else if (Name == "__veriobjc_mono_instantiate") {
        handleMonoInstantiateCall(E);
    } else if (Name == "__veriobjc_dag_edge") {
        recordDAGEdgeCall(E);
    }
    return true;
}

bool VeriObjCVisitor::VisitObjCMessageExpr(ObjCMessageExpr *E) {
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

} // namespace veriobjc
