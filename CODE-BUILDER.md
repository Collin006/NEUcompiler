# 后端设计

## 四元式结构

```cpp
struct FourTuple
{
    std::string operator_str; // 操作符
    std::string first_value; // 第一操作数
    std::string second_value; // 第二操作数
    std::string dist; // 目标操作数
};
```

## 四元式操作符

```cpp
+，-，*，/，wh，do，we，if，el，ie，:=,goto,lb,>,<,=,<=,>=,&&,||,!
```

## 四元式操作数规定

- 操作数全部用字符串表示
- 以字符开头的操作数识别为：变量、临时变量、字符串常量、字符常量
- 以数字开头的操作数识别为：整数常量、浮点数常量、跳转目标语句的序号

## 目标代码生成

### 指令的基本形式

op  Ri , Rk/M
其中，Ri、Rk/M 分别表示寄存器、寄存器或内存地址，含义是：Ri:= (Ri)op(Rk) 或 Ri:= (Ri)op(M)
若 op 为单目运算，则含义是：Ri:= op(Rk/M)

### 基本指令

``` text
LD Ri,Rk/M  …… Ri:= (Rk/M)
ST Ri,Rk/M  …… (Rk/M):= Ri

FJ Ri, M    …… 若 (Ri)==false 则转 M
TJ Ri, M    …… 若 (Ri)==true 则转 M
JMP _, M    …… 无条件转 M

ADD Ri,Rk/M  …… Ri:=(Ri)+(Rk/M)
SUB Ri,Rk/M  …… Ri:=(Ri)-(Rk/M)
MUL Ri,Rk/M  …… Ri:=(Ri)*(Rk/M)
DIV Ri,Rk/M  …… Ri:=(Ri)/(Rk/M)

LT(<),GT(>),EQ(==),LE(<=),GE(>=),NE(!=)
AND(&&),OR(||),NO(!)
```
