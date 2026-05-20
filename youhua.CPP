#include <iostream>
#include <vector>
#include <string>
#include <map>
#include <set>
#include <algorithm>
#include <sstream>
#include <regex>

using namespace std;

// ==================== 四元式结构 ====================
struct FourTuple
{
    std::string operator_str;
    std::string first_value;
    std::string second_value;
    std::string dist;
};

// ==================== 优化工具函数 ====================
namespace Utils
{
    bool isNumber(const std::string &s)
    {
        if (s.empty())
            return false;
        if (s[0] == '-' && s.length() > 1 && isdigit(s[1]))
            return true;
        return isdigit(s[0]);
    }

    bool isVar(const std::string &s)
    {
        if (s.empty())
            return false;
        if (isNumber(s))
            return false;
        return isalpha(s[0]) || s[0] == '_';
    }

    // 关键：识别临时变量（_t数字）
    bool isTempVar(const std::string &s)
    {
        if (s.empty() || !isVar(s))
            return false;
        if (s.size() >= 2 && s[0] == '_' && s[1] == 't')
        {
            for (size_t i = 2; i < s.size(); ++i)
                if (!isdigit(s[i]))
                    return false;
            return true;
        }
        return false;
    }

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
            op == ">" || op == "<" || op == "=" || op == "<=" || op == ">=" || op == "&&" || op == "||" || op == "!")
        {
            std::string r = Utils::compute(op, t.first_value, t.second_value);
            if (!r.empty())
            {
                std::cout << "常量折叠: (" << op << ", " << t.first_value << ", " << t.second_value << ", " << t.dist
                          << ") -> " << t.dist << " := " << r << std::endl;
                FourTuple new_t;
                new_t.operator_str = ":=";
                new_t.first_value = r;
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
        if (op == "+" && t.second_value == "0")
        {
            t.operator_str = ":=";
            t.first_value = t.first_value;
            t.second_value = "";
            simp = true;
        }
        else if (op == "+" && t.first_value == "0")
        {
            t.operator_str = ":=";
            t.first_value = t.second_value;
            t.second_value = "";
            simp = true;
        }
        else if (op == "-" && t.second_value == "0")
        {
            t.operator_str = ":=";
            t.first_value = t.first_value;
            t.second_value = "";
            simp = true;
        }
        else if (op == "*" && t.second_value == "1")
        {
            t.operator_str = ":=";
            t.first_value = t.first_value;
            t.second_value = "";
            simp = true;
        }
        else if (op == "*" && t.first_value == "1")
        {
            t.operator_str = ":=";
            t.first_value = t.second_value;
            t.second_value = "";
            simp = true;
        }
        else if (op == "*" && (t.first_value == "0" || t.second_value == "0"))
        {
            t.operator_str = ":=";
            t.first_value = "0";
            t.second_value = "";
            simp = true;
        }
        else if (op == "/" && t.second_value == "1")
        {
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

// ==================== 3. 死代码消除（只删临时变量） ====================
std::vector<FourTuple> deadCodeElimination(const std::vector<FourTuple> &input)
{
    int n = input.size();
    std::vector<std::set<std::string>> live(n + 1);
    std::set<std::string> cur;
    for (int i = n - 1; i >= 0; i--)
    {
        live[i] = cur;
        if (Utils::isVar(input[i].dist))
            cur.erase(input[i].dist);
        std::set<std::string> use = Utils::getUse(input[i]);
        cur.insert(use.begin(), use.end());
    }
    std::vector<FourTuple> output;
    int count = 0;
    for (int i = 0; i < n; i++)
    {
        // 关键：只删除临时变量的无用赋值
        if (input[i].operator_str == ":=" &&
            Utils::isTempVar(input[i].dist) &&
            live[i].find(input[i].dist) == live[i].end())
        {
            std::cout << "删除死代码: " << input[i].dist << " := " << input[i].first_value << std::endl;
            count++;
            continue;
        }
        output.push_back(input[i]);
    }
    std::cout << "死代码消除: " << count << " 条" << std::endl;
    return output;
}

// ==================== 4. 公共子表达式消除（精确失效） ====================
std::vector<FourTuple> commonSubexpressionElimination(const std::vector<FourTuple> &input)
{
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
                std::cout << "公共子表达式消除: " << t.dist << " 重用 " << available[key] << std::endl;
                FourTuple new_t;
                new_t.operator_str = ":=";
                new_t.first_value = available[key];
                new_t.dist = t.dist;
                output.push_back(new_t);
                count++;
                continue;
            }
            available[key] = t.dist;
        }
        // 精确失效：仅当被赋值的变量出现在表达式的操作数中
        if (op == ":=" && Utils::isVar(t.dist))
        {
            std::vector<std::string> to_remove;
            for (const auto &p : available)
            {
                size_t first_ = p.first.find('_');
                size_t second_ = p.first.find('_', first_ + 1);
                if (first_ == string::npos || second_ == string::npos)
                    continue;
                string first = p.first.substr(first_ + 1, second_ - first_ - 1);
                string second = p.first.substr(second_ + 1);
                if (first == t.dist || second == t.dist)
                    to_remove.push_back(p.first);
            }
            for (const auto &k : to_remove)
                available.erase(k);
        }
        output.push_back(t);
    }
    std::cout << "公共子表达式消除: " << count << " 个" << std::endl;
    return output;
}

