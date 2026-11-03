#include <clang/AST/ASTConsumer.h>
#include <clang/AST/ASTContext.h>
#include <clang/AST/Decl.h>
#include <clang/AST/DeclObjC.h>
#include <clang/AST/Expr.h>
#include <clang/AST/ExprObjC.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendPluginRegistry.h>
#include <llvm/ADT/StringRef.h>

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
        QualType T = D->getUnderlyingType();
        if (T->isObjCIdType()) {
            reportBannedId(D->getLocation());
        }
        return true;
    }

    // RULE 2: Ban inheritance from NSObject or any dynamic runtime root
    bool VisitObjCInterfaceDecl(ObjCInterfaceDecl *D) {
        for (const ObjCInterfaceDecl *Super = D->getSuperClass(); Super; Super = Super->getSuperClass()) {
            if (Super->getName() == "NSObject") {
                DiagnosticsEngine &Diag = Context->getDiagnostics();
                unsigned DiagID = Diag.getCustomDiagID(
                    DiagnosticsEngine::Error,
                    "VeriObjC Violation: Banned inheritance from 'NSObject'. VeriObjC classes are freestanding and must not inherit from the dynamic Apple runtime."
                );
                Diag.Report(D->getLocation(), DiagID);
                break;
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
        }
        return true;
    }

    // RULE 4: Dynamic message dispatch requires an explicitly typed receiver
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
