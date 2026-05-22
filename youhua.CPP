#include <iostream>
#include <vector>
#include <string>
#include <map>
#include <set>
#include <algorithm>
#include <sstream>

#include "youhua.h"

using namespace std;

// ==================== 优化工具函数 ====================
namespace Utils
{

    // 判断是否是常量（以数字开头）
    bool isNumber(const std::string &s)
    {
        if (s.empty())
            return false;
        if (s[0] == '-' && s.length() > 1 && isdigit(s[1]))
            return true;
        return isdigit(s[0]);
    }

    // 判断字符串是否是严格的整数下标，供跳转目标 stoi 前使用
    bool isIntegerIndex(const std::string &s)
    {
        if (s.empty())
            return false;

        size_t start = 0;
        if (s[0] == '-')
        {
            if (s.size() == 1)
                return false;
            start = 1;
        }

        for (size_t i = start; i < s.size(); i++)
        {
            if (!isdigit(static_cast<unsigned char>(s[i])))
                return false;
        }
        return true;
    }

    // 判断是否是变量（以字符开头）
    bool isVar(const std::string &s)
    {
        if (s.empty())
            return false;
        if (isNumber(s))
            return false;
        return isalpha(s[0]) || s[0] == '_';
    }

    // 计算常量表达式
    std::string compute(const std::string &op, const std::string &v1, const std::string &v2)
    {
        if (!isNumber(v1))
            return "";
        double a = stod(v1);
        double b = 0;
        if (op != "!")
        {
            if (!isNumber(v2))
                return "";
            b = stod(v2);
        }

        double r = 0;
        if (op == "+")
            r = a + b;
        else if (op == "-")
            r = a - b;
        else if (op == "*")
            r = a * b;
        else if (op == "/")
        {
            if (b == 0)
                return "";
            r = a / b;
        }
        else if (op == ">")
            r = a > b ? 1 : 0;
        else if (op == "<")
            r = a < b ? 1 : 0;
        else if (op == "=")
            r = a == b ? 1 : 0;
        else if (op == "<=")
            r = a <= b ? 1 : 0;
        else if (op == ">=")
            r = a >= b ? 1 : 0;
        else if (op == "&&")
            r = (a != 0 && b != 0) ? 1 : 0;
        else if (op == "||")
            r = (a != 0 || b != 0) ? 1 : 0;
        else if (op == "!")
            r = (a == 0) ? 1 : 0;
        else
            return "";

        return (r == (int)r) ? to_string((int)r) : to_string(r);
    }

    // 获取使用的变量
    std::set<std::string> getUse(const FourTuple &t)
    {
        std::set<std::string> s;
        if (isVar(t.first_value))
            s.insert(t.first_value);
        if (isVar(t.second_value))
            s.insert(t.second_value);
        return s;
    }
}

// ==================== 1. 常量折叠 ====================
std::vector<FourTuple> constantFolding(const std::vector<FourTuple> &input)
{
    std::vector<FourTuple> output;
    int count = 0;

    for (const auto &t : input)
    {
        std::string op = t.operator_str;

        if (op == "+" || op == "-" || op == "*" || op == "/" ||
            op == ">" || op == "<" || op == "=" ||
            op == "<=" || op == ">=" || op == "&&" || op == "||" || op == "!")
        {

            std::string r = Utils::compute(op, t.first_value, t.second_value);
            if (!r.empty())
            {
                std::cout << "常量折叠: (" << op << ", " << t.first_value
                          << ", " << t.second_value << ", " << t.dist
                          << ") -> " << t.dist << " := " << r << std::endl;
                FourTuple new_t;
                new_t.operator_str = ":=";
                new_t.first_value = r;
                new_t.second_value = "";
                new_t.dist = t.dist;
                output.push_back(new_t);
                count++;
                continue;
            }
        }

        output.push_back(t);
    }

    std::cout << "常量折叠: " << count << " 条" << std::endl;
    return output;
}

