#ifndef NEUCOMPILER_GLOBAL
#define NEUCOMPILER_GLOBAL
#include <string>

// ============================================================
// Token —— 词法分析器输出的最小语义单元
// ============================================================
//
// type 的所有可能取值：
//   "KEYWORD"   → 关键字（如 program, var, begin, end, if, then 等）
//                  此时 value 存放关键字表（keywordTable）中的下标（string 形式）
//   "ID"        → 标识符（如变量名、函数名等）
//                  此时 value 存放符号表（synbl）中的下标（string 形式）
//   "DELIMITER" → 界符/运算符（如 ; , : := + - * / ( ) 等）
//                  此时 value 存放界符表（delimiterTable）中的下标（string 形式）
//   "CONSL1"    → 整数常量（如 10, 20）
//                  此时 value 存放整常量表（consl1）中的下标（string 形式）
//   "CONSL2"    → 实数常量（如 3.14, 1.2e-3）
//                  此时 value 存放实常量表（consl2）中的下标（string 形式）
//   "STRING"    → 字符串字面量（如 'Hello', 'It''s Pascal'）
//                  此时 value 存放字符串原文（不含两端引号）
//
// line 记录该 Token 在源文件中的行号，从 1 开始计数，用于错误定位。
// ============================================================
struct Token {
    std::string type;   // 单词种别，取值见上方注释
    std::string value;  // 表内索引（KEYWORD/ID/DELIMITER/CONSL1/CONSL2）
                        // 或原文（STRING）。统一用 string 存储以便输出
    int         line;   // 所在行号（从 1 开始）
};

#endif //NEUCOMPILER_GLOBAL