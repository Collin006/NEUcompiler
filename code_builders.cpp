#include "code_builders.h"
#include <unordered_set>
#include <sstream>

namespace {

bool isIntegerString(const std::string& s) {
    if (s.empty()) return false;
    size_t i = (s[0] == '-' ? 1 : 0);
    if (i >= s.size()) return false;
    for (; i < s.size(); ++i) {
        if (s[i] < '0' || s[i] > '9') return false;
    }
    return true;
}

std::string valueOrPlaceholder(const std::string& value) {
    return value.empty() ? "_" : value;
}

}

std::string generateTargetCode(const std::vector<FourTuple>& quadruples) {
    std::unordered_set<int> jumpTargets;
    jumpTargets.reserve(quadruples.size());

    for (const FourTuple& q : quadruples) {
        if ((q.operator_str == "if" || q.operator_str == "do" ||
             q.operator_str == "el" || q.operator_str == "goto" ||
             q.operator_str == "we") &&
            isIntegerString(q.dist)) {
            jumpTargets.insert(std::stoi(q.dist));
        }
    }

    auto jumpLabel = [](int index) { return "L" + std::to_string(index); };
    auto emitBinary = [](std::ostringstream& out, const std::string& op, const FourTuple& q) {
        out << "  LD R, " << valueOrPlaceholder(q.first_value) << '\n';
        out << "  " << op << " R, " << valueOrPlaceholder(q.second_value) << '\n';
        out << "  ST R, " << valueOrPlaceholder(q.dist) << '\n';
    };

    std::ostringstream out;
    out << "===== 目标代码 =====\n";
    for (size_t i = 0; i < quadruples.size(); ++i) {
        if (jumpTargets.count(static_cast<int>(i)) != 0) {
            out << jumpLabel(static_cast<int>(i)) << ":\n";
        }

        const FourTuple& q = quadruples[i];
        if (q.operator_str == "+") emitBinary(out, "ADD", q);
        else if (q.operator_str == "-") emitBinary(out, "SUB", q);
        else if (q.operator_str == "*") emitBinary(out, "MUL", q);
        else if (q.operator_str == "/") emitBinary(out, "DIV", q);
        else if (q.operator_str == "<") emitBinary(out, "LT", q);
        else if (q.operator_str == ">") emitBinary(out, "GT", q);
        else if (q.operator_str == "<=") emitBinary(out, "LE", q);
        else if (q.operator_str == ">=") emitBinary(out, "GE", q);
        else if (q.operator_str == "=") emitBinary(out, "EQ", q);
        else if (q.operator_str == "<>") emitBinary(out, "NE", q);
        else if (q.operator_str == "&&") emitBinary(out, "AND", q);
        else if (q.operator_str == "||") emitBinary(out, "OR", q);
        else if (q.operator_str == "!") {
            out << "  LD R, " << valueOrPlaceholder(q.first_value) << '\n';
            out << "  NOT R, _\n";
            out << "  ST R, " << valueOrPlaceholder(q.dist) << '\n';
        } else if (q.operator_str == ":=") {
            out << "  LD R, " << valueOrPlaceholder(q.first_value) << '\n';
            out << "  ST R, " << valueOrPlaceholder(q.dist) << '\n';
        } else if (q.operator_str == "if" || q.operator_str == "do") {
            out << "  LD R, " << valueOrPlaceholder(q.first_value) << '\n';
            if (isIntegerString(q.dist)) out << "  FJ R, " << jumpLabel(std::stoi(q.dist)) << '\n';
            else out << "  FJ R, " << valueOrPlaceholder(q.dist) << '\n';
        } else if (q.operator_str == "el" || q.operator_str == "goto" || q.operator_str == "we") {
            if (isIntegerString(q.dist)) out << "  JMP _, " << jumpLabel(std::stoi(q.dist)) << '\n';
            else out << "  JMP _, " << valueOrPlaceholder(q.dist) << '\n';
        } else if (q.operator_str == "wh" || q.operator_str == "ie") {
            out << "  NOP\n";
        } else if (q.operator_str == "lb") {
            out << valueOrPlaceholder(q.first_value) << ":\n";
        } else {
            out << "  ; unsupported quadruple: (" << q.operator_str << ", "
                << q.first_value << ", " << q.second_value << ", " << q.dist << ")\n";
        }
    }
    return out.str();
}

void CodeBuilder::Init(
    const std::vector<MarkedFourTuple> &qt,
    const std::vector<std::unordered_map<std::string, int>> &active_record)
{
    code_index = 0; // 初始化操作指令区的索引
    QT = qt;
    ActiveRecord = active_record;
    RDL = null_value; // 初始化寄存器描述表
    while (!SEM.empty())
        SEM.pop(); // 清空分支语义分析栈
    while (!LoopSEM.empty())
        LoopSEM.pop(); // 清空循环语义分析栈
    Lables.clear(); // 清空标签表
    OBJ.clear();       // 清空操作指令区
}

