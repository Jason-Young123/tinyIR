# `pass.md`：在当前 tinyIR 工程中从零实践第一个 MLIR Pass

> 适用工程：当前 `tinyIR` 项目  
> 前置进度：已经完成 `.td -> .inc -> .h/.cpp -> libtinyir.a -> tinyir-opt`，并能用 `tinyir-opt` 解析自定义 Dialect。  
> 本讲义目标：在**不引入 CMake、Pass TableGen、PatternRewriter 等额外复杂度**的前提下，第一次完整跑通一个手写 MLIR Pass。

---

## 0. 你现在处于哪一步

你已经完成了 MLIR 工程中第一条主链：

```text
*.td
  ↓ mlir-tblgen
*_Dialect.*.inc
*_Types.*.inc
*_Ops.*.inc
  ↓
List.cpp / Tutorial.cpp
  ↓ c++
List.o / Tutorial.o
  ↓ ar
libtinyir.a
  ↓ 与 tinyir-opt.o 链接
tinyir-opt
```

现在要增加第二条主链：

```text
输入 .mlir
   ↓
tinyir-opt 解析为内存中的 MLIR IR
   ↓
PassManager
   ↓
SimplifyAddPass::runOnOperation()
   ↓
观察 / 修改 IR
   ↓
printer
   ↓
输出新的 MLIR
```

最重要的一点：

> Pass 不是一个单独运行的 C++ 程序。  
> `SimplifyAdd.cpp` 会被编译进 `libtinyir.a`，再链接进 `tinyir-opt`；真正运行时仍然执行 `tinyir-opt`。

---

# 1. 先调整目录：新增 `src/passes`

当前源码：

```text
src/
├── dialect/
│   ├── List.cpp
│   ├── List.h
│   ├── List.td
│   ├── Tutorial.cpp
│   ├── Tutorial.h
│   └── Tutorial.td
└── tinyir-opt.cpp
```

开始学习 pass 后，建议增加：

```text
src/
├── dialect/
│   ├── List.cpp
│   ├── List.h
│   ├── List.td
│   ├── Tutorial.cpp
│   ├── Tutorial.h
│   └── Tutorial.td
├── passes/
│   ├── Passes.h
│   └── SimplifyAdd.cpp
└── tinyir-opt.cpp
```

测试文件建议也稍微分层：

```text
test/
├── mytest1.mlir
├── mytest2.mlir
└── passes/
    └── simpleAdd.mlir
```

这里三个目录的职责非常明确：

```text
src/dialect/    定义“IR 长什么样”
src/passes/     定义“怎样分析/变换 IR”
src/tinyir-opt.cpp
                工具入口：注册 dialect、pass，然后启动 MlirOptMain

test/           输入给 tinyir-opt 的 MLIR 文本
build/          所有自动生成/编译/链接产物
```

这比把 pass 放进 `dialect/` 更合理，因为 Dialect 和 Pass 的职责不同：

```text
Dialect = 定义 IR
Pass    = 消费、分析或改写 IR
```

---

# 2. 增加 Pass 后，完整目录树应该是什么样

源码层建议：

```text
tinyIR/
├── Makefile
├── requirements.txt
├── src/
│   ├── dialect/
│   │   ├── List.cpp
│   │   ├── List.h
│   │   ├── List.td
│   │   ├── Tutorial.cpp
│   │   ├── Tutorial.h
│   │   └── Tutorial.td
│   ├── passes/
│   │   ├── Passes.h
│   │   └── SimplifyAdd.cpp
│   └── tinyir-opt.cpp
└── test/
    ├── mytest1.mlir
    ├── mytest2.mlir
    └── passes/
        └── simpleAdd.mlir
```

构建完成后：

```text
build/
├── bin/
│   └── tinyir-opt
├── gen/
│   ├── List_Dialect.cpp.inc
│   ├── List_Dialect.h.inc
│   ├── List_Ops.cpp.inc
│   ├── List_Ops.h.inc
│   ├── List_Types.cpp.inc
│   ├── List_Types.h.inc
│   ├── Tutorial_Dialect.cpp.inc
│   ├── Tutorial_Dialect.h.inc
│   ├── Tutorial_Ops.cpp.inc
│   ├── Tutorial_Ops.h.inc
│   ├── Tutorial_Types.cpp.inc
│   └── Tutorial_Types.h.inc
├── lib/
│   └── libtinyir.a
└── obj/
    ├── dialect/
    │   ├── List.d
    │   ├── List.o
    │   ├── Tutorial.d
    │   └── Tutorial.o
    ├── passes/
    │   ├── SimplifyAdd.d
    │   └── SimplifyAdd.o
    ├── tinyir-opt.d
    └── tinyir-opt.o
```

这里把 dialect/pass 的 `.o` 分到不同子目录，是因为以后很容易继续出现：

```text
src/passes/FoldList.cpp
src/passes/LowerList.cpp
src/passes/CanonicalizeFoo.cpp
```

不会把所有对象文件堆在 `build/obj/` 根目录。

---

# 3. 第一次 Pass 实践需要新增哪些文件

本轮只新增三个东西：

```text
src/passes/Passes.h
src/passes/SimplifyAdd.cpp
test/passes/simpleAdd.mlir
```

并修改两个已有文件：

```text
src/tinyir-opt.cpp
Makefile
```

不要新增：

```text
SimplifyAdd.td
Passes.td
CMakeLists.txt
```

这些以后学习 declarative pass specification 时再引入。

---

# 4. Step A：修改 Makefile，让 pass 参与构建

下面是在你当前 Makefile 思路上直接扩展后的完整版本。

