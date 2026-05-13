#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <cctype>
#include <stdexcept>
#include "global.h"
#include "synbl.h"
// 字符串还没有加入常量表
using namespace std;

static char toLowerChar(char ch) {
    return static_cast<char>(tolower(static_cast<unsigned char>(ch)));
}

static string toLowerString(const string& text) {
    string result;

    for (char ch : text) {
        result += toLowerChar(ch);
    }

    return result;
}

static bool isIdentifierStart(char ch) {
    return isalpha(static_cast<unsigned char>(ch)) || ch == '_';
}

static bool isIdentifierChar(char ch) {
    return isalnum(static_cast<unsigned char>(ch)) || ch == '_';
}

static bool isDelimiterStart(char ch) {
    string delimiters = "+-*/=<>:;,.()[]{}!&|";
    return delimiters.find(ch) != string::npos;
}


static void lexicalError(int line, const string& message) {
    throw runtime_error("Lexical error at line " + to_string(line) + ": " + message);
}

static int findKeywordIndex(const string& value) {
    string lowerValue = toLowerString(value);

    for (int i = 0; i < static_cast<int>(ctx.keywordTable.size()); i++) {
        if (lowerValue == toLowerString(ctx.keywordTable[i])) {
            return i;
        }
    }

    return -1;
}

static int findDelimiterIndex(const string& value) {
    for (int i = 0; i < static_cast<int>(ctx.delimiterTable.size()); i++) {
        if (value == ctx.delimiterTable[i]) {
            return i;
        }
    }
    return -1;
}

static int findSynblIndex(const string& value) {
    for (int i = 0; i < static_cast<int>(ctx.synbl.size()); i++) {
        if (ctx.synbl[i].name == value) {
            return i;
        }
    }

    return -1;
}

// 数字识别器
// 识别 Pascal 中的整数和简单实数
// 例如：123, 3.14
// 识别到数字后，把它加入常数表
// 约定：整数加入 CONSL1，实数加入 CONSL2
// 注意：如果小数点后面不是数字，则不把这个点当作数字的一部分
static Token scanNumber(istream& source, int line) {
    string value;
    int type = 1;   // 1 表示 int，2 表示 real，仅用于判断加入哪张常数表

    // 整数部分
    while (isdigit(static_cast<unsigned char>(source.peek()))) {
        value += static_cast<char>(source.get());
    }

    // 小数部分
    if (source.peek() == '.') {
        source.get();

        if (isdigit(static_cast<unsigned char>(source.peek()))) {
            type = 2;
            value += '.';

            while (isdigit(static_cast<unsigned char>(source.peek()))) {
                value += static_cast<char>(source.get());
            }
        } else {
            // 不是实数，把刚读入的小数点退回去
            source.unget();
        }
    }

    if (source.peek() == 'e' || source.peek() == 'E') {
        type = 2;
        value += static_cast<char>(source.get());

        if (source.peek() == '+' || source.peek() == '-') {
            value += static_cast<char>(source.get());
        }

        if (!isdigit(static_cast<unsigned char>(source.peek()))) {
            lexicalError(line, "invalid scientific notation: " + value);
        }

        while (isdigit(static_cast<unsigned char>(source.peek()))) {
            value += static_cast<char>(source.get());
        }
    }

    int tableIndex;

    if (type == 1) {
        ctx.consl1.push_back(stoi(value));
        tableIndex = static_cast<int>(ctx.consl1.size()) - 1;
    } else {
        ctx.consl2.push_back(stod(value));
        tableIndex = static_cast<int>(ctx.consl2.size()) - 1;
    }

    Token token;
    token.type = type == 1 ? "CONSL1" : "CONSL2";
    token.value = to_string(tableIndex);
    token.line = line;

    return token;
}

// 标识符 / 关键字识别器
// Pascal 标识符一般以字母开头，后面可以跟字母或数字
// 识别到一个单词后，先到关键字表中查找
// 如果关键字表中存在，则返回 KEYWORD；否则返回 ID

