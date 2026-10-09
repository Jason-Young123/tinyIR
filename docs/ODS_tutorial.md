# MLIR、ODS、TableGen 与 C++：新手开发流程

## 1. 先记住一句话

```text
MLIR      = 编译器 IR 框架
Dialect   = 一组领域相关的 Operation、Type 和 Attribute
ODS       = 描述 MLIR Operation 的规则
TableGen  = 读取这些规则并生成 C++ 的工具
C++       = 编译生成的代码，并实现复杂语义、优化和 lowering
```

它们的关系是：

```text
你在 .td 文件中写 ODS/TableGen 描述
            |
            v
        mlir-tblgen
            |
            v
生成 MyOps.h.inc / MyOps.cpp.inc 等 C++ 代码片段
            |
            v
C++ 编译器把生成代码和手写 C++ 一起编译
            |
            v
得到可以运行的 MLIR 编译器工具
```

**ODS 和 TableGen 发生在编译 MLIR 工具时；运行 `mlir-opt` 或自己的编译器时，通常不会再读取 `.td` 文件。**

## 2. MLIR 中的 Operation 是什么

MLIR 用 Operation 表示程序中的计算或结构：

```mlir
%result = arith.addi %lhs, %rhs : i32
```

这条 Operation 可以拆成：

```text
arith       dialect 名称
addi        Operation 名称
%lhs/%rhs   输入 operands
%result     输出 result
i32         类型
```

常见 Operation 例如：

```text
arith.addi       整数加法
linalg.matmul    矩阵乘法
scf.for          循环
func.func        函数
memref.load      内存加载
```

MLIR 允许用户定义自己的 dialect。例如 AI 编译器可以定义张量算子、布局、量化或硬件相关 Operation。

## 3. ODS 和 TableGen 分别是什么

### TableGen

TableGen 是 LLVM 的描述语言和代码生成工具。它可以把结构化描述转换为 C++ 代码。

```tablegen
class Example<string name> {
  string mnemonic = name;
}

def Add : Example<"add"> {
}
```

可以粗略理解为：

```text
class = 可复用的描述模板
def   = 一个具体定义
let   = 设置或覆盖字段
```

TableGen 中的 `class` 和 `def` 不是运行时 C++ 类和对象，而是供代码生成器读取的记录。

### ODS

ODS 的全称是 **Operation Definition Specification**。它是 MLIR 建立在 TableGen 之上的 Operation 描述机制。

MLIR 为 ODS 提供了专门的字段和基类，例如：

```text
Op
Dialect
ins / outs
arguments / results
regions
traits
assemblyFormat
```

因此：

```text
TableGen = 通用的描述和生成工具
ODS      = MLIR 用 TableGen 描述 Operation 的规则
```

ODS 的主要价值是：把 Operation 的结构集中描述在一个地方，并自动生成大量重复的 C++ 代码，例如 getter、builder、基础验证代码和 Operation 类声明。

## 4. 一个最小 Operation 示例

假设我们要定义：

```mlir
%result = my.add %a, %b : i32
```

可以在 `MyOps.td` 中写：

```tablegen
include "mlir/IR/OpBase.td"

def My_Dialect : Dialect {
  let name = "my";
  let cppNamespace = "::my";
}

class My_Op<string mnemonic, list<Trait> traits = []>
    : Op<My_Dialect, mnemonic, traits>;

def My_AddOp : My_Op<"add"> {
  let summary = "add two i32 values";

  let arguments = (ins
    I32:$lhs,
    I32:$rhs
  );

  let results = (outs
    I32:$result
  );

  let assemblyFormat =
      "$lhs `,` $rhs attr-dict `:` type($result)";
}
```

这段定义表达了：

```text
my.add 有两个 i32 输入：lhs、rhs
my.add 有一个 i32 输出：result
文本语法是：my.add %a, %b : i32
```

如果没有 custom syntax，也可以使用通用语法：

```mlir
%result = "my.add"(%a, %b) : (i32, i32) -> i32
```

## 5. `.td` 如何变成 C++

构建时，CMake 会调用 `mlir-tblgen`：

```text
MyOps.td
   |
   | mlir-tblgen
   v
MyOps.h.inc
MyOps.cpp.inc
```

这些 `.inc` 文件通常包含：

```text
Operation C++ 类声明
operand/result getter
builder 和基础验证代码
Operation 定义
辅助注册代码
```

手写 C++ 文件会包含这些生成文件。例如：

```cpp
// MyOps.h
#define GET_OP_CLASSES
#include "my/MyOps.h.inc"
```

```cpp
// MyDialect.cpp
#include "my/MyDialect.h"

#define GET_OP_CLASSES
#include "my/MyOps.cpp.inc"
```

Dialect 初始化时还需要注册 Operation，具体写法随项目版本和工程结构略有不同，常见形式是：

```cpp
void my::MyDialect::initialize() {
  addOperations<
#define GET_OP_LIST
#include "my/MyOps.cpp.inc"
  >();
}
```

这里的 `GET_OP_CLASSES`、`GET_OP_LIST` 是生成文件使用的宏，用来选择展开哪一部分代码。

## 6. C++ 还要负责什么

ODS 适合描述固定、结构化的规则：

```text
Operation 名称
输入和输出数量
输入和输出类型
属性
Region 数量
通用 traits
常规文本语法
```

C++ 主要负责 ODS 不方便表达或不属于 Operation 声明的内容：

```text
复杂 verifier
自定义 builder
特殊 parser/printer
rewrite pattern
优化 pass
lowering pass
代码生成
```

例如 `list.map` 可能要求：

```text
输入 list 的元素类型
必须等于
Region 的 Block 参数类型
```

