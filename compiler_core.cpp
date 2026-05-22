#include "compiler_core.h"

#include <fstream>
#include <sstream>
#include <exception>
#include <vector>

#include "global.h"
#include "parser.h"
#include "youhua.h"
#include "active_mark.h"
#include "code_builders.h"

using std::string;
using std::vector;

vector<Token> lexicalAnalyze(std::istream& source);

namespace {

string buildTokenDump(const vector<Token>& tokens) {
    std::ostringstream out;

    for (const auto& token : tokens) {
        out << "<" << token.type << ", " << token.value
            << ", line " << token.line << ">\n";
    }

    return out.str();
}

bool writeTextFile(const string& path, const string& content) {
    std::ofstream out(path, std::ios::out | std::ios::trunc);
    if (!out.is_open()) {
        return false;
    }
    out << content;
    return true;
}

}

CompilerResult compileSourceFile(const string& sourcePath,
                                 const CompilerOptions& options) {
    CompilerResult result;

    result.files.rawQuadruples.path = sourcePath + "_raw_quadruples.txt";
    result.files.parseLog.path = sourcePath + "_parse_log.txt";
    result.files.quadruples.path = sourcePath + "_quadruples.txt";
    result.files.activeMark.path = sourcePath + "_active_mark.txt";
    result.files.targetCode.path = sourcePath + "_target_code.txt";

    try {
        std::ifstream sourceFile(sourcePath);
        if (!sourceFile.is_open()) {
            result.errorMessage = "Error: cannot open source file: " + sourcePath;
            return result;
        }

        vector<Token> tokens = lexicalAnalyze(sourceFile);
        result.outputs.tokens = buildTokenDump(tokens);
        result.stages.lexical = true;

        string expressionDump;
        string expressionError;
        if (!Parser::getExpressionAnalysisDump(expressionDump, expressionError)) {
            result.errorMessage =
                "Error: 表达式 LR(1) 自动构建失败: " + expressionError;
            return result;
        }
        result.outputs.expressionAnalysis = expressionDump;
        result.stages.expression = true;

        Parser parser(tokens);
        result.parseOk = parser.parse();
        result.outputs.parseLog = parser.getLog();
        result.stages.parse = true;

        result.outputs.rawQuadruples = parser.getQuadrupleDump();
        result.stages.rawQuadruples = true;
        if (options.writeFiles) {
            if (writeTextFile(result.files.rawQuadruples.path,
                              result.outputs.rawQuadruples)) {
                result.files.rawQuadruples.written = true;
            }
        }

        parser.setQuadruples(optimize(parser.getQuadruples()));
        result.outputs.optimizedQuadruples = parser.getQuadrupleDump();
        result.stages.optimizedQuadruples = true;

        if (options.writeFiles) {
            if (parser.writeLogToFile(result.files.parseLog.path)) {
                result.files.parseLog.written = true;
            }

            if (writeTextFile(result.files.quadruples.path,
                              result.outputs.optimizedQuadruples)) {
                result.files.quadruples.written = true;
            }
        }

        if (!result.parseOk) {
            result.errorIsParse = true;
            result.errorMessage =
                "语法分析发现错误: " + parser.getErrorMessage();
            return result;
        }

        vector<BasicBlock> activeBlocks = buildBasicBlocks(parser.getQuadruples());
        auto [markedQuadruples, activeRecord] =
            ActiveMark(parser.getQuadruples(), activeBlocks);
        result.outputs.activeMark = dumpMarkedQuadruples(markedQuadruples);
        result.stages.activeMark = true;

        if (options.writeFiles) {
            std::ostringstream activeOutput;
            activeOutput << "===== 活跃信息标记 =====\n";
            activeOutput << result.outputs.activeMark;
            if (writeTextFile(result.files.activeMark.path, activeOutput.str())) {
                result.files.activeMark.written = true;
            }
        }
        static_cast<void>(activeRecord);

        result.outputs.targetCode = generateTargetCode(parser.getQuadruples());
        result.stages.targetCode = true;
        if (options.writeFiles) {
            if (writeTextFile(result.files.targetCode.path,
                              result.outputs.targetCode)) {
                result.files.targetCode.written = true;
            }
        }

        result.success = true;
        return result;
    } catch (const std::exception& e) {
        result.errorIsException = true;
        result.errorMessage = e.what();
        return result;
    }
}