// ==================== 2. 代数简化 ====================
std::vector<FourTuple> algebraicSimplify(const std::vector<FourTuple> &input)
{
    std::vector<FourTuple> output;
    int count = 0;

    for (auto t : input)
    {
        std::string op = t.operator_str;
        bool simp = false;

        // x + 0 = x
        if (op == "+" && t.second_value == "0")
        {
            std::cout << "代数简化: x+0 -> x" << std::endl;
            t.operator_str = ":=";
            t.first_value = t.first_value;
            t.second_value = "";
            simp = true;
        }
        // 0 + x = x
        else if (op == "+" && t.first_value == "0")
        {
            std::cout << "代数简化: 0+x -> x" << std::endl;
            t.operator_str = ":=";
            t.first_value = t.second_value;
            t.second_value = "";
            simp = true;
        }
        // x - 0 = x
        else if (op == "-" && t.second_value == "0")
        {
            std::cout << "代数简化: x-0 -> x" << std::endl;
            t.operator_str = ":=";
            t.first_value = t.first_value;
            t.second_value = "";
            simp = true;
        }
        // x * 1 = x
        else if (op == "*" && t.second_value == "1")
        {
            std::cout << "代数简化: x*1 -> x" << std::endl;
            t.operator_str = ":=";
            t.first_value = t.first_value;
            t.second_value = "";
            simp = true;
        }
        // 1 * x = x
        else if (op == "*" && t.first_value == "1")
        {
            std::cout << "代数简化: 1*x -> x" << std::endl;
            t.operator_str = ":=";
            t.first_value = t.second_value;
            t.second_value = "";
            simp = true;
        }
        // x * 0 = 0
        else if (op == "*" && (t.first_value == "0" || t.second_value == "0"))
        {
            std::cout << "代数简化: x*0 -> 0" << std::endl;
            t.operator_str = ":=";
            t.first_value = "0";
            t.second_value = "";
            simp = true;
        }
        // x / 1 = x
        else if (op == "/" && t.second_value == "1")
        {
            std::cout << "代数简化: x/1 -> x" << std::endl;
            t.operator_str = ":=";
            t.first_value = t.first_value;
            t.second_value = "";
            simp = true;
        }

        if (simp)
            count++;
        output.push_back(t);
    }

    std::cout << "代数简化: " << count << " 条" << std::endl;
    return output;
}

// ==================== 3. 死代码消除 ====================
std::vector<FourTuple> deadCodeElimination(const std::vector<FourTuple> &input)
{
    int n = input.size();
    std::vector<std::set<std::string>> live(n + 1);
    std::set<std::string> cur;

    // 从后向前分析活跃变量
    for (int i = n - 1; i >= 0; i--)
    {
        live[i] = cur;

        if (Utils::isVar(input[i].dist))
        {
            cur.erase(input[i].dist);
        }

        std::set<std::string> use = Utils::getUse(input[i]);
        cur.insert(use.begin(), use.end());
    }

    // 删除无用赋值
    std::vector<FourTuple> output;
    int count = 0;

    for (int i = 0; i < n; i++)
    {
        if (input[i].operator_str == ":=" &&
            Utils::isVar(input[i].dist) &&
            live[i].find(input[i].dist) == live[i].end())
        {

            std::cout << "删除死代码: " << input[i].dist
                      << " := " << input[i].first_value << std::endl;
            count++;
            continue;
        }
        output.push_back(input[i]);
    }

    std::cout << "死代码消除: " << count << " 条" << std::endl;
    return output;
}

// ==================== 4. 公共子表达式消除 ====================
std::vector<FourTuple> commonSubexpressionElimination(const std::vector<FourTuple> &input)
{
    // 用字符串作为key
    std::map<std::string, std::string> available;
    std::vector<FourTuple> output;
    int count = 0;

    for (const auto &t : input)
    {
        std::string op = t.operator_str;

        if (op == "+" || op == "-" || op == "*" || op == "/" ||
            op == ">" || op == "<" || op == "=" || op == "<=" || op == ">=")
        {

            std::string key = op + "_" + t.first_value + "_" + t.second_value;

            if (available.find(key) != available.end())
            {
                std::cout << "公共子表达式消除: " << t.dist
                          << " 重用 " << available[key] << std::endl;
                FourTuple new_t;
                new_t.operator_str = ":=";
                new_t.first_value = available[key];
                new_t.second_value = "";
                new_t.dist = t.dist;
                output.push_back(new_t);
                count++;
                continue;
            }

            available[key] = t.dist;
        }

        // 赋值语句可能使表达式失效
        if (op == ":=" && Utils::isVar(t.dist))
        {
            std::vector<std::string> to_remove;
            for (const auto &p : available)
            {
                // 如果key中包含被赋值的变量，则失效
                if (p.first.find(t.dist) != std::string::npos)
                {
                    to_remove.push_back(p.first);
                }
            }
            for (const auto &k : to_remove)
            {
                available.erase(k);
            }
        }

        output.push_back(t);
    }

    std::cout << "公共子表达式消除: " << count << " 个" << std::endl;
    return output;
}

