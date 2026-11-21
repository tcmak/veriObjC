#include "veriobjc/VeriObjCVisitor.h"

#include <clang/AST/ASTConsumer.h>
#include <clang/Basic/CodeGenOptions.h>
#include <clang/Basic/DiagnosticSema.h>
#include <clang/Basic/ParsedAttrInfo.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendPluginRegistry.h>
#include <clang/Sema/ParsedAttr.h>
#include <clang/Sema/Sema.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Plugins/PassPlugin.h>
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
        resetStaticDispatchModuleState();
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

protected:
    std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI, llvm::StringRef) override {
        CI.getCodeGenOpts().PassBuilderCallbacks.push_back(
            [](llvm::PassBuilder &PB) {
                registerVeriObjCPassBuilderCallbacks(PB);
            }
        );
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

extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo llvmGetPassPluginInfo() {
    return {
        LLVM_PLUGIN_API_VERSION,
        "VeriObjCPlugin",
        LLVM_VERSION_STRING,
        [](llvm::PassBuilder &PB) {
            veriobjc::registerVeriObjCPassBuilderCallbacks(PB);
        }
    };
}
