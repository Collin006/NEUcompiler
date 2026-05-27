#pragma once

#include <string>

struct CompilerStageOutputs {
    std::string tokens;
    std::string expressionAnalysis;
    std::string parseLog;
    std::string rawQuadruples;
    std::string optimizedQuadruples;
    std::string activeMark;
    std::string targetCode;
};

struct CompilerStageStatus {
    bool lexical = false;
    bool expression = false;
    bool parse = false;
    bool rawQuadruples = false;
    bool optimizedQuadruples = false;
    bool activeMark = false;
    bool targetCode = false;
};

struct CompilerOutputFile {
    std::string path;
    bool written = false;
};

struct CompilerOutputFiles {
    CompilerOutputFile rawQuadruples;
    CompilerOutputFile parseLog;
    CompilerOutputFile quadruples;
    CompilerOutputFile activeMark;
    CompilerOutputFile targetCode;
};

struct CompilerOptions {
    bool writeFiles = true;
};

struct CompilerResult {
    bool success = false;
    bool parseOk = false;
    bool errorIsException = false;
    bool errorIsParse = false;
    std::string errorMessage;
    CompilerStageOutputs outputs;
    CompilerStageStatus stages;
    CompilerOutputFiles files;
};

CompilerResult compileSourceFile(const std::string& sourcePath,
                                 const CompilerOptions& options = {});