// ==================== 5. 标号重定位 ====================
void relocateLabels(std::vector<FourTuple> &tuples)
{
    std::map<int, int> oldToNew;
    for (size_t i = 0; i < tuples.size(); ++i)
    {
        if (tuples[i].operator_str == "lb")
        {
            int old = std::stoi(tuples[i].dist);
            oldToNew[old] = i;
            tuples[i].dist = std::to_string(i);
        }
    }
    std::set<std::string> jumpOps = {"goto", "if", "do", "we", "ie", "el"};
    for (auto &t : tuples)
    {
        if (jumpOps.count(t.operator_str))
        {
            int oldTarget = std::stoi(t.dist);
            if (oldToNew.find(oldTarget) != oldToNew.end())
                t.dist = std::to_string(oldToNew[oldTarget]);
            else
                std::cerr << "警告: 跳转目标 " << oldTarget << " 未找到" << std::endl;
        }
    }
}

// ==================== 6. 基本块划分（新增，无四元式副本） ====================
struct BasicBlock
{
    int id;                // 基本块编号
    int start_index;       // 在原四元式序列中的起始索引
    int end_index;         // 结束索引（包含）
    std::set<int> prev;    // 前驱基本块 id
    std::set<int> next;    // 后继基本块 id
};

// 构建基本块：只保存索引范围，不复制四元式
std::vector<BasicBlock> buildBasicBlocks(const std::vector<FourTuple> &tuples)
{
    // 1. 确定入口点（leader）
    std::set<int> leaders;
    leaders.insert(0);  // 第一条指令是入口

    // 记录标号指令：lb 的 dist 值 -> 索引
    std::map<int, int> label2idx;
    for (int i = 0; i < (int)tuples.size(); ++i)
    {
        if (tuples[i].operator_str == "lb")
        {
            int label = std::stoi(tuples[i].dist);
            label2idx[label] = i;
        }
    }

    // 扫描跳转指令，其后继指令也是 leader
    for (int i = 0; i < (int)tuples.size(); ++i)
    {
        const std::string &op = tuples[i].operator_str;
        if (op == "goto" || op == "if" || op == "wh" || op == "do" ||
            op == "we" || op == "el" || op == "ie")
        {
            if (i + 1 < (int)tuples.size())
                leaders.insert(i + 1);
        }
        if (op == "goto")
        {
            int target = std::stoi(tuples[i].dist);
            if (label2idx.count(target))
                leaders.insert(label2idx[target]);
        }
    }

    // 2. 将 leader 排序，划分基本块
    std::vector<int> leader_list(leaders.begin(), leaders.end());
    std::sort(leader_list.begin(), leader_list.end());

    std::vector<BasicBlock> blocks;
    std::map<int, int> idx2block;   // 四元式索引 -> 所属基本块 id

    for (size_t i = 0; i < leader_list.size(); ++i)
    {
        BasicBlock bb;
        bb.id = (int)blocks.size();
        bb.start_index = leader_list[i];
        bb.end_index = (i + 1 < leader_list.size()) ? leader_list[i + 1] - 1 : (int)tuples.size() - 1;
        blocks.push_back(bb);
        for (int j = bb.start_index; j <= bb.end_index; ++j)
            idx2block[j] = bb.id;
    }

    // 3. 建立前驱和后继关系
    for (size_t i = 0; i < blocks.size(); ++i)
    {
        const BasicBlock &bb = blocks[i];
        if (bb.start_index > bb.end_index) continue;

        const FourTuple &last = tuples[bb.end_index];
        std::string last_op = last.operator_str;

        if (last_op == "goto")
        {
            int target = std::stoi(last.dist);
            if (label2idx.count(target) && idx2block.count(label2idx[target]))
            {
                int to_id = idx2block[label2idx[target]];
                blocks[i].next.insert(to_id);
                blocks[to_id].prev.insert(i);
            }
        }
        else if (last_op == "if" || last_op == "wh")
        {
            int target = std::stoi(last.dist);
            if (label2idx.count(target) && idx2block.count(label2idx[target]))
            {
                int to_id = idx2block[label2idx[target]];
                blocks[i].next.insert(to_id);
                blocks[to_id].prev.insert(i);
            }
            // 条件跳转的 fall-through 后继
            if (i + 1 < blocks.size())
            {
                blocks[i].next.insert(i + 1);
                blocks[i + 1].prev.insert(i);
            }
        }
        else
        {
            // 普通顺序流
            if (i + 1 < blocks.size())
            {
                blocks[i].next.insert(i + 1);
                blocks[i + 1].prev.insert(i);
            }
        }
    }

    return blocks;
}