```make
# MLIR
MLIR_ROOT        := /home/jason/.local/lib/python3.10/site-packages/mlir_wheel
MLIR_TOOL_DIR    := $(MLIR_ROOT)/bin
MLIR_INCLUDE_DIR := $(MLIR_ROOT)/include
MLIR_LIB_DIR     := $(MLIR_ROOT)/lib
TBLGEN := $(MLIR_TOOL_DIR)/mlir-tblgen
CXX    := c++
AR     := ar

# Project
PROJECT     := tinyir
OPT         := $(PROJECT)-opt
SRC_DIR     := src
DIALECT_DIR := $(SRC_DIR)/dialect
PASS_DIR    := $(SRC_DIR)/passes
BUILD_DIR   := build
TEST_DIR    := test

# 存放自动生成/编译文件
GEN_DIR         := $(BUILD_DIR)/gen
OBJ_DIR         := $(BUILD_DIR)/obj
DIALECT_OBJ_DIR := $(OBJ_DIR)/dialect
PASS_OBJ_DIR    := $(OBJ_DIR)/passes
LIB_DIR         := $(BUILD_DIR)/lib
BIN_DIR         := $(BUILD_DIR)/bin

# src
TD_SRCS      := $(wildcard $(DIALECT_DIR)/*.td)
DIALECT_SRCS := $(wildcard $(DIALECT_DIR)/*.cpp)
DIALECT_HDRS := $(wildcard $(DIALECT_DIR)/*.h)
PASS_SRCS    := $(wildcard $(PASS_DIR)/*.cpp)
PASS_HDRS    := $(wildcard $(PASS_DIR)/*.h)
OPT_SRC      := $(SRC_DIR)/$(OPT).cpp
TEST         := $(TEST_DIR)/passes/simpleAdd.mlir

# outputs
DIALECT_OBJS := $(patsubst $(DIALECT_DIR)/%.cpp,$(DIALECT_OBJ_DIR)/%.o,$(DIALECT_SRCS))
PASS_OBJS    := $(patsubst $(PASS_DIR)/%.cpp,$(PASS_OBJ_DIR)/%.o,$(PASS_SRCS))
OPT_OBJ      := $(OBJ_DIR)/$(OPT).o
LIB          := $(LIB_DIR)/lib$(PROJECT).a
BIN          := $(BIN_DIR)/$(OPT)
TBLGEN_STAMP := $(GEN_DIR)/.tblgen.stamp
DEPS         := $(DIALECT_OBJS:.o=.d) $(PASS_OBJS:.o=.d) $(OPT_OBJ:.o=.d)

# flags
CPPFLAGS := -I$(MLIR_INCLUDE_DIR) -I$(SRC_DIR) -I$(DIALECT_DIR) -I$(PASS_DIR) -I$(GEN_DIR)
CXXFLAGS := -std=c++17 -fPIC -MMD -MP
TBLGEN_FLAGS := -I$(DIALECT_DIR) -I$(MLIR_INCLUDE_DIR) --write-if-changed
LDFLAGS := -L $(MLIR_LIB_DIR) -Wl,-rpath,$(MLIR_LIB_DIR)
LDLIBS := -lMLIR -lLLVM

# 声明伪目标
.PHONY: all build tblgen test clean

all: build

build: $(BIN)

tblgen: $(TBLGEN_STAMP)

### +TBLGEN .td -> .inc
$(TBLGEN_STAMP): $(TD_SRCS)
	@mkdir -p $(GEN_DIR)
	@for td in $(TD_SRCS); do \
		name=$$(basename $$td .td); \
		for spec in \
			"dialect-decls:Dialect.h.inc" \
			"dialect-defs:Dialect.cpp.inc" \
			"typedef-decls:Types.h.inc" \
			"typedef-defs:Types.cpp.inc" \
			"op-decls:Ops.h.inc" \
			"op-defs:Ops.cpp.inc"; do \
			gen=$${spec%%:*}; \
			suffix=$${spec#*:}; \
			out="$(GEN_DIR)/$${name}_$${suffix}"; \
			echo "+TBLGEN  $$td --> $$out"; \
			$(TBLGEN) -gen-$$gen $(TBLGEN_FLAGS) $$td -o $$out || exit 1; \
		done; \
	done
	@touch $@

### +CXX dialect/*.cpp -> .o
$(DIALECT_OBJ_DIR)/%.o: $(DIALECT_DIR)/%.cpp $(TBLGEN_STAMP)
	@mkdir -p $(DIALECT_OBJ_DIR)
	@echo "+CXX     $< --> $@"
	@$(CXX) $(CPPFLAGS) $(CXXFLAGS) \
		-c $< \
		-o $@

### +CXX passes/*.cpp -> .o
$(PASS_OBJ_DIR)/%.o: $(PASS_DIR)/%.cpp
	@mkdir -p $(PASS_OBJ_DIR)
	@echo "+CXX     $< --> $@"
	@$(CXX) $(CPPFLAGS) $(CXXFLAGS) \
		-c $< \
		-o $@

### +AR .o -> .a
$(LIB): $(DIALECT_OBJS) $(PASS_OBJS)
	@mkdir -p $(LIB_DIR)
	@echo "+AR      $(DIALECT_OBJS) $(PASS_OBJS) --> $(LIB)"
	@$(AR) rcs $@ $^

### +CXX tinyir-opt.cpp -> .o
$(OPT_OBJ): $(OPT_SRC) $(DIALECT_HDRS) $(PASS_HDRS) $(TBLGEN_STAMP)
	@mkdir -p $(OBJ_DIR)
	@echo "+CXX     $(OPT_SRC) --> $(OPT_OBJ)"
	@$(CXX) $(CPPFLAGS) $(CXXFLAGS) \
		-c $(OPT_SRC) \
		-o $@

### +LD .o, .a -> bin
$(BIN): $(OPT_OBJ) $(LIB)
	@mkdir -p $(BIN_DIR)
	@echo "+LD      $(OPT_OBJ) $(LIB) --> $(BIN)"
	@$(CXX) $^ \
		$(LDFLAGS) \
		$(LDLIBS) \
		-o $@

### +TEST .mlir -> stdout
test: $(BIN)
	@echo "+TEST    $(TEST) --> stdout"
	@$(BIN) $(TEST)

-include $(DEPS)

clean:
	@echo "+CLEAN   $(BUILD_DIR)"
	@rm -rf $(BUILD_DIR)
```

