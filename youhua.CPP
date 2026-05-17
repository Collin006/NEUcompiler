#include <iostream>
#include <vector>
#include <string>
#include <map>
#include <set>
#include <algorithm>
#include <sstream>

using namespace std;

// ==================== 四元式结构 ====================
struct FourTuple
{
    std::string operator_str; // 操作符
    std::string first_value;  // 第一操作数
    std::string second_value; // 第二操作数
    std::string dist;         // 目标操作数
};

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
struct BasicBlock
{
    int id;
    std::vector<FourTuple> tuples;
    std::set<int> prev;
    std::set<int> next;
};

std::vector<BasicBlock> buildBasicBlocks(const std::vector<FourTuple> &tuples)
{
    // 确定入口
    std::set<int> leaders;
    leaders.insert(0);

    std::map<int, int> label2idx;
    for (int i = 0; i < tuples.size(); i++)
    {
        if (tuples[i].operator_str == "lb")
        {
            label2idx[std::stoi(tuples[i].dist)] = i;
        }
    }

    for (int i = 0; i < tuples.size(); i++)
    {
        std::string op = tuples[i].operator_str;
        if (op == "goto" || op == "if" || op == "wh" || op == "do" ||
            op == "we" || op == "el" || op == "ie")
        {
            if (i + 1 < tuples.size())
                leaders.insert(i + 1);
        }
        if (op == "goto")
        {
            int target = std::stoi(tuples[i].dist);
            if (label2idx.count(target))
                leaders.insert(label2idx[target]);
        }
    }

    // 划分基本块
    std::vector<int> leader_list(leaders.begin(), leaders.end());
    std::sort(leader_list.begin(), leader_list.end());

    std::vector<BasicBlock> blocks;
    std::map<int, int> idx2block;

    for (int i = 0; i < leader_list.size(); i++)
    {
        BasicBlock bb;
        bb.id = i;
        int start = leader_list[i];
        int end = (i + 1 < leader_list.size()) ? leader_list[i + 1] - 1 : tuples.size() - 1;

        for (int j = start; j <= end; j++)
        {
            bb.tuples.push_back(tuples[j]);
            idx2block[j] = i;
        }
        blocks.push_back(bb);
    }

    // 建立块间联系
    for (int i = 0; i < blocks.size(); i++)
    {
        if (blocks[i].tuples.empty())
            continue;

        std::string last_op = blocks[i].tuples.back().operator_str;

        if (last_op == "goto")
        {
            int target = std::stoi(blocks[i].tuples.back().dist);
            if (label2idx.count(target) && idx2block.count(label2idx[target]))
            {
                int to = idx2block[label2idx[target]];
                blocks[i].next.insert(to);
                blocks[to].prev.insert(i);
            }
        }
        else if (last_op == "if" || last_op == "wh")
        {
            int target = std::stoi(blocks[i].tuples.back().dist);
            if (label2idx.count(target) && idx2block.count(label2idx[target]))
            {
                int to = idx2block[label2idx[target]];
                blocks[i].next.insert(to);
                blocks[to].prev.insert(i);
            }
            if (i + 1 < blocks.size())
            {
                blocks[i].next.insert(i + 1);
                blocks[i + 1].prev.insert(i);
            }
        }
        else
        {
            if (i + 1 < blocks.size())
            {
                blocks[i].next.insert(i + 1);
                blocks[i + 1].prev.insert(i);
            }
        }
    }

    return blocks;
}

// ==================== 6. 优化管理器 ====================
std::vector<FourTuple> optimize(const std::vector<FourTuple> &input)
{
    std::cout << "\n========== 开始优化 ==========" << std::endl;
    std::cout << "原始四元式: " << input.size() << " 条" << std::endl;

    std::vector<FourTuple> result = input;

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

    std::cout << "优化后四元式: " << result.size() << " 条" << std::endl;
    std::cout << "========== 优化完成 ==========\n"
              << std::endl;

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

    // 优化
    std::vector<FourTuple> optimized = optimize(test);

    std::cout << "优化后四元式:" << std::endl;
    printTuples(optimized);

    return 0;
}