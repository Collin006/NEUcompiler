#include "quadruple_optimizer.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace std;

namespace {

bool isNumber(const string& s) {
    if (s.empty()) return false;
    if (s[0] == '-' && s.length() > 1 && isdigit(static_cast<unsigned char>(s[1]))) return true;
    return isdigit(static_cast<unsigned char>(s[0])) != 0;
}

bool isVar(const string& s) {
    if (s.empty() || isNumber(s)) return false;
    const unsigned char c = static_cast<unsigned char>(s[0]);
    return isalpha(c) != 0 || s[0] == '_';
}

string compute(const string& op, const string& v1, const string& v2) {
    if (!isNumber(v1)) return "";
    const double a = stod(v1);
    double b = 0;
    if (op != "!") {
        if (!isNumber(v2)) return "";
        b = stod(v2);
    }

    double r = 0;
    if (op == "+") r = a + b;
    else if (op == "-") r = a - b;
    else if (op == "*") r = a * b;
    else if (op == "/") {
        if (b == 0) return "";
        r = a / b;
    }
    else if (op == ">") r = a > b ? 1 : 0;
    else if (op == "<") r = a < b ? 1 : 0;
    else if (op == "=" || op == "==") r = a == b ? 1 : 0;
    else if (op == "<=") r = a <= b ? 1 : 0;
    else if (op == ">=") r = a >= b ? 1 : 0;
    else if (op == "&&") r = (a != 0 && b != 0) ? 1 : 0;
    else if (op == "||") r = (a != 0 || b != 0) ? 1 : 0;
    else if (op == "!") r = (a == 0) ? 1 : 0;
    else return "";

    const int ir = static_cast<int>(r);
    return (r == ir) ? to_string(ir) : to_string(r);
}

set<string> getUse(const FourTuple& t) {
    set<string> s;
    if (isVar(t.first_value)) s.insert(t.first_value);
    if (isVar(t.second_value)) s.insert(t.second_value);
    return s;
}

vector<FourTuple> constantFolding(const vector<FourTuple>& input) {
    vector<FourTuple> output;
    output.reserve(input.size());
    for (const auto& t : input) {
        const string& op = t.operator_str;
        if (op == "+" || op == "-" || op == "*" || op == "/" ||
            op == ">" || op == "<" || op == "=" || op == "==" ||
            op == "<=" || op == ">=" || op == "&&" || op == "||" || op == "!") {
            const string r = compute(op, t.first_value, t.second_value);
            if (!r.empty()) {
                output.push_back({":=", r, "", t.dist});
                continue;
            }
        }
        output.push_back(t);
    }
    return output;
}

vector<FourTuple> algebraicSimplify(const vector<FourTuple>& input) {
    vector<FourTuple> output;
    output.reserve(input.size());
    for (auto t : input) {
        const string& op = t.operator_str;
        if ((op == "+" && t.second_value == "0") ||
            (op == "-" && t.second_value == "0") ||
            (op == "*" && t.second_value == "1") ||
            (op == "/" && t.second_value == "1")) {
            t = {":=", t.first_value, "", t.dist};
        } else if ((op == "+" && t.first_value == "0") ||
                   (op == "*" && t.first_value == "1")) {
            t = {":=", t.second_value, "", t.dist};
        } else if (op == "*" && (t.first_value == "0" || t.second_value == "0")) {
            t = {":=", "0", "", t.dist};
        }
        output.push_back(t);
    }
    return output;
}

vector<FourTuple> deadCodeElimination(const vector<FourTuple>& input) {
    const int n = static_cast<int>(input.size());
    vector<set<string>> live(n + 1);
    set<string> cur;

    for (int i = n - 1; i >= 0; --i) {
        live[i] = cur;
        if (isVar(input[i].dist)) cur.erase(input[i].dist);
        const set<string> use = getUse(input[i]);
        cur.insert(use.begin(), use.end());
    }

    vector<FourTuple> output;
    output.reserve(input.size());
    for (int i = 0; i < n; ++i) {
        if (input[i].operator_str == ":=" &&
            isVar(input[i].dist) &&
            live[i].find(input[i].dist) == live[i].end()) {
            continue;
        }
        output.push_back(input[i]);
    }
    return output;
}

vector<FourTuple> commonSubexpressionElimination(const vector<FourTuple>& input) {
    map<string, string> available;
    vector<FourTuple> output;
    output.reserve(input.size());

    for (const auto& t : input) {
        const string& op = t.operator_str;
        if (op == "+" || op == "-" || op == "*" || op == "/" ||
            op == ">" || op == "<" || op == "=" || op == "==" || op == "<=" || op == ">=") {
            const string key = op + "_" + t.first_value + "_" + t.second_value;
            const auto it = available.find(key);
            if (it != available.end()) {
                output.push_back({":=", it->second, "", t.dist});
                continue;
            }
            available[key] = t.dist;
        }

        if (op == ":=" && isVar(t.dist)) {
            vector<string> toRemove;
            for (const auto& p : available) {
                if (p.first.find(t.dist) != string::npos) toRemove.push_back(p.first);
            }
            for (const auto& k : toRemove) available.erase(k);
        }

        output.push_back(t);
    }

    return output;
}

} // namespace

vector<FourTuple> optimizeQuadruples(const vector<FourTuple>& input) {
    vector<FourTuple> result = input;
    result = constantFolding(result);
    result = algebraicSimplify(result);
    result = commonSubexpressionElimination(result);
    result = constantFolding(result);
    result = deadCodeElimination(result);
    return result;
}