// ==================== 5. 基本块划分 ====================

std::vector<BasicBlock> buildBasicBlocks(const std::vector<FourTuple> &tuples)
{
    // 说明：当前四元式中存在两类“标记/跳转”信息：
    // 1. 函数入口标签：如 (lb, max_entry, , )，标签名保存在 first_value 中，不是数字下标；
    // 2. 控制流跳转目标：如 (if, _t89, , 152)、(do, _t98, , 172)、(we, , , 167)，dist 保存数字形式的四元式下标。
    // 因此不能对所有 lb.dist 或所有控制流 dist 直接 stoi，否则遇到空串或字符串标签会抛出 stoi: no conversion。

    std::set<int> leaders;
    if (tuples.empty())
        return {};

    leaders.insert(0);

    // 记录字符串标签到四元式下标的映射，供以后如果出现 goto max_entry 这类字符串跳转时使用。
    std::map<std::string, int> label2idx;
    for (int i = 0; i < static_cast<int>(tuples.size()); i++)
    {
        if (tuples[i].operator_str == "lb" && !tuples[i].first_value.empty())
        {
            label2idx[tuples[i].first_value] = i;
            leaders.insert(i); // 函数/过程入口本身也是基本块入口
        }
    }

    auto addTargetLeader = [&](const std::string &targetText)
    {
        if (targetText.empty())
            return;

        if (Utils::isIntegerIndex(targetText))
        {
            int target = std::stoi(targetText);
            if (target >= 0 && target < static_cast<int>(tuples.size()))
                leaders.insert(target);
        }
        else if (label2idx.count(targetText))
        {
            leaders.insert(label2idx[targetText]);
        }
    };

    for (int i = 0; i < static_cast<int>(tuples.size()); i++)
    {
        std::string op = tuples[i].operator_str;

        // 控制流语句的下一条语句是新的基本块入口
        if (op == "goto" || op == "if" || op == "do" || op == "we" ||
            op == "el" || op == "ie" || op == "ret")
        {
            if (i + 1 < static_cast<int>(tuples.size()))
                leaders.insert(i + 1);
        }

        // 带跳转目标的四元式，其目标位置也是基本块入口
        if (op == "goto" || op == "if" || op == "do" || op == "we" || op == "el")
        {
            addTargetLeader(tuples[i].dist);
        }
    }

    // 划分基本块
    std::vector<int> leader_list(leaders.begin(), leaders.end());
    std::sort(leader_list.begin(), leader_list.end());

    std::vector<BasicBlock> blocks;
    std::map<int, int> idx2block;

    for (int i = 0; i < static_cast<int>(leader_list.size()); i++)
    {
        BasicBlock bb;
        bb.id = i;
        int start = leader_list[i];
        int end = (i + 1 < static_cast<int>(leader_list.size()))
                      ? leader_list[i + 1] - 1
                      : static_cast<int>(tuples.size()) - 1;
        bb.start_index = start;
        bb.end_index = end;

        for (int j = start; j <= end; j++)
        {
            bb.tuples.push_back(tuples[j]);
            idx2block[j] = i;
        }
        blocks.push_back(bb);
    }

    auto connectBlocks = [&](int from, int targetIndex)
    {
        if (from < 0 || from >= static_cast<int>(blocks.size()))
            return;
        if (!idx2block.count(targetIndex))
            return;

        int to = idx2block[targetIndex];
        blocks[from].next.insert(to);
        blocks[to].prev.insert(from);
    };

    auto connectTarget = [&](int from, const std::string &targetText)
    {
        if (targetText.empty())
            return;

        if (Utils::isIntegerIndex(targetText))
        {
            int target = std::stoi(targetText);
            if (target >= 0 && target < static_cast<int>(tuples.size()))
                connectBlocks(from, target);
        }
        else if (label2idx.count(targetText))
        {
            connectBlocks(from, label2idx[targetText]);
        }
    };

    // 建立块间联系
    for (int i = 0; i < static_cast<int>(blocks.size()); i++)
    {
        if (blocks[i].tuples.empty())
            continue;

        const FourTuple &last = blocks[i].tuples.back();
        std::string last_op = last.operator_str;

        if (last_op == "goto" || last_op == "we" || last_op == "el")
        {
            // 无条件跳转：只有目标后继
            connectTarget(i, last.dist);
        }
        else if (last_op == "if" || last_op == "do")
        {
            // 条件跳转：目标后继 + 顺序后继
            connectTarget(i, last.dist);
            if (i + 1 < static_cast<int>(blocks.size()))
            {
                blocks[i].next.insert(i + 1);
                blocks[i + 1].prev.insert(i);
            }
        }
        else if (last_op == "ret")
        {
            // 返回语句没有顺序后继
            continue;
        }
        else
        {
            if (i + 1 < static_cast<int>(blocks.size()))
            {
                blocks[i].next.insert(i + 1);
                blocks[i + 1].prev.insert(i);
            }
        }
    }

    return blocks;
}

