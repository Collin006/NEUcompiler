#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <stack>
#include <vector>
#include <unordered_map>
#include <climits>
#include "four_tuple.h"

const std::string null_value = "NO_VALUE"; // 表示空值的特殊数
const int no_position = -1; // 表示标签没有OBJ索引的特殊值

enum DistType
{   
    Router,   // 寄存器
    Memory,   // 内存
    Constant, // 立即数
    Position, // 位置地址, 用于跳转指令
    Label,    // 标签, 用于跳转指令
    None,     // 无操作数, 用于跳转指令
};

enum Operators
{
    // 存取指令
    LD = 100,
    ST = 101,

    // 跳转语句
    FJ = 200,
    TJ = 201,
    JMP = 202,

    // 运算指令
    ADD = 300,
    SUB = 301,
    MUL = 302,
    DIV = 303,

    // 逻辑运算指令
    LT = 400,
    GT = 401,
    EQ = 402,
    LE = 403,
    GE = 404,
    NE = 405,
    AND = 406,
    OR = 407,
    NOT = 408
};

/*
 * 操作数
 */
struct Operand
{
    // 操作数
    DistType type; // 操作数类型
    /*
     * 操作数的索引
     * 如果是内存空间或是寄存器，则为内存地址或寄存器名称
     * 如果是立即数,为立即数的值
     * 如果是None,则为null_value
     * 如果是标签,则为标签的名称
     * 如果是位置地址,则为位置地址的OBJ索引
     */     
    std::string value;
};

/*
 * 标签
 */
struct Lable
{
    int position; // 标签的OBJ索引
    std::stack<int> backpatch; // 记录了所有引用了Lable,但尚未回填的目标代码索引
};

// 操作指令,包含操作指令和两个操作数
struct AimCodeToken
{
    int index; // 操作指令的索引
    std::string operator_str; // 操作语句
    Operand first_value;      // 第一操作数
    Operand second_value;     // 第二操作数
};

// 目标代码生成器
class CodeBuilder
{
protected:
    std::stack<int> SEM;           // 分支语义分析栈
    std::stack<int> LoopSEM;       // 循环语义分析栈
    std::unordered_map<std::string, Lable> Lables; // 标签表,存储所有标签的OBJ索引和回填栈
    std::string RDL;               // 寄存器描述表,存储当前操作数的名称
    std::vector<MarkedFourTuple> QT;     // 四元式区
    std::vector<AimCodeToken> OBJ; // 操作指令区
    int code_index; // 操作指令区的索引,用于生成目标代码
    std::vector<std::unordered_map<std::string, int>> ActiveRecord; // 活跃记录表,存储第i条四元式前活跃的变量

public:
    void Init(
        const std::vector<MarkedFourTuple>& qt,
        const std::vector<std::unordered_map<std::string, int>>& active_record
    );
    void BuildTokens();

private:
    Operators ScanOperator(const std::string& operator_str); // 扫描操作符
    std::string GetOperateCommand(Operators op); // 根据操作符生成相应的操作指令

    // 二元运算构建函数
    void BuildTwoOperandsToken(const int& index, const MarkedFourTuple& ft); // 构建二元运算的目标代码

    // 分支语句构建函数
    void BuildIfToken(const int& index, const MarkedFourTuple& ft); // 构建if语句的目标代码
    void BuildElseToken(const int& index, const MarkedFourTuple& ft); // 构建else语句的目标代码
    void BuildIfEndToken(const int& index, const MarkedFourTuple& ft); // 构建if语句结束的目标代码

    // 循环语句的构建函数
    void BuildWhileToken(const int& index, const MarkedFourTuple& ft); // 构建while语句的目标代码
    void BuildDoToken(const int& index, const MarkedFourTuple& ft); // 构建do语句的目标代码
    void BuildWhileEndToken(const int& index, const MarkedFourTuple& ft); // 构建while语句结束的目标代码

    // goto语句构建函数
    void BuildGotoToken(const int& index, const MarkedFourTuple& ft); // 构建goto语句的目标代码

    // Label语句构建函数
    void BuildLabelToken(const int& index, const MarkedFourTuple& ft); // 构建Label语句的目标代码
};

std::string generateTargetCode(const std::vector<FourTuple>& quadruples);