/**
 * @brief 扫描操作符(辅助二元运算的目标代码生成)
 *
 * @param operator_str 操作符的字符串表示
 * @return Operators 操作符的枚举值
 */
Operators CodeBuilder::ScanOperator(const std::string &operator_str)
{
    if (operator_str == "+")
    {
        return Operators::ADD;
    }
    else if (operator_str == "-")
    {
        return Operators::SUB;
    }
    else if (operator_str == "*")
    {
        return Operators::MUL;
    }
    else if (operator_str == "/")
    {
        return Operators::DIV;
    }
    else if (operator_str == "<")
    {
        return Operators::LT;
    }
    else if (operator_str == ">")
    {
        return Operators::GT;
    }
    else if (operator_str == "==")
    {
        return Operators::EQ;
    }
    else if (operator_str == "<=")
    {
        return Operators::LE;
    }
    else if (operator_str == ">=")
    {
        return Operators::GE;
    }
    else if (operator_str == "!=")
    {
        return Operators::NE;
    }
    else if (operator_str == "&&")
    {
        return Operators::AND;
    }
    else if (operator_str == "||")
    {
        return Operators::OR;
    }
    else if (operator_str == "!")
    {
        return Operators::NOT;
    }
    return Operators::Invalid;
}

/**
 * @brief 根据操作符生成相应的操作指令(辅助二元运算目标代码生成)
 *
 * @param op 操作符的枚举值
 * @return std::string 操作指令的字符串表示
 */
std::string CodeBuilder::GetOperateCommand(Operators op)
{
    switch (op)
    {
    case Operators::ADD:
        return "ADD";
    case Operators::SUB:
        return "SUB";
    case Operators::MUL:
        return "MUL";
    case Operators::DIV:
        return "DIV";
    case Operators::LT:
        return "LT";
    case Operators::GT:
        return "GT";
    case Operators::EQ:
        return "EQ";
    case Operators::LE:
        return "LE";
    case Operators::GE:
        return "GE";
    case Operators::NE:
        return "NE";
    case Operators::AND:
        return "AND";
    case Operators::OR:
        return "OR";
    case Operators::NOT:
        return "NOT";
    default:
        return "";
    }
}

void CodeBuilder::BuildTokens()
{
    return;
}

/**
 * @brief 构建二元运算的目标代码,支持 ADD/SUB/MUL/DIV、LT/GT/EQ/LE/GE/NE、AND/OR/NOT
 *
 * @param index 四元式的在四元式区中的索引
 * @param ft 四元式
 * @return std::vector<AimCodeToken> 二元运算的目标代码
 * */
void CodeBuilder::BuildTwoOperandsToken(const int &index, const MarkedFourTuple &ft)
{

    // 根据操作符生成相应的指令
    Operators op = ScanOperator(ft.operator_str);
    std::string command = GetOperateCommand(op);
    if (command == "")
    {
        return;
    }

    // 如果寄存器需要被复用,但原寄存器内的操作数是活跃的，则需要先将原寄存器内的操作数加载到与之同名的内存中
    // 当遇到立即数时，由于立即数与变量名肯定不同名，且寄存器中一定是左值（可判断活跃性的），所以逻辑依然成立
    std::string RDL_name = RDL;
    if (RDL_name != null_value && RDL_name != ft.first_value.value && ActiveRecord[index][RDL_name] == true)
    {
        Operand router = {DistType::Router, "R"};    // 寄存器操作数
        Operand dist = {DistType::Memory, RDL_name}; // 内存操作数
        code_index = OBJ.size();
        OBJ.push_back({code_index, "ST", router, dist});
    }

    // 如果第一操作数不在寄存器中，则需要先将其从内存中加载到寄存器中
    if (RDL_name != ft.first_value.value)
    {
        Operand router = {DistType::Router, "R"}; // 寄存器操作数
        Operand source;
        if (ft.first_value.active == is_constant)
        {
            source = {DistType::Constant, ft.first_value.value}; // 立即数操作数
        }
        else
        {
            source = {DistType::Memory, ft.first_value.value}; // 内存操作数
        }
        code_index = OBJ.size();
        OBJ.push_back({code_index, "LD", router, source});
    }

    // 生成加法指令
    Operand router = {DistType::Router, "R"}; // 寄存器操作数
    Operand source;
    if (ft.second_value.active == is_constant)
    {
        source = {DistType::Constant, ft.second_value.value};
    }
    else
    {
        source = {DistType::Memory, ft.second_value.value}; // 内存操作数
    }
    code_index = OBJ.size();
    OBJ.push_back({code_index, command, router, source});

    // 存储结果到内存中
    Operand dist = {DistType::Memory, ft.dist.value};
    code_index = OBJ.size();
    OBJ.push_back({code_index, "ST", router, dist});

    // 更新寄存器,现在应当存储的是结果
    RDL = ft.dist.value;

    return;
}

