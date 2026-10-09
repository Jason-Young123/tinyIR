# Step 1：用 `TutorialDialect.td` 生成 MLIR C++ 代码

本步骤只做 TableGen 代码生成，不编译 C++，也不运行 MLIR。

```text
TutorialDialect.td
        |
        | mlir-tblgen
        v
TutorialOps.h.inc
TutorialOps.cpp.inc
TutorialOpsDialect.h.inc
TutorialOpsDialect.cpp.inc
```

下面假设项目顶层目录名是 `wdir`，所有相对路径都从 `wdir` 执行。

## 0. 准备工具

本步骤假设已经在 `wdir` 中创建好虚拟环境，并且项目依赖中包含
`mlir-wheel`。`mlir-tblgen` 是 MLIR 发布包中的已编译工具，不是本项目
自己编译出来的。

```bash
cd wdir

# 进入项目 Python 环境；如果已经激活，可以不重复执行。
source .venv/bin/activate

# 让 shell 可以直接使用命令名 mlir-tblgen。
# 本文按要求不展开 mlir-tblgen 的实际绝对路径。
export PATH="$(python -m mlir_wheel --bin-dir):$PATH"

# 查看 MLIR TableGen 的 include 根目录。
# 这个目录下面应该存在 mlir/IR/OpBase.td。
python -m mlir_wheel --include-dir

# 验证工具已经可以直接调用。
mlir-tblgen --version
```

`python -m mlir_wheel --include-dir` 输出的目录记作
`MLIR_INCLUDE_DIR`。如果不使用本项目的 wheel，也可以把下面命令中的
`$(python -m mlir_wheel --include-dir)` 替换为自己的 MLIR include 根目录。

`MLIR_INCLUDE_DIR` 必须是包含 `mlir/` 子目录的目录：

```text
MLIR_INCLUDE_DIR/
└── mlir/
    ├── IR/
    │   └── OpBase.td
    └── Interfaces/
        └── SideEffectInterfaces.td
```

## 1. 初始目录

开始时，项目只需要有环境目录；`tutorial/` 和 `build/` 可以还不存在。

```text
wdir/
└── .venv/                         # 已安装 mlir-wheel 等依赖的虚拟环境
```

外部 MLIR wheel 中的 `.td` 文件不属于项目源码，但会作为本步骤的输入
依赖：

```text
MLIR_INCLUDE_DIR/
└── mlir/
    ├── IR/
    │   └── OpBase.td
    └── Interfaces/
        └── SideEffectInterfaces.td
```

## 2. 创建并手写输入文件

创建项目源码目录：

```bash
mkdir -p tutorial/dialect
```

此时目录为：

```text
wdir/
├── .venv/
└── tutorial/
    └── dialect/
```

在下面的位置新建唯一一个本步骤需要自己编写的文件：

```text
wdir/tutorial/dialect/TutorialDialect.td
```

文件内容如下。解释直接写在代码注释中；注释不会改变 TableGen 的定义
含义。

