# Step 3：编译并链接最终的 `tutorial-opt`

第二步已经把自定义 dialect 和 operation 编译成：

```text
wdir/build/tutorial/dialect/libTutorialDialect.a
```

本步骤再编译一个手写的 MLIR 驱动程序，并把它与第二步的静态库、
MLIR/LLVM 库链接起来，生成最终可执行文件：

```text
wdir/tutorial/tutorial-opt.cpp
        +
wdir/build/tutorial/dialect/libTutorialDialect.a
        +
MLIR_ROOT/lib/libMLIR.so
MLIR_ROOT/lib/libLLVM.so
        |
        | c++
        v
wdir/build/tutorial/tutorial-opt
```

本步骤不调用 `mlir-tblgen`，也不调用 CMake 或 Ninja。

下面假设项目顶层目录名是 `wdir`，所有命令都从 `wdir` 执行。

## 0. 第二步完成后的输入

第二步完成后，目录至少应该是：

```text
wdir/
├── .venv/                         # 已安装 MLIR wheel 的 Python 环境
├── build/
│   └── tutorial/
│       ├── dialect/
│       │   ├── libTutorialDialect.a
│       │   ├── TutorialDialect.cpp.o
│       │   ├── TutorialDialect.cpp.o.d
│       │   ├── TutorialOps.cpp.inc
│       │   ├── TutorialOps.cpp.inc.d
│       │   ├── TutorialOps.h.inc
│       │   ├── TutorialOps.h.inc.d
│       │   ├── TutorialOpsDialect.cpp.inc
│       │   ├── TutorialOpsDialect.cpp.inc.d
│       │   ├── TutorialOpsDialect.h.inc
│       │   └── TutorialOpsDialect.h.inc.d
│       └── # 本步骤的输出也放在这里
└── tutorial/
    └── dialect/
        ├── TutorialDialect.cpp
        ├── TutorialDialect.h
        └── TutorialDialect.td
```

本步骤需要的项目源码输入：

```text
手写输入：
  wdir/tutorial/tutorial-opt.cpp

第二步生成的直接链接输入：
  wdir/build/tutorial/dialect/libTutorialDialect.a
```

本步骤还依赖 MLIR wheel 中的头文件和共享库：

```text
MLIR_ROOT/include/mlir/**/*.h
MLIR_ROOT/lib/libMLIR.so
MLIR_ROOT/lib/libLLVM.so
```

`tutorial-opt.cpp` 直接 include `TutorialDialect.h`；而
`TutorialDialect.h` 又 include 第一步生成的 `.inc` 文件。因此这些
`.inc` 仍然是 C++ 编译时的间接输入：

```text
wdir/build/tutorial/dialect/TutorialOps.h.inc
wdir/build/tutorial/dialect/TutorialOps.cpp.inc
wdir/build/tutorial/dialect/TutorialOpsDialect.h.inc
wdir/build/tutorial/dialect/TutorialOpsDialect.cpp.inc
```

## 1. 准备编译环境和输出目录

从 `wdir` 执行：

```bash
cd wdir

# 激活项目虚拟环境。
# MLIR wheel 的头文件和库位于这个环境的 site-packages 中。
source .venv/bin/activate

# 取得 MLIR wheel 的安装根目录。
# 后面的命令把它记作 MLIR_ROOT。
# 例如：
#   MLIR_ROOT/include/mlir/IR/MLIRContext.h
#   MLIR_ROOT/lib/libMLIR.so
MLIR_ROOT="$(python -m mlir_wheel --root-dir)"

# 创建最终工具和 tutorial-opt.cpp 目标文件的输出目录。
# mkdir -p 不会删除 build/tutorial/dialect 中第一步和第二步已有的文件。
mkdir -p build/tutorial

# 检查本步骤需要的系统工具。
command -v c++
command -v ld
```

此时目录为：

```text
wdir/
├── .venv/
├── build/
│   └── tutorial/
│       └── dialect/
│           ├── libTutorialDialect.a
│           ├── TutorialDialect.cpp.o
│           ├── TutorialDialect.cpp.o.d
│           ├── TutorialOps.cpp.inc
│           ├── TutorialOps.cpp.inc.d
│           ├── TutorialOps.h.inc
│           ├── TutorialOps.h.inc.d
│           ├── TutorialOpsDialect.cpp.inc
│           ├── TutorialOpsDialect.cpp.inc.d
│           ├── TutorialOpsDialect.h.inc
│           └── TutorialOpsDialect.h.inc.d
└── tutorial/
    ├── tutorial-opt.cpp              # 本步骤手写输入
    └── dialect/
        ├── TutorialDialect.cpp
        ├── TutorialDialect.h
        └── TutorialDialect.td
```

