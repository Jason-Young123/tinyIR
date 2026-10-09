# Step 2：把 TableGen 生成代码编译成 `TutorialDialect` 静态库

第一步已经用 `mlir-tblgen` 生成了四个 C++ 代码片段。本步骤把这些
`.inc` 文件嵌入两个手写的 C++ 文件中，然后编译并打包成静态库。

```text
wdir/build/tutorial/dialect/*.inc
        +
wdir/tutorial/dialect/TutorialDialect.h
wdir/tutorial/dialect/TutorialDialect.cpp
        |
        | c++ -c
        v
TutorialDialect.cpp.o
        |
        | ar
        v
libTutorialDialect.a
```

本步骤不调用 `mlir-tblgen`，也不编译 `tutorial-opt.cpp`。它只构建
自定义 dialect 的静态库。

下面假设项目顶层目录名是 `wdir`，所有命令都从 `wdir` 执行。

## 0. 第一步完成后的输入

第一步完成后，目录至少应该是：

```text
wdir/
├── .venv/                         # 已安装 MLIR wheel 和 C++ 生成工具的环境
├── build/
│   └── tutorial/
│       └── dialect/
│           ├── TutorialOps.cpp.inc
│           ├── TutorialOps.cpp.inc.d
│           ├── TutorialOps.h.inc
│           ├── TutorialOps.h.inc.d
│           ├── TutorialOpsDialect.cpp.inc
│           ├── TutorialOpsDialect.cpp.inc.d
│           ├── TutorialOpsDialect.h.inc
│           └── TutorialOpsDialect.h.inc.d
└── tutorial/
    └── dialect/
        └── TutorialDialect.td             # 第一步手写的 TableGen 输入
```

本步骤的直接输入是：

```text
手写输入：
  wdir/tutorial/dialect/TutorialDialect.h
  wdir/tutorial/dialect/TutorialDialect.cpp

第一步生成的间接输入：
  wdir/build/tutorial/dialect/TutorialOps.h.inc
  wdir/build/tutorial/dialect/TutorialOps.cpp.inc
  wdir/build/tutorial/dialect/TutorialOpsDialect.h.inc
  wdir/build/tutorial/dialect/TutorialOpsDialect.cpp.inc

MLIR wheel 提供的间接输入：
  MLIR_ROOT/include/mlir/**/*.h
  MLIR_ROOT/lib/...
```

`TutorialDialect.td` 在本步骤不直接传给 C++ 编译器。它已经在第一步
被 `mlir-tblgen` 转成 `.inc` 文件；C++ 编译器只通过 `#include` 读取
这些生成文件。

## 1. 准备编译环境和目录

从 `wdir` 执行：

```bash
cd wdir

# 激活项目虚拟环境。
# 本步骤的 C++ 编译器通常来自操作系统；虚拟环境主要提供
# MLIR wheel 的头文件、库文件和辅助工具。
source .venv/bin/activate

# 取得 MLIR wheel 的安装根目录。
# 后面把它记作 MLIR_ROOT，例如：
#   MLIR_ROOT/include/mlir/IR/Dialect.h
MLIR_ROOT="$(python -m mlir_wheel --root-dir)"

# 创建 C++ 目标文件和静态库的输出目录。
mkdir -p build/tutorial/dialect

# 确认操作系统提供的工具可用。
command -v c++
command -v ar
command -v ranlib
```

此时目录为：

```text
wdir/
├── .venv/
├── build/
│   └── tutorial/
│       └── dialect/
│           ├── TutorialOps.cpp.inc
│           ├── TutorialOps.cpp.inc.d
│           ├── TutorialOps.h.inc
│           ├── TutorialOps.h.inc.d
│           ├── TutorialOpsDialect.cpp.inc
│           ├── TutorialOpsDialect.cpp.inc.d
│           ├── TutorialOpsDialect.h.inc
│           └── TutorialOpsDialect.h.inc.d
└── tutorial/
    └── dialect/
        └── TutorialDialect.td     # 已有的 TableGen 输入
```

