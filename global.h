#ifndef NEUCOMPILER_GLOBAL
#define NEUCOMPILER_GLOBAL
#include <string>
// Token 类型
struct Token {
    std::string type;   // 单词种别，例如 "ID", "NUMBER", "KEYWORD"
    std::string value;  // 单词原文，例如 "main", "123"
    int line;      // 所在行号
};
#endif //NEUCOMPILER_GLOBAL