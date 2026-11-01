#include "clang/AST/AST.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendPluginRegistry.h"

using namespace clang;

namespace {

class VeriObjCVisitor : public RecursiveASTVisitor<VeriObjCVisitor> {
private:
    ASTContext *Context;

    void reportBannedId(SourceLocation Loc) {
        DiagnosticsEngine &Diag = Context->getDiagnostics();
        unsigned DiagID = Diag.getCustomDiagID(
            DiagnosticsEngine::Error,
            "VeriObjC Violation: Banned use of generic 'id' type. Use strict nominal object typing."
        );
        Diag.Report(Loc, DiagID);
    }

public:
    explicit VeriObjCVisitor(ASTContext *Context) : Context(Context) {}

    // RULE 1: Intercept and BAN the generic 'id' keyword across all declarations
    bool VisitVarDecl(VarDecl *D) {
        QualType T = D->getType();
        if (T->isObjCIdType()) {
            reportBannedId(D->getLocation());
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
        return true;
    }

    bool VisitTypedefNameDecl(TypedefNameDecl *D) {
        // Prevent typedef id MyId;
        QualType T = D->getUnderlyingType();
        if (T->isObjCIdType()) {
            reportBannedId(D->getLocation());
        }
        return true;
    }
};

class VeriObjCConsumer : public ASTConsumer {
private:
    VeriObjCVisitor Visitor;

public:
    explicit VeriObjCConsumer(ASTContext *Context) : Visitor(Context) {}

    void HandleTranslationUnit(ASTContext &Context) override {
        Visitor.TraverseDecl(Context.getTranslationUnitDecl());
    }
};

class VeriObjCAction : public PluginASTAction {
protected:
    std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI, llvm::StringRef) override {
        return std::make_unique<VeriObjCConsumer>(&CI.getASTContext());
    }

    bool ParseArgs(const CompilerInstance &CI, const std::vector<std::string> &args) override {
        return true;
    }

    PluginASTAction::ActionType getActionType() override {
        return AddBeforeMainAction;
    }
};

} // namespace

// Register the plugin with Clang's FrontendPluginRegistry
static FrontendPluginRegistry::Add<VeriObjCAction> X(
    "veriobjc-check",
    "Enforce VeriObjC structural invariants and eliminate non-deterministic features"
);