/*
优化阶段错误说明：stoi: no conversion

问题现象：
    程序进入“四元式优化”后，在基本块划分阶段抛出异常：Compiler error: stoi: no conversion。

直接原因：
    原实现把某些四元式字段直接传给 std::stoi，例如 lb.dist、wh.dist。
    但当前四元式格式中，函数入口标签类似：
        (lb, max_entry, , )
    它的标签名在 first_value 中，dist 是空字符串。
    while 起始标记类似：
        (wh, , , )
    它也没有跳转目标。
    对空字符串或 max_entry 这类非数字字符串调用 stoi，就会触发 no conversion 异常。

修复思路：
    1. 新增 Utils::isIntegerIndex，用于在 stoi 前判断字符串是否真的是整数下标；
    2. lb 不再使用 dist 建立数字标签表，而是用 first_value 建立字符串标签到四元式下标的映射；
    3. if、do、we、goto、el 等带跳转目标的四元式，只有在 dist 是合法整数或已知字符串标签时才加入基本块入口；
    4. wh 只是循环开始标记，本身没有 dist 跳转目标，不能直接 stoi；
    5. ret 没有顺序后继，避免把函数返回后的下一条错误连成同一个控制流。

当前约定：
    - (lb, label, , )：label 是字符串入口标签，保存在 first_value；
    - (if, cond, , target)：target 是条件跳转目标，通常是数字形式的四元式下标；
    - (do, cond, , target)：target 是 while 条件为假时跳出的目标下标；
    - (we, , , target)：target 是 while 回跳位置；
    - (ret, , , )：函数/过程返回，无后继基本块。
*/

// ==================== 6. 优化管理器 ====================
// 对单个基本块内部进行局部优化
std::vector<FourTuple> optimizeBasicBlock(const BasicBlock &block)
{
    std::vector<FourTuple> result = block.tuples;

    // 常量折叠
    result = constantFolding(result);

    // 代数简化
    result = algebraicSimplify(result);

    // 公共子表达式消除
    result = commonSubexpressionElimination(result);

    // 再次常量折叠（可能产生新的常量表达式）
    result = constantFolding(result);

    // 死代码消除
    result = deadCodeElimination(result);

    return result;
}

// 对所有基本块分别进行优化，并重新拼接成四元式序列
std::vector<FourTuple> optimizeByBasicBlocks(const std::vector<BasicBlock> &blocks)
{
    std::vector<FourTuple> output;

    for (const auto &block : blocks)
    {
        std::cout << "\n----- 优化基本块 B" << block.id << " -----" << std::endl;
        std::vector<FourTuple> optimizedBlock = optimizeBasicBlock(block);
        output.insert(output.end(), optimizedBlock.begin(), optimizedBlock.end());
    }

    return output;
}