/**
 * @brief 构建if语句的目标代码
 *
 * @param index 四元式的在四元式区中的索引
 * @param ft 四元式
 * @return std::vector<AimCodeToken> if语句的目标代码
 */
void CodeBuilder::BuildIfToken(const int &index, const MarkedFourTuple &ft)
{
    // 如果寄存器需要被复用,但原寄存器内的操作数是活跃的，则需要先将原寄存器内的操作数加载到与之同名的内存中
    // 当遇到立即数时，由于立即数与变量名肯定不同名，且寄存器中一定是左值（可判断活跃性的），所以逻辑依然成立
    std::string RDL_name = RDL;
    if (RDL_name != null_value && RDL_name != ft.first_value.value && ActiveRecord[index][RDL_name] == true)
    {
        Operand router = {DistType::Router, "R"};    // 寄存器操作数
        Operand dist = {DistType::Memory, RDL_name}; // 内存操作数
        code_index = OBJ.size();
        OBJ.push_back({code_index, "ST", router, dist});
    }

    // 如果第一操作数不在寄存器中，则需要先将其从内存中加载到寄存器中
    if (RDL_name != ft.first_value.value)
    {
        Operand router = {DistType::Router, "R"}; // 寄存器操作数
        Operand source;
        if (ft.first_value.active == is_constant)
        {
            source = {DistType::Constant, ft.first_value.value}; // 立即数操作数
        }
        else
        {
            source = {DistType::Memory, ft.first_value.value}; // 内存操作数
        }
        code_index = OBJ.size();
        OBJ.push_back({code_index, "LD", router, source});
    }

    // 生成待回填的IF指令
    code_index = OBJ.size();
    Operand router = {DistType::Router, "R"};          // 寄存器操作数
    Operand source = {DistType::Position, "?"};        // 位置地址操作数
    SEM.push(code_index);                              // 记录当前指令的索引, 用于回填跳转指令
    OBJ.push_back({code_index, "JF", router, source}); // 生成待回填的IF指令

    // 清空RDL
    RDL = null_value;
}

/**
 * @brief 构建else语句的目标代码
 *
 * @param index 四元式的在四元式区中的索引
 * @param ft 四元式
 * @return std::vector<AimCodeToken> else语句的目标代码
 */
void CodeBuilder::BuildElseToken(const int &index, const MarkedFourTuple &ft)
{
    // 保存寄存器中的活跃数据到内存
    if (RDL != null_value && ActiveRecord[index][RDL] == true)
    {
        Operand router = {DistType::Router, "R"}; // 寄存器操作数
        Operand dist = {DistType::Memory, RDL};   // 内存操作数
        code_index = OBJ.size();
        OBJ.push_back({code_index, "ST", router, dist});
    }

    // 生成JMP指令
    code_index = OBJ.size();
    Operand none = {DistType::None, "_"};           // 寄存器操作数
    Operand dist = {DistType::Position, "?"};       // 位置地址操作数
    OBJ.push_back({code_index, "JMP", none, dist}); // 生成JMP指令

    // 回填IF指令的跳转地址
    if (!SEM.empty())
    {
        int if_index = SEM.top();
        int position = code_index + 1;

        OBJ[if_index].second_value = {DistType::Position, std::to_string(position)}; // 回填跳转地址
        SEM.pop();
    }

    // 记录当前指令的索引, 用于回填跳转指令
    SEM.push(code_index);

    // 清空RDL
    RDL = null_value;
}

/**
 * @brief 构建if语句结束的目标代码
 *
 * @param index 四元式的在四元式区中的索引
 * @param ft 四元式
 * @return std::vector<AimCodeToken> if语句结束的目标代码
 */
void CodeBuilder::BuildIfEndToken(const int &index, const MarkedFourTuple &ft)
{
    // 保存寄存器中的活跃数据到内存
    if (RDL != null_value && ActiveRecord[index][RDL] == true)
    {
        Operand router = {DistType::Router, "R"}; // 寄存器操作数
        Operand dist = {DistType::Memory, RDL};   // 内存操作数
        code_index = OBJ.size();
        OBJ.push_back({code_index, "ST", router, dist});
    }

    // 回填JMP指令的跳转地址
    if (!SEM.empty())
    {
        int jmp_index = SEM.top();
        int position = code_index + 1;

        OBJ[jmp_index].second_value = {DistType::Position, std::to_string(position)}; // 回填跳转地址
        SEM.pop();
    }

    // 清空RDL
    RDL = null_value;
}

