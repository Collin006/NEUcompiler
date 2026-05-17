#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <exception>
#include <cstring>
#include <sstream>
#include <limits>

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

#include "global.h"
#include "synbl.h"
#include "parser.h"
#include "quadruple_optimizer.h"

using namespace std;
// 词法分析器接口
// 读取 source，返回 token 序列
vector<Token> lexicalAnalyze(istream& source);

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

    // 控制台强制 UTF-8 编码（SetConsoleOutputCP 设置控制台代码页）
    // 注意：不要使用 _setmode + _O_U8TEXT，会导致 buffer_size % 2 == 0 断言失败
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    // 如果没有提供源文件路径
    if (argc < 2) {
        cerr << "Usage: compiler <source-file>\n";
        return 1;
    }

    // 获取源文件路径
    string sourcePath = argv[1];

    // 打开源文件
    ifstream sourceFile(sourcePath);

    // 判断文件是否打开成功
    if (!sourceFile.is_open()) {
        cerr << "Error: cannot open source file: "
             << sourcePath << '\n';
        return 1;
    }

    try {
        // 1. 词法分析
        cout << "===== 词法分析 =====\n";
        vector<Token> tokens = lexicalAnalyze(sourceFile);

        vector<string> tokenLines;
        tokenLines.reserve(tokens.size());
        for (const Token& token : tokens) {
            tokenLines.push_back("<" + token.type + ", " + token.value +
                                 ", line " + to_string(token.line) + ">");
        }
        printLinesPaged(tokenLines, 30);

        // 2. 自动构建并输出表达式 SELECT 集与 LR(1) 分析表（词法分析后）
        cout << "\n===== 表达式分析表构建 =====\n";
        string expressionDump;
        string expressionError;
        if (!Parser::getExpressionAnalysisDump(expressionDump, expressionError)) {
            cerr << "Error: 表达式 LR(1) 自动构建失败: " << expressionError << '\n';
            return 1;
        }
        printLinesPaged(splitLines(expressionDump), 30);

        // 3. 语法分析
        cout << "\n===== 语法分析 =====\n";
        Parser parser(tokens);
        bool parseOk = parser.parse();

        // 输出日志到控制台（分段）
        printLinesPaged(splitLines(parser.getLog()), 30);

        // 4. 四元式输出
        cout << "\n===== 四元式输出 =====\n";
        printLinesPaged(splitLines(parser.getQuadrupleDump()), 30);

        // 5. 四元式优化（插入在四元式输出与目标代码生成之间）
        cout << "\n===== 四元式优化 =====\n";
        parser.setQuadruples(optimizeQuadruples(parser.getQuadruples()));
        printLinesPaged(splitLines(parser.getQuadrupleDump()), 30);

        // 自动写日志文件（与源文件同目录）
        string logPath = sourcePath + "_parse_log.txt";
        if (parser.writeLogToFile(logPath)) {
            cout << "\n[LOG] 详细日志已写入: " << logPath << "\n";
        }

        // 单独输出四元式文件（优化后，不含语法树，便于后续处理）
        string quadPath = sourcePath + "_quadruples.txt";
        {
            ofstream qout(quadPath, ios::out | ios::trunc);
            if (qout.is_open()) {
                qout << parser.getQuadrupleDump();
                qout.close();
                cout << "[LOG] 四元式文件已写入: " << quadPath << "\n";
            }
        }

        if (!parseOk) {
            cerr << "\n[FAIL] 语法分析发现错误: "
                 << parser.getErrorMessage() << '\n';
            return 1;
        }

        cout << "\n[PASS] 词法、语法与四元式生成均通过。\n";

    }
    catch (const exception& e) {
        cerr << "Compiler error: " << e.what() << '\n';
        return 1;
    }

    sourceFile.close();

    return 0;
}
