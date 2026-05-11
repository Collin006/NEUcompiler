#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <exception>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#endif

#include "global.h"
#include "synbl.h"
#include "parser.h"

using namespace std;
// 词法分析器接口
// 读取 source，返回 token 序列
vector<Token> lexicalAnalyze(istream& source);

int main(int argc, char* argv[]) {

    // 控制台强制 UTF-8 输出（解决中文乱码）
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

        for (const Token& token : tokens) {
            cout << "<"
                 << token.type << ", "
                 << token.value << ", line "
                 << token.line
                 << ">" << endl;
        }

        // 2. 语法分析
        cout << "\n===== 语法分析 =====\n";
        Parser parser(tokens);
        bool parseOk = parser.parse();

        // 输出日志到控制台
        cout << parser.getLog();

        // 自动写日志文件（与源文件同目录）
        string logPath = sourcePath + "_parse_log.txt";
        if (parser.writeLogToFile(logPath)) {
            cout << "\n[LOG] 详细日志已写入: " << logPath << "\n";
        }

        if (!parseOk) {
            cerr << "\n[FAIL] 语法分析发现错误: "
                 << parser.getErrorMessage() << '\n';
            return 1;
        }

        cout << "\n[PASS] 词法和语法分析均通过。\n";

    }
    catch (const exception& e) {
        cerr << "Compiler error: " << e.what() << '\n';
        return 1;
    }

    sourceFile.close();

    return 0;
}