上面的四个 `.inc` 和四个 `.inc.d` 是第一步留下的文件；
`mkdir -p build/tutorial/dialect` 只是在目录不存在时创建目录，
不会删除或清空其中已有的文件。第二步后面的 `#include` 正是依赖这些
`.inc` 文件。如果它们不存在，必须先完成 `step1.md` 中的四次
`mlir-tblgen` 调用，不能直接进入本步骤。

## 2. 手写 `TutorialDialect.h`

创建文件：

```text
wdir/tutorial/dialect/TutorialDialect.h
```

完整内容如下。解释全部放在 C++ 注释中：

```cpp
#ifndef TUTORIAL_TUTORIAL_DIALECT_H
#define TUTORIAL_TUTORIAL_DIALECT_H
// 头文件保护，避免 TutorialDialect.h 被同一个翻译单元重复展开。

#include "mlir/Bytecode/BytecodeOpInterface.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/Dialect.h"
#include "mlir/IR/OpDefinition.h"
#include "mlir/IR/OpImplementation.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"
// 这些是 MLIR wheel 提供的真实 C++ 头文件。
// 编译命令中的：
//   -I"$MLIR_ROOT/include"
// 会让：
//   #include "mlir/IR/Dialect.h"
// 解析为：
//   $MLIR_ROOT/include/mlir/IR/Dialect.h

#include "dialect/TutorialOpsDialect.h.inc"
// 这是第一步生成的 dialect 声明片段。
// 编译命令中的：
//   -I"$PWD/build/tutorial"
// 会让这条 include 解析为：
//   $PWD/build/tutorial/dialect/TutorialOpsDialect.h.inc
//
// include 字符串里已经有 dialect/，所以 -I 要指向 tutorial，
// 不能只指向 tutorial/dialect；否则会查找多余的 dialect/dialect。

#define GET_OP_CLASSES
#include "dialect/TutorialOps.h.inc"
// 先定义 GET_OP_CLASSES，再 include 生成文件。
// TutorialOps.h.inc 会根据这个宏生成 ConstantOp 的完整 C++ 类声明。
// 这个 .inc 文件不是独立头文件，而是被预处理器嵌入本文件。
//
// 概念上，预处理后这里包含了类似：
//   namespace mlir::tutorial {
//   class ConstantOp : public mlir::Op<...> { ... };
//   }

#endif // TUTORIAL_TUTORIAL_DIALECT_H
```

此时目录增加：

```text
wdir/
├── .venv/
├── build/
│   └── tutorial/
│       └── dialect/
│           ├── TutorialOps.cpp.inc
│           ├── TutorialOps.cpp.inc.d
│           ├── TutorialOps.h.inc
│           ├── TutorialOps.h.inc.d
│           ├── TutorialOpsDialect.cpp.inc
│           ├── TutorialOpsDialect.cpp.inc.d
│           ├── TutorialOpsDialect.h.inc
│           └── TutorialOpsDialect.h.inc.d
└── tutorial/
    └── dialect/
        ├── TutorialDialect.h       # 本步骤手写的 C++ 头文件
        └── TutorialDialect.td
```

## 3. 手写 `TutorialDialect.cpp`

创建文件：

```text
wdir/tutorial/dialect/TutorialDialect.cpp
```

完整内容如下：