这种跨 operand 和 Region 的关系通常需要 C++ verifier：

```cpp
LogicalResult MapOp::verify() {
  Type inputElementType =
      getInput().getType().getElementType();
  Type bodyArgumentType =
      getBodyBlock().getArgument(0).getType();

  if (inputElementType != bodyArgumentType)
    return emitOpError(
        "body argument type must match input element type");

  return success();
}
```

ODS 中可以声明需要额外 verifier：

```tablegen
let hasVerifier = 1;
```

## 7. ODS 不实现 Operation 的“计算行为”

下面的 ODS 定义：

```tablegen
def My_AddOp : My_Op<"add"> {
  let arguments = (ins I32:$lhs, I32:$rhs);
  let results = (outs I32:$result);
}
```

只说明 `my.add` 的结构和合法形式，并没有实现真正的加法算法。

实际编译流程中，通常还需要编写 lowering 或转换：

```text
my.add
  -> arith.addi
  -> LLVM IR add
  -> CPU 加法指令
```

因此：

```text
ODS       描述 Operation “长什么样”
C++ pass  决定 Operation “如何被转换和优化”
```

## 8. 一个标准开发流程

### 第 1 步：先设计 Operation

先明确：

```text
名称是什么？
有几个输入和输出？
每个值是什么类型？
有哪些属性？
是否包含 Region？
哪些情况非法？
将来要降低到哪个 dialect？
```

### 第 2 步：定义 Dialect 和 Operation

在 `MyDialect.td`、`MyOps.td` 中使用 ODS 描述：

```text
Dialect
Operation
arguments / results
类型约束
traits
regions
assemblyFormat
```

### 第 3 步：在 CMake 中配置 TableGen

项目通常使用类似下面的 CMake 规则：

```cmake
add_mlir_dialect(MyOps my)
```

或者显式配置：

```cmake
set(LLVM_TARGET_DEFINITIONS MyOps.td)
mlir_tablegen(MyOps.h.inc -gen-op-decls)
mlir_tablegen(MyOps.cpp.inc -gen-op-defs)
add_public_tablegen_target(MLIRMyOpsIncGen)
```

不同 MLIR 项目的 CMake 细节可能不同，但核心都是让 `mlir-tblgen` 处理 `.td` 文件。

### 第 4 步：包含并注册生成代码

在 C++ 中包含生成的 `.inc` 文件，实现 Dialect 初始化，并注册 Operation。

### 第 5 步：补充 C++ 逻辑

只为 ODS 无法表达的内容写 C++：

```text
复杂验证
自定义解析/打印
辅助方法
rewrite
lowering
优化 pass
```

### 第 6 步：编写 `.mlir` 测试

测试 Operation 是否能正确解析和打印：

```mlir
func.func @test(%a: i32, %b: i32) -> i32 {
  %r = my.add %a, %b : i32
  return %r : i32
}
```

也可以用 generic syntax 检查底层结构：

```mlir
%r = "my.add"(%a, %b) : (i32, i32) -> i32
```

还应测试：

```text
非法类型是否被拒绝
verifier 错误信息是否清楚
custom syntax 是否能 round-trip
lowering 是否产生预期 Operation
```

### 第 7 步：构建和调试

构建的大致过程是：

```text
CMake
  -> mlir-tblgen 生成 .inc
  -> 编译生成代码和手写 C++
  -> 链接 dialect library 和编译器工具
  -> 运行 MLIR 测试
```

遇到 ODS 问题时，可以直接查看生成结果：

```bash
mlir-tblgen --gen-op-decls \
  -I /path/to/mlir/include MyOps.td

mlir-tblgen --gen-op-defs \
  -I /path/to/mlir/include MyOps.td
```

这样可以判断问题来自：

```text
.td 定义
类型约束
assemblyFormat
生成代码
C++ 注册或链接
```

## 9. 课件练习命令在流程中的位置

```bash
git checkout exercises/day-2-session-1
git pull
cmake --build build --target check-tutorial
```

它们分别表示：

```text
切换到课程指定练习分支
更新远程代码
构建并运行课程定义的检查目标
```

这不是 ODS 语法，而是课程仓库的开发和测试流程。通常对应的文件关系是：

```text
ListOps.td
  你修改的 ODS/TableGen 定义

ListOps.h.inc / ListOps.cpp.inc
  mlir-tblgen 自动生成的 C++ 片段

ListOps.cpp
  手写 verifier、parser、printer 等逻辑

test/list/roundtrip.mlir
  MLIR 文本解析和打印测试

check-tutorial
  课程仓库提供的构建和测试目标
```

## 10. 最后总结

```text
MLIR：
  提供可扩展的编译器 IR、Dialect、Operation、Region 和 Pass 框架

ODS：
  用声明式方式描述 MLIR Operation

TableGen：
  读取 ODS 描述并生成 C++ 代码

C++：
  编译生成代码，注册 Operation，实现复杂验证、优化和 lowering

.mlir：
  使用这些 Operation 表示程序
```

新增一个 Operation 时，最典型的顺序是：

```text
设计语义
  -> 编写 .td
  -> TableGen 生成 C++
  -> C++ 包含并注册
  -> 补充 verifier / pass
  -> 编写 .mlir 测试
  -> 构建并运行测试
```

官方参考：

- [MLIR Operation Definition Specification](https://mlir.llvm.org/docs/DefiningDialects/Operations/)
- [MLIR Creating a Dialect](https://mlir.llvm.org/docs/Tutorials/CreatingADialect/)
- [MLIR Toy Tutorial](https://mlir.llvm.org/docs/Tutorials/Toy/Ch-2/)