static Token scanIdentifierOrKeyword(istream& source, int line)
{
    string value;

    while (isIdentifierChar(static_cast<char>(source.peek()))) {
        value += static_cast<char>(source.get());
    }

    int keywordIndex = findKeywordIndex(value);

    Token token;

    if (keywordIndex != -1) {
        token.type = "KEYWORD";
        token.value = to_string(keywordIndex);
        token.line = line;
        return token;
    }

    int synblIndex = findSynblIndex(value);

    if (synblIndex == -1) {
        SynblItem item;
        item.name = value;
        item.typ = -1;
        item.cat = "";
        item.addr = "(-1, -1)";
        ctx.synbl.push_back(item);
        synblIndex = static_cast<int>(ctx.synbl.size()) - 1;
    }

    token.type = "ID";
    token.value = to_string(synblIndex);
    token.line = line;

    return token;
}

// 界符 / 运算符识别器
// 识别 Pascal 中常见的单字符和双字符界符
// 如果识别到的界符在界符表中，就返回 DELIMITER
static Token scanDelimiter(istream& source, int line) {
    string value;

    char ch = static_cast<char>(source.get());
    value += ch;

    char next = static_cast<char>(source.peek());

    if ((ch == ':' && next == '=') ||
        (ch == '<' && next == '=') ||
        (ch == '>' && next == '=') ||
        (ch == '<' && next == '>') ||
        (ch == '.' && next == '.') ||
        (ch == '&' && next == '&') ||
        (ch == '|' && next == '|')) {
        value += static_cast<char>(source.get());
    }

    int delimiterIndex = findDelimiterIndex(value);

    if (delimiterIndex == -1) {
        lexicalError(line, "unknown delimiter: " + value);
    }

    Token token;
    token.type = "DELIMITER";
    token.value = to_string(delimiterIndex);
    token.line = line;

    return token;
}

static void skipBraceComment(istream& source, int& line) {
    source.get();

    while (source.peek() != EOF) {
        char ch = static_cast<char>(source.get());

        if (ch == '\n') {
            line++;
        } else if (ch == '}') {
            return;
        }
    }

    lexicalError(line, "unclosed comment");
}

static void skipParenStarComment(istream& source, int& line) {
    source.get();
    source.get();

    while (source.peek() != EOF) {
        char ch = static_cast<char>(source.get());

        if (ch == '\n') {
            line++;
        } else if (ch == '*' && source.peek() == ')') {
            source.get();
            return;
        }
    }

    lexicalError(line, "unclosed comment");
}

static Token scanString(istream& source, int line) {
    string value;

    source.get();

    while (source.peek() != EOF) {
        char ch = static_cast<char>(source.get());

        if (ch == '\n') {
            lexicalError(line, "unclosed string literal");
        }

        if (ch == '\'') {
            if (source.peek() == '\'') {
                value += '\'';
                source.get();
            } else {
                Token token;
                token.type = "STRING";
                token.value = value;
                token.line = line;
                return token;
            }
        } else {
            value += ch;
        }
    }

    lexicalError(line, "unclosed string literal");

    Token token;
    return token;
}

vector<Token> lexicalAnalyze(istream& source) {
    vector<Token> tokens;
    int line = 1;

    while (source.peek() != EOF) {
        char ch = static_cast<char>(source.peek());

        if (ch == '\n') {
            source.get();
            line++;
        } else if (isspace(ch)) {
            source.get();
        } else if (isdigit(static_cast<unsigned char>(ch))) {
            tokens.push_back(scanNumber(source, line));
        } else if (ch == '{') {
            skipBraceComment(source, line);
        } else if (ch == '(') {
            source.get();
            if (source.peek() == '*') {
                source.unget();
                skipParenStarComment(source, line);
            } else {
                source.unget();
                tokens.push_back(scanDelimiter(source, line));
            }
        } else if (ch == '\'') {
            tokens.push_back(scanString(source, line));
        } else if (isIdentifierStart(ch)) {
            tokens.push_back(scanIdentifierOrKeyword(source, line));
        } else if (isDelimiterStart(ch)) {
            tokens.push_back(scanDelimiter(source, line));
        } else {
            lexicalError(line, string("unknown character: ") + ch);
        }
    }

    return tokens;
}