## 2. 手写 `tutorial-opt.cpp`

本步骤需要自己编写的源文件是：

```text
wdir/tutorial/tutorial-opt.cpp
```

完整内容如下，解释全部写在 C++ 注释中：

```cpp
//===- tutorial-opt.cpp - Tutorial MLIR Optimizer Driver ------------------===//
//
// A simple mlir-opt-like tool for the tutorial.
//
//===----------------------------------------------------------------------===//

#include "mlir/IR/MLIRContext.h"
// 通过编译命令中的：
//   -I"$MLIR_ROOT/include"
// 找到：
//   $MLIR_ROOT/include/mlir/IR/MLIRContext.h
// MLIRContext 是 MLIR 对象、dialect 和 operation 所属的上下文。

#include "mlir/InitAllDialects.h"
// 声明 registerAllDialects。
// 该函数用于把 MLIR 自带的 dialect 加入 DialectRegistry。

#include "mlir/InitAllExtensions.h"
// 声明 registerAllExtensions。
// 该函数用于注册 MLIR 自带的 extensions。

#include "mlir/InitAllPasses.h"
// 声明 registerAllPasses。
// 该函数用于注册 MLIR 自带的 pass。

#include "mlir/Tools/mlir-opt/MlirOptMain.h"
// 声明 MlirOptMain 和 asMainReturnCode。
// MlirOptMain 提供类似 mlir-opt 的命令行、MLIR 读取、解析和打印流程。

#include "dialect/TutorialDialect.h"
// 通过编译命令中的：
//   -I"$PWD/tutorial"
// 找到：
//   $PWD/tutorial/dialect/TutorialDialect.h
//
// TutorialDialect.h 还会间接 include：
//   $PWD/build/tutorial/dialect/TutorialOpsDialect.h.inc
//   $PWD/build/tutorial/dialect/TutorialOps.h.inc
// 因此本文件可以使用 TutorialDialect 类型。

int main(int argc, char **argv) {
  mlir::DialectRegistry registry;
  // 创建一个 dialect 注册表。
  // 后面会向其中加入标准 dialect 和 tutorial dialect。

  mlir::registerAllDialects(registry);
  // 注册 MLIR 提供的标准 dialect。
  // 例如输入文件中的 func.func 才能被识别。

  mlir::registerAllPasses();
  // 注册 MLIR 提供的所有 pass。
  // 没有指定 pass 时，工具仍然可以完成读取和打印。

  mlir::registerAllExtensions(registry);
  // 注册 MLIR extensions。

  registry.insert<mlir::tutorial::TutorialDialect>();
  // 注册本项目的 tutorial dialect。
  // TutorialDialect::initialize() 会进一步注册 ConstantOp。
  // 因此 tutorial.constant 才能被 parser 识别。

  return mlir::asMainReturnCode(
      mlir::MlirOptMain(argc, argv, "Tutorial optimizer driver\n", registry));
  // 启动类似 mlir-opt 的主流程：
  //   读取命令行参数；
  //   读取 MLIR 输入文件；
  //   使用 registry 中的 dialect 解析输入；
  //   按要求运行 pass；
  //   将结果打印到标准输出。
  //
  // "Tutorial optimizer driver\n" 是工具帮助信息中显示的说明字符串。
}
```

这个文件是本步骤唯一需要自己手写的项目源文件。它的职责不是定义
`ConstantOp`，而是提供程序入口和注册表：

```text
tutorial-opt.cpp
    = main()
    + 注册标准 dialect
    + 注册标准 pass
    + 注册 extensions
    + 注册 TutorialDialect
    + 调用 MlirOptMain
```

创建文件后目录增加：