## 4.1 相比你之前的 Makefile，真正新增了什么

新增 pass 源码目录：

```make
PASS_DIR := $(SRC_DIR)/passes
```

自动发现所有 pass：

```make
PASS_SRCS := $(wildcard $(PASS_DIR)/*.cpp)
PASS_HDRS := $(wildcard $(PASS_DIR)/*.h)
```

把：

```text
src/passes/SimplifyAdd.cpp
```

映射成：

```text
build/obj/passes/SimplifyAdd.o
```

对应：

```make
PASS_OBJS := $(patsubst $(PASS_DIR)/%.cpp,$(PASS_OBJ_DIR)/%.o,$(PASS_SRCS))
```

然后归档时：

```make
$(LIB): $(DIALECT_OBJS) $(PASS_OBJS)
```

于是 `libtinyir.a` 现在包含：

```text
List.o
Tutorial.o
SimplifyAdd.o
```

可以把它理解成：

```text
libtinyir.a
├── dialect implementation
└── pass implementation
```

---

# 5. Step B：增加 `Passes.h`

创建：

```text
src/passes/Passes.h
```

内容：

```cpp
#ifndef TINYIR_PASSES_H
#define TINYIR_PASSES_H

namespace tinyir {

void registerSimplifyAddPass();

} // namespace tinyir

#endif // TINYIR_PASSES_H
```

为什么要单独有这个文件？

因为：

```text
SimplifyAdd.cpp
```

负责：

```text
“pass 到底怎么实现”
```

而：

```text
Passes.h
```

只向外暴露：

```text
“这里有一个可以注册的 pass”
```

于是 `tinyir-opt.cpp` 不需要知道：

```cpp
struct SimplifyAddPass ...
```

的内部实现。

它只需要知道：

```cpp
tinyir::registerSimplifyAddPass();
```

这就是最简单的“接口与实现分离”。

---

# 6. Step C：在 `tinyir-opt.cpp` 注册 Pass

你原来的 `tinyir-opt.cpp` 已经负责注册 dialect。

现在增加：

```cpp
#include "passes/Passes.h"
```

以及：

```cpp
tinyir::registerSimplifyAddPass();
```

例如：

```cpp
#include "mlir/InitAllDialects.h"
#include "mlir/InitAllExtensions.h"
#include "mlir/InitAllPasses.h"
#include "mlir/Tools/mlir-opt/MlirOptMain.h"

#include "dialect/List.h"
#include "dialect/Tutorial.h"
#include "passes/Passes.h"

int main(int argc, char **argv) {
  mlir::DialectRegistry registry;

  // MLIR自带dialect
  mlir::registerAllDialects(registry);

  // MLIR自带pass
  mlir::registerAllPasses();

  // MLIR extensions
  mlir::registerAllExtensions(registry);

  // tinyIR自定义dialect
  registry.insert<
      mlir::tutorial::TutorialDialect,
      mlir::list::ListDialect>();

  // tinyIR自定义pass
  tinyir::registerSimplifyAddPass();

  return mlir::asMainReturnCode(
      mlir::MlirOptMain(
          argc,
          argv,
          "tinyIR optimizer driver\n",
          registry));
}
```

注意：

```cpp
mlir::registerAllPasses();
```

只注册 MLIR 自带 pass。

它**不会自动发现**：

```text
src/passes/SimplifyAdd.cpp
```

所以还必须显式调用：

```cpp
tinyir::registerSimplifyAddPass();
```

---

# 7. Step D：准备第一个 Pass 测试输入

创建：

```text
test/passes/simpleAdd.mlir
```

内容：

```mlir
module {
  func.func @add_one(%x: i32) -> i32 {
    %c1 = arith.constant 1 : i32
    %y = arith.addi %x, %c1 : i32
    return %y : i32
  }
}
```

这段 IR 可以画成：

```text
%x
 │
 │       %c1 = 1
 │        │
 └──┐  ┌──┘
    ▼  ▼
 arith.addi
     │
     ▼
     %y
     │
     ▼
 func.return
```

后面的 pass 就围绕这条：

```mlir
%y = arith.addi %x, %c1 : i32
```

逐步学习。

---

# 8. 先理解 Pass 的最小结构

一个最基本的手写 pass 可以分成四部分：

```text
1. include
2. Pass 类
3. runOnOperation()
4. 注册函数
```

骨架：

```cpp
struct SimplifyAddPass
    : mlir::PassWrapper<
          SimplifyAddPass,
          mlir::OperationPass<mlir::func::FuncOp>> {

  mlir::StringRef getArgument() const override {
    return "simplify-add";
  }

  mlir::StringRef getDescription() const override {
    return "...";
  }

  void runOnOperation() override {
    ...
  }
};
```

最重要的是：

```cpp
mlir::OperationPass<mlir::func::FuncOp>
```

它表示：

> 这个 pass 的“当前 operation”是一个 `func.func`。

因此：

```cpp
getOperation()
```

得到的是：

```cpp
mlir::func::FuncOp
```

而不是整个 `builtin.module`。

所以运行 pipeline 时我们会写：

```text
builtin.module(
  func.func(
    simplify-add
  )
)
```

即：

```bash
--pass-pipeline='builtin.module(func.func(simplify-add))'
```

---

# 9. Step 0：只证明 Pass 能运行

这是第一次学习 pass 时最重要的一步。

先不要查 `arith.addi`，不要创建 operation，也不要修改 IR。

