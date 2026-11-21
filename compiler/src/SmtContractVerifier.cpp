#include "veriobjc/VeriObjCVisitor.h"
#include "host/smt_bridge.h"

#include <llvm/ADT/SmallString.h>
#include <llvm/Support/FormatVariadic.h>
#include <llvm/Support/raw_ostream.h>
#include <cctype>
#include <optional>

using namespace clang;

namespace veriobjc {
namespace {

struct SmtBvValue {
    std::string Expr;
    unsigned Width = 0; // 0 = Bool sort; > 0 = (_ BitVec Width)
    bool IsSigned = true;
    bool IsConstLiteral = false;
    uint64_t ConstBits = 0;
};

struct SmtSymbolInfo {
    std::string SmtName;
    std::string DisplayName;
    unsigned Width = 32;
    bool IsSigned = true;
};

std::string formatHexBv(uint64_t Bits, unsigned Width) {
    if (Width == 0u) {
        return (Bits & 1ULL) ? "true" : "false";
    }
    if (Width < 64u) {
        Bits &= ((1ULL << Width) - 1ULL);
    }
    if (Width % 4u == 0u) {
        unsigned HexDigits = Width / 4u;
        std::string Out = "#x";
        Out.reserve(2u + HexDigits);
        for (int i = static_cast<int>(HexDigits) - 1; i >= 0; --i) {
            unsigned Nib = static_cast<unsigned>((Bits >> (i * 4)) & 0xFULL);
            Out.push_back(Nib < 10u ? static_cast<char>('0' + Nib) : static_cast<char>('a' + (Nib - 10u)));
        }
        return Out;
    }
    std::string Out = "#b";
    Out.reserve(2u + Width);
    for (int i = static_cast<int>(Width) - 1; i >= 0; --i) {
        Out.push_back(((Bits >> i) & 1ULL) ? '1' : '0');
    }
    return Out;
}

SmtBvValue toBoolSort(const SmtBvValue &V) {
    if (V.Width == 0u) return V;
    return {"(not (= " + V.Expr + " " + formatHexBv(0ULL, V.Width) + "))", 0u, false, false, 0ULL};
}

SmtBvValue castBvTo(const SmtBvValue &V, unsigned TargetWidth, bool TargetSigned) {
    if (TargetWidth == 0u) {
        return toBoolSort(V);
    }
    if (V.Width == 0u) {
        return {
            "(ite " + V.Expr + " " + formatHexBv(1ULL, TargetWidth) + " " + formatHexBv(0ULL, TargetWidth) + ")",
            TargetWidth,
            TargetSigned,
            false,
            0ULL
        };
    }
    if (V.Width == TargetWidth) {
        return {V.Expr, TargetWidth, TargetSigned, V.IsConstLiteral, V.ConstBits};
    }
    if (V.Width < TargetWidth) {
        unsigned Diff = TargetWidth - V.Width;
        std::string ExtOp = V.IsSigned ? "sign_extend" : "zero_extend";
        return {
            "((_ " + ExtOp + " " + std::to_string(Diff) + ") " + V.Expr + ")",
            TargetWidth,
            TargetSigned,
            false,
            0ULL
        };
    }
    unsigned Hi = TargetWidth - 1u;
    return {
        "((_ extract " + std::to_string(Hi) + " 0) " + V.Expr + ")",
        TargetWidth,
        TargetSigned,
        false,
        0ULL
    };
}

// Standard C integer promotions & usual arithmetic conversions
std::pair<SmtBvValue, SmtBvValue> usualArithConvert(SmtBvValue L, SmtBvValue R) {
    auto promoteInt = [](SmtBvValue V) {
        if (V.Width < 32u) {
            // C integer promotion: narrow types (bool, int8, uint8, int16, uint16) promote to signed int32_t
            return castBvTo(V, 32u, true);
        }
        return V;
    };
    L = promoteInt(std::move(L));
    R = promoteInt(std::move(R));

    if (L.Width == R.Width) {
        if (L.IsSigned == R.IsSigned) {
            return {L, R};
        }
        L.IsSigned = false;
        R.IsSigned = false;
        return {L, R};
    }

    if (L.Width < R.Width) {
        if (!L.IsSigned && R.IsSigned) {
            // Wider signed type can represent all values of narrower unsigned type (e.g., uint32_t -> int64_t)
            L = castBvTo(L, R.Width, true);
            return {L, R};
        }
        L = castBvTo(L, R.Width, R.IsSigned);
        return {L, R};
    }

    if (!R.IsSigned && L.IsSigned) {
        R = castBvTo(R, L.Width, true);
        return {L, R};
    }
    R = castBvTo(R, L.Width, L.IsSigned);
    return {L, R};
}

bool extractScalarIntInfo(const ASTContext &Ctx, QualType T, unsigned &OutWidth, bool &OutSigned) {
    if (T.isNull()) return false;
    QualType Canon = T.getCanonicalType();
    if (Canon->isBooleanType()) {
        OutWidth = 8u;
        OutSigned = false;
        return true;
    }
    if (Canon->isIntegerType() || Canon->isEnumeralType()) {
        uint64_t Bits = Ctx.getTypeSize(Canon);
        if (Bits == 0ULL || Bits > 64ULL) return false;
        OutWidth = static_cast<unsigned>(Bits);
        OutSigned = Canon->isSignedIntegerOrEnumerationType();
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Recursive-Descent C Contract Expression Parser (@pre / @post -> QF_BV)
// ---------------------------------------------------------------------------

class ContractExprParser {
private:
    llvm::StringRef Src;
    size_t Pos = 0;
    const llvm::StringMap<SmtSymbolInfo> &SymTable;

    void skipWs() {
        while (Pos < Src.size() && std::isspace(static_cast<unsigned char>(Src[Pos]))) {
            ++Pos;
        }
    }

    bool match(llvm::StringRef Tok) {
        skipWs();
        if (Src.substr(Pos).starts_with(Tok)) {
            // Ensure two-char operators don't partially match (e.g., '<' on '<=' or '<<', '&' on '&&', '=' on '==')
            if (Tok == "<" && Pos + 1 < Src.size() && (Src[Pos + 1] == '=' || Src[Pos + 1] == '<')) return false;
            if (Tok == ">" && Pos + 1 < Src.size() && (Src[Pos + 1] == '=' || Src[Pos + 1] == '>')) return false;
            if (Tok == "&" && Pos + 1 < Src.size() && Src[Pos + 1] == '&') return false;
            if (Tok == "|" && Pos + 1 < Src.size() && Src[Pos + 1] == '|') return false;
            if (Tok == "!" && Pos + 1 < Src.size() && Src[Pos + 1] == '=') return false;
            Pos += Tok.size();
            return true;
        }
        return false;
    }

    std::optional<SmtBvValue> parsePrimary() {
        skipWs();
        if (Pos >= Src.size()) return std::nullopt;

        if (match("(")) {
            auto Inner = parseLogicalOr();
            if (!Inner || !match(")")) return std::nullopt;
            return Inner;
        }

        // Integer literal (decimal or 0x hex)
        if (std::isdigit(static_cast<unsigned char>(Src[Pos]))) {
            uint64_t Val = 0ULL;
            bool IsUnsignedSuffix = false;
            bool Is64Suffix = false;
            if (Src.substr(Pos).starts_with("0x") || Src.substr(Pos).starts_with("0X")) {
                Pos += 2;
                if (Pos >= Src.size() || !std::isxdigit(static_cast<unsigned char>(Src[Pos]))) return std::nullopt;
                while (Pos < Src.size() && std::isxdigit(static_cast<unsigned char>(Src[Pos]))) {
                    char C = Src[Pos++];
                    uint64_t Nib = (C >= '0' && C <= '9') ? (C - '0')
                                 : (C >= 'a' && C <= 'f') ? (C - 'a' + 10)
                                                          : (C - 'A' + 10);
                    Val = (Val << 4u) | Nib;
                }
            } else {
                while (Pos < Src.size() && std::isdigit(static_cast<unsigned char>(Src[Pos]))) {
                    Val = Val * 10ULL + static_cast<uint64_t>(Src[Pos++] - '0');
                }
            }
            while (Pos < Src.size() && (Src[Pos] == 'u' || Src[Pos] == 'U' || Src[Pos] == 'l' || Src[Pos] == 'L')) {
                if (Src[Pos] == 'u' || Src[Pos] == 'U') IsUnsignedSuffix = true;
                if (Src[Pos] == 'l' || Src[Pos] == 'L') Is64Suffix = true;
                ++Pos;
            }

            unsigned W = 32u;
            bool Sgn = !IsUnsignedSuffix;
            if (Is64Suffix || Val > 0xFFFFFFFFULL) {
                W = 64u;
                if (!IsUnsignedSuffix && Val > 0x7FFFFFFFFFFFFFFFULL) Sgn = false;
            } else if (!IsUnsignedSuffix && Val > 0x7FFFFFFFULL) {
                W = 64u;
            }
            return SmtBvValue{formatHexBv(Val, W), W, Sgn, true, Val};
        }

        // Identifier or dotted struct path (e.g., hdr.msg_len, return.generation, result)
        if (std::isalpha(static_cast<unsigned char>(Src[Pos])) || Src[Pos] == '_') {
            size_t Start = Pos;
            while (Pos < Src.size() &&
                   (std::isalnum(static_cast<unsigned char>(Src[Pos])) || Src[Pos] == '_' || Src[Pos] == '.')) {
                ++Pos;
            }
            llvm::StringRef Ident = Src.slice(Start, Pos);
            if (Ident == "true") {
                return SmtBvValue{"true", 0u, false, true, 1ULL};
            }
            if (Ident == "false") {
                return SmtBvValue{"false", 0u, false, true, 0ULL};
            }
            std::string LookupKey = Ident.str();
            if (LookupKey == "return") LookupKey = "result";
            else if (llvm::StringRef(LookupKey).starts_with("return.")) {
                LookupKey = "result." + LookupKey.substr(7);
            }
            auto It = SymTable.find(LookupKey);
            if (It == SymTable.end()) return std::nullopt;
            return SmtBvValue{It->second.SmtName, It->second.Width, It->second.IsSigned, false, 0ULL};
        }

        return std::nullopt;
    }

    std::optional<SmtBvValue> parseUnary() {
        skipWs();
        if (match("!")) {
            auto Sub = parseUnary();
            if (!Sub) return std::nullopt;
            SmtBvValue B = toBoolSort(*Sub);
            return SmtBvValue{"(not " + B.Expr + ")", 0u, false, false, 0ULL};
        }
        if (match("~")) {
            auto Sub = parseUnary();
            if (!Sub) return std::nullopt;
            SmtBvValue V = (Sub->Width < 32u) ? castBvTo(*Sub, 32u, true) : *Sub;
            return SmtBvValue{"(bvnot " + V.Expr + ")", V.Width, V.IsSigned, false, 0ULL};
        }
        if (match("-")) {
            auto Sub = parseUnary();
            if (!Sub) return std::nullopt;
            if (Sub->IsConstLiteral && Sub->Width > 0u) {
                uint64_t NegBits = 0ULL - Sub->ConstBits;
                unsigned W = (Sub->ConstBits > 0x80000000ULL) ? 64u : Sub->Width;
                return SmtBvValue{formatHexBv(NegBits, W), W, true, true, NegBits};
            }
            SmtBvValue V = (Sub->Width < 32u) ? castBvTo(*Sub, 32u, true) : *Sub;
            return SmtBvValue{"(bvneg " + V.Expr + ")", V.Width, true, false, 0ULL};
        }
        return parsePrimary();
    }

    std::optional<SmtBvValue> parseMultiplicative() {
        auto L = parseUnary();
        if (!L) return std::nullopt;
        for (;;) {
            if (match("*")) {
                auto R = parseUnary();
                if (!R) return std::nullopt;
                auto [CL, CR] = usualArithConvert(*L, *R);
                L = SmtBvValue{"(bvmul " + CL.Expr + " " + CR.Expr + ")", CL.Width, CL.IsSigned, false, 0ULL};
            } else if (match("/")) {
                auto R = parseUnary();
                if (!R) return std::nullopt;
                auto [CL, CR] = usualArithConvert(*L, *R);
                std::string Op = CL.IsSigned ? "bvsdiv" : "bvudiv";
                L = SmtBvValue{"(" + Op + " " + CL.Expr + " " + CR.Expr + ")", CL.Width, CL.IsSigned, false, 0ULL};
            } else if (match("%")) {
                auto R = parseUnary();
                if (!R) return std::nullopt;
                auto [CL, CR] = usualArithConvert(*L, *R);
                std::string Op = CL.IsSigned ? "bvsrem" : "bvurem";
                L = SmtBvValue{"(" + Op + " " + CL.Expr + " " + CR.Expr + ")", CL.Width, CL.IsSigned, false, 0ULL};
            } else {
                break;
            }
        }
        return L;
    }

    std::optional<SmtBvValue> parseAdditive() {
        auto L = parseMultiplicative();
        if (!L) return std::nullopt;
        for (;;) {
            if (match("+")) {
                auto R = parseMultiplicative();
                if (!R) return std::nullopt;
                auto [CL, CR] = usualArithConvert(*L, *R);
                L = SmtBvValue{"(bvadd " + CL.Expr + " " + CR.Expr + ")", CL.Width, CL.IsSigned, false, 0ULL};
            } else if (match("-")) {
                auto R = parseMultiplicative();
                if (!R) return std::nullopt;
                auto [CL, CR] = usualArithConvert(*L, *R);
                L = SmtBvValue{"(bvsub " + CL.Expr + " " + CR.Expr + ")", CL.Width, CL.IsSigned, false, 0ULL};
            } else {
                break;
            }
        }
        return L;
    }

    std::optional<SmtBvValue> parseShift() {
        auto L = parseAdditive();
        if (!L) return std::nullopt;
        for (;;) {
            if (match("<<")) {
                auto R = parseAdditive();
                if (!R) return std::nullopt;
                SmtBvValue CL = (L->Width < 32u) ? castBvTo(*L, 32u, true) : *L;
                SmtBvValue CR = castBvTo(*R, CL.Width, false);
                L = SmtBvValue{"(bvshl " + CL.Expr + " " + CR.Expr + ")", CL.Width, CL.IsSigned, false, 0ULL};
            } else if (match(">>")) {
                auto R = parseAdditive();
                if (!R) return std::nullopt;
                SmtBvValue CL = (L->Width < 32u) ? castBvTo(*L, 32u, true) : *L;
                SmtBvValue CR = castBvTo(*R, CL.Width, false);
                std::string Op = CL.IsSigned ? "bvashr" : "bvlshr";
                L = SmtBvValue{"(" + Op + " " + CL.Expr + " " + CR.Expr + ")", CL.Width, CL.IsSigned, false, 0ULL};
            } else {
                break;
            }
        }
        return L;
    }

    std::optional<SmtBvValue> parseRelational() {
        auto L = parseShift();
        if (!L) return std::nullopt;
        for (;;) {
            llvm::StringRef Rel;
            if (match("<=")) Rel = "le";
            else if (match(">=")) Rel = "ge";
            else if (match("<")) Rel = "lt";
            else if (match(">")) Rel = "gt";
            else break;

            auto R = parseShift();
            if (!R) return std::nullopt;
            auto [CL, CR] = usualArithConvert(*L, *R);
            std::string Op = (CL.IsSigned ? "bvs" : "bvu") + Rel.str();
            L = SmtBvValue{"(" + Op + " " + CL.Expr + " " + CR.Expr + ")", 0u, false, false, 0ULL};
        }
        return L;
    }

    std::optional<SmtBvValue> parseEquality() {
        auto L = parseRelational();
        if (!L) return std::nullopt;
        for (;;) {
            bool IsEq = false;
            if (match("==")) IsEq = true;
            else if (match("!=")) IsEq = false;
            else break;

            auto R = parseRelational();
            if (!R) return std::nullopt;
            std::string EqExpr;
            if (L->Width == 0u && R->Width == 0u) {
                EqExpr = "(= " + L->Expr + " " + R->Expr + ")";
            } else {
                auto [CL, CR] = usualArithConvert(*L, *R);
                EqExpr = "(= " + CL.Expr + " " + CR.Expr + ")";
            }
            L = SmtBvValue{IsEq ? EqExpr : "(not " + EqExpr + ")", 0u, false, false, 0ULL};
        }
        return L;
    }

    std::optional<SmtBvValue> parseBitwiseAnd() {
        auto L = parseEquality();
        if (!L) return std::nullopt;
        while (match("&")) {
            auto R = parseEquality();
            if (!R) return std::nullopt;
            auto [CL, CR] = usualArithConvert(*L, *R);
            L = SmtBvValue{"(bvand " + CL.Expr + " " + CR.Expr + ")", CL.Width, CL.IsSigned, false, 0ULL};
        }
        return L;
    }

    std::optional<SmtBvValue> parseBitwiseXor() {
        auto L = parseBitwiseAnd();
        if (!L) return std::nullopt;
        while (match("^")) {
            auto R = parseBitwiseAnd();
            if (!R) return std::nullopt;
            auto [CL, CR] = usualArithConvert(*L, *R);
            L = SmtBvValue{"(bvxor " + CL.Expr + " " + CR.Expr + ")", CL.Width, CL.IsSigned, false, 0ULL};
        }
        return L;
    }

    std::optional<SmtBvValue> parseBitwiseOr() {
        auto L = parseBitwiseXor();
        if (!L) return std::nullopt;
        while (match("|")) {
            auto R = parseBitwiseXor();
            if (!R) return std::nullopt;
            auto [CL, CR] = usualArithConvert(*L, *R);
            L = SmtBvValue{"(bvor " + CL.Expr + " " + CR.Expr + ")", CL.Width, CL.IsSigned, false, 0ULL};
        }
        return L;
    }

    std::optional<SmtBvValue> parseLogicalAnd() {
        auto L = parseBitwiseOr();
        if (!L) return std::nullopt;
        while (match("&&")) {
            auto R = parseBitwiseOr();
            if (!R) return std::nullopt;
            SmtBvValue BL = toBoolSort(*L);
            SmtBvValue BR = toBoolSort(*R);
            L = SmtBvValue{"(and " + BL.Expr + " " + BR.Expr + ")", 0u, false, false, 0ULL};
        }
        return L;
    }

    std::optional<SmtBvValue> parseLogicalOr() {
        auto L = parseLogicalAnd();
        if (!L) return std::nullopt;
        while (match("||")) {
            auto R = parseLogicalAnd();
            if (!R) return std::nullopt;
            SmtBvValue BL = toBoolSort(*L);
            SmtBvValue BR = toBoolSort(*R);
            L = SmtBvValue{"(or " + BL.Expr + " " + BR.Expr + ")", 0u, false, false, 0ULL};
        }
        return L;
    }

public:
    ContractExprParser(llvm::StringRef Src, const llvm::StringMap<SmtSymbolInfo> &SymTable)
        : Src(Src), SymTable(SymTable) {}

    std::optional<std::string> parseBoolCondition() {
        auto Val = parseLogicalOr();
        skipWs();
        if (!Val || Pos != Src.size()) return std::nullopt;
        return toBoolSort(*Val).Expr;
    }
};

// ---------------------------------------------------------------------------
// Bounded Symbolic Clang AST -> QF_BV Evaluator
// ---------------------------------------------------------------------------

struct SymValue {
    bool IsStruct = false;
    SmtBvValue Scalar;
    llvm::StringMap<SmtBvValue> Fields;
};

struct SymExecEnv {
    llvm::DenseMap<const ValueDecl *, SymValue> Vars;
    std::string PathCond = "true";
    std::string ReturnedSoFar = "false";
    std::vector<std::pair<std::string, SymValue>> ReturnCases;
};

SymValue mergeSymValues(const std::string &Cond, const SymValue &ThenV, const SymValue &ElseV) {
    if (ThenV.IsStruct && ElseV.IsStruct) {
        SymValue Out;
        Out.IsStruct = true;
        for (const auto &KV : ThenV.Fields) {
            llvm::StringRef FName = KV.first();
            auto EIt = ElseV.Fields.find(FName);
            if (EIt != ElseV.Fields.end()) {
                Out.Fields[FName] = {
                    "(ite " + Cond + " " + KV.second.Expr + " " + EIt->second.Expr + ")",
                    KV.second.Width,
                    KV.second.IsSigned,
                    false,
                    0ULL
                };
            }
        }
        return Out;
    }
    return SymValue{
        false,
        {"(ite " + Cond + " " + ThenV.Scalar.Expr + " " + ElseV.Scalar.Expr + ")",
         ThenV.Scalar.Width,
         ThenV.Scalar.IsSigned,
         false,
         0ULL},
        {}
    };
}

class AstSmtEncoder {
private:
    const ASTContext &Ctx;

    std::optional<SymValue> evalCallInline(const CallExpr *CE, SymExecEnv &Env, unsigned InlineDepth) {
        if (InlineDepth >= 8u) return std::nullopt;
        const FunctionDecl *Callee = CE->getDirectCallee();
        if (!Callee) return std::nullopt;
        const FunctionDecl *Def = Callee->getDefinition();
        if (!Def || !Def->hasBody() || Def->getNumParams() != CE->getNumArgs()) {
            return std::nullopt;
        }

        SymExecEnv CalleeEnv;
        for (unsigned i = 0; i < Def->getNumParams(); ++i) {
            auto ArgVal = evalExpr(CE->getArg(i), Env, InlineDepth);
            if (!ArgVal) return std::nullopt;
            CalleeEnv.Vars[Def->getParamDecl(i)] = std::move(*ArgVal);
        }

        if (!execStmt(Def->getBody(), CalleeEnv, InlineDepth + 1u) || CalleeEnv.ReturnCases.empty()) {
            return std::nullopt;
        }
        return foldReturnCases(CalleeEnv.ReturnCases);
    }

public:
    explicit AstSmtEncoder(const ASTContext &Ctx) : Ctx(Ctx) {}

    static SymValue foldReturnCases(const std::vector<std::pair<std::string, SymValue>> &Cases) {
        SymValue Acc = Cases.back().second;
        for (int i = static_cast<int>(Cases.size()) - 2; i >= 0; --i) {
            Acc = mergeSymValues(Cases[i].first, Cases[i].second, Acc);
        }
        return Acc;
    }

    std::optional<SymValue> evalExpr(const Expr *E, SymExecEnv &Env, unsigned InlineDepth) {
        if (!E) return std::nullopt;
        E = E->IgnoreParens();

        if (const auto *ICE = dyn_cast<ImplicitCastExpr>(E)) {
            CastKind CK = ICE->getCastKind();
            if (CK == CK_LValueToRValue || CK == CK_NoOp || CK == CK_FunctionToPointerDecay) {
                return evalExpr(ICE->getSubExpr(), Env, InlineDepth);
            }
            auto Sub = evalExpr(ICE->getSubExpr(), Env, InlineDepth);
            if (!Sub || Sub->IsStruct) return Sub;
            if (CK == CK_IntegralToBoolean) {
                return SymValue{false, toBoolSort(Sub->Scalar), {}};
            }
            unsigned DstW = 0;
            bool DstS = true;
            if (extractScalarIntInfo(Ctx, ICE->getType(), DstW, DstS)) {
                return SymValue{false, castBvTo(Sub->Scalar, DstW, DstS), {}};
            }
            return std::nullopt;
        }

        if (const auto *CCE = dyn_cast<CStyleCastExpr>(E)) {
            auto Sub = evalExpr(CCE->getSubExpr(), Env, InlineDepth);
            if (!Sub || Sub->IsStruct) return Sub;
            if (CCE->getType()->isBooleanType()) {
                return SymValue{false, toBoolSort(Sub->Scalar), {}};
            }
            unsigned DstW = 0;
            bool DstS = true;
            if (extractScalarIntInfo(Ctx, CCE->getType(), DstW, DstS)) {
                return SymValue{false, castBvTo(Sub->Scalar, DstW, DstS), {}};
            }
            return std::nullopt;
        }

        if (const auto *IL = dyn_cast<IntegerLiteral>(E)) {
            unsigned W = 32u;
            bool Sgn = true;
            if (!extractScalarIntInfo(Ctx, IL->getType(), W, Sgn)) return std::nullopt;
            uint64_t Bits = IL->getValue().getZExtValue();
            return SymValue{false, {formatHexBv(Bits, W), W, Sgn, true, Bits}, {}};
        }

        if (const auto *BL = dyn_cast<CXXBoolLiteralExpr>(E)) {
            uint64_t Bits = BL->getValue() ? 1ULL : 0ULL;
            return SymValue{false, {formatHexBv(Bits, 8u), 8u, false, true, Bits}, {}};
        }

        if (const auto *DRE = dyn_cast<DeclRefExpr>(E)) {
            if (const auto *ECD = dyn_cast<EnumConstantDecl>(DRE->getDecl())) {
                unsigned W = 32u;
                bool Sgn = true;
                extractScalarIntInfo(Ctx, DRE->getType(), W, Sgn);
                uint64_t Bits = ECD->getInitVal().getZExtValue();
                return SymValue{false, {formatHexBv(Bits, W), W, Sgn, true, Bits}, {}};
            }
            auto It = Env.Vars.find(DRE->getDecl());
            if (It != Env.Vars.end()) return It->second;
            return std::nullopt;
        }

        if (const auto *ME = dyn_cast<MemberExpr>(E)) {
            if (ME->isArrow()) return std::nullopt;
            auto Base = evalExpr(ME->getBase(), Env, InlineDepth);
            if (!Base || !Base->IsStruct) return std::nullopt;
            auto FIt = Base->Fields.find(ME->getMemberDecl()->getName());
            if (FIt == Base->Fields.end()) return std::nullopt;
            return SymValue{false, FIt->second, {}};
        }

        if (const auto *UO = dyn_cast<UnaryOperator>(E)) {
            auto Sub = evalExpr(UO->getSubExpr(), Env, InlineDepth);
            if (!Sub || Sub->IsStruct) return std::nullopt;
            unsigned ResW = 0;
            bool ResS = true;
            extractScalarIntInfo(Ctx, UO->getType(), ResW, ResS);
            switch (UO->getOpcode()) {
                case UO_LNot: {
                    SmtBvValue B = toBoolSort(Sub->Scalar);
                    return SymValue{false, {"(not " + B.Expr + ")", 0u, false, false, 0ULL}, {}};
                }
                case UO_Not: {
                    SmtBvValue V = castBvTo(Sub->Scalar, ResW ? ResW : Sub->Scalar.Width, ResS);
                    return SymValue{false, {"(bvnot " + V.Expr + ")", V.Width, V.IsSigned, false, 0ULL}, {}};
                }
                case UO_Minus: {
                    SmtBvValue V = castBvTo(Sub->Scalar, ResW ? ResW : Sub->Scalar.Width, ResS);
                    return SymValue{false, {"(bvneg " + V.Expr + ")", V.Width, V.IsSigned, false, 0ULL}, {}};
                }
                case UO_Plus:
                    return Sub;
                default:
                    return std::nullopt;
            }
        }

        if (const auto *BO = dyn_cast<BinaryOperator>(E)) {
            BinaryOperator::Opcode Op = BO->getOpcode();
            if (Op == BO_LAnd || Op == BO_LOr) {
                auto L = evalExpr(BO->getLHS(), Env, InlineDepth);
                auto R = evalExpr(BO->getRHS(), Env, InlineDepth);
                if (!L || !R || L->IsStruct || R->IsStruct) return std::nullopt;
                SmtBvValue BL = toBoolSort(L->Scalar);
                SmtBvValue BR = toBoolSort(R->Scalar);
                std::string Comb = (Op == BO_LAnd)
                    ? ("(and " + BL.Expr + " " + BR.Expr + ")")
                    : ("(or " + BL.Expr + " " + BR.Expr + ")");
                return SymValue{false, {Comb, 0u, false, false, 0ULL}, {}};
            }

            auto L = evalExpr(BO->getLHS(), Env, InlineDepth);
            auto R = evalExpr(BO->getRHS(), Env, InlineDepth);
            if (!L || !R || L->IsStruct || R->IsStruct) return std::nullopt;

            unsigned ResW = 0;
            bool ResS = true;
            extractScalarIntInfo(Ctx, BO->getType(), ResW, ResS);

            switch (Op) {
                case BO_Add:
                case BO_Sub:
                case BO_Mul:
                case BO_Div:
                case BO_Rem:
                case BO_And:
                case BO_Or:
                case BO_Xor: {
                    unsigned W = ResW ? ResW : L->Scalar.Width;
                    SmtBvValue CL = castBvTo(L->Scalar, W, ResS);
                    SmtBvValue CR = castBvTo(R->Scalar, W, ResS);
                    std::string BvOp;
                    if (Op == BO_Add) BvOp = "bvadd";
                    else if (Op == BO_Sub) BvOp = "bvsub";
                    else if (Op == BO_Mul) BvOp = "bvmul";
                    else if (Op == BO_Div) BvOp = ResS ? "bvsdiv" : "bvudiv";
                    else if (Op == BO_Rem) BvOp = ResS ? "bvsrem" : "bvurem";
                    else if (Op == BO_And) BvOp = "bvand";
                    else if (Op == BO_Or) BvOp = "bvor";
                    else BvOp = "bvxor";
                    return SymValue{false, {"(" + BvOp + " " + CL.Expr + " " + CR.Expr + ")", W, ResS, false, 0ULL}, {}};
                }
                case BO_Shl:
                case BO_Shr: {
                    unsigned W = ResW ? ResW : L->Scalar.Width;
                    SmtBvValue CL = castBvTo(L->Scalar, W, ResS);
                    SmtBvValue CR = castBvTo(R->Scalar, W, false);
                    std::string BvOp = (Op == BO_Shl) ? "bvshl" : (ResS ? "bvashr" : "bvlshr");
                    return SymValue{false, {"(" + BvOp + " " + CL.Expr + " " + CR.Expr + ")", W, ResS, false, 0ULL}, {}};
                }
                case BO_EQ:
                case BO_NE: {
                    auto [CL, CR] = usualArithConvert(L->Scalar, R->Scalar);
                    std::string Eq = "(= " + CL.Expr + " " + CR.Expr + ")";
                    return SymValue{false, {Op == BO_EQ ? Eq : "(not " + Eq + ")", 0u, false, false, 0ULL}, {}};
                }
                case BO_LT:
                case BO_LE:
                case BO_GT:
                case BO_GE: {
                    auto [CL, CR] = usualArithConvert(L->Scalar, R->Scalar);
                    std::string Prefix = CL.IsSigned ? "bvs" : "bvu";
                    std::string Suffix = (Op == BO_LT) ? "lt" : (Op == BO_LE) ? "le" : (Op == BO_GT) ? "gt" : "ge";
                    return SymValue{false, {"(" + Prefix + Suffix + " " + CL.Expr + " " + CR.Expr + ")", 0u, false, false, 0ULL}, {}};
                }
                default:
                    return std::nullopt;
            }
        }

        if (const auto *CO = dyn_cast<ConditionalOperator>(E)) {
            auto Cond = evalExpr(CO->getCond(), Env, InlineDepth);
            auto TVal = evalExpr(CO->getTrueExpr(), Env, InlineDepth);
            auto FVal = evalExpr(CO->getFalseExpr(), Env, InlineDepth);
            if (!Cond || !TVal || !FVal || Cond->IsStruct) return std::nullopt;
            return mergeSymValues(toBoolSort(Cond->Scalar).Expr, *TVal, *FVal);
        }

        if (const auto *CE = dyn_cast<CallExpr>(E)) {
            return evalCallInline(CE, Env, InlineDepth);
        }

        return std::nullopt;
    }

    bool execStmt(const Stmt *S, SymExecEnv &Env, unsigned InlineDepth) {
        if (!S) return true;

        if (const auto *CS = dyn_cast<CompoundStmt>(S)) {
            for (const Stmt *Sub : CS->body()) {
                if (!execStmt(Sub, Env, InlineDepth)) return false;
            }
            return true;
        }

        if (const auto *DS = dyn_cast<DeclStmt>(S)) {
            for (const Decl *D : DS->decls()) {
                const auto *VD = dyn_cast<VarDecl>(D);
                if (!VD) continue;
                if (VD->getInit()) {
                    auto InitV = evalExpr(VD->getInit(), Env, InlineDepth);
                    if (!InitV) return false;
                    Env.Vars[VD] = std::move(*InitV);
                } else {
                    unsigned W = 0;
                    bool Sgn = true;
                    if (!extractScalarIntInfo(Ctx, VD->getType(), W, Sgn)) return false;
                    Env.Vars[VD] = SymValue{false, {formatHexBv(0ULL, W), W, Sgn, true, 0ULL}, {}};
                }
            }
            return true;
        }

        if (const auto *BO = dyn_cast<BinaryOperator>(S); BO && BO->isAssignmentOp()) {
            const auto *DRE = dyn_cast<DeclRefExpr>(BO->getLHS()->IgnoreParenImpCasts());
            const auto *VD = DRE ? dyn_cast<VarDecl>(DRE->getDecl()) : nullptr;
            if (!VD) return false;
            auto RhsV = evalExpr(BO->getRHS(), Env, InlineDepth);
            if (!RhsV) return false;

            SymValue NewVal = *RhsV;
            if (BO->getOpcode() != BO_Assign) {
                auto OldIt = Env.Vars.find(VD);
                if (OldIt == Env.Vars.end() || OldIt->second.IsStruct || RhsV->IsStruct) return false;
                unsigned W = OldIt->second.Scalar.Width;
                bool Sgn = OldIt->second.Scalar.IsSigned;
                SmtBvValue CR = castBvTo(RhsV->Scalar, W, Sgn);
                std::string Op;
                if (BO->getOpcode() == BO_AddAssign) Op = "bvadd";
                else if (BO->getOpcode() == BO_SubAssign) Op = "bvsub";
                else if (BO->getOpcode() == BO_MulAssign) Op = "bvmul";
                else return false;
                NewVal = SymValue{false, {"(" + Op + " " + OldIt->second.Scalar.Expr + " " + CR.Expr + ")", W, Sgn, false, 0ULL}, {}};
            }

            std::string ActiveGuard = "(and " + Env.PathCond + " (not " + Env.ReturnedSoFar + "))";
            auto ExistingIt = Env.Vars.find(VD);
            if (ExistingIt != Env.Vars.end()) {
                Env.Vars[VD] = mergeSymValues(ActiveGuard, NewVal, ExistingIt->second);
            } else {
                Env.Vars[VD] = std::move(NewVal);
            }
            return true;
        }

        if (const auto *IS = dyn_cast<IfStmt>(S)) {
            auto CondV = evalExpr(IS->getCond(), Env, InlineDepth);
            if (!CondV || CondV->IsStruct) return false;
            std::string CondBool = toBoolSort(CondV->Scalar).Expr;

            std::string SavedPath = Env.PathCond;
            SymExecEnv ThenEnv = Env;
            ThenEnv.PathCond = "(and " + SavedPath + " " + CondBool + ")";
            if (!execStmt(IS->getThen(), ThenEnv, InlineDepth)) return false;

            SymExecEnv ElseEnv = Env;
            ElseEnv.PathCond = "(and " + SavedPath + " (not " + CondBool + "))";
            ElseEnv.ReturnedSoFar = ThenEnv.ReturnedSoFar;
            ElseEnv.ReturnCases = ThenEnv.ReturnCases;
            if (IS->getElse()) {
                if (!execStmt(IS->getElse(), ElseEnv, InlineDepth)) return false;
            }

            // Merge modified local variables across Then/Else
            Env.ReturnedSoFar = ElseEnv.ReturnedSoFar;
            Env.ReturnCases = std::move(ElseEnv.ReturnCases);
            for (const auto &[VD, TV] : ThenEnv.Vars) {
                auto EIt = ElseEnv.Vars.find(VD);
                if (EIt != ElseEnv.Vars.end()) {
                    if (!TV.IsStruct && !EIt->second.IsStruct && TV.Scalar.Expr == EIt->second.Scalar.Expr) {
                        Env.Vars[VD] = TV;
                    } else {
                        Env.Vars[VD] = mergeSymValues(CondBool, TV, EIt->second);
                    }
                }
            }
            return true;
        }

        if (const auto *RS = dyn_cast<ReturnStmt>(S)) {
            if (!RS->getRetValue()) return false;
            auto RetV = evalExpr(RS->getRetValue(), Env, InlineDepth);
            if (!RetV) return false;
            std::string ActiveGuard = "(and " + Env.PathCond + " (not " + Env.ReturnedSoFar + "))";
            Env.ReturnCases.push_back({ActiveGuard, std::move(*RetV)});
            Env.ReturnedSoFar = "(or " + Env.ReturnedSoFar + " " + ActiveGuard + ")";
            return true;
        }

        if (isa<NullStmt>(S)) {
            return true;
        }

        // Ignore void cast expressions like `(void)frame;`
        if (const auto *CE = dyn_cast<CStyleCastExpr>(S); CE && CE->getType()->isVoidType()) {
            return true;
        }

        return false;
    }
};

std::string formatWitnessForType(const rt_z3_witness_entry &W, const SmtSymbolInfo &Info) {
    if (!W.has_numeric_val) {
        return W.raw_smt_val;
    }
    if (Info.IsSigned) {
        uint64_t Bits = W.unsigned_val;
        int64_t SVal = W.signed_val;
        if (Info.Width > 0u && Info.Width < 64u) {
            uint64_t Mask = (1ULL << Info.Width) - 1ULL;
            Bits &= Mask;
            if ((Bits >> (Info.Width - 1u)) & 1ULL) {
                SVal = static_cast<int64_t>(Bits | ~Mask);
            } else {
                SVal = static_cast<int64_t>(Bits);
            }
        } else if (Info.Width == 64u) {
            SVal = static_cast<int64_t>(Bits);
        }
        return std::to_string(SVal);
    }
    uint64_t UVal = W.unsigned_val;
    if (Info.Width > 0u && Info.Width < 64u) {
        UVal &= ((1ULL << Info.Width) - 1ULL);
    }
    return std::to_string(UVal);
}

llvm::StringRef z3StatusReportLabel(rt_z3_status St) {
    switch (St) {
        case Z3_UNSAT_PROVEN:       return "UNSAT_PROVEN";
        case Z3_SAT_COUNTEREXAMPLE: return "SAT_COUNTEREXAMPLE";
        case Z3_UNKNOWN_TIMEOUT:    return "UNKNOWN_TIMEOUT";
        case Z3_SOLVER_UNAVAILABLE: return "SOLVER_UNAVAILABLE";
        case Z3_INVALID_QUERY:      return "INVALID_QUERY";
    }
    return "UNKNOWN_TIMEOUT";
}

} // namespace

bool VeriObjCVisitor::hasContractAnnotations(const FunctionDecl *FD) {
    if (!FD) return false;
    for (const auto *A : FD->specific_attrs<AnnotateAttr>()) {
        llvm::StringRef Ann = A->getAnnotation();
        if (Ann.starts_with("objc_pre:") || Ann.starts_with("objc_post:")) {
            return true;
        }
    }
    return false;
}

void VeriObjCVisitor::verifyFunctionContractWithZ3(const FunctionDecl *FD) {
    if (!FD || !FD->hasBody()) return;

    llvm::SmallVector<llvm::StringRef, 4> PreStrs;
    llvm::SmallVector<llvm::StringRef, 4> PostStrs;
    for (const auto *A : FD->specific_attrs<AnnotateAttr>()) {
        llvm::StringRef Ann = A->getAnnotation();
        if (Ann.starts_with("objc_pre:")) {
            PreStrs.push_back(Ann.drop_front(9));
        } else if (Ann.starts_with("objc_post:")) {
            PostStrs.push_back(Ann.drop_front(10));
        }
    }
    if (PreStrs.empty() && PostStrs.empty()) return;

    std::string CombinedPreStr = PreStrs.empty() ? "true" : PreStrs[0].str();
    for (size_t i = 1; i < PreStrs.size(); ++i) {
        CombinedPreStr += " && (" + PreStrs[i].str() + ")";
    }
    std::string CombinedPostStr = PostStrs.empty() ? "true" : PostStrs[0].str();
    for (size_t i = 1; i < PostStrs.size(); ++i) {
        CombinedPostStr += " && (" + PostStrs[i].str() + ")";
    }

    SourceManager &SM = Context->getSourceManager();
    unsigned Line = SM.getSpellingLineNumber(FD->getLocation());
    unsigned Col = SM.getSpellingColumnNumber(FD->getLocation());

    // 1. Build QF_BV declarations for parameters and return value
    llvm::StringMap<SmtSymbolInfo> SymTable;
    llvm::StringMap<SmtSymbolInfo> SmtNameToInfo;
    std::vector<std::string> OrderedSmtVars;
    std::string DeclsScript;
    SymExecEnv Env;

    auto registerVar = [&](const std::string &CKey, const std::string &SmtName, unsigned W, bool Sgn) {
        SmtSymbolInfo Info{SmtName, CKey, W, Sgn};
        SymTable[CKey] = Info;
        SmtNameToInfo[SmtName] = Info;
        OrderedSmtVars.push_back(SmtName);
        DeclsScript += "(declare-const " + SmtName + " (_ BitVec " + std::to_string(W) + "))\n";
    };

    bool SignatureSupported = true;
    for (const ParmVarDecl *PVD : FD->parameters()) {
        std::string PName = PVD->getNameAsString();
        QualType PTy = PVD->getType();
        unsigned W = 0;
        bool Sgn = true;
        if (extractScalarIntInfo(*Context, PTy, W, Sgn)) {
            registerVar(PName, PName, W, Sgn);
            Env.Vars[PVD] = SymValue{false, {PName, W, Sgn, false, 0ULL}, {}};
        } else if (const auto *RT = PTy->getAs<RecordType>()) {
            const RecordDecl *RD = RT->getDecl()->getDefinition();
            if (!RD) {
                SignatureSupported = false;
                break;
            }
            SymValue StructVal;
            StructVal.IsStruct = true;
            for (const FieldDecl *Field : RD->fields()) {
                unsigned FW = 0;
                bool FSgn = true;
                if (!extractScalarIntInfo(*Context, Field->getType(), FW, FSgn)) {
                    SignatureSupported = false;
                    break;
                }
                std::string FName = Field->getNameAsString();
                std::string CKey = PName + "." + FName;
                std::string SmtKey = PName + "__dot__" + FName;
                registerVar(CKey, SmtKey, FW, FSgn);
                StructVal.Fields[FName] = {SmtKey, FW, FSgn, false, 0ULL};
            }
            Env.Vars[PVD] = std::move(StructVal);
        } else {
            SignatureSupported = false;
            break;
        }
    }

    QualType RetTy = FD->getReturnType();
    bool RetIsStruct = false;
    unsigned RetW = 0;
    bool RetSgn = true;
    llvm::SmallVector<std::pair<std::string, SmtSymbolInfo>, 4> RetStructFields;

    if (SignatureSupported) {
        if (extractScalarIntInfo(*Context, RetTy, RetW, RetSgn)) {
            registerVar("result", "result", RetW, RetSgn);
        } else if (const auto *RT = RetTy->getAs<RecordType>()) {
            const RecordDecl *RD = RT->getDecl()->getDefinition();
            if (!RD) {
                SignatureSupported = false;
            } else {
                RetIsStruct = true;
                for (const FieldDecl *Field : RD->fields()) {
                    unsigned FW = 0;
                    bool FSgn = true;
                    if (!extractScalarIntInfo(*Context, Field->getType(), FW, FSgn)) {
                        SignatureSupported = false;
                        break;
                    }
                    std::string FName = Field->getNameAsString();
                    std::string CKey = "result." + FName;
                    std::string SmtKey = "result__dot__" + FName;
                    registerVar(CKey, SmtKey, FW, FSgn);
                    RetStructFields.push_back({FName, SymTable[CKey]});
                }
            }
        } else {
            SignatureSupported = false;
        }
    }

    // Always probe Z3 first so --z3-path=/bad/path fails closed with Z3_SOLVER_UNAVAILABLE
    char SolverVer[128] = {0};
    char ProbeErr[512] = {0};
    const char *Z3Bin = Opts.Z3Path.empty() ? nullptr : Opts.Z3Path.c_str();
    if (!rt_z3_probe_version(Z3Bin, Opts.SmtTimeoutMs, SolverVer, sizeof(SolverVer), ProbeErr, sizeof(ProbeErr))) {
        llvm::StringRef ShownPath = Opts.Z3Path.empty() ? VERIOBJC_DEFAULT_Z3_PATH : llvm::StringRef(Opts.Z3Path);
        reportError(
            FD->getLocation(),
            "VeriObjC Violation: SMT solver unavailable (Z3_SOLVER_UNAVAILABLE) for function '%0' at %1:%2 (path '%3'): %4."
        ) << FD->getName() << Line << Col << ShownPath << ProbeErr;
        return;
    }

    AstSmtEncoder Encoder(*Context);
    bool BodyEncoded = SignatureSupported && Encoder.execStmt(FD->getBody(), Env, 0u) && !Env.ReturnCases.empty();

    std::optional<std::string> PreSmt;
    std::optional<std::string> PostSmt;
    if (BodyEncoded) {
        ContractExprParser PreParser(CombinedPreStr, SymTable);
        PreSmt = PreParser.parseBoolCondition();
        ContractExprParser PostParser(CombinedPostStr, SymTable);
        PostSmt = PostParser.parseBoolCondition();
    }

    if (!BodyEncoded || !PreSmt || !PostSmt) {
        if (Opts.EmitSmtReport) {
            llvm::errs() << "[VeriObjC --smt-report] Function: " << FD->getName()
                         << " | Pre: (assert (" << CombinedPreStr << "))"
                         << " | Post: (assert (not (" << CombinedPostStr << ")))"
                         << " | Solver: " << SolverVer
                         << " | Status: UNKNOWN_TIMEOUT\n";
        }
        if (Opts.SmtVerify) {
            reportError(
                FD->getLocation(),
                "VeriObjC Violation: SMT contract verification inconclusive (Z3_UNKNOWN_TIMEOUT) in function '%0' at %1:%2: body or '@post(%3)' exceeds bounded QF_BV encoding."
            ) << FD->getName() << Line << Col << CombinedPostStr;
        }
        return;
    }

    SymValue FinalRet = AstSmtEncoder::foldReturnCases(Env.ReturnCases);
    std::string BodyAssertion;
    if (!RetIsStruct) {
        SmtBvValue CastRet = castBvTo(FinalRet.Scalar, RetW, RetSgn);
        BodyAssertion = "(assert (= result " + CastRet.Expr + "))\n";
    } else {
        for (const auto &[FName, FInfo] : RetStructFields) {
            auto FIt = FinalRet.Fields.find(FName);
            if (FIt != FinalRet.Fields.end()) {
                SmtBvValue CastF = castBvTo(FIt->second, FInfo.Width, FInfo.IsSigned);
                BodyAssertion += "(assert (= " + FInfo.SmtName + " " + CastF.Expr + "))\n";
            }
        }
    }

    std::string Query = "(set-option :produce-models true)\n";
    Query += "(set-option :timeout " + std::to_string(Opts.SmtTimeoutMs) + ")\n";
    Query += "(set-logic QF_BV)\n";
    Query += DeclsScript;
    Query += "(assert " + *PreSmt + ")\n";
    Query += BodyAssertion;
    Query += "(assert (not " + *PostSmt + "))\n";
    Query += "(check-sat)\n";
    if (!OrderedSmtVars.empty()) {
        Query += "(get-value (";
        for (size_t i = 0; i < OrderedSmtVars.size(); ++i) {
            if (i > 0) Query += " ";
            Query += OrderedSmtVars[i];
        }
        Query += "))\n";
    }

    rt_z3_discharge_result Res;
    rt_z3_status Status = rt_z3_discharge_smtlib2(Z3Bin, Opts.SmtTimeoutMs, Query.c_str(), &Res);

    if (Status == Z3_SOLVER_UNAVAILABLE) {
        llvm::StringRef ShownPath = Opts.Z3Path.empty() ? VERIOBJC_DEFAULT_Z3_PATH : llvm::StringRef(Opts.Z3Path);
        reportError(
            FD->getLocation(),
            "VeriObjC Violation: SMT solver unavailable (Z3_SOLVER_UNAVAILABLE) for function '%0' at %1:%2 (path '%3'): %4."
        ) << FD->getName() << Line << Col << ShownPath << Res.diagnostic_detail;
        return;
    }

    if (Opts.EmitSmtReport) {
        llvm::errs() << "[VeriObjC --smt-report] Function: " << FD->getName()
                     << " | Pre: (assert (" << CombinedPreStr << "))"
                     << " | Post: (assert (not (" << CombinedPostStr << ")))"
                     << " | Solver: " << Res.solver_version
                     << " | Status: " << z3StatusReportLabel(Status) << "\n";
    }

    if (!Opts.SmtVerify) {
        return;
    }

    if (Status == Z3_SAT_COUNTEREXAMPLE) {
        std::string WitnessStr;
        for (uint32_t i = 0; i < Res.witness_count; ++i) {
            const rt_z3_witness_entry &W = Res.witnesses[i];
            auto InfoIt = SmtNameToInfo.find(W.name);
            std::string ShownName = (InfoIt != SmtNameToInfo.end()) ? InfoIt->second.DisplayName : std::string(W.name);
            std::string ShownVal = (InfoIt != SmtNameToInfo.end())
                ? formatWitnessForType(W, InfoIt->second)
                : std::string(W.raw_smt_val);
            if (!WitnessStr.empty()) WitnessStr += ", ";
            WitnessStr += ShownName + " = " + ShownVal;
        }
        if (WitnessStr.empty()) {
            WitnessStr = "<model empty>";
        }

        reportError(
            FD->getLocation(),
            "VeriObjC Violation: SMT contract counterexample (Z3_SAT_COUNTEREXAMPLE) in function '%0' at %1:%2: postcondition '@post(%3)' violated under '@pre(%4)' with witness { %5 }."
        ) << FD->getName() << Line << Col << CombinedPostStr << CombinedPreStr << WitnessStr;
        return;
    }

    if (Status != Z3_UNSAT_PROVEN) {
        reportError(
            FD->getLocation(),
            "VeriObjC Violation: SMT contract verification failed closed (Z3_UNKNOWN_TIMEOUT) in function '%0' at %1:%2 for '@post(%3)' (timeout: %4 ms)."
        ) << FD->getName() << Line << Col << CombinedPostStr << Opts.SmtTimeoutMs;
    }
}

void VeriObjCVisitor::verifyAllContractedFunctions() {
    if (!Opts.SmtVerify && !Opts.EmitSmtReport) {
        return;
    }
    if (ContractedFunctions.empty()) {
        char SolverVer[128] = {0};
        char ProbeErr[512] = {0};
        const char *Z3Bin = Opts.Z3Path.empty() ? nullptr : Opts.Z3Path.c_str();
        if (!rt_z3_probe_version(Z3Bin, Opts.SmtTimeoutMs, SolverVer, sizeof(SolverVer), ProbeErr, sizeof(ProbeErr))) {
            llvm::StringRef ShownPath = Opts.Z3Path.empty() ? VERIOBJC_DEFAULT_Z3_PATH : llvm::StringRef(Opts.Z3Path);
            reportError(
                SourceLocation(),
                "VeriObjC Violation: SMT solver unavailable (Z3_SOLVER_UNAVAILABLE) at path '%0': %1."
            ) << ShownPath << ProbeErr;
        }
        return;
    }
    for (const FunctionDecl *FD : ContractedFunctions) {
        verifyFunctionContractWithZ3(FD);
    }
}

} // namespace veriobjc
