#ifndef NEUCOMPILER_SYNBL_H
#define NEUCOMPILER_SYNBL_H

#include <string>
#include <vector>

using namespace std;

// =====================
// 符号表的体系结构设计
// =====================

// SYNBL：符号表
// NAME : 标识符名字
// TYP  : 指针，指向类型表 TYPEL 的相应项
// CAT  : 种类编码
// ADDR : 地址/附加信息指针
//
// 说明：
// CAT 采用种类编码：
// f  : 函数
// c  : 常量
// t  : 类型
// d  : 域名，即记录字段名
// v  : 变量
// vn : 换名形参
// vf : 赋值形参
//
// ADDR 统一存储为 "(level, offset)"：
// - level：符号静态层次
// - offset：该层次内的顺序偏移（不适用时为 -1）
struct SynblItem {
    string name;    // NAME：名字
    int typ;        // TYP：指针，指向类型表 TYPEL 的相应项
    string cat;     // CAT：种类编码，如 f、c、t、d、v、vn、vf
    string addr;    // ADDR：地址信息，采用 "(level, offset)" 形式存储
};

// TYPEL：类型表
// TVAL   : 类型代码
// TPOINT : 指针
//
// 说明：
// TVAL 采用类型代码：
// i : 整型
// r : 实型
// c : 字符型
// b : 布尔型
// a : 数组型
// d : 结构型
//
// TPOINT 根据 TVAL 不同指向不同信息表项：
// 1. 基本数据类型 i、r、c、b：TPOINT = -1，表示空指针
// 2. 数组类型 a：TPOINT 指向数组表 AINFL
// 3. 结构类型 d：TPOINT 指向结构表 RINFL
struct TypelItem {
    string tval;    // TVAL：类型代码，如 i、r、c、b、a、d
    int tpoint;     // TPOINT：基本类型为 -1，数组指向 AINFL，结构指向 RINFL
};

struct PfinflItem {
    int level;      // LEVEL：该过程/函数的静态层次嵌套号
    int off;        // OFF：自身数据区起始单元相对值区区头的位置
    int fn;         // FN：形式参数个数
    int entry;      // ENTRY：目标程序入口地址，运行时填写
    int param;      // PARAM：指针，指向形参表；暂未单独建表时可为 -1
};

// LENL：长度表
// 存放相应数据类型所占值单元个数
struct LenlItem {
    int length;     // 值单元个数
};


struct AinflItem {
    int low;    // LOW：数组下界
    int up;     // UP：数组上界
    int ctp;    // CTP：成分类型指针，指向 TYPEL 中的数组元素类型
    int clen;   // CLEN：成分类型长度，即每个元素占多少值单元
};

// RINFL：结构表/记录表
// 每个域占表中一个记录
struct RinflItem {
    string id;   // ID：结构的域名
    int off;     // OFF：区距，即该域首地址相对于所在记录值区区头的位置
    int tp;      // TP：域成分类型指针，指向 TYPEL 中的信息
};

// 编译器上下文
// 统一管理编译过程中使用的各种表和状态
class CompilerContext {
public:

    // SYNBL：符号表
    vector<SynblItem> synbl;

    // TYPEL：类型表
    vector<TypelItem> typel;

    // PFINFL：过程/函数信息表
    vector<PfinflItem> pfinfl;

    // CONSL1：常量表1
    vector<int> consl1;

    // CONSL2：常量表2
    vector<double> consl2;

    // LENL：长度表
    vector<LenlItem> lenl;

    // AINFL：数组表
    vector<AinflItem> ainfl;

    // RINFL：结构/记录表
    vector<RinflItem> rinfl;

    // KEYWORD：关键字表
    vector<string> keywordTable;

    // DELIMITER：界符表
    vector<string> delimiterTable;

public:

    CompilerContext() {
        // 初始化关键字表
        keywordTable = {
            "program",
            "var",
            "const",
            "type",
            "array",
            "of",
            "record",
            "integer",
            "real",
            "char",
            "boolean",
            "string",
            "function",
            "procedure",
            "begin",
            "end",
            "if",
            "then",
            "else",
            "while",
            "do",
            "for",
            "to",
            "downto",
            "repeat",
            "until",
            "not",
            "and",
            "or",
            "div",
            "mod",
            "true",
            "false",
            "read",
            "readln",
            "write",
            "writeln"
        };

        // 初始化界符表
        delimiterTable = {
            ",",
            ":",
            ";",
            ".",
            "..",
            "(",
            ")",
            "[",
            "]",
            "+",
            "-",
            "*",
            "/",
            ":=",
            "=",
            "<",
            ">",
            "<=",
            ">=",
            "<>",
            "&&",
            "||",
            "!"
        };
    }
};

extern CompilerContext ctx;
#endif //NEUCOMPILER_SYNBL_H
