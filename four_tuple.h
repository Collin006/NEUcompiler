#pragma once
#include <string>
#include <climits>

const int is_constant = INT_MIN; // 表示立即数的特殊数

struct FourTuple
{
    std::string operator_str; // 操作符
    std::string first_value; // 第一操作数
    std::string second_value; // 第二操作数
    std::string dist; // 目标操作数
};

struct MarkedValue
{
    std::string value; // 变量名或立即数的值
    int active; // 是否活跃,如果是立即数，则为特殊值is_constant
};

// 标记了活跃信息的四元式
struct MarkedFourTuple
{
    std::string operator_str; // 操作符
    MarkedValue first_value; // 第一操作数
    MarkedValue second_value; // 第二操作数
    MarkedValue dist; // 目标操作数
};