```tablegen
// 这是普通注释：说明该文件定义一个最小 tutorial dialect。

#ifndef TUTORIAL_DIALECT
// TableGen 预处理器的头文件保护宏，防止同一个 .td 文件被重复展开。
#define TUTORIAL_DIALECT

include "mlir/IR/OpBase.td"
// TableGen 的 include，不是 C++ #include。
// mlir-tblgen 会在每一个 -I 目录下查找这个相对路径：
//   <某个 -I 目录>/mlir/IR/OpBase.td
// 该文件提供 Dialect、Op、I32、I32Attr、ins、outs 等基础定义。

include "mlir/Interfaces/SideEffectInterfaces.td"
// 同样是 TableGen include。
// 该文件提供 Pure 等 operation trait。
// 它实际通过：
//   -I<MLIR_INCLUDE_DIR>
// 找到：
//   <MLIR_INCLUDE_DIR>/mlir/Interfaces/SideEffectInterfaces.td

def Tutorial_Dialect : Dialect {
  // def 创建一个具体的 TableGen record。
  // Tutorial_Dialect 是 TableGen 内部 record 名，不是最终 MLIR 文本名称。
  // 冒号表示它继承 MLIR 提供的 Dialect 类。

  let name = "tutorial";
  // dialect 在 MLIR 中的名字是 tutorial。
  // 它参与组成 operation 的完整名字，例如 tutorial.constant。

  let cppNamespace = "::mlir::tutorial";
  // 生成的 C++ 类型放在 ::mlir::tutorial namespace 中。
  // 这里的 :: 是 C++ 全局 namespace 起点，不是 MLIR operation 语法。

  let summary = "A minimal dialect for checking the tutorial setup";
  // dialect 的摘要，主要用于生成注释或文档。
}

class Tutorial_Op<string mnemonic, list<Trait> traits = []>
    : Op<Tutorial_Dialect, mnemonic, traits>;
// class 定义一个可复用的 TableGen 模板，不创建最终的具体 operation。
// string mnemonic 是 operation 的助记符参数，例如 "constant"。
// list<Trait> traits 是 trait 列表参数，默认值 [] 表示没有 trait。
// 冒号表示继承 Op；这里把 dialect、mnemonic 和 traits 传给父类 Op。
// 这类似“所有 tutorial operation 共用的一层模板”。

def Tutorial_ConstantOp : Tutorial_Op<"constant", [Pure]> {
  // def 创建一个具体的 TableGen record。
  // 它继承 Tutorial_Op，并把模板参数具体设置为：
  //   mnemonic = "constant"
  //   traits   = [Pure]
  // 因此最终的 MLIR operation 名字是 tutorial.constant。

  let summary = "Produce a constant i32 value";
  // 该 operation 的摘要。

  let arguments = (ins I32Attr:$value);
  // ins 表示 operation 的输入。
  // I32Attr 表示一个 32 位 signless integer attribute，来自 OpBase.td。
  // $value 是这个输入的 TableGen 名字，后面的 assemblyFormat 会引用它。
  // 这里定义的是属性，不是 SSA operand，所以 operation 没有输入 SSA 值。

  let results = (outs I32:$result);
  // outs 表示 operation 的结果。
  // I32 表示 32 位 signless integer 类型。
  // $result 是结果的 TableGen 名字，后面的 type($result) 会引用它。

  let assemblyFormat = "$value attr-dict `:` type($result)";
  // 定义 operation 的自定义文本格式：
  //   $value       打印/解析 value 属性，例如 42；
  //   attr-dict    允许额外属性字典；
  //   `:`          反引号中的冒号是必须出现的字面量；
  //   type($result)打印/解析 result 的类型，例如 i32。
  // 因此一个合法的 operation 可以写成：
  //   tutorial.constant 42 : i32
}

#endif // TUTORIAL_DIALECT
// 结束头文件保护区域。
```

此时只有手写输入文件：

```text
wdir/
├── .venv/
└── tutorial/
    └── dialect/
        └── TutorialDialect.td    # 手写输入
```

本文件中 `def`、`class` 和 `:` 的关系可以概括为：

```text
class ... : Parent
    = 定义一个可复用的 TableGen 类/模板，并继承 Parent；

def ... : Parent
    = 创建一个具体的 TableGen record，并继承 Parent；

:
    = 在 class 和 def 中都表示父类关系，不表示“声明类对象”。
```

因此本例的关系是：

```text
Dialect                         # MLIR 提供的 TableGen 基类
  └── Tutorial_Dialect          # def：具体 dialect record

Op                              # MLIR 提供的 TableGen 基类
  └── Tutorial_Op               # class：可复用 operation 模板
       └── Tutorial_ConstantOp  # def：具体 operation record
```

## 3. 创建生成文件目录

手工创建输出目录：

```bash
mkdir -p build/tutorial/dialect
```

这里选择 `build/tutorial/dialect` 是为了与本项目之后的 CMake 构建布局
一致；本步骤本身不会调用 CMake。

目录变为：

```text
wdir/
├── .venv/
├── build/
│   └── tutorial/
│       └── dialect/               # TableGen 输出目录
└── tutorial/
    └── dialect/
        └── TutorialDialect.td     # 手写输入
```

## 4. 第一次调用：生成 operation 声明

输入和输出：

