#include "veriobjc/VeriObjCVisitor.h"

#include <clang/AST/ASTConsumer.h>
#include <clang/Basic/DiagnosticSema.h>
#include <clang/Basic/FileManager.h>
#include <clang/Basic/ParsedAttrInfo.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendPluginRegistry.h>
#include <clang/Sema/ParsedAttr.h>
#include <clang/Sema/Sema.h>
#include <llvm/ADT/StringSet.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/MemoryBuffer.h>
#include <memory>

using namespace clang;

namespace veriobjc {
namespace {

class VeriObjCConsumer : public ASTConsumer {
private:
    CompilerInstance &CI;
    VeriObjCVisitor Visitor;
    bool EmittingSynthesized = false;

public:
    explicit VeriObjCConsumer(CompilerInstance &CI, VeriObjCPluginOptions Opts = {})
        : CI(CI), Visitor(&CI.getASTContext(), Opts) {
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
        Visitor.finalizeTranslationUnitAudits();
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
    VeriObjCPluginOptions Opts;

    static std::string mangleTypeIdent(llvm::StringRef S) {
        std::string Out;
        for (char C : S) {
            if ((C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') || (C >= '0' && C <= '9')) {
                Out.push_back(C);
            } else if (C == '*') {
                Out += "_ptr";
            } else if (C != ' ' && C != '\t') {
                Out.push_back('_');
            }
        }
        return Out;
    }

    static bool isIdentChar(char C) {
        return (C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') ||
               (C >= '0' && C <= '9') || C == '_';
    }

    static bool isInLineComment(llvm::StringRef Text, size_t Offset) {
        size_t LineStart = Text.rfind('\n', Offset);
        LineStart = (LineStart == llvm::StringRef::npos) ? 0 : (LineStart + 1);
        return Text.substr(LineStart, Offset - LineStart).contains("//");
    }

    static void lowerVVectorAndByValueInterfacesInSource(CompilerInstance &CI, llvm::StringRef InFile) {
        if (InFile.empty() || !CI.hasFileManager() || !CI.hasSourceManager()) return;
        auto FE = CI.getFileManager().getFileRef(InFile);
        if (!FE) {
            llvm::consumeError(FE.takeError());
            return;
        }
        auto BufOrErr = CI.getFileManager().getBufferForFile(*FE);
        if (!BufOrErr) return;

        std::string Src = (*BufOrErr)->getBuffer().str();
        std::string Out = Src;
        std::vector<std::string> SynthStructs;
        llvm::StringSet<> EmittedNames;

        // 1. VG-005: Lower vvector<ElemType, Cap> (and VVector<ElemType, Cap>)
        size_t Pos = 0;
        while (Pos < Out.size()) {
            size_t MatchPos = std::string::npos;
            size_t PrefixLen = 0;
            size_t P1 = Out.find("vvector<", Pos);
            size_t P2 = Out.find("VVector<", Pos);
            if (P1 != std::string::npos && (P2 == std::string::npos || P1 <= P2)) {
                MatchPos = P1;
                PrefixLen = 8;
            } else if (P2 != std::string::npos) {
                MatchPos = P2;
                PrefixLen = 8;
            } else {
                break;
            }

            if ((MatchPos > 0 && isIdentChar(Out[MatchPos - 1])) || isInLineComment(Out, MatchPos)) {
                Pos = MatchPos + PrefixLen;
                continue;
            }
            size_t ClosePos = Out.find('>', MatchPos + PrefixLen);
            size_t LineEnd = Out.find('\n', MatchPos);
            if (ClosePos == std::string::npos || (LineEnd != std::string::npos && ClosePos > LineEnd)) {
                Pos = MatchPos + PrefixLen;
                continue;
            }

            std::string Inside = Out.substr(MatchPos + PrefixLen, ClosePos - (MatchPos + PrefixLen));
            size_t CommaPos = Inside.find(',');
            if (CommaPos != std::string::npos) {
                std::string Elem = llvm::StringRef(Inside).substr(0, CommaPos).trim().str();
                std::string Cap = llvm::StringRef(Inside).substr(CommaPos + 1).trim().str();
                std::string StructName = "__veriobjc_vvec_mono_" + mangleTypeIdent(Elem) + "_" + mangleTypeIdent(Cap);
                if (EmittedNames.insert(StructName).second) {
                    bool IsPtr = (Elem.find('*') != std::string::npos);
                    std::string Attrs = IsPtr
                        ? ("__attribute__((annotate(\"objc_vvector:" + Cap + "\")))")
                        : ("__attribute__((annotate(\"objc_vvector:" + Cap + "\"), annotate(\"objc_flat\")))");
                    SynthStructs.push_back(
                        "struct " + Attrs + " " + StructName +
                        " { uint32_t count; uint32_t cap; " + Elem + " buf[" + Cap + "]; };"
                    );
                }
                std::string Repl = "struct " + StructName;
                Out.replace(MatchPos, ClosePos - MatchPos + 1, Repl);
                Pos = MatchPos + Repl.size();
                continue;
            }

            // 2. VG-006: VVector<ElemType> allocated or returned by value (not followed by '*')
            if (Out.compare(MatchPos, 8, "VVector<") == 0) {
                size_t After = ClosePos + 1;
                while (After < Out.size() && (Out[After] == ' ' || Out[After] == '\t')) After++;
                if (After < Out.size() && isIdentChar(Out[After])) {
                    std::string Elem = llvm::StringRef(Inside).trim().str();
                    if (Elem.find(':') == std::string::npos) {
                        std::string StructName = "__veriobjc_vvec_byval_" + mangleTypeIdent(Elem);
                        if (EmittedNames.insert(StructName).second) {
                            SynthStructs.push_back(
                                "struct __attribute__((annotate(\"objc_vvector:4\"), annotate(\"objc_interface_by_value:VVector\"))) " +
                                StructName + " { uint32_t count; uint32_t cap; " + Elem + " buf[4]; };"
                            );
                        }
                        size_t IdentStart = After;
                        size_t IdentEnd = IdentStart;
                        while (IdentEnd < Out.size() && isIdentChar(Out[IdentEnd])) IdentEnd++;
                        std::string IdentName = Out.substr(IdentStart, IdentEnd - IdentStart);

                        std::string Repl = "struct " + StructName;
                        Out.replace(MatchPos, ClosePos - MatchPos + 1, Repl);

                        std::string RecvPat = "[&" + IdentName + " ";
                        std::string RecvRepl = "[((VVector<" + Elem + "> *)&" + IdentName + ") ";
                        size_t RPos = 0;
                        while ((RPos = Out.find(RecvPat, RPos)) != std::string::npos) {
                            Out.replace(RPos, RecvPat.size(), RecvRepl);
                            RPos += RecvRepl.size();
                        }
                        Pos = MatchPos + Repl.size();
                        continue;
                    }
                }
            }

            Pos = ClosePos + 1;
        }

        // 3. VG-006: Detect any user @interface ClsName allocated or returned by value (not followed by '*')
        std::vector<std::string> UserInterfaces;
        size_t IPos = 0;
        while ((IPos = Out.find("@interface ", IPos)) != std::string::npos) {
            if (isInLineComment(Out, IPos)) {
                IPos += 11;
                continue;
            }
            size_t NameStart = IPos + 11;
            while (NameStart < Out.size() && (Out[NameStart] == ' ' || Out[NameStart] == '\t')) NameStart++;
            size_t NameEnd = NameStart;
            while (NameEnd < Out.size() && isIdentChar(Out[NameEnd])) NameEnd++;
            if (NameEnd > NameStart) {
                std::string IName = Out.substr(NameStart, NameEnd - NameStart);
                if (IName != "VVector" && IName != "VVectorItem") {
                    UserInterfaces.push_back(IName);
                }
            }
            IPos = NameEnd;
        }

        for (const std::string &IName : UserInterfaces) {
            size_t SPos = 0;
            while ((SPos = Out.find(IName, SPos)) != std::string::npos) {
                if (SPos > 0 && isIdentChar(Out[SPos - 1])) {
                    SPos += IName.size();
                    continue;
                }
                size_t EndPos = SPos + IName.size();
                if (EndPos < Out.size() && isIdentChar(Out[EndPos])) {
                    SPos = EndPos;
                    continue;
                }
                size_t LineStart = Out.rfind('\n', SPos);
                LineStart = (LineStart == std::string::npos) ? 0 : (LineStart + 1);
                llvm::StringRef Prefix = llvm::StringRef(Out).substr(LineStart, SPos - LineStart).trim();
                if (!Prefix.empty()) {
                    SPos = EndPos;
                    continue;
                }
                size_t NextTok = EndPos;
                while (NextTok < Out.size() && (Out[NextTok] == ' ' || Out[NextTok] == '\t')) NextTok++;
                if (NextTok < Out.size() && isIdentChar(Out[NextTok])) {
                    std::string StructName = "__veriobjc_iface_byval_" + IName;
                    if (EmittedNames.insert(StructName).second) {
                        SynthStructs.push_back(
                            "struct __attribute__((annotate(\"objc_interface_by_value:" + IName + "\"))) " +
                            StructName + " { uint32_t __vobj_byval_slot; };"
                        );
                    }
                    std::string Repl = "struct " + StructName;
                    Out.replace(SPos, IName.size(), Repl);
                    SPos += Repl.size();
                    continue;
                }
                SPos = EndPos;
            }
        }

        if (!SynthStructs.empty()) {
            std::string Combined;
            for (const std::string &S : SynthStructs) {
                Combined += " " + S;
            }
            size_t InsertAnchor = Out.rfind("@end");
            if (InsertAnchor != std::string::npos) {
                InsertAnchor += 4;
            } else {
                size_t LastInc = Out.rfind("#include");
                InsertAnchor = (LastInc != std::string::npos) ? Out.find('\n', LastInc) : 0;
            }
            if (InsertAnchor != std::string::npos) {
                Out.insert(InsertAnchor, Combined);
            }
        }

        if (Out != Src) {
            CI.getSourceManager().overrideFileContents(
                *FE, llvm::MemoryBuffer::getMemBufferCopy(Out, InFile)
            );
        }
    }

protected:
    std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI, llvm::StringRef InFile) override {
        lowerVVectorAndByValueInterfacesInSource(CI, InFile);
        return std::make_unique<VeriObjCConsumer>(CI, Opts);
    }

    bool ParseArgs(const CompilerInstance &CI, const std::vector<std::string> &args) override {
        for (const std::string &A : args) {
            if (A == "--effects-report") Opts.EmitEffectsReport = true;
            else if (A == "--branch-report") Opts.EmitBranchReport = true;
            else if (A == "--mono-report") Opts.EmitMonoReport = true;
            else if (A == "--export-header-stdout") Opts.EmitExportHeader = true;
            else if (A == "--smt-report") Opts.EmitSmtReport = true;
            else {
                DiagnosticsEngine &Diag = CI.getDiagnostics();
                unsigned ID = Diag.getCustomDiagID(
                    DiagnosticsEngine::Error,
                    "VeriObjC Plugin: unknown argument '%0'"
                );
                Diag.Report(ID) << A;
                return false;
            }
        }
        return true;
    }

    PluginASTAction::ActionType getActionType() override {
        return AddBeforeMainAction;
    }
};

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

inline constexpr char kAttrSealed[]             = "objc_sealed";
inline constexpr char kAttrFinal[]              = "objc_final";
inline constexpr char kAttrConsumed[]           = "objc_consumed";
inline constexpr char kAttrFlat[]               = "objc_flat";
inline constexpr char kAttrVConstexpr[]         = "objc_vconstexpr";
inline constexpr char kAttrExport[]             = "objc_export";
inline constexpr char kAttrHwKernel[]           = "objc_hw_kernel";
inline constexpr char kAttrNolatency[]          = "objc_nolatency";
inline constexpr char kAttrPure[]               = "objc_pure";
inline constexpr char kAttrCold[]               = "objc_cold";
inline constexpr char kAttrEffectAlloc[]        = "objc_effect_alloc";
inline constexpr char kAttrEffectIo[]           = "objc_effect_io";
inline constexpr char kAttrEffectBlocking[]     = "objc_effect_blocking";
inline constexpr char kAttrEffectFence[]        = "objc_effect_fence";
inline constexpr char kAttrEffectSharedRetain[] = "objc_effect_shared_retain";
inline constexpr char kAttrSoA[]                = "objc_soa";
inline constexpr char kAttrVVariant[]           = "objc_vvariant";
inline constexpr char kAttrVTrait[]             = "objc_vtrait";
inline constexpr char kAttrInplaceOnly[]        = "objc_inplace_only";
inline constexpr char kAttrSpecialize[]         = "objc_specialize";

} // namespace
} // namespace veriobjc

using veriobjc::VeriObjCAction;
using veriobjc::VeriObjCAnnotateAttrInfo;

static ParsedAttrInfoRegistry::Add<VeriObjCAnnotateAttrInfo<veriobjc::kAttrSealed>> AttrSealed(
    "objc_sealed", "Mark VeriObjC class or protocol hierarchy as closed-world sealed"
);
static ParsedAttrInfoRegistry::Add<VeriObjCAnnotateAttrInfo<veriobjc::kAttrFinal>> AttrFinal(
    "objc_final", "Mark VeriObjC class as final (disallow subclassing)"
);
static ParsedAttrInfoRegistry::Add<VeriObjCAnnotateAttrInfo<veriobjc::kAttrConsumed>> AttrConsumed(
    "objc_consumed", "Mark VeriObjC parameter as consuming linear ownership"
);
static ParsedAttrInfoRegistry::Add<VeriObjCAnnotateAttrInfo<veriobjc::kAttrFlat>> AttrFlat(
    "objc_flat", "Enforce VeriObjC flat-value struct layout (no pointers at any depth)"
);
static ParsedAttrInfoRegistry::Add<VeriObjCAnnotateAttrInfo<veriobjc::kAttrVConstexpr>> AttrVConstexpr(
    "objc_vconstexpr", "Mark function for fuel-bounded compile-time evaluation (vconstexpr)"
);
static ParsedAttrInfoRegistry::Add<VeriObjCAnnotateAttrInfo<veriobjc::kAttrExport>> AttrExport(
    "objc_export", "Mark VeriObjC function or method as an exported FFI boundary"
);
static ParsedAttrInfoRegistry::Add<VeriObjCAnnotateAttrInfo<veriobjc::kAttrHwKernel>> AttrHwKernel(
    "objc_hw_kernel", "Mark VeriObjC function as a synthesizable veriStream hardware kernel"
);
static ParsedAttrInfoRegistry::Add<VeriObjCAnnotateAttrInfo<veriobjc::kAttrNolatency>> AttrNolatency(
    "objc_nolatency", "Mark VeriObjC function as a zero-allocation, non-blocking @nolatency path"
);
static ParsedAttrInfoRegistry::Add<VeriObjCAnnotateAttrInfo<veriobjc::kAttrPure>> AttrPure(
    "objc_pure", "Mark VeriObjC function as effect-free @pure"
);
static ParsedAttrInfoRegistry::Add<VeriObjCAnnotateAttrInfo<veriobjc::kAttrCold>> AttrCold(
    "objc_cold", "Mark VeriObjC function as @cold diagnostic quarantine"
);
static ParsedAttrInfoRegistry::Add<VeriObjCAnnotateAttrInfo<veriobjc::kAttrEffectAlloc>> AttrEffectAlloc(
    "objc_effect_alloc", "Declare @alloc effect on function signature"
);
static ParsedAttrInfoRegistry::Add<VeriObjCAnnotateAttrInfo<veriobjc::kAttrEffectIo>> AttrEffectIo(
    "objc_effect_io", "Declare @io effect on function signature"
);
static ParsedAttrInfoRegistry::Add<VeriObjCAnnotateAttrInfo<veriobjc::kAttrEffectBlocking>> AttrEffectBlocking(
    "objc_effect_blocking", "Declare @blocking effect on function signature"
);
static ParsedAttrInfoRegistry::Add<VeriObjCAnnotateAttrInfo<veriobjc::kAttrEffectFence>> AttrEffectFence(
    "objc_effect_fence", "Declare @fence effect on function signature"
);
static ParsedAttrInfoRegistry::Add<VeriObjCAnnotateAttrInfo<veriobjc::kAttrEffectSharedRetain>> AttrEffectSharedRetain(
    "objc_effect_shared_retain", "Declare @shared-retain effect on function signature"
);
static ParsedAttrInfoRegistry::Add<VeriObjCAnnotateAttrInfo<veriobjc::kAttrSoA>> AttrSoA(
    "objc_soa", "Mark struct as Struct-of-Arrays (SoA) cache-aligned layout"
);
static ParsedAttrInfoRegistry::Add<VeriObjCAnnotateAttrInfo<veriobjc::kAttrVVariant>> AttrVVariant(
    "objc_vvariant", "Mark struct as a closed-set vvariant algebraic data type"
);
static ParsedAttrInfoRegistry::Add<VeriObjCAnnotateAttrInfo<veriobjc::kAttrVTrait>> AttrVTrait(
    "objc_vtrait", "Mark function as a closed compile-time vtrait table"
);
static ParsedAttrInfoRegistry::Add<VeriObjCAnnotateAttrInfo<veriobjc::kAttrInplaceOnly>> AttrInplaceOnly(
    "objc_inplace_only", "Require destination-slot built-in-place construction (forbid copies)"
);
static ParsedAttrInfoRegistry::Add<VeriObjCAnnotateAttrInfo<veriobjc::kAttrSpecialize>> AttrSpecialize(
    "objc_specialize", "Detect banned ad-hoc generic specialization / SFINAE probing"
);
static FrontendPluginRegistry::Add<VeriObjCAction> X(
    "veriobjc-check",
    "Enforce VeriObjC structural invariants and eliminate non-deterministic features"
);