只验证：

```text
源码能编译
↓
pass 能注册
↓
命令行能找到 simplify-add
↓
PassManager 会调用 runOnOperation()
```

创建：

```text
src/passes/SimplifyAdd.cpp
```

内容：

```cpp
#include "passes/Passes.h"

#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Pass/PassRegistry.h"

namespace {

struct SimplifyAddPass
    : mlir::PassWrapper<
          SimplifyAddPass,
          mlir::OperationPass<mlir::func::FuncOp>> {

  mlir::StringRef getArgument() const override {
    return "simplify-add";
  }

  mlir::StringRef getDescription() const override {
    return "The first tinyIR pass";
  }

  void runOnOperation() override {
    mlir::func::FuncOp function = getOperation();
    function.emitRemark("SimplifyAddPass is running");
  }
};

} // namespace

void tinyir::registerSimplifyAddPass() {
  mlir::PassRegistration<SimplifyAddPass>();
}
```

这里推荐：

```cpp
mlir::PassRegistration<SimplifyAddPass>();
```

而不是手动写 allocator lambda。

对于能默认构造的 pass，这种写法更直接。

---

# 10. 构建并第一次运行

执行：

```bash
make clean
make build
```

正常日志应该新增：

```text
+CXX     src/passes/SimplifyAdd.cpp --> build/obj/passes/SimplifyAdd.o
```

并最终：

```text
+AR      ... build/obj/passes/SimplifyAdd.o --> build/lib/libtinyir.a
+LD      build/obj/tinyir-opt.o build/lib/libtinyir.a --> build/bin/tinyir-opt
```

先确认命令行已经存在这个 pass：

```bash
build/bin/tinyir-opt --help | grep simplify-add
```

然后运行：

```bash
build/bin/tinyir-opt test/passes/simpleAdd.mlir \
  --pass-pipeline='builtin.module(func.func(simplify-add))'
```

你应该看到一条类似：

```text
remark: SimplifyAddPass is running
```

同时标准输出仍然打印原始 MLIR。

到这一步，才说明你的第一条 Pass 链真正打通：

```text
SimplifyAdd.cpp
  ↓ compile
SimplifyAdd.o
  ↓ archive
libtinyir.a
  ↓ link
tinyir-opt
  ↓ register
simplify-add
  ↓ PassManager
runOnOperation()
```

---

# 11. 给 Makefile 增加专门的 pass 测试目标（可选但推荐）

你现在的：

```make
test:
```

可以继续保留。

如果希望学习 pass 时输入固定 pipeline，可以再加一个：

```make
.PHONY: pass-test
```

以及：

```make
pass-test: $(BIN)
	@echo "+PASS    $(TEST) --> simplify-add"
	@$(BIN) $(TEST) \
		--pass-pipeline='builtin.module(func.func(simplify-add))'
```

以后只需要：

```bash
make pass-test
```

输出类似：

```text
+PASS    test/passes/simpleAdd.mlir --> simplify-add
```

这样不会把普通 parser 测试和 pass 测试混在一起。

---

# 12. Step 1：只查询 `arith.addi`，不修改 IR

现在才开始“看” IR。

把 `SimplifyAdd.cpp` 替换成：

```cpp
#include "passes/Passes.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Pass/PassRegistry.h"
#include "llvm/Support/raw_ostream.h"

namespace {

struct SimplifyAddPass
    : mlir::PassWrapper<
          SimplifyAddPass,
          mlir::OperationPass<mlir::func::FuncOp> // 这个pass仅针对func.func生效
      > 
{

  mlir::StringRef getArgument() const override {
    return "simplify-add";
  }

  mlir::StringRef getDescription() const override {
    return "Inspect arith.addi operations";
  }

  void runOnOperation() override {
    mlir::func::FuncOp function = getOperation();

    function.walk([&](mlir::arith::AddIOp add) {
      mlir::Value lhs = add.getLhs();
      mlir::Value rhs = add.getRhs();

      llvm::errs() << "found arith.addi\n";

      llvm::errs() << "  lhs: ";
      lhs.dump();

      llvm::errs() << "  rhs: ";
      rhs.dump();

      llvm::errs() << "  result type: ";
      add.getResult().getType().dump();
    });
  }
};

} // namespace

void tinyir::registerSimplifyAddPass() {
  mlir::PassRegistration<SimplifyAddPass>();
}
```

运行：

```bash
make build
make pass-test
```

可能看到：

```text
found arith.addi
  lhs: %arg0 : i32
  rhs: %0 = arith.constant 1 : i32
  result type: i32
```

## 12.1 这一轮你只需要掌握五个东西

```text
function.walk(...)   遍历函数内部的 operation
arith::AddIOp        只匹配 arith.addi
add.getLhs()         左 operand 对应的 Value
add.getRhs()         右 operand 对应的 Value
add.getResult()      结果 Value
```

这里完全没有修改 IR。

因此这是一个**只读 pass**。

---

# 13. Operation、Value、OpOperand：现在先建立最小概念

看到：

```mlir
%y = arith.addi %x, %c1 : i32
```

可以理解成：

```text
             Operation
        ┌──────────────────┐
%x ────▶│                  │
%c1 ───▶│    arith.addi    │────▶ %y
        │                  │
        └──────────────────┘
```

其中：

```text
arith.addi   = Operation
%x           = Value
%c1          = Value
%y           = Value
```

而 Operation 内部“引用某个 Value 的输入位置”叫：

```text
OpOperand
```

因此：

```cpp
add.getRhs()
```

得到：

```text
Value
```

而：

```cpp
add->getOpOperand(1)
```

得到：

```text
OpOperand
```

两者区别以后做 replacement 时非常重要。

---

# 14. Step 2：学习创建 detached Operation

这一轮不是为了做有意义的优化，只是理解：