```text
输入：
  wdir/tutorial/dialect/TutorialDialect.td
  MLIR_INCLUDE_DIR/mlir/IR/OpBase.td
  MLIR_INCLUDE_DIR/mlir/Interfaces/SideEffectInterfaces.td
  以及上述 .td 文件继续 include 的 MLIR .td 依赖

输出：
  wdir/build/tutorial/dialect/TutorialOps.h.inc
  wdir/build/tutorial/dialect/TutorialOps.h.inc.d
```

执行：

```bash
mlir-tblgen \
  -gen-op-decls \
  -I"$PWD/tutorial/dialect" \
  -I"$(python -m mlir_wheel --include-dir)" \
  "$PWD/tutorial/dialect/TutorialDialect.td" \
  --write-if-changed \
  -o "$PWD/build/tutorial/dialect/TutorialOps.h.inc" \
  -d "$PWD/build/tutorial/dialect/TutorialOps.h.inc.d"
```

```text
# mlir-tblgen：读取 TableGen 输入并运行一个生成后端。
# -gen-op-decls：选择 operation declarations 后端，只生成 operation 声明。
# -I"$PWD/tutorial/dialect"：
#   将项目自己的 dialect 目录加入 TableGen include 搜索路径。
#   如果以后写 include "Local.td"，它可以在这里查找。
# -I"$(python -m mlir_wheel --include-dir)"：
#   将包含 mlir/ 子目录的 MLIR include 根目录加入搜索路径。
#   include "mlir/IR/OpBase.td" 会拼成：
#   <MLIR_INCLUDE_DIR>/mlir/IR/OpBase.td。
# "$PWD/tutorial/dialect/TutorialDialect.td"：唯一的项目主输入文件。
# --write-if-changed：只有内容变化时才改写输出文件。
# -o .../TutorialOps.h.inc：指定生成的 C++ 声明片段。
# -d .../TutorialOps.h.inc.d：写出依赖清单，供构建系统判断何时重生成。
```

生成文件的代表性内容如下；下面的注释用于解释结构，完整文件由工具
生成，不能手工维护：

```cpp
namespace mlir::tutorial {
// cppNamespace = "::mlir::tutorial" 使 operation C++ 类型放在这个 namespace。

class ConstantOp;
// Tutorial_ConstantOp 这个具体 TableGen record 生成了 ConstantOp 前置声明。

class ConstantOp : public ::mlir::Op<
    ConstantOp,
    ::mlir::OpTrait::ZeroRegions,
    ::mlir::OpTrait::OneResult,
    /* 还包括 I32 result、Pure trait 等由 TableGen 推导出的 traits */> {
  // 这里是生成的 operation C++ 类声明。

  static constexpr ::llvm::StringLiteral getOperationName() {
    return ::llvm::StringLiteral("tutorial.constant");
    // dialect name "tutorial" + mnemonic "constant" 得到完整 operation 名。
  }

  ::mlir::TypedValue<::mlir::IntegerType> getResult();
  // results = (outs I32:$result) 生成结果访问器。

  ::mlir::IntegerAttr getValueAttr();
  // arguments = (ins I32Attr:$value) 生成属性访问器。

  static void build(
      ::mlir::OpBuilder &builder,
      ::mlir::OperationState &state,
      ::mlir::Type result,
      ::mlir::IntegerAttr value);
  // TableGen 根据属性和结果自动生成 builder 声明。
};
} // namespace mlir::tutorial
```

此时目录增加：

```text
wdir/
├── .venv/
├── build/
│   └── tutorial/
│       └── dialect/
│           ├── TutorialOps.h.inc       # 生成：operation 声明
│           └── TutorialOps.h.inc.d     # 生成：依赖清单
└── tutorial/
    └── dialect/
        └── TutorialDialect.td           # 手写输入
```

## 5. 第二次调用：生成 operation 定义

输入仍然是同一个 `.td` 文件和它 include 的所有 `.td` 依赖。

```text
输入：
  wdir/tutorial/dialect/TutorialDialect.td
  MLIR_INCLUDE_DIR/mlir/.../*.td 依赖

输出：
  wdir/build/tutorial/dialect/TutorialOps.cpp.inc
  wdir/build/tutorial/dialect/TutorialOps.cpp.inc.d
```

执行：

