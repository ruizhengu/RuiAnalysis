#include <iostream>
#include <filesystem>
#include <fstream>
// #include <nlohmann/json.hpp>
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/raw_ostream.h"

using namespace clang;
using namespace clang::tooling;
using namespace llvm;
using namespace std;
// using json = nlohmann::json;

// Command-line options
static cl::OptionCategory MyToolCategory("my-tool options");
static cl::extrahelp CommonHelp(CommonOptionsParser::HelpMessage);
static cl::extrahelp MoreHelp("\nMore help text...\n");
static std::map<std::string, std::vector<std::string> > globalResults;


class CallAnalyser : public RecursiveASTVisitor<CallAnalyser> {
    ASTContext &Context;

    static string getMethodFullName(FunctionDecl *func) {
        // if method
        if (auto *method = dyn_cast<CXXMethodDecl>(func)) {
            if (auto *cls = method->getParent()) {
                return cls->getNameAsString() + "::" + method->getNameAsString();
            }
        }
        // if function
        return func->getNameAsString();
    }

public:
    // Constructor
    explicit CallAnalyser(ASTContext &Context) : Context(Context) {
    }

    /**
     * Visit methods (in classes)
     *
     * @param method
     * @return
     */
    bool VisitCXXMethodDecl(CXXMethodDecl *method) {
        // Get class and method name
        // string className = method->getParent()->getNameAsString();
        // string methodName = method->getNameAsString();
        string fullMethodName = getMethodFullName(method);

        llvm::outs() << "=== Found Method: " << fullMethodName << " ===\n";

        // Analyse method body
        if (method->hasBody()) {
            analyseMethodBody(method, fullMethodName);
        }
        llvm::outs() << "---\n";
        return true;
    }

    /**
     * Visit functions (not in classes)
     *
     * @param func
     * @return
     */
    bool VisitFunctionDecl(FunctionDecl *func) {
        // Skip methods (handled above)
        if (isa<CXXMethodDecl>(func)) {
            return true;
        }

        string functionName = getMethodFullName(func);
        llvm::outs() << "=== Found Function: " << functionName << " ===\n";

        if (func->hasBody()) {
            analyseMethodBody(func, functionName);
        }
        llvm::outs() << "---\n";
        return true;
    }

private:
    /**
     * Analyse the method content
     *
     * @param funcDecl
     * @param callerName
     */
    void analyseMethodBody(FunctionDecl *funcDecl, const string &callerName) {
        // Get method body
        Stmt *body = funcDecl->getBody();
        if (!body) return;

        // Visitor to find call expressions in method
        CallExprVisitor callVisitor(Context, callerName);
        callVisitor.TraverseStmt(body);
    }

    /**
     * Visitor class: find function calls in methods
     */
    class CallExprVisitor : public RecursiveASTVisitor<CallExprVisitor> {
        ASTContext &Context;
        string callerName;
        vector<string> callees;

    public:
        CallExprVisitor(ASTContext &Context, const string &callerName)
            : Context(Context), callerName(callerName) {
        }

        bool VisitCallExpr(CallExpr *callExpr) {
            llvm::outs() << "Found call expression: ";
            // get callee
            FunctionDecl *callee = callExpr->getDirectCallee();
            if (callee) {
                string calleeName = getMethodFullName(callee);
                callees.push_back(calleeName);
                llvm::outs() << callerName << " calls " << calleeName << "\n";
            } else {
                llvm::outs() << callerName << " invalid call expression!\n";
            }

            return true;
        }

        const vector<string> &getCallees() const {
            return callees;
        }
    };
};

/**
 * Manage analysis process
 */
class CallExprConsumer : public ASTConsumer {
    CallAnalyser analyser;
    string fileName;

public:
    // Constructor
    explicit CallExprConsumer(ASTContext &Context, const string &fileName) : analyser(Context), fileName(fileName) {
    }

    void HandleTranslationUnit(ASTContext &Context) override {
        llvm::outs() << "Starting Analysis\n";
        // Traverse AST
        analyser.TraverseDecl(Context.getTranslationUnitDecl());
        llvm::outs() << "Analysis Complete\n";
    };
};

/**
 * Create analyser
 */
class CallExprAction : public ASTFrontendAction {
public:
    std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI, StringRef InFile) override {
        return std::make_unique<CallExprConsumer>(CI.getASTContext(), InFile.str());
    }
};

vector<string> findProjectFiles(const string &projectDir) {
    vector<string> files;

    try {
        for (const auto &entry: filesystem::recursive_directory_iterator(projectDir)) {
            if (entry.is_regular_file()) {
                string extension = entry.path().extension().string();
                // if (extension == ".cpp" || extension == ".cxx" || extension == ".cc" ||
                //     extension == ".h" || extension == ".hpp" || extension == ".hxx") {
                if (extension == ".cpp" || extension == ".c") {
                    files.push_back(entry.path().string());
                }
            }
        }
    } catch (const filesystem::filesystem_error &e) {
        errs() << "Error: Could not read directory '" << projectDir << "': " << e.what() << "\n";
    }
    return files;
}

int main(int argc, const char **argv) {
    auto ExpectedParser = CommonOptionsParser::create(argc, argv, MyToolCategory);
    if (!ExpectedParser) {
        llvm::errs() << ExpectedParser.takeError();
        return 1;
    }
    CommonOptionsParser &OptionsParser = ExpectedParser.get();

    vector<string> inPaths = OptionsParser.getSourcePathList();
    vector<string> allFiles;
    for (auto &p: inPaths) {
        if (filesystem::is_directory(p)) {
            auto more = findProjectFiles(p);
            allFiles.insert(allFiles.end(), more.begin(), more.end());
        } else {
            allFiles.push_back(p);
        }
    }

    // ClangTool Tool(OptionsParser.getCompilations(), OptionsParser.getSourcePathList());
    ClangTool Tool(OptionsParser.getCompilations(), allFiles);
    int res = Tool.run(newFrontendActionFactory<CallExprAction>().get());

    json::Array arr;
    for (string file: allFiles) {
        json::Object obj;
        json::Object inner;

        json::Array callsArr;
        inner["calls"] = std::move(callsArr);
        // obj[fname] = std::move(inner);

        arr.push_back(std::move(obj));
    }

    llvm::outs() << llvm::formatv("{0:2}", llvm::json::Value(std::move(arr))) << "\n";

    return res;
}
