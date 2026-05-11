#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <exception>
#include "global.h"
#include "synbl.h"
#include "parser.h"

using namespace std;
// 词法分析器接口
// 读取 source，返回 token 序列
vector<Token> lexicalAnalyze(istream& source);

int main(int argc, char* argv[]) {

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

        // 输出日志
        cout << parser.getLog();

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