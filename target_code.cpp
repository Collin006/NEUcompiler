#include "target_code.h"

#include <sstream>
#include <string>
#include <unordered_set>

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

} // namespace

std::string generateTargetCode(const std::vector<FourTuple>& quadruples) {
    std::unordered_set<int> jumpTargets;
    jumpTargets.reserve(quadruples.size());

    for (const FourTuple& q : quadruples) {
        if ((q.operator_str == "if" || q.operator_str == "do" ||
             q.operator_str == "el" || q.operator_str == "goto" ||
             q.operator_str == "we") &&
            isIntegerString(q.dist)) {
            jumpTargets.insert(stoi(q.dist));
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
            if (isIntegerString(q.dist)) out << "  FJ R, " << jumpLabel(stoi(q.dist)) << '\n';
            else out << "  FJ R, " << valueOrPlaceholder(q.dist) << '\n';
        } else if (q.operator_str == "el" || q.operator_str == "goto" || q.operator_str == "we") {
            if (isIntegerString(q.dist)) out << "  JMP _, " << jumpLabel(stoi(q.dist)) << '\n';
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
