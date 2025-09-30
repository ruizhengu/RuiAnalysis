// #include <iostream>
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

using namespace clang;
using namespace clang::tooling;
using namespace llvm;
using namespace std;
using json = nlohmann::json;

// Command-line options
static cl::OptionCategory MyToolCategory("my-tool options");
static cl::extrahelp CommonHelp(CommonOptionsParser::HelpMessage);
static cl::extrahelp MoreHelp("\nMore help text...\n");

static json globalResults = json::object();
static vector<filesystem::path> inputRootDirs; // directories provided as inputs

static string toDisplayPath(const string &absoluteOrInputPath) {
    filesystem::path absPath = filesystem::weakly_canonical(filesystem::path(absoluteOrInputPath));
    for (const auto &root: inputRootDirs) {
        std::error_code ec;
        filesystem::path rel = absPath.lexically_relative(root);
        // Use this relative path if it does not traverse outside the root
        if (!rel.empty() && rel.native().find("..") != 0) {
            return rel.string();
        }
    }
    return absPath.filename().string();
}

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

class CallAnalyser : public RecursiveASTVisitor<CallAnalyser> {
    string currentFileName;
    string currentFunction;
    vector<string> currentCalls;

    static string getMethodFullName(const FunctionDecl *func) {
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
            const string fileKey = toDisplayPath(currentFileName);

            if (!globalResults.contains(fileKey)) {
                globalResults[fileKey] = json::object();
            }
            globalResults[fileKey][currentFunction] = currentCalls;
        }
        currentCalls.clear();
    }

public:
    // Constructor
    explicit CallAnalyser(const string &fileName) : currentFileName(fileName) {
    }

    /**
     * Visit methods (in classes)
     *
     * @param method
     * @return
     */
    bool VisitCXXMethodDecl(CXXMethodDecl *method) {
        // storeResults(); // store previous method
        // Get current class and method name
        currentFunction = getMethodFullName(method);

        outs() << "=== Found Method: " << currentFunction << " ===\n";

        // Analyse method body
        if (method->hasBody()) {
            analyseMethodBody(method, currentFunction);
        }
        outs() << "---\n";

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
        // storeResults(); // store previous function

        currentFunction = getMethodFullName(func);
        outs() << "=== Found Function: " << currentFunction << " ===\n";

        if (func->hasBody()) {
            analyseMethodBody(func, currentFunction);
        }
        outs() << "---\n";

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
        CallExprVisitor callVisitor(callerName, currentCalls);
        callVisitor.TraverseStmt(body);
    }

    /**
     * Visitor class: find function calls in methods
     */
    class CallExprVisitor : public RecursiveASTVisitor<CallExprVisitor> {
        string callerName;
        vector<string> &calls;

    public:
        CallExprVisitor(const string &callerName, vector<string> &calls)
            : callerName(callerName), calls(calls) {
        }

        bool VisitCallExpr(CallExpr *callExpr) {
            outs() << "Found call expression: ";
            // get callee
            FunctionDecl *callee = callExpr->getDirectCallee();
            if (callee) {
                string calleeName = getMethodFullName(callee);
                calls.push_back(calleeName);
                outs() << callerName << " calls " << calleeName << "\n";
            } else {
                outs() << callerName << " invalid call expression!\n";
            }

            return true;
        }
    };
};

/**
 * Manage analysis process
 */
class CallExprConsumer : public ASTConsumer {
    CallAnalyser analyser;

public:
    // Constructor
    explicit CallExprConsumer(ASTContext &Context, const string &fileName)
        : analyser(fileName) {
    }

    void HandleTranslationUnit(ASTContext &Context) override {
        outs() << "Starting Analysis\n";
        // Traverse AST
        analyser.TraverseDecl(Context.getTranslationUnitDecl());
        outs() << "Analysis Complete\n";
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

int main(int argc, const char **argv) {
    auto ExpectedParser = CommonOptionsParser::create(argc, argv, MyToolCategory);
    if (!ExpectedParser) {
        llvm::errs() << ExpectedParser.takeError();
        return 1;
    }
    CommonOptionsParser &OptionsParser = ExpectedParser.get();

    vector<string> inPaths = OptionsParser.getSourcePathList();
    vector<string> allFiles;
    for (const auto &p: inPaths) {
        if (filesystem::is_directory(p)) {
            auto more = findProjectFiles(p);
            allFiles.insert(allFiles.end(), more.begin(), more.end());
        } else {
            allFiles.push_back(p);
        }
    }
    // Record input root directories for relative path computation
    inputRootDirs.clear();
    for (const auto &p: inPaths) {
        if (filesystem::is_directory(p)) {
            inputRootDirs.emplace_back(filesystem::weakly_canonical(filesystem::path(p)));
        } else {
            inputRootDirs.emplace_back(filesystem::weakly_canonical(filesystem::path(p)).parent_path());
        }
    }
    for (const string &file: allFiles) {
        outs() << "Found File: " << file << "\n";
    }
    ClangTool Tool(OptionsParser.getCompilations(), allFiles);
    int res = Tool.run(newFrontendActionFactory<CallExprAction>().get());

    // Output the aggregated JSON map: file -> function -> [callees]
    outs() << globalResults.dump(2) << "\n";

    // Save JSON to file
    const string outputFile = "call_graph.json";
    ofstream ofs(outputFile, ios::out | ios::trunc);
    ofs << globalResults.dump(2);
    ofs.close();
    return res;
}
