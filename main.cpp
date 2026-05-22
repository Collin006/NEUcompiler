#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <exception>
#include <cstring>
#include <sstream>
#include <limits>
#include <unordered_set>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#else
#include <unistd.h>
#endif

#include "compiler_core.h"

using namespace std;

namespace {

bool isInteractiveInput() {
#ifdef _WIN32
    return _isatty(_fileno(stdin)) != 0;
#else
    return isatty(fileno(stdin)) != 0;
#endif
}

void waitForEnterIfNeeded() {
    if (!isInteractiveInput()) return;
    cout << "\n[按回车继续下一段]";
    cout.flush();
    cin.ignore(numeric_limits<streamsize>::max(), '\n');
}

void printLinesPaged(const vector<string>& lines, size_t pageSize = 40) {
    if (lines.empty()) return;

    for (size_t i = 0; i < lines.size(); ++i) {
        cout << lines[i] << '\n';
        if ((i + 1) % pageSize == 0 && i + 1 < lines.size()) {
            waitForEnterIfNeeded();
        }
    }
}

vector<string> splitLines(const string& text) {
    vector<string> lines;
    istringstream iss(text);
    string line;

    while (getline(iss, line)) {
        lines.push_back(line);
    }

    return lines;
}

} // namespace

int main(int argc, char* argv[]) {

#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    if (argc < 2) {
        cerr << "Usage: compiler <source-file>\n";
        return 1;
    }

    string sourcePath = argv[1];

    CompilerResult result = compileSourceFile(sourcePath);

    if (!result.stages.lexical && !result.stages.expression &&
        !result.stages.parse && !result.errorMessage.empty()) {
        cerr << (result.errorIsException ? "Compiler error: " : "")
             << result.errorMessage << '\n';
        return 1;
    }

    if (result.stages.lexical) {
        cout << "===== 词法分析 =====\n";
        printLinesPaged(splitLines(result.outputs.tokens), 30);
    }

    if (!result.stages.expression && !result.errorMessage.empty() &&
        !result.errorIsParse) {
        cerr << result.errorMessage << '\n';
        return 1;
    }

    if (result.stages.expression) {
        cout << "\n===== 表达式分析表构建 =====\n";
        printLinesPaged(splitLines(result.outputs.expressionAnalysis), 30);
    }

    if (!result.stages.parse && result.errorIsException) {
        cerr << "Compiler error: " << result.errorMessage << '\n';
        return 1;
    }

    if (result.stages.parse) {
        cout << "\n===== 语法分析 =====\n";
        printLinesPaged(splitLines(result.outputs.parseLog), 30);
    }

    if (result.stages.rawQuadruples) {
        cout << "\n===== 四元式输出 =====\n";
        printLinesPaged(splitLines(result.outputs.rawQuadruples), 30);
        if (result.files.rawQuadruples.written) {
            cout << "[LOG] 原始四元式已写入: "
                 << result.files.rawQuadruples.path << "\n";
        }
    }

    if (result.stages.optimizedQuadruples) {
        cout << "\n===== 四元式优化 =====\n";
        printLinesPaged(splitLines(result.outputs.optimizedQuadruples), 30);
        if (result.files.parseLog.written) {
            cout << "\n[LOG] 详细日志已写入: "
                 << result.files.parseLog.path << "\n";
        }
        if (result.files.quadruples.written) {
            cout << "[LOG] 四元式文件已写入: "
                 << result.files.quadruples.path << "\n";
        }
    }

    if (result.errorIsParse) {
        cerr << "\n[FAIL] " << result.errorMessage << '\n';
        return 1;
    }

    if (result.stages.activeMark) {
        cout << "\n===== 活跃信息标记 =====\n";
        printLinesPaged(splitLines(result.outputs.activeMark), 30);
        if (result.files.activeMark.written) {
            cout << "[LOG] 活跃信息标记已写入: "
                 << result.files.activeMark.path << "\n";
        }
    }

    if (result.stages.targetCode) {
        cout << "\n===== 目标代码生成 =====\n";
        printLinesPaged(splitLines(result.outputs.targetCode), 30);
        if (result.files.targetCode.written) {
            cout << "[LOG] 目标代码文件已写入: "
                 << result.files.targetCode.path << "\n";
        }
    }

    if (result.success) {
        cout << "\n[PASS] 前端与后端流程已连通，目标代码已生成。\n";
        return 0;
    }

    if (!result.errorMessage.empty()) {
        cerr << result.errorMessage << '\n';
    }
    return 1;
}
