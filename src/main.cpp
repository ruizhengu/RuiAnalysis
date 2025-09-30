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

// static json globalResults = json::object();
static vector<filesystem::path> inputRootDirs; // directories provided as inputs
static json ffmpegResults = json::object();

/**
 * Check if API belongs to FFmpeg
 *
 * @param decl
 * @param Context
 * @return
 */
static bool isFFmpegAPIDecl(const FunctionDecl *decl, const ASTContext &Context) {
    if (!decl) return false;

    // Heuristic: File name contains FFmpeg libs
    const string name = decl->getNameAsString();
    if (!name.empty()) {
        // return true;
        if (
            name.rfind("avutil", 0) == 0 ||
            name.rfind("swscale", 0) == 0 ||
            name.rfind("swresample", 0) == 0 ||
            name.rfind("avcodec", 0) == 0 ||
            name.rfind("avformat", 0) == 0 ||
            name.rfind("avdevice", 0) == 0 ||
            name.rfind("avfilter", 0) == 0 ||
            name.rfind("ffmpeg", 0) == 0
        ) {
            return true;
        }
    }

    // Heuristic: Header path contains FFmpeg libs
    // Get callee source location
    const SourceManager &SM = Context.getSourceManager();
    SourceLocation loc = decl->getLocation();
    if (!loc.isValid()) return false;

    SourceLocation spellingLoc = SM.getSpellingLoc(loc);
    StringRef filenameRef = SM.getFilename(spellingLoc);
    string path = filenameRef.str();
    // lowercase checking
    string lower;
    lower.resize(path.size());
    transform(path.begin(), path.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
    // FFmpeg library source: https://www.ffmpeg.org/documentation.html
    return (
        lower.find("avutil") != string::npos ||
        lower.find("swscale") != string::npos ||
        lower.find("swresample") != string::npos ||
        lower.find("avcodec") != string::npos ||
        lower.find("avformat") != string::npos ||
        lower.find("avdevice") != string::npos ||
        lower.find("avfilter") != string::npos ||
        lower.find("ffmpeg") != string::npos
    );
}

/**
 * Get the relative file path to store in the result
 *
 * @param absoluteOrInputPath
 * @return
 */
static string toDisplayPath(const string &absoluteOrInputPath) {
    filesystem::path absPath = filesystem::weakly_canonical(filesystem::path(absoluteOrInputPath));
    for (const auto &root: inputRootDirs) {
        std::error_code ec;
        filesystem::path rel = absPath.lexically_relative(root);
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
                // Find all c++ and c files
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
    ASTContext &Context;
    string currentFileName;
    string currentFunction;
    vector<string> currentCalls;
    vector<string> currentFfmpegCalls;

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
        if (!currentFunction.empty() && !currentFfmpegCalls.empty()) {
            const string fileKey = toDisplayPath(currentFileName);
            if (!ffmpegResults.contains(fileKey)) {
                ffmpegResults[fileKey] = json::object();
            }
            ffmpegResults[fileKey][currentFunction] = currentFfmpegCalls;
        }
        currentCalls.clear();
        currentFfmpegCalls.clear();
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
        CallExprVisitor callVisitor(Context, callerName, currentCalls, currentFfmpegCalls);
        callVisitor.TraverseStmt(body);
    }

    /**
     * Visitor class: find function calls in methods
     */
    class CallExprVisitor : public RecursiveASTVisitor<CallExprVisitor> {
        ASTContext &Context;
        string callerName;
        vector<string> &calls;
        vector<string> &ffmpegCalls;

    public:
        CallExprVisitor(ASTContext &Context, const string &callerName, vector<string> &calls,
                        vector<string> &ffmpegCalls)
            : Context(Context), callerName(callerName), calls(calls), ffmpegCalls(ffmpegCalls) {
        }

        bool VisitCallExpr(CallExpr *callExpr) {
            outs() << "Found call expression: ";
            // get callee
            FunctionDecl *callee = callExpr->getDirectCallee();
            if (!callee) {
                outs() << callerName << " invalid call expression!\n";
                return true;
            }
            string calleeName = getMethodFullName(callee);
            calls.push_back(calleeName);
            if (isFFmpegAPIDecl(callee, Context)) {
                ffmpegCalls.push_back(calleeName);
                outs() << callerName << " calls " << calleeName << "\n";
            }
            return true;
        };
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
        : analyser(Context, fileName) {
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
    ClangTool Tool(OptionsParser.getCompilations(), allFiles);
    int res = Tool.run(newFrontendActionFactory<CallExprAction>().get());

    outs() << ffmpegResults.dump(2) << "\n";
    // Save FFmpeg calls in JSON file
    const string ffmpegOutput = "ffmpeg_calls.json";
    ofstream ofs2(ffmpegOutput, ios::out | ios::trunc);
    ofs2 << ffmpegResults.dump(2);
    ofs2.close();
    return res;
};