std::vector<FourTuple> optimize(const std::vector<FourTuple> &input)
{
    std::cout << "\n========== 开始优化 ==========" << std::endl;
    std::cout << "原始四元式: " << input.size() << " 条" << std::endl;

    // 先划分基本块，再在每个基本块内部做局部优化
    std::vector<BasicBlock> blocks = buildBasicBlocks(input);
    std::cout << "基本块数量: " << blocks.size() << " 个" << std::endl;

    std::vector<FourTuple> result = optimizeByBasicBlocks(blocks);

    std::cout << "优化后四元式: " << result.size() << " 条" << std::endl;
    std::cout << "========== 优化完成 ==========" << std::endl;

    return result;
}

// ==================== 辅助函数：打印四元式 ====================
void printTuples(const std::vector<FourTuple> &tuples)
{
    for (size_t i = 0; i < tuples.size(); i++)
    {
        std::cout << i << ": (" << tuples[i].operator_str
                  << ", " << tuples[i].first_value
                  << ", " << tuples[i].second_value
                  << ", " << tuples[i].dist << ")" << std::endl;
    }
}

void printBlocks(const std::vector<BasicBlock> &blocks)
{
    std::cout << "\n===== 基本块结构 =====" << std::endl;
    for (const auto &bb : blocks)
    {
        std::cout << "B" << bb.id << " 前驱:{";
        for (int p : bb.prev)
            std::cout << "B" << p << " ";
        std::cout << "} 后继:{";
        for (int n : bb.next)
            std::cout << "B" << n << " ";
        std::cout << "}" << std::endl;
        for (const auto &t : bb.tuples)
        {
            std::cout << "  (" << t.operator_str << ", "
                      << t.first_value << ", " << t.second_value
                      << ", " << t.dist << ")" << std::endl;
        }
    }
}

/*
// ==================== 测试 ====================
int main()
{
    std::vector<FourTuple> test;
    FourTuple t;

    // 赋值
    t.operator_str = ":=";
    t.first_value = "10";
    t.second_value = "";
    t.dist = "a";
    test.push_back(t);

    t.operator_str = ":=";
    t.first_value = "20";
    t.second_value = "";
    t.dist = "b";
    test.push_back(t);

    // 常量表达式（可折叠）
    t.operator_str = "*";
    t.first_value = "3";
    t.second_value = "5";
    t.dist = "t1";
    test.push_back(t);

    t.operator_str = "+";
    t.first_value = "t1";
    t.second_value = "10";
    t.dist = "t2";
    test.push_back(t);

    // 无用赋值
    t.operator_str = ":=";
    t.first_value = "100";
    t.second_value = "";
    t.dist = "x";
    test.push_back(t);

    // 公共子表达式
    t.operator_str = "+";
    t.first_value = "a";
    t.second_value = "b";
    t.dist = "t3";
    test.push_back(t);

    t.operator_str = "+";
    t.first_value = "a";
    t.second_value = "b";
    t.dist = "t4";
    test.push_back(t);

    // 代数简化
    t.operator_str = "+";
    t.first_value = "t3";
    t.second_value = "0";
    t.dist = "t5";
    test.push_back(t);

    t.operator_str = "*";
    t.first_value = "t4";
    t.second_value = "1";
    t.dist = "t6";
    test.push_back(t);

    // 使用结果
    t.operator_str = ":=";
    t.first_value = "t5";
    t.second_value = "";
    t.dist = "result";
    test.push_back(t);

    std::cout << "原始四元式:" << std::endl;
    printTuples(test);

    // 划分基本块并打印基本块结构
    std::vector<BasicBlock> blocks = buildBasicBlocks(test);
    printBlocks(blocks);

    // 按基本块进行优化
    std::vector<FourTuple> optimized = optimize(test);

    std::cout << "优化后四元式:" << std::endl;
    printTuples(optimized);

    return 0;
}
*/
