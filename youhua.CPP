/**
 * @file youhua.cpp
 * @brief 四元式优化器
 *
 * 对语法分析生成的四元式序列进行优化，包括：
 *   1. 常量折叠 —— 编译时计算常量表达式
 *   2. 代数简化 —— 利用代数恒等式消除冗余运算
 *   3. 公共子表达式消除 —— 避免重复计算相同表达式
 *   4. 基本块划分 —— 按控制流拆分代码，建立控制流图
 *   5. 全局死代码消除 —— 基于活跃变量分析删除无用赋值
 *   6. 跳转目标重映射 —— 优化后重新计算跳转地址
 *
 * 优化流水线：
 *   原始四元式 → 基本块划分 → 块内局部优化 → 全局死代码消除 → 跳转目标重映射 → 优化后四元式
 */

#include <iostream>
#include <vector>
#include <string>
#include <map>
#include <set>
#include <algorithm>
#include <sstream>
#include <cassert>

#include "youhua.h"

using namespace std;

// ===================================================================
// 优化工具函数（Utils 命名空间）
// 提供常量判断、变量判断、常量计算、变量使用收集等基础功能
// ===================================================================
namespace Utils
{
    /**
     * @brief 判断字符串是否是数值常量（整数或实数）
     * @param s 待判断的字符串
     * @return true 如果字符串表示一个数值（允许前导负号）
     *
     * 例如："10" → true, "-5" → true, "3.14" → true(isdigit判断通过),
     *       "abc" → false, "" → false
     */
    bool isNumber(const std::string &s)
    {
        if (s.empty())
            return false;
        // 处理负数：以 '-' 开头，且后面至少有一位数字
        if (s[0] == '-' && s.length() > 1 && isdigit(s[1]))
            return true;
        // 正数或以数字开头
        return isdigit(s[0]);
    }

    /**
     * @brief 判断字符串是否是纯整数索引（用于跳转目标的 stoi 前安全检查）
     * @param s 待判断的字符串
     * @return true 如果字符串是纯数字（允许负号），可以安全地调用 stoi
     *
     * 与 isNumber 的区别：isNumber 对 "3.14" 也返回 true，
     * 而 isIntegerIndex 只对纯整数返回 true，因为跳转目标必须是整数下标。
     */
    bool isIntegerIndex(const std::string &s)
    {
        if (s.empty())
            return false;

        size_t start = 0;
        // 处理负号
        if (s[0] == '-')
        {
            if (s.size() == 1)
                return false;   // 单独的 '-' 不是合法整数
            start = 1;
        }

        // 每个字符都必须是数字
        for (size_t i = start; i < s.size(); i++)
        {
            if (!isdigit(static_cast<unsigned char>(s[i])))
                return false;
        }
        return true;
    }

    /**
     * @brief 判断字符串是否是一个变量名
     * @param s 待判断的字符串
     * @return true 如果是变量（以字母或下划线开头，且不是纯数字）
     *
     * 变量命名约定：以字母或 '_' 开头，如 a, _t1, _result$max 等
     */
    bool isVar(const std::string &s)
    {
        if (s.empty())
            return false;
        if (isNumber(s))      // 纯数字不是变量
            return false;
        return isalpha(s[0]) || s[0] == '_';
    }