```cpp
#include "dialect/TutorialDialect.h"
// 编译命令中的：
//   -I"$PWD/tutorial"
// 让这条 include 解析为：
//   $PWD/tutorial/dialect/TutorialDialect.h
//
// 这个头文件又会继续 include：
//   TutorialOpsDialect.h.inc
//   TutorialOps.h.inc
// 因此 C++ 编译器会间接看到 dialect 和 ConstantOp 的声明。

#include "mlir/IR/DialectImplementation.h"
// 通过 -I"$MLIR_ROOT/include" 找到 MLIR 的 dialect 实现接口。

using namespace mlir;
using namespace mlir::tutorial;
// 允许下面直接使用 TutorialDialect 和 ConstantOp，
// 而不必每次写完整的 mlir::tutorial::TutorialDialect。

#include "dialect/TutorialOpsDialect.cpp.inc"
// 通过 -I"$PWD/build/tutorial" 找到：
//   $PWD/build/tutorial/dialect/TutorialOpsDialect.cpp.inc
//
// 这个生成片段提供 TutorialDialect 的构造函数和析构函数定义。
// 构造函数内部会调用 initialize()。

void TutorialDialect::initialize() {
  addOperations<
#define GET_OP_LIST
#include "dialect/TutorialOps.cpp.inc"
      >();
}
// 这里先定义 GET_OP_LIST，再 include operation 定义片段。
// TutorialOps.cpp.inc 的 GET_OP_LIST 分支只展开为：
//   ::mlir::tutorial::ConstantOp
//
// 因此预处理后大致等价于：
//   void TutorialDialect::initialize() {
//     addOperations<::mlir::tutorial::ConstantOp>();
//   }
//
// 这一步把 ConstantOp 注册到 TutorialDialect。

#define GET_OP_CLASSES
#include "dialect/TutorialOps.cpp.inc"
// 再次 include 同一个生成文件，但这次定义 GET_OP_CLASSES。
// 这个分支生成 ConstantOp 的方法实现，包括：
//   - builder；
//   - parser/printer；
//   - verifier；
//   - 属性和结果访问器；
//   - TableGen 根据 assemblyFormat 推导出的文本处理逻辑。
//
// 同一个 .inc 可以被不同宏控制，分别提供 operation 类型列表和
// operation 完整定义。
```

此时目录增加：

```text
wdir/
├── .venv/
├── build/
│   └── tutorial/
│       └── dialect/
│           ├── TutorialOps.cpp.inc
│           ├── TutorialOps.cpp.inc.d
│           ├── TutorialOps.h.inc
│           ├── TutorialOps.h.inc.d
│           ├── TutorialOpsDialect.cpp.inc
│           ├── TutorialOpsDialect.cpp.inc.d
│           ├── TutorialOpsDialect.h.inc
│           └── TutorialOpsDialect.h.inc.d
└── tutorial/
    └── dialect/
        ├── TutorialDialect.cpp      # 本步骤手写的 C++ 源文件
        ├── TutorialDialect.h        # 本步骤手写的 C++ 头文件
        └── TutorialDialect.td
```

此时两个手写文件和四个生成文件的关系是：

```text
TutorialDialect.h
  ├── TutorialOpsDialect.h.inc
  └── TutorialOps.h.inc

TutorialDialect.cpp
  ├── TutorialDialect.h
  ├── TutorialOpsDialect.cpp.inc
  └── TutorialOps.cpp.inc
```

## 4. 编译 `TutorialDialect.cpp`

### 输入和输出

```text
主输入：
  wdir/tutorial/dialect/TutorialDialect.cpp

间接输入：
  wdir/tutorial/dialect/TutorialDialect.h
  wdir/build/tutorial/dialect/TutorialOps.h.inc
  wdir/build/tutorial/dialect/TutorialOps.cpp.inc
  wdir/build/tutorial/dialect/TutorialOpsDialect.h.inc
  wdir/build/tutorial/dialect/TutorialOpsDialect.cpp.inc
  MLIR_ROOT/include/mlir/**/*.h

输出：
  wdir/build/tutorial/dialect/TutorialDialect.cpp.o
  wdir/build/tutorial/dialect/TutorialDialect.cpp.o.d
```

### 调用工具

本步骤调用系统 C++ 编译器：

```bash
command -v c++
c++ --version
```

`c++` 通常是系统 `g++` 或 `clang++` 的兼容入口。本项目当前环境中
使用系统 GCC C++ 编译器；它不是 `mlir-tblgen`，也不是 Python 工具。

### 逐行执行的完整命令

```bash
c++ \
  -std=c++17 \
  -fPIC \
  -I"$MLIR_ROOT/include" \
  -I"$PWD/tutorial" \
  -I"$PWD/build/tutorial" \
  -MD \
  -MF "$PWD/build/tutorial/dialect/TutorialDialect.cpp.o.d" \
  -MT "$PWD/build/tutorial/dialect/TutorialDialect.cpp.o" \
  -c "$PWD/tutorial/dialect/TutorialDialect.cpp" \
  -o "$PWD/build/tutorial/dialect/TutorialDialect.cpp.o"
```