```bash
mlir-tblgen \
  -gen-op-defs \
  -I"$PWD/tutorial/dialect" \
  -I"$(python -m mlir_wheel --include-dir)" \
  "$PWD/tutorial/dialect/TutorialDialect.td" \
  --write-if-changed \
  -o "$PWD/build/tutorial/dialect/TutorialOps.cpp.inc" \
  -d "$PWD/build/tutorial/dialect/TutorialOps.cpp.inc.d"
```

```text
# -gen-op-defs：选择 operation definitions 后端，生成 ConstantOp 方法实现。
# 其余 -I、输入文件、--write-if-changed、-o、-d 的含义与上一条命令相同，
# 但输出文件改成 TutorialOps.cpp.inc。
```

生成文件的代表性内容如下：

```cpp
#ifdef GET_OP_LIST
// 手写 C++ 在 include 这个文件前定义 GET_OP_LIST 时，会进入这个分支。
#undef GET_OP_LIST
// 生成器清掉宏，避免宏状态泄漏到后面的 C++ 代码。

::mlir::tutorial::ConstantOp
// 这是 operation 类型列表中的一个元素。
// 因而：
//   addOperations<
//   #define GET_OP_LIST
//   #include "dialect/TutorialOps.cpp.inc"
//   >();
// 预处理后等价于：
//   addOperations<::mlir::tutorial::ConstantOp>();

#endif // GET_OP_LIST

static ::llvm::LogicalResult verifyResultType(/* ... */) {
  // results = (outs I32:$result) 使生成代码检查结果是否为 32 位整数。
}

::llvm::LogicalResult ConstantOpAdaptor::verify(/* ... */) {
  // arguments = (ins I32Attr:$value) 使生成代码检查 value 是否为 I32Attr。
}

void ConstantOp::build(/* ... */) {
  // 自动生成 builder，把 result 类型和 value 属性写入 OperationState。
}

// 这里还会生成 parse/print/verifier 等实现，
// assemblyFormat = "$value attr-dict `:` type($result)"
// 被用来自动生成 tutorial.constant 的文本解析和打印逻辑。
```

此时目录增加：

```text
wdir/
├── .venv/
├── build/
│   └── tutorial/
│       └── dialect/
│           ├── TutorialOps.cpp.inc       # 生成：operation 定义
│           ├── TutorialOps.cpp.inc.d     # 生成：依赖清单
│           ├── TutorialOps.h.inc         # 生成：operation 声明
│           └── TutorialOps.h.inc.d       # 生成：依赖清单
└── tutorial/
    └── dialect/
        └── TutorialDialect.td             # 手写输入
```

## 6. 第三次调用：生成 dialect 声明

输入和输出：

```text
输入：
  wdir/tutorial/dialect/TutorialDialect.td
  MLIR_INCLUDE_DIR/mlir/.../*.td 依赖

输出：
  wdir/build/tutorial/dialect/TutorialOpsDialect.h.inc
  wdir/build/tutorial/dialect/TutorialOpsDialect.h.inc.d
```

执行：

```bash
mlir-tblgen \
  -gen-dialect-decls \
  -dialect=tutorial \
  -I"$PWD/tutorial/dialect" \
  -I"$(python -m mlir_wheel --include-dir)" \
  "$PWD/tutorial/dialect/TutorialDialect.td" \
  --write-if-changed \
  -o "$PWD/build/tutorial/dialect/TutorialOpsDialect.h.inc" \
  -d "$PWD/build/tutorial/dialect/TutorialOpsDialect.h.inc.d"
```

```text
# -gen-dialect-decls：选择 dialect declarations 后端，生成 dialect 类声明。
# -dialect=tutorial：告诉生成器选择 name = "tutorial" 的 Dialect record。
# 这里的 tutorial 是 MLIR dialect 名，不是 C++ namespace 名。
# 其余参数负责查找输入和 include，并指定 .inc 与 .d 输出位置。
```

生成文件的代表性内容如下：