```text
“怎样手工描述一条 MLIR operation”
```

把文件替换成：

```cpp
#include "passes/Passes.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/OperationSupport.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Pass/PassRegistry.h"

namespace {

struct SimplifyAddPass
    : mlir::PassWrapper<
          SimplifyAddPass,
          mlir::OperationPass<mlir::func::FuncOp>> {

  mlir::StringRef getArgument() const override {
    return "simplify-add";
  }

  mlir::StringRef getDescription() const override {
    return "Create a detached operation for learning";
  }

  void runOnOperation() override {
    mlir::func::FuncOp function = getOperation();

    function.walk([&](mlir::arith::AddIOp add) {
      mlir::OperationState state(add.getLoc(), "arith.addi");

      state.addOperands({
          add.getLhs(),
          add.getRhs()
      });

      state.addTypes(add.getResult().getType());

      mlir::Operation *detached =
          mlir::Operation::create(state);

      detached->dump();

      detached->destroy();
    });
  }
};

} // namespace

void tinyir::registerSimplifyAddPass() {
  mlir::PassRegistration<SimplifyAddPass>();
}
```

这里的：

```cpp
mlir::OperationState state(...)
```

可以理解为一张“创建 Operation 的订单”。

```text
operation name
location
operands
result types
attributes
regions
...
```

都可以逐步写进它。

然后：

```cpp
mlir::Operation::create(state)
```

才真正创建对象。

但是它没有被插入任何 Block：

```text
detached operation
```

所以最后用：

```cpp
detached->destroy();
```

直接释放。

这一轮执行完后，最终打印出来的 MLIR **不应该增加任何新指令**。

---

# 15. `destroy()` 和 `erase()` 不要混淆

先记：

```text
没有 parent Block 的 detached Operation
    → destroy()

已经存在于 Block 中的 Operation
    → erase()
```

不要把：

```cpp
destroy()
```

理解成普通的“删除任意 operation”。

后面实际删除 IR 中已有 operation 时会用：

```cpp
erase()
```

---

# 16. Step 3：使用 `OpBuilder` 真正插入 Operation

手写：

```cpp
OperationState
Operation::create
```

更接近底层。

日常 MLIR pass 中更常见的是：

```cpp
OpBuilder
```

这一轮在原来的 `arith.addi` 后面插入一条相同的 `arith.addi`。

为了避免“遍历过程中不断遇到自己刚插入的 operation”，先找到目标，再离开 `walk` 后修改 IR。

```cpp
#include "passes/Passes.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/Builders.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Pass/PassRegistry.h"

namespace {

struct SimplifyAddPass
    : mlir::PassWrapper<
          SimplifyAddPass,
          mlir::OperationPass<mlir::func::FuncOp>> {

  mlir::StringRef getArgument() const override {
    return "simplify-add";
  }

  mlir::StringRef getDescription() const override {
    return "Insert an arith.addi for learning";
  }

  void runOnOperation() override {
    mlir::func::FuncOp function = getOperation();

    mlir::arith::AddIOp target = nullptr;

    function.walk([&](mlir::arith::AddIOp add) {
      if (!target)
        target = add;
    });

    if (!target)
      return;

    mlir::OpBuilder builder(function.getContext());

    builder.setInsertionPointAfter(target);

    builder.create<mlir::arith::AddIOp>(
        target.getLoc(),
        target.getLhs(),
        target.getRhs());
  }
};

} // namespace

void tinyir::registerSimplifyAddPass() {
  mlir::PassRegistration<SimplifyAddPass>();
}
```

原来：

```mlir
%0 = arith.constant 1 : i32
%1 = arith.addi %arg0, %0 : i32
return %1 : i32
```

运行后可能变成：

```mlir
%0 = arith.constant 1 : i32
%1 = arith.addi %arg0, %0 : i32
%2 = arith.addi %arg0, %0 : i32
return %1 : i32
```

注意：

```text
%2 没有使用者
```

这是刻意的，因为这一轮只学习“创建 + 插入”。

---

# 17. `setInsertionPointAfter` 为什么重要

Builder 不只是知道：

```text
创建什么 operation
```

还必须知道：

```text
把它放在哪里
```

因此：

```cpp
builder.setInsertionPointAfter(target);
```

决定：

```text
插入位置
```

而：

```cpp
builder.create<mlir::arith::AddIOp>(...)
```

决定：

```text
创建内容
```

两者要一起理解。

---

# 18. Step 4：第一次修改 use-def 链

现在学习修改：

```mlir
%y = arith.addi %x, %c1 : i32
```

把右 operand `%c1` 改成 `%x`：

```mlir
%y = arith.addi %x, %x : i32
```

代码：

```cpp
#include "passes/Passes.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Pass/PassRegistry.h"

namespace {

struct SimplifyAddPass
    : mlir::PassWrapper<
          SimplifyAddPass,
          mlir::OperationPass<mlir::func::FuncOp>> {

  mlir::StringRef getArgument() const override {
    return "simplify-add";
  }

  mlir::StringRef getDescription() const override {
    return "Replace one operand use";
  }

  void runOnOperation() override {
    mlir::func::FuncOp function = getOperation();

    function.walk([&](mlir::arith::AddIOp add) {
      mlir::Value lhs = add.getLhs();

      mlir::OpOperand &rhsUse =
          add->getOpOperand(1);

      rhsUse.set(lhs);
    });
  }
};

} // namespace

void tinyir::registerSimplifyAddPass() {
  mlir::PassRegistration<SimplifyAddPass>();
}
```

这里：

```cpp
getOpOperand(1)
```

中的索引从 0 开始：

```text
0 = lhs
1 = rhs
```

然后：

```cpp
rhsUse.set(lhs)
```

只修改这一条 use。

---

# 19. 修改一个 use 与修改所有 uses

单条输入：