```text
# c++：
#   调用系统 C++ 编译器。
#
# -std=c++17：
#   使用 C++17 语法和标准库规则。
#
# -fPIC：
#   生成位置无关的目标代码；当前 MLIR/LLVM 构建规则使用该选项。
#
# -I"$MLIR_ROOT/include"：
#   查找 MLIR 的 C++ 头文件。
#   例如：
#     #include "mlir/IR/Dialect.h"
#   查找：
#     $MLIR_ROOT/include/mlir/IR/Dialect.h
#
# -I"$PWD/tutorial"：
#   查找项目手写头文件。
#   例如：
#     #include "dialect/TutorialDialect.h"
#   查找：
#     $PWD/tutorial/dialect/TutorialDialect.h
#
# -I"$PWD/build/tutorial"：
#   查找第一步生成的 .inc 文件。
#   例如：
#     #include "dialect/TutorialOps.h.inc"
#   查找：
#     $PWD/build/tutorial/dialect/TutorialOps.h.inc
#
# -MD：
#   让编译器记录当前 .o 文件依赖的头文件和 .inc 文件。
#
# -MF <file>：
#   把依赖清单写到指定的 .d 文件。
#
# -MT <target>：
#   指定 .d 文件中对应的目标文件名。
#
# -c：
#   只编译，不链接；因此当前命令不会生成可执行文件。
#
# "$PWD/tutorial/dialect/TutorialDialect.cpp"：
#   C++ 编译的主输入文件。
#   TutorialDialect.td 不会作为命令行输入传给 c++。
#
# -o "$PWD/build/tutorial/dialect/TutorialDialect.cpp.o"：
#   指定目标文件输出位置。
```

C++ 编译器实际处理过程是：

```text
TutorialDialect.cpp
        |
        | 预处理器展开 #include、#define 和生成的 .inc
        v
一个完整的 C++ 翻译单元
        |
        | c++ -c
        v
TutorialDialect.cpp.o
```

`TutorialDialect.h`、MLIR 头文件和 `.inc` 文件不会作为多个独立的源文件
分别编译。它们会被预处理器通过 `#include` 合并进
`TutorialDialect.cpp` 对应的翻译单元。

编译成功后，目录增加：

```text
wdir/
├── .venv/
├── build/
│   └── tutorial/
│       └── dialect/
│           ├── TutorialDialect.cpp.o       # C++ 编译生成的目标文件
│           ├── TutorialDialect.cpp.o.d     # C++ 头文件依赖清单
│           ├── TutorialOps.cpp.inc
│           ├── TutorialOps.cpp.inc.d
│           ├── TutorialOps.h.inc
│           ├── TutorialOps.h.inc.d
│           ├── TutorialOpsDialect.cpp.inc
│           ├── TutorialOpsDialect.cpp.inc.d
│           ├── TutorialOpsDialect.h.inc
│           └── TutorialOpsDialect.h.inc.d
└── tutorial/
    └── dialect/
        ├── TutorialDialect.cpp
        ├── TutorialDialect.h
        └── TutorialDialect.td
```

`TutorialDialect.cpp.o` 是 ELF relocatable object，不是可以直接运行的
程序。它里面包含了自定义 dialect 和 `ConstantOp` 的机器码，但还没有
和 MLIR 库、系统库以及最终程序的 `main()` 链接。

## 5. 打包成 `libTutorialDialect.a`

### 输入和输出

```text
输入：
  wdir/build/tutorial/dialect/TutorialDialect.cpp.o

输出：
  wdir/build/tutorial/dialect/libTutorialDialect.a
```

### 调用工具

静态库由 GNU Binutils 提供的 `ar` 创建：

```bash
ar --version
```

本步骤也可以使用 `ranlib` 单独生成静态库符号索引。

### 完整命令

```bash
ar rcs \
  "$PWD/build/tutorial/dialect/libTutorialDialect.a" \
  "$PWD/build/tutorial/dialect/TutorialDialect.cpp.o"
```

