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

bool isIntegerString(const string& s) {
    if (s.empty()) return false;
    size_t i = (s[0] == '-' ? 1 : 0);
    if (i >= s.size()) return false;
    for (; i < s.size(); ++i) {
        if (s[i] < '0' || s[i] > '9') return false;
    }
    return true;
}

string valueOrPlaceholder(const string& value) {
    return value.empty() ? "_" : value;
}

string formatMarkedValue(const MarkedValue& value) {
    string text = value.value.empty() ? "_" : value.value;
    if (value.active == is_constant) {
        return text + "/C";
    }
    return text + (value.active ? "/1" : "/0");
}

string dumpMarkedQuadruples(const vector<MarkedFourTuple>& marked) {
    ostringstream out;
    for (size_t i = 0; i < marked.size(); ++i) {
        const auto& mft = marked[i];
        out << i << ": (" << mft.operator_str << ", "
            << formatMarkedValue(mft.first_value) << ", "
            << formatMarkedValue(mft.second_value) << ", "
            << formatMarkedValue(mft.dist) << ")\n";
    }
    return out.str();
}

string generateTargetCode(const vector<FourTuple>& quadruples) {
    unordered_set<int> jumpTargets;
    jumpTargets.reserve(quadruples.size());

    for (const FourTuple& q : quadruples) {
        if ((q.operator_str == "if" || q.operator_str == "do" ||
             q.operator_str == "el" || q.operator_str == "goto" ||
             q.operator_str == "we") &&
            isIntegerString(q.dist)) {
            jumpTargets.insert(stoi(q.dist));
        }
    }

    auto jumpLabel = [](int index) { return "L" + to_string(index); };
    auto emitBinary = [](ostringstream& out, const string& op, const FourTuple& q) {
        out << "  LD R, " << valueOrPlaceholder(q.first_value) << '\n';
        out << "  " << op << " R, " << valueOrPlaceholder(q.second_value) << '\n';
        out << "  ST R, " << valueOrPlaceholder(q.dist) << '\n';
    };

    ostringstream out;
    out << "===== 目标代码 =====\n";
    for (size_t i = 0; i < quadruples.size(); ++i) {
        if (jumpTargets.count(static_cast<int>(i)) != 0) {
            out << jumpLabel(static_cast<int>(i)) << ":\n";
        }

        const FourTuple& q = quadruples[i];
        if (q.operator_str == "+") emitBinary(out, "ADD", q);
        else if (q.operator_str == "-") emitBinary(out, "SUB", q);
        else if (q.operator_str == "*") emitBinary(out, "MUL", q);
        else if (q.operator_str == "/") emitBinary(out, "DIV", q);
        else if (q.operator_str == "<") emitBinary(out, "LT", q);
        else if (q.operator_str == ">") emitBinary(out, "GT", q);
        else if (q.operator_str == "<=") emitBinary(out, "LE", q);
        else if (q.operator_str == ">=") emitBinary(out, "GE", q);
        else if (q.operator_str == "=") emitBinary(out, "EQ", q);
        else if (q.operator_str == "<>") emitBinary(out, "NE", q);
        else if (q.operator_str == "&&") emitBinary(out, "AND", q);
        else if (q.operator_str == "||") emitBinary(out, "OR", q);
        else if (q.operator_str == "!") {
            out << "  LD R, " << valueOrPlaceholder(q.first_value) << '\n';
            out << "  NOT R, _\n";
            out << "  ST R, " << valueOrPlaceholder(q.dist) << '\n';
        } else if (q.operator_str == ":=") {
            out << "  LD R, " << valueOrPlaceholder(q.first_value) << '\n';
            out << "  ST R, " << valueOrPlaceholder(q.dist) << '\n';
        } else if (q.operator_str == "if" || q.operator_str == "do") {
            out << "  LD R, " << valueOrPlaceholder(q.first_value) << '\n';
            if (isIntegerString(q.dist)) out << "  FJ R, " << jumpLabel(stoi(q.dist)) << '\n';
            else out << "  FJ R, " << valueOrPlaceholder(q.dist) << '\n';
        } else if (q.operator_str == "el" || q.operator_str == "goto" || q.operator_str == "we") {
            if (isIntegerString(q.dist)) out << "  JMP _, " << jumpLabel(stoi(q.dist)) << '\n';
            else out << "  JMP _, " << valueOrPlaceholder(q.dist) << '\n';
        } else if (q.operator_str == "wh" || q.operator_str == "ie") {
            out << "  NOP\n";
        } else if (q.operator_str == "lb") {
            out << valueOrPlaceholder(q.first_value) << ":\n";
        } else {
            out << "  ; unsupported quadruple: (" << q.operator_str << ", "
                << q.first_value << ", " << q.second_value << ", " << q.dist << ")\n";
        }
    }
    return out.str();
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