```cpp
namespace mlir::tutorial {
// 由 cppNamespace = "::mlir::tutorial" 生成。

class TutorialDialect : public ::mlir::Dialect {
  // def Tutorial_Dialect : Dialect 创建的具体 dialect C++ 类。

  explicit TutorialDialect(::mlir::MLIRContext *context);
  // MLIRContext 是 dialect 注册和创建所需的上下文。

  void initialize();
  // dialect 构造时会调用这个初始化钩子；
  // 本项目在 TutorialDialect.cpp 中手写其定义并注册 ConstantOp。

public:
  ~TutorialDialect() override;

  static constexpr ::llvm::StringLiteral getDialectNamespace() {
    return ::llvm::StringLiteral("tutorial");
    // 由 let name = "tutorial" 生成。
  }
};
} // namespace mlir::tutorial

MLIR_DECLARE_EXPLICIT_TYPE_ID(::mlir::tutorial::TutorialDialect);
// 为这个 dialect 声明 MLIR 的显式 TypeID。
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
│           ├── TutorialOpsDialect.h.inc       # 生成：dialect 声明
│           └── TutorialOpsDialect.h.inc.d     # 生成：依赖清单
└── tutorial/
    └── dialect/
        └── TutorialDialect.td                 # 手写输入
```

## 7. 第四次调用：生成 dialect 定义

输入和输出：

```text
输入：
  wdir/tutorial/dialect/TutorialDialect.td
  MLIR_INCLUDE_DIR/mlir/.../*.td 依赖

输出：
  wdir/build/tutorial/dialect/TutorialOpsDialect.cpp.inc
  wdir/build/tutorial/dialect/TutorialOpsDialect.cpp.inc.d
```

执行：

```bash
mlir-tblgen \
  -gen-dialect-defs \
  -dialect=tutorial \
  -I"$PWD/tutorial/dialect" \
  -I"$(python -m mlir_wheel --include-dir)" \
  "$PWD/tutorial/dialect/TutorialDialect.td" \
  --write-if-changed \
  -o "$PWD/build/tutorial/dialect/TutorialOpsDialect.cpp.inc" \
  -d "$PWD/build/tutorial/dialect/TutorialOpsDialect.cpp.inc.d"
```

```text
# -gen-dialect-defs：选择 dialect definitions 后端，生成 TutorialDialect 方法实现。
# -dialect=tutorial：只生成 name = "tutorial" 的 dialect。
# 其余参数仍然分别指定 include 搜索路径、主输入、代码输出和依赖输出。
```

生成文件的代表性内容如下：

```cpp
MLIR_DEFINE_EXPLICIT_TYPE_ID(::mlir::tutorial::TutorialDialect);
// 与头文件中的 MLIR_DECLARE_EXPLICIT_TYPE_ID 配对，定义 dialect 的 TypeID。

namespace mlir::tutorial {

TutorialDialect::TutorialDialect(::mlir::MLIRContext *context)
    : ::mlir::Dialect(
          getDialectNamespace(),
          context,
          ::mlir::TypeID::get<TutorialDialect>()) {
  initialize();
  // dialect 对象构造时调用 initialize()。
  // addOperations<ConstantOp>() 通常由项目手写的 initialize() 完成。
}

TutorialDialect::~TutorialDialect() = default;
// 生成默认析构函数定义。

} // namespace mlir::tutorial
```

## 8. 最终目录树

四条命令成功后，项目目录如下：

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
        └── TutorialDialect.td
```

四个 `.inc` 文件都是 `mlir-tblgen` 的文本输出，不需要自己写，也不是
可以独立执行的程序：

```text
TutorialOps.h.inc
    = ConstantOp 的 C++ 声明；

TutorialOps.cpp.inc
    = ConstantOp 的 C++ 定义，以及 GET_OP_LIST 模式下的 operation 类型列表；

TutorialOpsDialect.h.inc
    = TutorialDialect 的 C++ 类声明；

TutorialOpsDialect.cpp.inc
    = TutorialDialect 的 C++ 构造函数、析构函数和 TypeID 定义；

*.inc.d
    = 依赖清单，不参与 MLIR 运行时逻辑，用于判断输入 .td 改变后是否需要重生成。
```

本步骤的最终数据流是：

```text
wdir/tutorial/dialect/TutorialDialect.td
        +
MLIR_INCLUDE_DIR/mlir/ 下的基础 .td 文件
        +
四种生成选项
        |
        | 四次独立的 mlir-tblgen 调用
        v
wdir/build/tutorial/dialect/*.inc
wdir/build/tutorial/dialect/*.inc.d
```