```cpp
rhsUse.set(lhs);
```

表示：

```text
只把当前这一个 operand 指向的新 Value 改掉
```

而：

```cpp
oldValue.replaceAllUsesWith(newValue);
```

表示：

```text
所有使用 oldValue 的地方都改成 newValue
```

这是两个完全不同的操作。

初学时不要把：

```cpp
replaceAllUsesWith
```

理解成普通赋值；它是在修改 SSA use-def 图。

---

# 20. Step 5：安全删除无用 Operation

先把测试输入暂时改成：

```mlir
module {
  func.func @add_one(%x: i32) -> i32 {
    %unused = arith.constant 123 : i32
    %c1 = arith.constant 1 : i32
    %y = arith.addi %x, %c1 : i32
    return %y : i32
  }
}
```

这里：

```text
%unused
```

没有任何使用者。

因此对应 `arith.constant` 可以安全删除。

代码：

```cpp
#include "passes/Passes.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Pass/PassRegistry.h"
#include "llvm/ADT/SmallVector.h"

namespace {

struct SimplifyAddPass
    : mlir::PassWrapper<
          SimplifyAddPass,
          mlir::OperationPass<mlir::func::FuncOp>> {

  mlir::StringRef getArgument() const override {
    return "simplify-add";
  }

  mlir::StringRef getDescription() const override {
    return "Erase unused arith.constant operations";
  }

  void runOnOperation() override {
    mlir::func::FuncOp function = getOperation();

    llvm::SmallVector<mlir::Operation *, 4> dead;

    function.walk([&](mlir::arith::ConstantOp constant) {
      if (constant.getResult().use_empty())
        dead.push_back(constant.getOperation());
    });

    for (mlir::Operation *operation : dead)
      operation->erase();
  }
};

} // namespace

void tinyir::registerSimplifyAddPass() {
  mlir::PassRegistration<SimplifyAddPass>();
}
```

这里先：

```text
walk
↓
收集 dead operations
```

然后：

```text
walk 结束
↓
erase
```

比“遍历链表时马上删除当前节点”更容易理解，也更安全。

---

# 21. 为什么删除前必须看 `use_empty()`

SSA 中：

```text
Value
  ↓
可能有很多 users
```

如果仍然有人使用某个 operation 的 result，却把 defining operation 删除：

```text
user
  ↓
引用一个已经不存在的 Value
```

IR 就被破坏。

所以最基础的安全条件：

```cpp
constant.getResult().use_empty()
```

表示：

```text
这个 result 已经没有任何使用者
```

才能：

```cpp
operation->erase();
```

---

# 22. Step 6：观察 Region / Block / Type / Attribute

这一轮主要为了把之前看到的 IR 层级和 C++ API 对起来。

```cpp
#include "passes/Passes.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/Builders.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Pass/PassRegistry.h"

namespace {

struct SimplifyAddPass
    : mlir::PassWrapper<
          SimplifyAddPass,
          mlir::OperationPass<mlir::func::FuncOp>> {

  mlir::StringRef getArgument() const override {
    return "simplify-add";
  }

  mlir::StringRef getDescription() const override {
    return "Inspect regions, blocks, types and attributes";
  }

  void runOnOperation() override {
    mlir::func::FuncOp function = getOperation();

    mlir::Region &body = function.getBody();

    if (body.empty())
      return;

    mlir::Block &entry = body.front();

    for (mlir::Operation &operation : entry)
      operation.dump();

    mlir::OpBuilder builder(function.getContext());

    function.walk([&](mlir::arith::AddIOp add) {
      mlir::Type resultType =
          add.getResult().getType();

      resultType.dump();

      add->setAttr(
          "tinyir.seen",
          builder.getUnitAttr());
    });
  }
};

} // namespace

void tinyir::registerSimplifyAddPass() {
  mlir::PassRegistration<SimplifyAddPass>();
}
```

这几个概念对应：

```text
func.func
  │
  └── Region
       │
       └── Block
            ├── arith.constant
            ├── arith.addi
            └── func.return
```

因此：

```cpp
function.getBody()
```

得到：

```text
Region
```

而：

```cpp
body.front()
```

得到：

```text
第一个 Block
```

---

# 23. Type 和 Attribute 是什么

Type 属于 SSA Value：

```mlir
%x : i32
```

其中：

```text
i32
```

就是 Type。

C++：

```cpp
mlir::Type type = value.getType();
```

Attribute 通常属于 operation：

```mlir
arith.addi ... {tinyir.seen} : i32
```

其中：

```text
tinyir.seen
```

是 Attribute。

代码：

```cpp
add->setAttr(
    "tinyir.seen",
    builder.getUnitAttr());
```

会真正修改 IR。

而：

```cpp
dump()
```

只是观察，不会修改 IR。

---

# 24. 第一个相对完整、但仍然安全的 Pass

前面的 Step 0～6 是为了分开学习 API。

真正写 pass 时，不应该机械地把“创建、替换、删除、改属性”全部同时执行。

下面这个版本只做：

```text
1. 找 arith.addi
2. 打印它
3. 检查 result 是否为 i32
4. 给它添加一个 tinyir.seen 属性
5. 不随意修改 operands
6. 不随意删除 operations
```

