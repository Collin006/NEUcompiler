#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <exception>
#include "global.h"
#include "synbl.h"

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
        // 调用词法分析器，得到 token 序列
        vector<Token> tokens = lexicalAnalyze(sourceFile);

        // 暂时打印 token，方便测试
        for (const Token& token : tokens) {
            cout << "<"
                 << token.type << ", "
                 << token.value << ", line "
                 << token.line
                 << ">" << endl;
        }
    }
    catch (const exception& e) {
        cerr << "Compiler error: " << e.what() << '\n';
        return 1;
    }

    sourceFile.close();

    return 0;
}