    /**
     * @brief 计算常量表达式的结果
     * @param op  运算符（+, -, *, /, >, <, =, <=, >=, &&, ||, !）
     * @param v1  第一操作数（必须是数字字符串）
     * @param v2  第二操作数（单目运算符 ! 时可忽略）
     * @return 计算结果（字符串形式），如果无法计算则返回空字符串
     *
     * 支持整数运算、关系运算和逻辑运算。
     * 如果结果是整数（如 4.0），则去掉小数点后的零，返回 "4"。
     */
    std::string compute(const std::string &op, const std::string &v1, const std::string &v2)
    {
        // 第一操作数必须是数字
        if (!isNumber(v1))
            return "";
        double a = stod(v1);

        double b = 0;
        if (op != "!")   // 逻辑非只需要一个操作数
        {
            if (!isNumber(v2))
                return "";
            b = stod(v2);
        }

        double r = 0;

        // 算术运算
        if (op == "+")
            r = a + b;
        else if (op == "-")
            r = a - b;
        else if (op == "*")
            r = a * b;
        else if (op == "/")
        {
            if (b == 0)        // 除零保护
                return "";
            r = a / b;
        }
        // 关系运算：结果用 1（真）和 0（假）表示
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
        // 逻辑运算
        else if (op == "&&")
            r = (a != 0 && b != 0) ? 1 : 0;
        else if (op == "||")
            r = (a != 0 || b != 0) ? 1 : 0;
        else if (op == "!")
            r = (a == 0) ? 1 : 0;
        else
            return "";

        // 如果是整数（如 4.0），返回整数字符串 "4"，否则返回完整的浮点数字符串
        return (r == (int)r) ? to_string((int)r) : to_string(r);
    }