```cpp
#include "passes/Passes.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/Builders.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Pass/PassRegistry.h"
#include "llvm/Support/raw_ostream.h"

namespace {

struct SimplifyAddPass
    : mlir::PassWrapper<
          SimplifyAddPass,
          mlir::OperationPass<mlir::func::FuncOp>> {

  mlir::StringRef getArgument() const override {
    return "simplify-add";
  }

  mlir::StringRef getDescription() const override {
    return "Inspect arith.addi operations safely";
  }

  void runOnOperation() override {
    mlir::func::FuncOp function = getOperation();
    mlir::OpBuilder builder(function.getContext());

    bool failed = false;

    function.walk([&](mlir::arith::AddIOp add) {
      mlir::Value lhs = add.getLhs();
      mlir::Value rhs = add.getRhs();
      mlir::OpResult result = add.getResult();

      llvm::errs() << "found arith.addi: ";
      add.dump();

      llvm::errs() << "  lhs type: ";
      lhs.getType().dump();

      llvm::errs() << "  rhs type: ";
      rhs.getType().dump();

      if (!result.getType().isSignlessInteger(32)) {
        add.emitError("expected an i32 result");
        failed = true;
        return;
      }

      add->setAttr(
          "tinyir.seen",
          builder.getUnitAttr());
    });

    if (failed)
      signalPassFailure();
  }
};

} // namespace

void tinyir::registerSimplifyAddPass() {
  mlir::PassRegistration<SimplifyAddPass>();
}
```

这里特意没有在 `walk` 的 lambda 中直接：

```cpp
signalPassFailure();
```

后马上假设遍历已经停止。

而是记录：

```cpp
bool failed
```

等遍历结束后统一：

```cpp
signalPassFailure();
```

对于初学代码更直观。

---

# 25. 运行 Pass 时到底发生了什么

命令：

```bash
build/bin/tinyir-opt test/passes/simpleAdd.mlir \
  --pass-pipeline='builtin.module(func.func(simplify-add))'
```

可以展开成：

```text
tinyir-opt
   │
   ├── 1. 注册 builtin / func / arith / tutorial / list ...
   │
   ├── 2. 注册 simplify-add
   │
   ├── 3. parser 读取 simpleAdd.mlir
   │
   ▼
builtin.module
   │
   ▼
func.func @add_one
   │
   ▼
SimplifyAddPass::runOnOperation()
   │
   ├── walk arith.addi
   ├── 查询 Value / Type
   ├── 可选修改 IR
   │
   ▼
printer
   │
   ▼
stdout
```

这里最容易混淆的一点：

```text
SimplifyAdd.cpp 不负责读取 .mlir 文件。
```

读取输入的是：

```text
MlirOptMain
```

Pass 收到的是：

```text
已经解析好的内存 IR
```

---

# 26. 为什么 pipeline 是 `builtin.module(func.func(simplify-add))`

你的 pass 定义：

```cpp
mlir::OperationPass<mlir::func::FuncOp>
```

所以它只能运行在：

```text
func.func
```

上。

而输入最外层是：

```mlir
module {
  func.func ...
}
```

对应：

```text
builtin.module
└── func.func
```

所以 textual pipeline：

```text
builtin.module(
  func.func(
    simplify-add
  )
)
```

压成一行就是：

```bash
--pass-pipeline='builtin.module(func.func(simplify-add))'
```

以后如果你写：

```cpp
OperationPass<mlir::ModuleOp>
```

那么层级就会不同。

因此 pass 的：

```cpp
OperationPass<...>
```

和命令行的：

```text
--pass-pipeline=...
```

必须一起理解。

---

# 27. 如何确认 pass 已经注册成功

执行：

```bash
build/bin/tinyir-opt --help | grep simplify-add
```

如果找得到：

```text
simplify-add
```

说明：

```text
SimplifyAdd.o
  ↓
libtinyir.a
  ↓
tinyir-opt
  ↓
registerSimplifyAddPass()
```

这条注册链已经成功。

如果找不到，优先检查：

```text
1. SimplifyAdd.cpp 是否编译
2. SimplifyAdd.o 是否进入 libtinyir.a
3. tinyir-opt.cpp 是否 include Passes.h
4. 是否调用 tinyir::registerSimplifyAddPass()
5. 是否重新链接 tinyir-opt
```

---

# 28. 这一阶段不要急着学习 PatternRewriter

MLIR 中真正大量的局部优化会使用：

```text
RewritePattern
OpRewritePattern
PatternRewriter
applyPatternsGreedily
canonicalization
```

这些非常重要。

但你目前第一次接触 pass，如果马上把：

```text
Pass
Pattern
RewriteRule
Canonicalization
Conversion
Legality
TypeConverter
```

全部混在一起，会很难形成层次感。

当前阶段先把 Pass 本身学清楚：

```text
Pass
├── 当前 operation 是谁
├── 怎样遍历 IR
├── 怎样读 operand/result
├── 怎样创建 operation
├── 怎样修改 use-def
├── 怎样删除 operation
└── 怎样 signalPassFailure
```

然后下一阶段再进入：

```text
Pass
  ↓
PatternRewriter
  ↓
Declarative Rewrite / Conversion
```

---

# 29. 和 AI Compiler 的关系

你现在这个：

```text
simplify-add
```

只是极小的教学 pass。

但 AI 编译器中的很多实际工作仍然遵循相同模式：

```text
读某种 IR
↓
匹配某些 operation
↓
分析 shape / type / attribute / use-def
↓
创建或替换 operation
↓
删除旧 operation
↓
进入下一层 IR
```

例如未来可能出现：

```text
Tensor / Linalg / TOSA / StableHLO
        ↓
pattern / pass
        ↓
Linalg / SCF / Affine
        ↓
lowering pass
        ↓
LLVM / GPU / accelerator dialect
```

所以你现在学习：

```cpp
walk
Value
OpOperand
OpBuilder
erase
Region
Block
```

不是“玩具 API”。

它们就是之后理解 lowering、fusion、canonicalization、bufferization 等流程的基础。

---

# 30. 推荐的实际练习顺序

严格按照下面顺序做，不要一次把最终版复制进去：