// 打印基本块信息（仅用于调试）
void printBlocks(const std::vector<FourTuple> &tuples, const std::vector<BasicBlock> &blocks)
{
    std::cout << "\n========== 基本块划分 ==========\n";
    for (const auto &bb : blocks)
    {
        std::cout << "B" << bb.id << " [" << bb.start_index << ", " << bb.end_index << "] 前驱:{";
        for (int p : bb.prev) std::cout << "B" << p << " ";
        std::cout << "} 后继:{";
        for (int n : bb.next) std::cout << "B" << n << " ";
        std::cout << "}\n";
        for (int i = bb.start_index; i <= bb.end_index; ++i)
        {
            const auto &t = tuples[i];
            std::cout << "  " << i << ": (" << t.operator_str << ", " << t.first_value
                      << ", " << t.second_value << ", " << t.dist << ")\n";
        }
    }
}

// ==================== 7. 优化管理器 ====================
std::vector<FourTuple> optimize(const std::vector<FourTuple> &input)
{
    std::cout << "\n========== 开始优化 ==========" << std::endl;
    std::cout << "原始四元式: " << input.size() << " 条" << std::endl;
    auto result = input;
    result = constantFolding(result);
    result = algebraicSimplify(result);
    result = commonSubexpressionElimination(result);
    result = constantFolding(result); // 再次折叠
    result = deadCodeElimination(result);
    relocateLabels(result); // 必须最后调用
    std::cout << "优化后四元式: " << result.size() << " 条" << std::endl;
    std::cout << "========== 优化完成 ==========\n" << std::endl;
    return result;
}

// ==================== 辅助函数 ====================
void printTuples(const std::vector<FourTuple> &tuples)
{
    for (size_t i = 0; i < tuples.size(); i++)
        std::cout << i << ": (" << tuples[i].operator_str << ", "
                  << tuples[i].first_value << ", " << tuples[i].second_value
                  << ", " << tuples[i].dist << ")" << std::endl;
}

// ==================== 测试主函数（演示基本块划分） ====================
int main()
{
    // 这里可以放你的测试代码
    // 示例：构造一个简单的四元式序列
    std::vector<FourTuple> test;
    FourTuple t;

    t.operator_str = "lb"; t.first_value = ""; t.second_value = ""; t.dist = "100"; test.push_back(t);
    t.operator_str = ":="; t.first_value = "10"; t.second_value = ""; t.dist = "a"; test.push_back(t);
    t.operator_str = "if"; t.first_value = "a"; t.second_value = "b"; t.dist = "200"; test.push_back(t);
    t.operator_str = "goto"; t.first_value = ""; t.second_value = ""; t.dist = "100"; test.push_back(t);
    t.operator_str = "lb"; t.first_value = ""; t.second_value = ""; t.dist = "200"; test.push_back(t);
    t.operator_str = ":="; t.first_value = "a"; t.second_value = "1"; t.dist = "b"; test.push_back(t);
    t.operator_str = "goto"; t.first_value = ""; t.second_value = ""; t.dist = "100"; test.push_back(t);

    std::cout << "原始四元式:\n";
    printTuples(test);

    // 构建基本块（不修改四元式，只划分）
    auto blocks = buildBasicBlocks(test);
    printBlocks(test, blocks);

    // 如需优化，调用 optimize(test) 即可
    // auto optimized = optimize(test);
    // printTuples(optimized);

    return 0;
}