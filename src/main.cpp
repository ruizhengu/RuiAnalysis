#include <iostream>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/Support/CommandLine.h"
// #include "llvm/Support/JSON.h"
// #include "llvm/Support/raw_ostream.h"

using namespace clang;
using namespace clang::tooling;
using namespace llvm;
using namespace std;
using json = nlohmann::json;

// Command-line options
static cl::OptionCategory MyToolCategory("my-tool options");
static cl::extrahelp CommonHelp(CommonOptionsParser::HelpMessage);
static cl::extrahelp MoreHelp("\nMore help text...\n");

static json globalResults = json::object();;

class CallAnalyser : public RecursiveASTVisitor<CallAnalyser> {
    ASTContext &Context;
    string currentFileName;
    string currentFunction;
    vector<string> currentCalls;

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

    void storeResults() {
        if (!currentFunction.empty() && !currentCalls.empty()) {
            if (!globalResults.contains(currentFileName)) {
                globalResults[currentFileName] = json::object();
            }

            outs() << "currentFileName" << currentFileName;
            outs() << "currentFunction" << currentFunction;
            globalResults[currentFileName][currentFunction] = currentCalls;
        }
        currentCalls.clear();
    }

public:
    // Constructor
    explicit CallAnalyser(ASTContext &Context, const string &fileName) : Context(Context), currentFileName(fileName) {
    }

    /**
     * Visit methods (in classes)
     *
     * @param method
     * @return
     */
    bool VisitCXXMethodDecl(CXXMethodDecl *method) {
        storeResults(); // store previous method
        // Get current class and method name
        currentFunction = getMethodFullName(method);

        llvm::outs() << "=== Found Method: " << currentFunction << " ===\n";

        // Analyse method body
        if (method->hasBody()) {
            analyseMethodBody(method, currentFunction);
        }
        llvm::outs() << "---\n";

        storeResults(); // store current method
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
        storeResults(); // store previous function

        currentFunction = getMethodFullName(func);
        llvm::outs() << "=== Found Function: " << currentFunction << " ===\n";

        if (func->hasBody()) {
            analyseMethodBody(func, currentFunction);
        }
        llvm::outs() << "---\n";

        storeResults(); // store current function
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
        CallExprVisitor callVisitor(Context, callerName, currentCalls);
        callVisitor.TraverseStmt(body);
    }

    /**
     * Visitor class: find function calls in methods
     */
    class CallExprVisitor : public RecursiveASTVisitor<CallExprVisitor> {
        ASTContext &Context;
        string callerName;
        vector<string> &calls;

    public:
        CallExprVisitor(ASTContext &Context, const string &callerName, vector<string> &calls)
            : Context(Context), callerName(callerName), calls(calls) {
        }

        bool VisitCallExpr(CallExpr *callExpr) {
            llvm::outs() << "Found call expression: ";
            // get callee
            FunctionDecl *callee = callExpr->getDirectCallee();
            if (callee) {
                string calleeName = getMethodFullName(callee);
                calls.push_back(calleeName);
                llvm::outs() << callerName << " calls " << calleeName << "\n";
            } else {
                llvm::outs() << callerName << " invalid call expression!\n";
            }

            return true;
        }

        // const vector<string> &getCallees() const {
        //     return callees;
        // }
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
    explicit CallExprConsumer(ASTContext &Context, const string &fileName)
        : analyser(Context, fileName), fileName(fileName) {
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
    for (string file: allFiles) {
        outs() << "Found File: " << file << "\n";
    }
    // ClangTool Tool(OptionsParser.getCompilations(), OptionsParser.getSourcePathList());
    ClangTool Tool(OptionsParser.getCompilations(), allFiles);
    int res = Tool.run(newFrontendActionFactory<CallExprAction>().get());

    outs() << "globalResults: " << globalResults.dump(4) << "\n";
    json resultArr = json::array();
    for (auto &fileEntry: globalResults.items()) {
        string fileName = fileEntry.key();
        outs() << "=== Found File: " << fileName << " ===\n";
        json functionCallsObj = json::object();

        // fileEntry.value() contains the function-calls mapping for this file
        for (auto &functionEntry: fileEntry.value().items()) {
            string functionName = functionEntry.key();
            json callsArray = functionEntry.value(); // This should already be an array

            functionCallsObj[functionName] = callsArray;
        }

        json fileObj = json::object();
        fileObj[fileName] = functionCallsObj;
        resultArr.push_back(fileObj);
    }

    // Output the JSON
    llvm::outs() << resultArr.dump(2) << "\n";

    return res;
}