```text
Step 0
只 emitRemark
目标：证明 pass 构建、注册、pipeline 都正确

Step 1
walk + dump
目标：学会寻找 arith.addi

Step 2
OperationState + detached operation
目标：理解 operation 是怎样被描述和创建的

Step 3
OpBuilder
目标：第一次真正向 Block 插入 operation

Step 4
OpOperand::set
目标：理解 use-def 链可以怎样修改

Step 5
use_empty + erase
目标：学习删除 operation 的基本安全条件

Step 6
Region / Block / Type / Attribute
目标：建立 MLIR IR 层级感

最后
只保留真正符合 pass 语义的修改
```

每一轮都：

```bash
make build
make pass-test
```

然后比较：

```text
修改前 MLIR
修改后 MLIR
stderr 调试信息
```

---

# 31. 这一阶段常见错误

## 31.1 `unknown pass 'simplify-add'`

说明 pass 没注册成功。

检查：

```cpp
tinyir::registerSimplifyAddPass();
```

以及：

```bash
build/bin/tinyir-opt --help | grep simplify-add
```

---

## 31.2 `can't add pass ... restricted to 'func.func'`

通常说明 pipeline 层级写错。

你的 pass 是：

```cpp
OperationPass<mlir::func::FuncOp>
```

所以用：

```bash
--pass-pipeline='builtin.module(func.func(simplify-add))'
```

---

## 31.3 修改了 `SimplifyAdd.cpp`，但执行结果没变化

先看：

```bash
make build
```

是否真的出现：

```text
+CXX src/passes/SimplifyAdd.cpp --> ...
```

如果没有，检查 Makefile 的：

```make
PASS_SRCS
PASS_OBJS
```

和 `.d` 依赖。

---

## 31.4 链接时报 `undefined reference`

说明：

```text
声明存在
但是实现没有被链接进去
```

先确认：

```bash
ar t build/lib/libtinyir.a
```

应能看到：

```text
SimplifyAdd.o
```

---

## 31.5 `erase()` 后崩溃

常见原因：

```text
1. operation 的 result 还有 users
2. 遍历过程中错误破坏正在 walk 的结构
3. 后面的代码还在使用已经 erase 的 operation wrapper
```

初学阶段坚持：

```text
先收集
后删除
```

---

# 32. 当前阶段最终应该掌握什么

完成这份讲义后，你不需要背所有 MLIR API。

只要真正理解下面这张图：

```text
.mlir
  ↓ parser
Operation / Region / Block / Value
  ↓
PassManager
  ↓
runOnOperation()
  ├── walk        查询 operation
  ├── getLhs      取得 Value
  ├── getOpOperand取得 use
  ├── OpBuilder   创建 operation
  ├── set         修改输入边
  ├── erase       删除 operation
  └── setAttr     修改 attribute
  ↓
printer
  ↓
新的 .mlir
```

以及工程链：

```text
src/passes/SimplifyAdd.cpp
        ↓
build/obj/passes/SimplifyAdd.o
        ↓
build/lib/libtinyir.a
        ↓
build/bin/tinyir-opt
        ↓
registerSimplifyAddPass()
        ↓
--pass-pipeline='builtin.module(func.func(simplify-add))'
```

---

# 33. 完成本阶段后的目录树

最终大致：

```text
.
├── Makefile
├── build
│   ├── bin
│   │   └── tinyir-opt
│   ├── gen
│   │   ├── List_Dialect.cpp.inc
│   │   ├── List_Dialect.h.inc
│   │   ├── List_Ops.cpp.inc
│   │   ├── List_Ops.h.inc
│   │   ├── List_Types.cpp.inc
│   │   ├── List_Types.h.inc
│   │   ├── Tutorial_Dialect.cpp.inc
│   │   ├── Tutorial_Dialect.h.inc
│   │   ├── Tutorial_Ops.cpp.inc
│   │   ├── Tutorial_Ops.h.inc
│   │   ├── Tutorial_Types.cpp.inc
│   │   └── Tutorial_Types.h.inc
│   ├── lib
│   │   └── libtinyir.a
│   └── obj
│       ├── dialect
│       │   ├── List.d
│       │   ├── List.o
│       │   ├── Tutorial.d
│       │   └── Tutorial.o
│       ├── passes
│       │   ├── SimplifyAdd.d
│       │   └── SimplifyAdd.o
│       ├── tinyir-opt.d
│       └── tinyir-opt.o
├── requirements.txt
├── src
│   ├── dialect
│   │   ├── List.cpp
│   │   ├── List.h
│   │   ├── List.td
│   │   ├── Tutorial.cpp
│   │   ├── Tutorial.h
│   │   └── Tutorial.td
│   ├── passes
│   │   ├── Passes.h
│   │   └── SimplifyAdd.cpp
│   └── tinyir-opt.cpp
└── test
    ├── mytest1.mlir
    ├── mytest2.mlir
    └── passes
        └── simpleAdd.mlir
```

---

# 34. 下一阶段该学什么

完成本讲义以后，建议按照：

```text
① Pass 基础
   ↓
② RewritePattern / PatternRewriter
   ↓
③ canonicalization
   ↓
④ Pass TableGen（自动生成 pass boilerplate）
   ↓
⑤ Dialect Conversion
   ↓
⑥ TypeConverter + ConversionTarget
   ↓
⑦ 多级 lowering
```

对于 AI 编译器学习，最值得重点掌握的是：

```text
PatternRewriter
Dialect Conversion
Type Conversion
Shape/Type reasoning
Linalg / Tensor / SCF / Affine
Bufferization
LLVM/GPU lowering
```

但在进入它们之前，先确保你能不看答案解释：

```text
为什么 SimplifyAddPass 的 OperationPass 类型是 func::FuncOp？
为什么 pipeline 里要嵌套 func.func？
Value 和 OpOperand 有什么区别？
为什么 erase 前必须处理 uses？
为什么 SimplifyAdd.cpp 不能直接执行？
为什么 pass 编译进静态库后还需要注册？
```

如果这六个问题已经能清楚回答，就可以进入 PatternRewriter。