```text
wdir/
├── .venv/
├── build/
│   └── tutorial/
│       └── dialect/
│           ├── libTutorialDialect.a
│           ├── TutorialDialect.cpp.o
│           ├── TutorialDialect.cpp.o.d
│           ├── TutorialOps.cpp.inc
│           ├── TutorialOps.cpp.inc.d
│           ├── TutorialOps.h.inc
│           ├── TutorialOps.h.inc.d
│           ├── TutorialOpsDialect.cpp.inc
│           ├── TutorialOpsDialect.cpp.inc.d
│           ├── TutorialOpsDialect.h.inc
│           └── TutorialOpsDialect.h.inc.d
└── tutorial/
    ├── tutorial-opt.cpp              # 本步骤手写输入
    └── dialect/
        ├── TutorialDialect.cpp
        ├── TutorialDialect.h
        └── TutorialDialect.td
```

## 3. 编译 `tutorial-opt.cpp`

### 输入和输出

```text
主输入：
  wdir/tutorial/tutorial-opt.cpp

间接输入：
  wdir/tutorial/dialect/TutorialDialect.h
  wdir/build/tutorial/dialect/TutorialOps.h.inc
  wdir/build/tutorial/dialect/TutorialOpsDialect.h.inc
  MLIR_ROOT/include/mlir/**/*.h

输出：
  wdir/build/tutorial/tutorial-opt.cpp.o
  wdir/build/tutorial/tutorial-opt.cpp.o.d
```

### 调用工具

调用系统 C++ 编译器：

```bash
command -v c++
c++ --version
```

```text
# c++ 通常是系统 GCC 或 Clang 的编译器驱动。
# 它会依次调用预处理器、编译器和汇编器。
# 本次使用 -c，所以不会调用链接阶段，也不会生成最终可执行文件。
```

### 完整编译命令

```bash
c++ \
  -std=c++17 \
  -fPIC \
  -I"$MLIR_ROOT/include" \
  -I"$PWD/tutorial" \
  -I"$PWD/build/tutorial" \
  -MD \
  -MF "$PWD/build/tutorial/tutorial-opt.cpp.o.d" \
  -MT "$PWD/build/tutorial/tutorial-opt.cpp.o" \
  -c "$PWD/tutorial/tutorial-opt.cpp" \
  -o "$PWD/build/tutorial/tutorial-opt.cpp.o"
```

```text
# c++：
#   调用系统 C++ 编译器。
#
# -std=c++17：
#   使用 C++17 语言标准。
#
# -fPIC：
#   生成位置无关代码；与前一步编译 dialect 时的设置保持一致。
#
# -I"$MLIR_ROOT/include"：
#   MLIR C++ 头文件的搜索根目录。
#
#   例如：
#     #include "mlir/IR/MLIRContext.h"
#   解析为：
#     $MLIR_ROOT/include/mlir/IR/MLIRContext.h
#
# -I"$PWD/tutorial"：
#   项目源码头文件的搜索根目录。
#
#   例如：
#     #include "dialect/TutorialDialect.h"
#   解析为：
#     $PWD/tutorial/dialect/TutorialDialect.h
#
# -I"$PWD/build/tutorial"：
#   第一、二步生成的 .inc 文件的搜索根目录。
#
#   例如：
#     #include "dialect/TutorialOps.h.inc"
#   解析为：
#     $PWD/build/tutorial/dialect/TutorialOps.h.inc
#
#   include 字符串中已经包含 dialect/，所以 -I 要指向 tutorial，
#   不能只指向 tutorial/dialect。
#
# -MD：
#   让编译器记录本次 .o 文件依赖的头文件和 .inc 文件。
#
# -MF "$PWD/build/tutorial/tutorial-opt.cpp.o.d"：
#   指定 C++ 依赖清单的输出文件。
#
# -MT "$PWD/build/tutorial/tutorial-opt.cpp.o"：
#   指定 .d 文件中对应的目标文件名。
#
# -c：
#   只编译，不链接。
#
# "$PWD/tutorial/tutorial-opt.cpp"：
#   本次编译的主输入文件。
#
# -o "$PWD/build/tutorial/tutorial-opt.cpp.o"：
#   指定目标文件的输出路径。
```

这条命令的内部数据流是：

```text
tutorial-opt.cpp
        |
        | 预处理：展开 #include 和间接包含的 .inc
        v
完整 C++ 翻译单元
        |
        | 编译、汇编
        v
build/tutorial/tutorial-opt.cpp.o
```

注意，下面这些文件不会作为独立命令行输入传给 `c++`：

```text
TutorialDialect.h
TutorialOps.h.inc
TutorialOpsDialect.h.inc
```