    /**
     * @brief 获取四元式中使用的变量集合
     * @param t 四元式
     * @return 变量名集合（first_value 和 second_value 中的变量）
     *
     * 用于活跃变量分析：收集该指令读取了哪些变量。
     * 注意：dist（目标操作数）是被定义的变量，不是被使用的，所以不收集。
     */
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

// ===================================================================
// 优化 Pass 1：常量折叠
// ===================================================================

/**
 * @brief 常量折叠优化
 *
 * 扫描所有四元式，将编译时能计算出结果的表达式替换为直接赋值。
 *
 * 例如：
 *   优化前：(+, 3, 5, _t1)     ← 3 + 5 在编译时就能算出来
 *   优化后：(:=, 8, , _t1)     ← 直接赋值 8
 *
 * @param input 原始四元式序列
 * @return 常量折叠后的四元式序列
 */
std::vector<FourTuple> constantFolding(const std::vector<FourTuple> &input)
{
    std::vector<FourTuple> output;
    int count = 0;   // 统计折叠了几条

    for (const auto &t : input)
    {
        std::string op = t.operator_str;

        // 只处理可能产生常量结果的运算符
        if (op == "+" || op == "-" || op == "*" || op == "/" ||
            op == ">" || op == "<" || op == "=" ||
            op == "<=" || op == ">=" || op == "&&" || op == "||" || op == "!")
        {
            // 尝试计算常量结果
            std::string r = Utils::compute(op, t.first_value, t.second_value);
            if (!r.empty())   // 计算成功
            {
                std::cout << "常量折叠: (" << op << ", " << t.first_value
                          << ", " << t.second_value << ", " << t.dist
                          << ") -> " << t.dist << " := " << r << std::endl;

                // 生成新的赋值四元式，替换原来的运算
                FourTuple new_t;
                new_t.operator_str = ":=";
                new_t.first_value = r;          // 常量值
                new_t.second_value = "";
                new_t.dist = t.dist;            // 保持原来的目标变量
                output.push_back(new_t);
                count++;
                continue;   // 跳过原来的运算四元式
            }
        }

        // 无法折叠，原样保留
        output.push_back(t);
    }

    std::cout << "常量折叠: " << count << " 条" << std::endl;
    return output;
}

// ===================================================================
// 优化 Pass 2：代数简化
// ===================================================================

/**
 * @brief 代数简化优化
 *
 * 利用代数恒等式消除冗余运算，将运算指令替换为赋值指令。
 *
 * 支持的简化规则：
 *   x + 0 → x        0 + x → x
 *   x - 0 → x
 *   x * 1 → x        1 * x → x
 *   x * 0 → 0
 *   x / 1 → x
 *
 * @param input 原始四元式序列
 * @return 代数简化后的四元式序列
 */
std::vector<FourTuple> algebraicSimplify(const std::vector<FourTuple> &input)
{
    std::vector<FourTuple> output;
    int count = 0;

    for (auto t : input)   // 按值传递，允许修改
    {
        std::string op = t.operator_str;
        bool simp = false;   // 标记是否发生了简化

        // 规则 1：x + 0 = x
        if (op == "+" && t.second_value == "0")
        {
            std::cout << "代数简化: x+0 -> x" << std::endl;
            t.operator_str = ":=";
            t.first_value = t.first_value;   // 保持原来的第一操作数
            t.second_value = "";
            simp = true;
        }
        // 规则 2：0 + x = x
        else if (op == "+" && t.first_value == "0")
        {
            std::cout << "代数简化: 0+x -> x" << std::endl;
            t.operator_str = ":=";
            t.first_value = t.second_value;   // 结果就是第二操作数
            t.second_value = "";
            simp = true;
        }
        // 规则 3：x - 0 = x
        else if (op == "-" && t.second_value == "0")
        {
            std::cout << "代数简化: x-0 -> x" << std::endl;
            t.operator_str = ":=";
            t.first_value = t.first_value;
            t.second_value = "";
            simp = true;
        }
        // 规则 4：x * 1 = x
        else if (op == "*" && t.second_value == "1")
        {
            std::cout << "代数简化: x*1 -> x" << std::endl;
            t.operator_str = ":=";
            t.first_value = t.first_value;
            t.second_value = "";
            simp = true;
        }
        // 规则 5：1 * x = x
        else if (op == "*" && t.first_value == "1")
        {
            std::cout << "代数简化: 1*x -> x" << std::endl;
            t.operator_str = ":=";
            t.first_value = t.second_value;
            t.second_value = "";
            simp = true;
        }
        // 规则 6：x * 0 = 0（或 0 * x = 0）
        else if (op == "*" && (t.first_value == "0" || t.second_value == "0"))
        {
            std::cout << "代数简化: x*0 -> 0" << std::endl;
            t.operator_str = ":=";
            t.first_value = "0";
            t.second_value = "";
            simp = true;
        }
        // 规则 7：x / 1 = x
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

// ===================================================================
// 优化 Pass 3：公共子表达式消除
// ===================================================================

/**
 * @brief 公共子表达式消除（CSE）
 *
 * 在同一个基本块内，如果同一个表达式计算了多次，第二次及以后直接用第一次的结果。
 *
 * 例如：
 *   优化前：(+, a, b, _t3)     ← 第一次算 a+b
 *           ...
 *           (+, a, b, _t4)     ← 又算了一遍 a+b
 *   优化后：(+, a, b, _t3)
 *           ...
 *           (:=, _t3, , _t4)   ← 直接复用 _t3 的值
 *
 * 注意：如果中间对 a 或 b 进行了赋值，则之前 a+b 的结果失效。
 *
 * @param input 原始四元式序列
 * @return CSE 后的四元式序列
 */
std::vector<FourTuple> commonSubexpressionElimination(const std::vector<FourTuple> &input)
{
    // available: key = "op_firstvalue_secondvalue" → 结果变量名
    // 例如：key = "+_a_b" → value = "_t3"
    std::map<std::string, std::string> available;
    std::vector<FourTuple> output;
    int count = 0;

    for (const auto &t : input)
    {
        std::string op = t.operator_str;

        // 只处理二元运算符（算术和关系运算）
        if (op == "+" || op == "-" || op == "*" || op == "/" ||
            op == ">" || op == "<" || op == "=" || op == "<=" || op == ">=")
        {
            // 构造唯一标识这个表达式的 key
            std::string key = op + "_" + t.first_value + "_" + t.second_value;

            // 如果这个表达式之前已经计算过，直接复用
            if (available.find(key) != available.end())
            {
                std::cout << "公共子表达式消除: " << t.dist
                          << " 重用 " << available[key] << std::endl;

                // 生成赋值指令，用之前的结果
                FourTuple new_t;
                new_t.operator_str = ":=";
                new_t.first_value = available[key];   // 引用之前的结果变量
                new_t.second_value = "";
                new_t.dist = t.dist;
                output.push_back(new_t);
                count++;
                continue;   // 跳过原来的运算四元式
            }

            // 第一次遇到这个表达式，记录它的结果变量
            available[key] = t.dist;
        }

        // 处理赋值语句：如果对某个变量赋值了，包含该变量的所有表达式失效
        // 例如：a := xxx 之后，所有包含 a 的 key（如 "+_a_b"）都失效
        if (op == ":=" && Utils::isVar(t.dist))
        {
            std::vector<std::string> to_remove;
            for (const auto &p : available)
            {
                // 检查 key 中是否包含被赋值的变量名
                if (p.first.find(t.dist) != std::string::npos)
                {
                    to_remove.push_back(p.first);
                }
            }
            // 删除失效的表达式记录
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

// ===================================================================
// 优化 Pass 4：基本块划分（为后续优化做准备）
// ===================================================================

/**
 * @brief 将四元式序列划分为基本块，并建立控制流图
 *
 * 基本块是单入口、单出口的直线代码段。
 *
 * 划分原则：
 *   基本块的入口（领导指令）是：
 *     1. 第一条指令
 *     2. 跳转目标（if/do/we/el/goto 的跳转目标）
 *     3. 跳转指令的下一条
 *     4. 函数入口标签（lb）
 *
 * 同时建立块之间的前驱（prev）和后继（next）关系，形成控制流图。
 *
 * @param tuples 四元式序列
 * @return 基本块列表，包含控制流边信息
 */
std::vector<BasicBlock> buildBasicBlocks(const std::vector<FourTuple> &tuples)
{
    std::set<int> leaders;   // 存储所有基本块入口的索引
    if (tuples.empty())
        return {};

    // 规则1：第一条指令是基本块入口
    leaders.insert(0);

    // 建立标签名到四元式索引的映射
    // 例如：max_entry → 0（函数入口）
    std::map<std::string, int> label2idx;
    for (int i = 0; i < static_cast<int>(tuples.size()); i++)
    {
        if (tuples[i].operator_str == "lb" && !tuples[i].first_value.empty())
        {
            label2idx[tuples[i].first_value] = i;
            leaders.insert(i);   // 规则4：函数入口也是基本块入口
        }
    }

    // Lambda：将跳转目标添加到 leaders
    // 支持数字下标（如 "152"）和字符串标签（如 "max_entry"）
    auto addTargetLeader = [&](const std::string &targetText)
    {
        if (targetText.empty())
            return;

        // 如果是数字下标，直接转换
        if (Utils::isIntegerIndex(targetText))
        {
            int target = std::stoi(targetText);
            if (target >= 0 && target < static_cast<int>(tuples.size()))
                leaders.insert(target);
        }
        // 如果是字符串标签，查找映射
        else if (label2idx.count(targetText))
        {
            leaders.insert(label2idx[targetText]);
        }
    };

    // 扫描所有四元式，找出所有基本块入口
    for (int i = 0; i < static_cast<int>(tuples.size()); i++)
    {
        std::string op = tuples[i].operator_str;

        // 规则3：跳转指令的下一条是基本块入口
        if (op == "goto" || op == "if" || op == "do" || op == "we" ||
            op == "el" || op == "ie" || op == "ret")
        {
            if (i + 1 < static_cast<int>(tuples.size()))
                leaders.insert(i + 1);
        }

        // 规则2：跳转目标是基本块入口
        if (op == "goto" || op == "if" || op == "do" || op == "we" || op == "el")
        {
            addTargetLeader(tuples[i].dist);
        }
    }

    // 将 leader 集合排序，相邻 leader 之间就是一个基本块
    std::vector<int> leader_list(leaders.begin(), leaders.end());
    std::sort(leader_list.begin(), leader_list.end());

    std::vector<BasicBlock> blocks;
    std::map<int, int> idx2block;   // 四元式索引 → 基本块编号的映射

    // 划分基本块
    for (int i = 0; i < static_cast<int>(leader_list.size()); i++)
    {
        BasicBlock bb;
        bb.id = i;
        int start = leader_list[i];
        // 基本块的范围：从当前 leader 到下一个 leader 的前一条指令
        int end = (i + 1 < static_cast<int>(leader_list.size()))
                      ? leader_list[i + 1] - 1
                      : static_cast<int>(tuples.size()) - 1;
        bb.start_index = start;
        bb.end_index = end;

        // 将这个范围内的指令加入基本块
        for (int j = start; j <= end; j++)
        {
            bb.tuples.push_back(tuples[j]);
            idx2block[j] = i;   // 记录每条指令属于哪个基本块
        }
        blocks.push_back(bb);
    }

    // Lambda：连接两个基本块（from 的后继加上 to，to 的前驱加上 from）
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

    // Lambda：根据跳转目标字符串连接基本块
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

    // 建立控制流边
    for (int i = 0; i < static_cast<int>(blocks.size()); i++)
    {
        if (blocks[i].tuples.empty())
            continue;

        // 取基本块的最后一条指令，决定后继
        const FourTuple &last = blocks[i].tuples.back();
        std::string last_op = last.operator_str;

        if (last_op == "goto" || last_op == "we" || last_op == "el")
        {
            // 无条件跳转：只有目标一个后继
            connectTarget(i, last.dist);
        }
        else if (last_op == "if" || last_op == "do")
        {
            // 条件跳转：有两个后继 —— 跳转目标 和 顺序下一条
            connectTarget(i, last.dist);
            if (i + 1 < static_cast<int>(blocks.size()))
            {
                blocks[i].next.insert(i + 1);
                blocks[i + 1].prev.insert(i);
            }
        }
        else if (last_op == "ret")
        {
            // 返回指令：没有后继（控制流在此结束）
            continue;
        }
        else
        {
            // 普通指令：顺序执行到下一个基本块
            if (i + 1 < static_cast<int>(blocks.size()))
            {
                blocks[i].next.insert(i + 1);
                blocks[i + 1].prev.insert(i);
            }
        }
    }

    return blocks;
}

// ===================================================================
// 优化 Pass 5：全局死代码消除（增强版）
// ===================================================================

/**
 * @brief 全局死代码消除
 *
 * 通过后向活跃变量分析，找出后续不再被读取的赋值指令并删除。
 *
 * 核心算法：
 *   从最后一条指令向前扫描，维护一个"活跃变量集合"。
 *   - 遇到使用变量 → 加入活跃集
 *   - 遇到定义变量 → 从活跃集移除
 *   - 如果一条 := 的目标变量在当前位置不在活跃集中 → 可以删除（死代码）
 *
 * 特殊保护：
 *   - call 的实参变量（_p0$func 等）被标记为"隐式使用"
 *   - ret 的出口变量（_result$func 等）被标记为"必须活跃"
 *   - 所有被 funcExitUses 和 callParamUses 标记的变量都不会被删除
 *
 * @param input         局部优化后的四元式序列
 * @param funcExitUses  每个 ret 对应的出口活跃变量集
 * @param callParamUses 每个 call 对应的参数变量集
 * @return 死代码消除后的四元式序列
 */
static std::vector<FourTuple>
globalDeadCodeElimination(const std::vector<FourTuple> &input,
                          const std::map<int, std::set<std::string>> &funcExitUses,
                          const std::map<int, std::set<std::string>> &callParamUses)
{
    int n = input.size();
    // live[i] 表示执行到第 i 条指令之前，哪些变量是活跃的
    std::vector<std::set<std::string>> live(n + 1);
    std::set<std::string> cur;   // 当前活跃变量集合（从后向前累积）

    // ========== 第一遍：后向扫描，计算每个位置的活跃变量集合 ==========
    for (int i = n - 1; i >= 0; i--)
    {
        live[i] = cur;   // 记录当前位置的活跃集

        // 特殊处理 call：call 会隐式使用实参变量
        // 例如 call max_entry 之前有 _p0$max := a，这个 _p0$max 必须被视为"被使用"
        if (input[i].operator_str == "call")
        {
            auto it = callParamUses.find(i);
            if (it != callParamUses.end())
                cur.insert(it->second.begin(), it->second.end());
        }

        // 特殊处理 ret：函数出口处，返回值等变量必须活跃
        // 例如 max 函数中的 _result$max，在 ret 时必须是活跃的
        if (input[i].operator_str == "ret")
        {
            auto it = funcExitUses.find(i);
            if (it != funcExitUses.end())
                cur.insert(it->second.begin(), it->second.end());
        }

        // 处理当前指令的定义（dist 是被写入的变量，不再"活跃"）
        if (Utils::isVar(input[i].dist))
            cur.erase(input[i].dist);

        // 处理当前指令的使用（first_value 和 second_value 是被读取的变量，变为活跃）
        std::set<std::string> use = Utils::getUse(input[i]);
        cur.insert(use.begin(), use.end());
    }

    // ========== 第二遍：正向扫描，删除死代码 ==========
    std::vector<FourTuple> output;
    int count = 0;
    for (int i = 0; i < n; i++)
    {
        // 判断条件：是赋值指令，目标是变量，且目标变量在当前位置不活跃
        if (input[i].operator_str == ":=" &&
            Utils::isVar(input[i].dist) &&
            live[i].find(input[i].dist) == live[i].end())
        {
            // ★★★ 安全保护：检查这个变量是否被 funcExitUses 或 callParamUses 标记 ★★★
            // 如果是，说明它可能跨函数使用，不能删除
            bool isExitVar = false;
            for (const auto &p : funcExitUses) {
                if (p.second.count(input[i].dist)) {
                    isExitVar = true;
                    break;
                }
            }
            if (!isExitVar) {
                for (const auto &p : callParamUses) {
                    if (p.second.count(input[i].dist)) {
                        isExitVar = true;
                        break;
                    }
                }
            }

            // 只有确定不是出口/参数变量时，才真正删除
            if (!isExitVar) {
                std::cout << "删除死代码: " << input[i].dist
                          << " := " << input[i].first_value << std::endl;
                count++;
                continue;   // 跳过这条指令（即删除）
            }
        }
        output.push_back(input[i]);
    }
    std::cout << "死代码消除: " << count << " 条" << std::endl;
    return output;
}

// ===================================================================
// 辅助函数：收集函数出口活跃变量
// ===================================================================

/**
 * @brief 收集每个 ret 指令对应的出口活跃变量
 *
 * 算法：
 *   1. 识别所有函数（lb...ret 区间）
 *   2. 收集每个函数体内所有被赋值的变量
 *   3. 为每个 ret 关联其所属函数的所有被赋值变量
 *
 * 这是保守策略：把函数内所有被赋值过的变量都视为出口活跃。
 * 虽然可能保留一些真正无用的变量，但绝对安全，不会误删。
 *
 * @param tuples 四元式序列
 * @return 映射表：ret的索引 → 出口活跃变量集
 */
static std::map<int, std::set<std::string>>
buildFuncExitUses(const std::vector<FourTuple> &tuples)
{
    std::map<int, std::set<std::string>> ret2uses;

    // 函数信息：起始位置 + 内部所有被赋值变量
    struct FuncInfo {
        int start;                        // lb 所在的索引
        std::set<std::string> defs;       // 函数内所有被赋值的变量
    };

    std::vector<FuncInfo> funcs;
    int n = tuples.size();

    // 第一遍：识别函数并收集定义
    for (int i = 0; i < n; i++)
    {
        if (tuples[i].operator_str == "lb")
        {
            // 遇到函数入口标签，开始一个新函数
            FuncInfo f;
            f.start = i;
            funcs.push_back(f);
        }
        else if (!funcs.empty() && tuples[i].operator_str != "lb")
        {
            // 在函数体内，收集所有被赋值的变量
            if (tuples[i].operator_str == ":=" && Utils::isVar(tuples[i].dist))
                funcs.back().defs.insert(tuples[i].dist);
        }
    }

    // 第二遍：为每个 ret 找到所属函数，赋予其出口变量集
    for (int i = 0; i < n; i++)
    {
        if (tuples[i].operator_str == "ret")
        {
            // 从后向前找最近的 lb（即这个 ret 所在的函数）
            int funcIdx = -1;
            for (int k = funcs.size() - 1; k >= 0; k--)
            {
                if (funcs[k].start < i)
                {
                    funcIdx = k;
                    break;
                }
            }
            if (funcIdx != -1)
                ret2uses[i] = funcs[funcIdx].defs;   // 该函数内所有被赋值变量都视为出口活跃
        }
    }
    return ret2uses;
}

// ===================================================================
// 辅助函数：收集 call 使用的参数变量
// ===================================================================

/**
 * @brief 收集每个 call 指令对应的参数变量
 *
 * 根据编译器的命名约定：
 *   实参临时变量名格式为：_p{序号}$作用域$函数名
 *   例如：_p0$max, _p1$max
 *
 * 算法：对每个 call，向前扫描找到所有 _pN$funcName 格式的赋值目标变量。
 *
 * @param tuples 四元式序列
 * @return 映射表：call的索引 → 参数变量集
 */
static std::map<int, std::set<std::string>>
buildCallParamUses(const std::vector<FourTuple> &tuples)
{
    std::map<int, std::set<std::string>> call2uses;

    for (int i = 0; i < (int)tuples.size(); i++)
    {
        if (tuples[i].operator_str == "call")
        {
            // 获取调用的函数名（call 的 first_value 是入口标签，如 max_entry）
            std::string funcName = tuples[i].first_value;

            // 去掉 _entry 后缀，得到函数基础名（如 max）
            std::string funcBase;
            size_t pos = funcName.rfind("_entry");
            if (pos != std::string::npos)
                funcBase = funcName.substr(0, pos);
            else
                funcBase = funcName;

            // 向前扫描，收集所有 _pN$funcBase 格式的参数变量
            std::set<std::string> usedParams;
            for (int j = i - 1; j >= 0; j--)
            {
                // 遇到另一个 call、lb 或 ret 就停止（参数不会跨越这些边界）
                if (tuples[j].operator_str == "call" ||
                    tuples[j].operator_str == "lb" ||
                    tuples[j].operator_str == "ret")
                    break;

                if (tuples[j].operator_str == ":=")
                {
                    const std::string &dest = tuples[j].dist;
                    // 检查是否是 _p{数字}$funcBase 格式
                    // dest.find("_p") == 0 确保以 _p 开头
                    // dest.find("$" + funcBase) != npos 确保包含 $funcName
                    if (dest.find("_p") == 0 && dest.find("$" + funcBase) != std::string::npos)
                        usedParams.insert(dest);
                }
            }

            if (!usedParams.empty())
                call2uses[i] = usedParams;
        }
    }
    return call2uses;
}

// ===================================================================
// 优化 Pass 6：跳转目标重映射
// ===================================================================

/**
 * @brief 跳转目标重映射
 *
 * 优化后指令序列变短，索引发生变化。
 * 原来 if/do/we/el/goto 的跳转目标（dist 字段）还是基于旧索引的。
 * 本函数根据旧索引→新索引的映射，更新所有跳转目标。
 *
 * @param input   优化后的四元式序列
 * @param origIdx 每条指令对应的原始索引（-1 表示新生成的指令）
 * @return 跳转目标更新后的四元式序列
 */
static std::vector<FourTuple>
remapJumpTargets(const std::vector<FourTuple> &input,
                 const std::vector<int> &origIdx)
{
    // 建立 旧索引 → 新索引 的映射
    // 只映射 origIdx >= 0 的（即保留了原始索引的指令）
    std::map<int, int> old2new;
    for (int i = 0; i < (int)input.size(); i++)
    {
        if (origIdx[i] >= 0)
            old2new[origIdx[i]] = i;
    }

    std::vector<FourTuple> output = input;

    // 遍历所有指令，更新跳转目标
    for (auto &t : output)
    {
        std::string op = t.operator_str;

        // 只处理带跳转目标的指令
        if (op == "if" || op == "do" || op == "we" || op == "el" || op == "goto")
        {
            // 确保跳转目标是数字索引
            if (!t.dist.empty() && Utils::isIntegerIndex(t.dist))
            {
                int oldTarget = std::stoi(t.dist);
                auto it = old2new.find(oldTarget);

                if (it != old2new.end())
                    t.dist = std::to_string(it->second);   // 更新为新索引
                else
                    // 目标不存在（被优化掉了），输出警告
                    std::cerr << "警告：跳转目标 " << oldTarget << " 在优化后不存在！" << std::endl;
           