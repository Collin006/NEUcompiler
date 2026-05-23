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

#include "global.h"
#include "synbl.h"
#include "parser.h"
#include "youhua.h"
#include "active_mark.h"
#include "code_builders.h"

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

#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    if (argc < 2) {
        cerr << "Usage: compiler <source-file>\n";
        return 1;
    }

    string sourcePath = argv[1];

    ifstream sourceFile(sourcePath);

    if (!sourceFile.is_open()) {
        cerr << "Error: cannot open source file: "
             << sourcePath << '\n';
        return 1;
    }

    try {
        cout << "===== 词法分析 =====\n";
        vector<Token> tokens = lexicalAnalyze(sourceFile);

        vector<string> tokenLines;
        tokenLines.reserve(tokens.size());
        for (const Token& token : tokens) {
            tokenLines.push_back("<" + token.type + ", " + token.value +
                                 ", line " + to_string(token.line) + ">");
        }
        printLinesPaged(tokenLines, 30);

        cout << "\n===== 表达式分析表构建 =====\n";
        string expressionDump;
        string expressionError;
        if (!Parser::getExpressionAnalysisDump(expressionDump, expressionError)) {
            cerr << "Error: 表达式 LR(1) 自动构建失败: " << expressionError << '\n';
            return 1;
        }
        printLinesPaged(splitLines(expressionDump), 30);

        cout << "\n===== 语法分析 =====\n";
        Parser parser(tokens);
        bool parseOk = parser.parse();

        printLinesPaged(splitLines(parser.getLog()), 30);

        cout << "\n===== 四元式输出 =====\n";
        printLinesPaged(splitLines(parser.getQuadrupleDump()), 30);

        // 保存原始四元式（优化前）
        string rawQuadPath = sourcePath + "_raw_quadruples.txt";
        {
            ofstream rqout(rawQuadPath, ios::out | ios::trunc);
            if (rqout.is_open()) {
                rqout << parser.getQuadrupleDump();
                rqout.close();
                cout << "[LOG] 原始四元式已写入: " << rawQuadPath << "\n";
            }
        }

        cout << "\n===== 四元式优化 =====\n";
        parser.setQuadruples(optimize(parser.getQuadruples()));
        printLinesPaged(splitLines(parser.getQuadrupleDump()), 30);

        string logPath = sourcePath + "_parse_log.txt";
        if (parser.writeLogToFile(logPath)) {
            cout << "\n[LOG] 详细日志已写入: " << logPath << "\n";
        }

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

        cout << "\n===== 活跃信息标记 =====\n";
        vector<BasicBlock> activeBlocks = buildBasicBlocks(parser.getQuadruples());
        auto [markedQuadruples, activeRecord] = ActiveMark(parser.getQuadruples(), activeBlocks);
        string activeDump = dumpMarkedQuadruples(markedQuadruples);
        printLinesPaged(splitLines(activeDump), 30);

        string activePath = sourcePath + "_active_mark.txt";
        {
            ofstream aout(activePath, ios::out | ios::trunc);
            if (aout.is_open()) {
                aout << "===== 活跃信息标记 =====\n";
                aout << activeDump;
                aout.close();
                cout << "[LOG] 活跃信息标记已写入: " << activePath << "\n";
            }
        }
        static_cast<void>(activeRecord);

        cout << "\n===== 目标代码生成 =====\n";
        string targetDump = generateTargetCode(parser.getQuadruples());
        printLinesPaged(splitLines(targetDump), 30);

        string targetPath = sourcePath + "_target_code.txt";
        {
            ofstream tout(targetPath, ios::out | ios::trunc);
            if (tout.is_open()) {
                tout << targetDump;
                tout.close();
                cout << "[LOG] 目标代码文件已写入: " << targetPath << "\n";
            }
        }

        cout << "\n[PASS] 前端与后端流程已连通，目标代码已生成。\n";

    }
    catch (const exception& e) {
        cerr << "Compiler error: " << e.what() << '\n';
        return 1;
    }

    sourceFile.close();

    return 0;
}