它们是由源文件中的 `#include` 递归读入的。`TutorialDialect.cpp` 和
`TutorialDialect.cpp.o` 已经在第二步被编译并打包到
`libTutorialDialect.a`，本次不再重新编译它们。

编译成功后目录增加：

```text
wdir/
├── .venv/
├── build/
│   └── tutorial/
│       ├── tutorial-opt.cpp.o       # 本次 C++ 编译输出
│       ├── tutorial-opt.cpp.o.d     # 本次 C++ 依赖清单
│       └── dialect/
│           ├── libTutorialDialect.a
│           ├── TutorialDialect.cpp.o
│           ├── TutorialDialect.cpp.o.d
│           ├── TutorialOps.cpp.inc
│           ├── TutorialOps.cpp.inc.d
│           ├── TutorialOps.h.inc
│           ├── TutorialOps.h.inc.d
│           ├── TutorialOpsDialect.cpp.inc
│           ├── TutorialOpsDialect.cpp.inc.d
│           ├── TutorialOpsDialect.h.inc
│           └── TutorialOpsDialect.h.inc.d
└── tutorial/
    ├── tutorial-opt.cpp
    └── dialect/
        ├── TutorialDialect.cpp
        ├── TutorialDialect.h
        └── TutorialDialect.td
```

## 4. 链接生成 `tutorial-opt`

### 输入和输出

```text
输入：
  wdir/build/tutorial/tutorial-opt.cpp.o
  wdir/build/tutorial/dialect/libTutorialDialect.a
  MLIR_ROOT/lib/libMLIR.so
  MLIR_ROOT/lib/libLLVM.so

输出：
  wdir/build/tutorial/tutorial-opt
```

### 链接工具

仍然调用 `c++`，但这次不使用 `-c`。因此 `c++` 会调用系统链接器
完成最终链接：

```bash
command -v ld
```

```text
# 不建议直接手写 ld 命令。
# c++ 作为编译器驱动会自动补充 C++ 标准库、libc、启动文件和系统运行库。
# c++ 最终会在内部调用 ld。
```

### 完整链接命令

```bash
c++ \
  "$PWD/build/tutorial/tutorial-opt.cpp.o" \
  "$PWD/build/tutorial/dialect/libTutorialDialect.a" \
  -L"$MLIR_ROOT/lib" \
  -Wl,-rpath,"$MLIR_ROOT/lib" \
  -lMLIR \
  -lLLVM \
  -o "$PWD/build/tutorial/tutorial-opt"
```

```text
# "$PWD/build/tutorial/tutorial-opt.cpp.o"：
#   包含 main() 和对 MLIR API 的调用。
#
# "$PWD/build/tutorial/dialect/libTutorialDialect.a"：
#   包含 TutorialDialect、ConstantOp 及其生成实现。
#
# -L"$MLIR_ROOT/lib"：
#   把 MLIR wheel 的库目录加入链接器搜索路径。
#
#   例如：
#     -lMLIR
#   会在这里查找：
#     $MLIR_ROOT/lib/libMLIR.so
#     或 $MLIR_ROOT/lib/libMLIR.a
#
# -Wl,-rpath,"$MLIR_ROOT/lib"：
#   把 MLIR wheel 的库目录写入最终可执行文件的运行时搜索路径。
#   这样执行 tutorial-opt 时，动态加载器可以找到 libMLIR.so。
#   -Wl,xxx 表示把后面的选项传给底层链接器，而不是 c++ 自己解析。
#
# -lMLIR：
#   链接 MLIR 共享库。
#   它提供 MlirOptMain、DialectRegistry、MLIRContext 等实现。
#
# -lLLVM：
#   链接 LLVM 共享库。
#   MLIR 的实现依赖 LLVM 基础设施。
#
# 链接顺序：
#   tutorial-opt.cpp.o
#   libTutorialDialect.a
#   -lMLIR
#   -lLLVM
#   静态库放在引用它的目标文件之后，再放它依赖的 MLIR/LLVM 库，
#   这样链接器可以按顺序解析未定义符号。
#
# -o "$PWD/build/tutorial/tutorial-opt"：
#   指定最终可执行文件的输出路径。
```

这条命令的内部数据流是：