void CodeBuilder::BuildWhileToken(const int &index, const MarkedFourTuple &ft)
{
    code_index = OBJ.size();
    Operand none1 = {DistType::None, "_"};             // 无操作数
    Operand none2 = {DistType::None, "_"};             // 无操作数
    OBJ.push_back({code_index, "LOOP", none1, none2}); // 生成LOOP标记

    // 记录循环入口
    LoopSEM.push(code_index);

    // 清空RDL
    RDL = null_value;
}

void CodeBuilder::BuildDoToken(const int &index, const MarkedFourTuple &ft)
{
    // 保存寄存器中的活跃数据到内存
    std::string RDL_name = RDL;
    if (RDL_name != null_value && RDL_name != ft.first_value.value && ActiveRecord[index][RDL_name] == true)
    {
        Operand router = {DistType::Router, "R"};    // 寄存器操作数
        Operand dist = {DistType::Memory, RDL_name}; // 内存操作数
        code_index = OBJ.size();
        OBJ.push_back({code_index, "ST", router, dist});
    }

    // 如果第一操作数不在寄存器中，则需要先将其从内存中加载到寄存器中
    if (RDL_name != ft.first_value.value)
    {
        Operand router = {DistType::Router, "R"}; // 寄存器操作数
        Operand source;
        if (ft.first_value.active == is_constant)
        {
            source = {DistType::Constant, ft.first_value.value}; // 立即数操作数
        }
        else
        {
            source = {DistType::Memory, ft.first_value.value}; // 内存操作数
        }
        code_index = OBJ.size();
        OBJ.push_back({code_index, "LD", router, source});
    }

    code_index = OBJ.size();
    Operand router = {DistType::Router, "R"}; // 寄存器操作数
    Operand dist = {DistType::Position, "?"}; // 位置地址操作数

    OBJ.push_back({code_index, "JF", router, dist}); // 生成JF指令

    SEM.push(code_index); // 记录当前指令的索引, 用于回填跳转指令

    // 清空RDL
    RDL = null_value;
}

void CodeBuilder::BuildWhileEndToken(const int &index, const MarkedFourTuple &ft)
{
    //先生成回到循环入口的JMP指令
    if(!LoopSEM.empty())
    {
        int entry_index = LoopSEM.top();

        code_index = OBJ.size();
        Operand none = {DistType::None, "_"}; // 无操作数
        Operand dist = {DistType::Position, std::to_string(entry_index)}; // 位置地址操作数
        OBJ.push_back({code_index, "JMP", none, dist}); // 生成JMP指令
    }

    // 再回填do语句的跳转地址
    if(!SEM.empty())
    {
        int jmp_index = SEM.top();
        int position = code_index + 1;

        OBJ[jmp_index].second_value = {DistType::Position, std::to_string(position)}; // 回填跳转地址
        SEM.pop();
    }

    // 跳出循环语句
    LoopSEM.pop();
    // 清空RDL
    RDL = null_value;
}

void CodeBuilder::BuildLabelToken(const int &index, const MarkedFourTuple &ft)
{
    std::string lable_name = ft.dist.value;
    auto info = Lables[lable_name];

    if(info.position != no_position)
    {
        // 错误，标签已经定义了
        return;
    }

    // 记录标签的OBJ索引
    code_index = OBJ.size();
    info.position = code_index;

    // 生成标签指令
    OBJ.push_back({code_index, lable_name, {DistType::None, "_"}, {DistType::None, "_"}});

    while(!info.backpatch.empty())
    {
        int backpatch_index = info.backpatch.top();
        OBJ[backpatch_index].second_value = {DistType::Position, std::to_string(info.position)}; // 回填跳转地址
        info.backpatch.pop();
    }
}

void CodeBuilder::BuildGotoToken(const int &index, const MarkedFourTuple &ft)
{
    std::string lable_name = ft.dist.value;
    auto info = Lables[lable_name];
    code_index = OBJ.size();

    Operand none = {DistType::None, "_"}; // 无操作数
    Operand dist = {DistType::Position, "?"}; // 位置地址操作数

    OBJ.push_back({code_index, "JMP", none, dist}); // 生成JMP指令

    if(info.position == no_position)
    {
        //标签未定义，加入回填队列
        info.backpatch.push(index);
    } 
    else {
        // 标签已经存在
        OBJ[code_index].second_value = {DistType::Position, std::to_string(info.position)}; // 回填跳转地址
    }
}