```text
# ar：
#   把多个 .o 目标文件归档成静态库。
#
# r：
#   把目标文件加入 archive；若同名成员已存在则替换。
#
# c：
#   如果静态库文件不存在则创建它。
#
# s：
#   创建静态库符号索引，使链接器可以快速查找其中的符号。
#
# 第一个路径：
#   静态库输出文件。
#
# 第二个路径：
#   要被打包进静态库的目标文件。
```

`ar rcs` 已经包含了符号索引操作，因此通常不需要再运行 `ranlib`。如果
希望把两个动作分开，也可以执行：

```bash
ar rc \
  "$PWD/build/tutorial/dialect/libTutorialDialect.a" \
  "$PWD/build/tutorial/dialect/TutorialDialect.cpp.o"

ranlib "$PWD/build/tutorial/dialect/libTutorialDialect.a"
```

```text
# ar rc：
#   只创建/更新 archive，不主动创建符号索引。
#
# ranlib：
#   为静态库生成或更新符号索引。
```

检查输出：

```bash
file build/tutorial/dialect/TutorialDialect.cpp.o
file build/tutorial/dialect/libTutorialDialect.a
```

```text
# 第一个输出应该表明 .o 是 ELF relocatable object。
# 第二个输出应该表明 .a 是 ar archive。
#
# 这两个文件都不是最终的 tutorial-opt 可执行文件。
```

也可以查看静态库中包含的目标文件：

```bash
ar t build/tutorial/dialect/libTutorialDialect.a
```

预期包含：

```text
TutorialDialect.cpp.o
```

此时最终目录为：

```text
wdir/
├── .venv/
├── build/
│   └── tutorial/
│       └── dialect/
│           ├── libTutorialDialect.a       # 本步骤最终输出
│           ├── TutorialDialect.cpp.o       # c++ -c 输出
│           ├── TutorialDialect.cpp.o.d     # C++ 依赖清单
│           ├── TutorialOps.cpp.inc         # 第一步输出
│           ├── TutorialOps.cpp.inc.d
│           ├── TutorialOps.h.inc           # 第一步输出
│           ├── TutorialOps.h.inc.d
│           ├── TutorialOpsDialect.cpp.inc  # 第一步输出
│           ├── TutorialOpsDialect.cpp.inc.d
│           ├── TutorialOpsDialect.h.inc    # 第一步输出
│           └── TutorialOpsDialect.h.inc.d
└── tutorial/
    └── dialect/
        ├── TutorialDialect.cpp             # 本步骤手写输入
        ├── TutorialDialect.h               # 本步骤手写输入
        └── TutorialDialect.td              # 第一步手写输入
```

## 6. 本步骤的完整数据流

```text
wdir/tutorial/dialect/TutorialDialect.h
        +
wdir/tutorial/dialect/TutorialDialect.cpp
        +
wdir/build/tutorial/dialect/TutorialOps.h.inc
wdir/build/tutorial/dialect/TutorialOps.cpp.inc
wdir/build/tutorial/dialect/TutorialOpsDialect.h.inc
wdir/build/tutorial/dialect/TutorialOpsDialect.cpp.inc
        +
MLIR_ROOT/include/mlir/**/*.h
        |
        | c++ -std=c++17 ... -c TutorialDialect.cpp
        v
wdir/build/tutorial/dialect/TutorialDialect.cpp.o
        |
        | ar rcs libTutorialDialect.a TutorialDialect.cpp.o
        v
wdir/build/tutorial/dialect/libTutorialDialect.a
```

本步骤需要自己写的文件是：

```text
wdir/tutorial/dialect/TutorialDialect.h
wdir/tutorial/dialect/TutorialDialect.cpp
```

本步骤自动生成的文件是：

```text
wdir/build/tutorial/dialect/TutorialDialect.cpp.o
wdir/build/tutorial/dialect/TutorialDialect.cpp.o.d
wdir/build/tutorial/dialect/libTutorialDialect.a
```

下一步会使用这个静态库，再编译和链接手写的 `tutorial-opt.cpp`，生成
最终的 `tutorial-opt` 可执行文件。