```text
tutorial-opt.cpp.o
        +
libTutorialDialect.a
        +
libMLIR.so
        +
libLLVM.so
        |
        | c++ 链接驱动 -> ld
        v
build/tutorial/tutorial-opt
```

`libMLIR.so` 和 `libLLVM.so` 来自 MLIR wheel，不是第二步生成的项目文件。
`libTutorialDialect.a` 则来自第二步。

链接成功后目录增加：

```text
wdir/
├── .venv/
├── build/
│   └── tutorial/
│       ├── tutorial-opt                  # 本步骤最终输出
│       ├── tutorial-opt.cpp.o
│       ├── tutorial-opt.cpp.o.d
│       └── dialect/
│           ├── libTutorialDialect.a
│           ├── TutorialDialect.cpp.o
│           ├── TutorialDialect.cpp.o.d
│           ├── TutorialOps.cpp.inc
│           ├── TutorialOps.cpp.inc.d
│           ├── TutorialOps.h.inc
│           ├── TutorialOps.h.inc.d
│           ├── TutorialOpsDialect.cpp.inc
│           ├── TutorialOpsDialect.cpp.inc.d
│           ├── TutorialOpsDialect.h.inc
│           └── TutorialOpsDialect.h.inc.d
└── tutorial/
    ├── tutorial-opt.cpp
    └── dialect/
        ├── TutorialDialect.cpp
        ├── TutorialDialect.h
        └── TutorialDialect.td
```

## 5. Step 3 完成后的完整目录

```text
wdir/
├── .venv/                              # Python/MLIR 依赖环境
├── build/
│   └── tutorial/
│       ├── tutorial-opt                # Step 3 最终可执行文件
│       ├── tutorial-opt.cpp.o          # tutorial-opt.cpp 的目标文件
│       ├── tutorial-opt.cpp.o.d        # C++ 依赖清单
│       └── dialect/
│           ├── libTutorialDialect.a    # Step 2 静态库
│           ├── TutorialDialect.cpp.o   # Step 2 目标文件
│           ├── TutorialDialect.cpp.o.d
│           ├── TutorialOps.cpp.inc     # Step 1 operation 定义
│           ├── TutorialOps.cpp.inc.d
│           ├── TutorialOps.h.inc       # Step 1 operation 声明
│           ├── TutorialOps.h.inc.d
│           ├── TutorialOpsDialect.cpp.inc
│           ├── TutorialOpsDialect.cpp.inc.d
│           ├── TutorialOpsDialect.h.inc
│           └── TutorialOpsDialect.h.inc.d
└── tutorial/
    ├── tutorial-opt.cpp                # Step 3 手写输入
    ├── dialect/
    │   ├── TutorialDialect.cpp         # Step 2 手写输入
    │   ├── TutorialDialect.h           # Step 2 手写输入
    │   └── TutorialDialect.td          # Step 1 手写输入
```

此时 `build/tutorial/tutorial-opt` 已经是链接完成的 ELF 可执行文件，
但本步骤只负责生成它，不在本步骤执行运行时验证；运行和测试统一放在
下一步。

## 6. 本步骤的完整数据流

```text
wdir/tutorial/tutorial-opt.cpp
        +
wdir/tutorial/dialect/TutorialDialect.h
        +
wdir/build/tutorial/dialect/TutorialOps.h.inc
wdir/build/tutorial/dialect/TutorialOpsDialect.h.inc
        +
MLIR_ROOT/include/mlir/**/*.h
        |
        | c++ -std=c++17 ... -c tutorial-opt.cpp
        v
wdir/build/tutorial/tutorial-opt.cpp.o
        |
        + wdir/build/tutorial/dialect/libTutorialDialect.a
        + MLIR_ROOT/lib/libMLIR.so
        + MLIR_ROOT/lib/libLLVM.so
        |
        | c++ ... -lMLIR -lLLVM
        v
wdir/build/tutorial/tutorial-opt
```

本步骤需要自己写的文件：

```text
wdir/tutorial/tutorial-opt.cpp
```

本步骤使用第二步已有的文件：

```text
wdir/build/tutorial/dialect/libTutorialDialect.a
```

本步骤生成的文件：

```text
wdir/build/tutorial/tutorial-opt.cpp.o
wdir/build/tutorial/tutorial-opt.cpp.o.d
wdir/build/tutorial/tutorial-opt
```

下一步将单独处理 `build/tutorial/tutorial-opt` 的运行和测试。
