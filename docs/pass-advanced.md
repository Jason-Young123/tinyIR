# `pass-advanced.md`：tinyIR 的 MLIR C++ IR API 进阶教程

> 适用工程：当前 `tinyIR` 项目  
> 前置教程：已经完成 `pass.md`，能注册、运行手写 `OperationPass<func::FuncOp>`，并实践过 `walk`、`OperationState`、`OpBuilder`、`OpOperand::set`、`replaceAllUsesWith`、`erase`、Region / Block / Type / Attribute 的基础观察。  
> 对照课件：**MLIR Fundamentals: Introduction to C++ IR Data Structures and APIs**, Matthias Springer, ACM Europe School on MLIR 2026。  
> 本教程目标：把 `pass.md` 中“为了第一次跑通 Pass 而刻意略过”的 C++ IR 核心 API 补齐，并把课件中最重要的 **IR 数据结构、链表语义、use-def、Block/Region、Builder/Rewriter、Pass 调试**真正落到 tinyIR 仓库中。

---

# 0. 先给结论：当前 `pass.md` 是否缺了很多核心操作？

**是，但这是有意的，不是写错了。**

`pass.md` 的定位是：

```text
第一次跑通 Pass
    ↓
能 walk Operation
    ↓
能创建 / 插入 Operation
    ↓
能改 operand
    ↓
能做 RAUW
    ↓
能安全 erase
```

这条主线没有问题，而且非常适合作为第一讲。

但如果以这份 97 页课件为基准，`pass.md` 目前只覆盖了课件的一部分。真正还需要补齐的内容主要有：

| 课件主题 | `pass.md` 当前状态 | 本教程 |
|---|---|---|
| Operation / OpResult / Value / OpOperand | 已有基础 | 补生命周期、复杂度、typed/generic API |
| `isa` / `cast` / `dyn_cast` | 基本缺失 | **补齐** |
| parent Block / prev-next Operation | 缺失 | **补齐** |
| Block 内双向链表与移动 Operation | 缺失 | **补齐** |
| `getUses()` / `getUsers()` | 只讲 RAUW 概念 | **系统补齐** |
| 修改 use 时的 iterator invalidation | 缺失 | **重点补齐** |
| 多 result Operation 的删除安全 | 讲得不够完整 | **补齐** |
| BlockArgument | 只有概念 | **补齐 API 与陷阱** |
| 多 Block / CFG | 基本缺失 | **补齐** |
| BlockOperand / successor use-def | 缺失 | **补齐** |
| Region / Block / Operation 三层所有权 | 只有观察 | **系统补齐** |
| Type uniquing / 修改类型的全局一致性 | 只有基础 | **补齐** |
| Attribute / DictionaryAttr / 代价 | 只有基础 | **补齐** |
| Properties | 缺失 | **补概念与边界** |
| InsertionGuard | 缺失 | **补齐** |
| dominance 与错误 insertion point | 缺失 | **重点补齐** |
| Builder / OpBuilder / Rewriter 分工 | PatternRewriter 刻意没讲 | **补齐** |
| `clone` / `replaceOp` / `mergeBlock` 等 | 缺失 | **补接口地图** |
| Pass failure | 缺失 | **补齐** |
| `-mlir-print-ir-before/after-all` | 缺失 | **补齐** |
| `--verify-each=0` | 缺失 | **补齐** |

所以这份 `pass-advanced.md` 不是推翻 `pass.md`，而是把它从：

```text
“我会写第一个 Pass”
```

继续推进到：

```text
“我理解 MLIR IR 在 C++ 内存里到底怎么组织，
并且知道怎样安全地导航、改写和调试它。”
```

---

# 1. 对照课件：我们接下来真正要掌握的 IR 心智模型

课件一开始要求区分：

```text
Operation
Region
Block
BlockArgument
OpResult
OpOperand
Value
```

把它画成一张完整关系图：

```text
Operation
├── operands[] ----------------------┐
│    └── OpOperand ------------------┼──> Value
│                                    │     ├── OpResult
├── results[] -----------------------┘     └── BlockArgument
│
├── attributes / properties
│
└── regions[]
     └── Region
          └── blocks[]
               └── Block
                    ├── arguments[] -> BlockArgument
                    └── operations[]
                         └── Operation
```

这里最容易混淆的是：

```text
Value ≠ Operation
Value ≠ OpOperand
```

更精确地说：

```text
Value
├── OpResult       // 由 Operation 定义
└── BlockArgument  // 由 Block 定义

OpOperand          // 某个 Operation 对某个 Value 的“一次使用”
```

例如：

```mlir
%r = arith.addi %a, %b : i32
```

对应：

```text
arith.addi                  Operation

%a                          Value
%b                          Value
%r                          Value / OpResult

arith.addi 的 operand #0    OpOperand，引用 %a
arith.addi 的 operand #1    OpOperand，引用 %b
```

后面所有 RAUW、DCE、rewriter，本质都建立在这张图上。

---

# 2. 延续 `pass.md` 后的目录树

`pass.md` 结束时你大致有：

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
    └── passes/
        └── simpleAdd.mlir
```

现在继续增加：

```text
tinyIR/
├── src/
│   └── passes/
│       ├── Passes.h
│       ├── SimplifyAdd.cpp
│       ├── InspectIR.cpp
│       ├── UseDef.cpp
│       ├── CFGInspect.cpp
│       ├── BuilderSafety.cpp
│       └── RewriterDemo.cpp
└── test/
    └── passes/
        ├── simpleAdd.mlir
        └── advanced/
            ├── inspect-ir.mlir
            ├── use-def.mlir
            ├── cfg.mlir
            ├── builder.mlir
            └── rewriter.mlir
```

职责变成：

```text
SimplifyAdd.cpp     pass 入门：最小可运行变换
InspectIR.cpp       Operation / Block / Region / cast / 链表导航
UseDef.cpp          Value / OpOperand / uses / users / RAUW
CFGInspect.cpp      BlockArgument / successor / BlockOperand / Region
BuilderSafety.cpp   insertion point / InsertionGuard / dominance
RewriterDemo.cpp    从裸 API 过渡到 Rewriter 风格
```

你的 Makefile 已经使用：

```make
PASS_SRCS := $(wildcard $(PASS_DIR)/*.cpp)
```

因此这些 `.cpp` 不需要再逐个写构建规则；新增文件后会自动参与编译。

---

# 3. 先扩展 `Passes.h`

把：

```cpp
namespace tinyir {

void registerSimplifyAddPass();

}
```

扩成：

```cpp
#ifndef TINYIR_PASSES_H
#define TINYIR_PASSES_H

namespace tinyir {

void registerSimplifyAddPass();
void registerInspectIRPass();
void registerUseDefPass();
void registerCFGInspectPass();
void registerBuilderSafetyPass();
void registerRewriterDemoPass();

} // namespace tinyir

#endif
```

并在 `tinyir-opt.cpp` 中继续注册：

```cpp
tinyir::registerSimplifyAddPass();
tinyir::registerInspectIRPass();
tinyir::registerUseDefPass();
tinyir::registerCFGInspectPass();
tinyir::registerBuilderSafetyPass();
tinyir::registerRewriterDemoPass();
```

这样每一讲都可以单独运行。

---

# 4. Step 1：typed Operation 与 generic `Operation *`

课件第 22、25 页的关键不是“有两种写法”，而是：

```text
arith::AddIOp
```

本质上只是对：

```text
Operation *
```

的一层 typed wrapper。

例如：

```cpp
mlir::arith::AddIOp add = ...;
mlir::Operation *generic = add.getOperation();
```

反过来：

```cpp
mlir::Operation *op = ...;

if (auto add = llvm::dyn_cast<mlir::arith::AddIOp>(op)) {
  ...
}
```

三种 cast 要分清：

```cpp
llvm::isa<mlir::arith::AddIOp>(op)
```

只问：

```text
“是不是？”
```

返回 `bool`。

```cpp
llvm::dyn_cast<mlir::arith::AddIOp>(op)
```

问：

```text
“如果是，就给我 typed wrapper；否则返回空。”
```

适合不确定类型。

```cpp
llvm::cast<mlir::arith::AddIOp>(op)
```

表示：

```text
“我确定它就是 AddIOp。”
```

如果不是，会 assert。

## 4.1 为什么 typed op 通常按值传？

推荐：

```cpp
void inspectAdd(mlir::arith::AddIOp add);
```

而不是：

```cpp
void inspectAdd(mlir::arith::AddIOp *add);
```

因为 typed op 自身非常轻量，本质上就是 wrapper。

相反，generic Operation 应该：

```cpp
void inspectGeneric(mlir::Operation *op);
```

而不是按值：

```cpp
void inspectGeneric(mlir::Operation op); // 不允许
```

`Operation` 不能随便 copy，因为它参与 parent、use-def、Region/Block 等大量内部结构。

---

# 5. Step 2：实现 `InspectIRPass`

创建：

```text
src/passes/InspectIR.cpp
```

核心代码：

```cpp
#include "passes/Passes.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/Operation.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Pass/PassRegistry.h"
#include "llvm/Support/raw_ostream.h"

namespace {

struct InspectIRPass
    : mlir::PassWrapper<
          InspectIRPass,
          mlir::OperationPass<mlir::func::FuncOp>> {

  mlir::StringRef getArgument() const override {
    return "inspect-ir";
  }

  mlir::StringRef getDescription() const override {
    return "Inspect MLIR Operation/Block/Region structure";
  }

  void runOnOperation() override {
    mlir::func::FuncOp func = getOperation();

    func.walk([&](mlir::Operation *op) {
      llvm::errs() << "\n=== operation ===\n";
      llvm::errs() << "name      : "
                   << op->getName().getStringRef() << "\n";
      llvm::errs() << "operands  : "
                   << op->getNumOperands() << "\n";
      llvm::errs() << "results   : "
                   << op->getNumResults() << "\n";
      llvm::errs() << "regions   : "
                   << op->getNumRegions() << "\n";

      if (mlir::Block *block = op->getBlock()) {
        llvm::errs() << "has parent block\n";

        if (mlir::Operation *prev = op->getPrevNode())
          llvm::errs() << "prev      : "
                       << prev->getName() << "\n";

        if (mlir::Operation *next = op->getNextNode())
          llvm::errs() << "next      : "
                       << next->getName() << "\n";
      }

      if (auto add =
              llvm::dyn_cast<mlir::arith::AddIOp>(op)) {
        llvm::errs() << "typed as arith.addi\n";
        llvm::errs() << "lhs       : ";
        add.getLhs().dump();
        llvm::errs() << "rhs       : ";
        add.getRhs().dump();
      }
    });
  }
};

} // namespace

void tinyir::registerInspectIRPass() {
  mlir::PassRegistration<InspectIRPass>();
}
```

测试：

```bash
build/bin/tinyir-opt test/passes/advanced/inspect-ir.mlir \
  --pass-pipeline='builtin.module(func.func(inspect-ir))'
```

这一讲要建立：

```text
typed op 适合“我知道这是哪种 op”
generic Operation * 适合“我在写通用 IR 基础设施”
```

---

# 6. Step 3：Operation 在 Block 里不是数组，而是双向链表

这是课件里非常重要、`pass.md` 基本没展开的一点。

一个 Block 内：

```text
op0 <-> op1 <-> op2 <-> op3
```

Operations 形成双向链表。

因此：

```cpp
op->getPrevNode();
op->getNextNode();
```

是自然操作。

Block 也能：

```cpp
block->front();
block->back();

block->begin();
block->end();

block->rbegin();
block->rend();
```

还可以：

```cpp
block->getOps<mlir::arith::AddIOp>();
```

## 6.1 为什么“取第 i 个 Operation”不自然？

如果你想：

```text
获取 block 中第 1000 个 operation
```

没有数组式的 O(1) 随机访问。

你需要从头走链表：

```text
front
 ↓
next
 ↓
next
 ↓
...
```

复杂度：

```text
O(i)
```

这说明 MLIR IR 数据结构优化的是：

```text
插入
删除
移动
遍历
use-def 改写
```

而不是：

```text
按 index 随机访问
```

这对理解 MLIR 的性能非常重要。

---

# 7. Step 4：移动 Operation

课件专门强调：

```cpp
op->moveBefore(other);
op->moveAfter(other);
```

由于 Block 内部是双向链表，移动一个 op 的结构成本通常接近常数级链表更新。

例如：

```cpp
mlir::Operation *c0 = ...;
mlir::Operation *c1 = ...;

c1->moveBefore(c0);
```

但这里有一个非常重要的语义限制：

> **链表上能移动，不代表 SSA / dominance 上合法。**

例如：

```mlir
%c0 = arith.constant 0 : i32
%c1 = arith.constant 1 : i32
%r  = arith.addi %c0, %c1 : i32
```

如果你把 `%r` 移到 `%c1` 前：

```mlir
%c0 = arith.constant 0 : i32
%r  = arith.addi %c0, %c1 : i32
%c1 = arith.constant 1 : i32
```

虽然链表操作本身成功了，但 IR 违反 dominance。

所以必须区分：

```text
数据结构允许
≠
IR 语义合法
```

---

# 8. Step 5：OpResult 的查询、复杂度与生命周期

generic Operation：

```cpp
mlir::OpResult r = op->getResult(0);
```

typed op：

```cpp
mlir::Value r = add.getResult();
```

课件强调：

```text
查询第 i 个 result 通常是 O(1)
```

这与前面的 Block operation 链表形成对比：

```text
operation result index       → O(1)
block 第 i 个 operation      → O(i)
```

## 8.1 最危险的生命周期错误

不要这样：

```cpp
mlir::Value v = op->getResult(0);
op->erase();

llvm::errs() << v.getType(); // 错
```

因为如果 `v` 是：

```text
OpResult
```

它的生命周期依附于 defining Operation。

Operation 被销毁后：

```text
v 内部指向 defining op 的状态已经失效
```

这就是 use-after-free。

因此：

```text
Value 是轻量 handle
```

不意味着：

```text
Value 独立拥有底层对象
```

---

# 9. Step 6：为什么不能“原地给 Operation 增加 result”？

假设：

```mlir
%0 = "test.dummy"() : () -> i32
```

想变成：

```mlir
%0, %1 = "test.dummy"() : () -> (i32, i32)
```

不能简单：

```cpp
oldOp->addResult(...)
```

因为 Operation 的内存布局在创建时就已经根据 operand/result/region 数量确定。

典型做法是：

```text
创建新的 Operation
    ↓
把旧 result 的 uses RAUW 到新 result
    ↓
erase 旧 Operation
```

这条原则以后做：

```text
signature conversion
dialect lowering
结构性 op rewrite
```

时都会反复出现。

---

# 10. Step 7：`OpOperand` 是 use-def 链的关键节点

再看：

```mlir
%c2 = arith.addi %c1, %c1 : i32
```

虽然文本里 `%c1` 写了两次，但 C++ IR 里对应的是：

```text
OpOperand #0 ──> %c1
OpOperand #1 ──> %c1
```

两个不同的 `OpOperand`。

所以：

```cpp
op->getOperand(0)
```

返回：

```text
Value
```

而：

```cpp
op->getOpOperand(0)
```

返回：

```text
OpOperand&
```

区别：

```text
Value         = “被引用的是谁”
OpOperand     = “哪一个输入槽位正在引用它”
```

想修改某一个 use，必须操作：

```cpp
mlir::OpOperand &use = op->getOpOperand(0);
use.set(newValue);
```

或者：

```cpp
op->setOperand(0, newValue);
```

---

# 11. Step 8：系统学习 `getUses()` 与 `getUsers()`

给测试：

```mlir
module {
  func.func @use_def(%x: i32) -> i32 {
    %c1 = arith.constant 1 : i32
    %a = arith.addi %x, %c1 : i32
    %b = arith.addi %a, %c1 : i32
    %c = arith.addi %a, %b : i32
    return %c : i32
  }
}
```

对 `%a`：

```cpp
for (mlir::OpOperand &use : a.getUses()) {
  mlir::Operation *owner = use.getOwner();

  llvm::errs() << "used by "
               << owner->getName()
               << " operand #"
               << use.getOperandNumber()
               << "\n";
}
```

如果只关心 user operation：

```cpp
for (mlir::Operation *user : a.getUsers()) {
  llvm::errs() << user->getName() << "\n";
}
```

区别：

```text
getUses()
    返回每一个 OpOperand
    同一个 user 如果使用两次 Value，会出现两次

getUsers()
    返回 user Operation
    更适合只关心“谁在用”
```

## 11.1 use 顺序不要当作稳定顺序

课件特别强调：

```text
uses have no deterministic order
```

所以不能写这种逻辑：

```text
“第一个 use 一定是某个 add”
```

use-def 链本质是为了高效插入/移除 use，而不是提供稳定排序。

---

# 12. Step 9：为什么修改 use 时需要 early increment？

这是 MLIR / LLVM 风格容器里非常重要的一个坑。

危险写法：

```cpp
for (mlir::OpOperand &use : value.getUses()) {
  use.set(other);
}
```

为什么危险？

因为：

```cpp
use.set(other)
```

会把当前 `OpOperand`：

```text
从 oldValue 的 use-list 摘掉
```

再：

```text
挂到 newValue 的 use-list
```

也就是说你正在遍历一个链表，同时修改这个链表。

课件给出的安全思想：

```cpp
for (mlir::OpOperand &use :
     llvm::make_early_inc_range(value.getUses())) {
  use.set(other);
}
```

`make_early_inc_range` 的关键语义：

```text
先把 iterator 前移
再把当前 element 交给循环体
```

所以即使循环体把当前 use 从链表中摘掉，也不会破坏“下一步往哪里走”。

## 12.1 为什么 `replaceAllUsesWith` 更好？

如果你只是：

```text
把 oldValue 的所有 use 换成 newValue
```

直接：

```cpp
oldValue.replaceAllUsesWith(newValue);
```

更清晰。

手写 early-inc 主要用于：

```text
只替换部分 uses
按 user 类型筛选
按 block / region 条件筛选
教学理解底层
```

---

# 13. Step 10：实现 `UseDefPass`

创建：

```text
src/passes/UseDef.cpp
```

建议先做只读版本：

```cpp
#include "passes/Passes.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Pass/PassRegistry.h"
#include "llvm/Support/raw_ostream.h"

namespace {

struct UseDefPass
    : mlir::PassWrapper<
          UseDefPass,
          mlir::OperationPass<mlir::func::FuncOp>> {

  mlir::StringRef getArgument() const override {
    return "use-def-demo";
  }

  void runOnOperation() override {
    mlir::func::FuncOp func = getOperation();

    func.walk([&](mlir::arith::AddIOp add) {
      mlir::Value result = add.getResult();

      llvm::errs() << "\nvalue: ";
      result.dump();

      for (mlir::OpOperand &use : result.getUses()) {
        llvm::errs()
            << "  user = "
            << use.getOwner()->getName()
            << ", operand #"
            << use.getOperandNumber()
            << "\n";
      }
    });
  }
};

} // namespace

void tinyir::registerUseDefPass() {
  mlir::PassRegistration<UseDefPass>();
}
```

这一阶段先不要修改 IR。

---

# 14. Step 11：安全判断 dead Operation——不能只看一个 result
 
课件给了一个非常关键的反例：

```cpp
mlir::OpResult v = ...;

if (v.use_empty())
  v.getDefiningOp()->erase();
```

仍然可能错。

为什么？

因为 defining op 可能有多个 results：

```mlir
%a, %b = "test.two_results"() : () -> (i32, i32)
```

可能：

```text
%a 没人用
%b 仍然有人用
```

只看 `%a.use_empty()` 就 erase 整个 op 会把 `%b` 的定义一起删掉。

更安全的 generic 判断：

```cpp
bool allUnused = llvm::all_of(
    op->getResults(),
    [](mlir::Value v) {
      return v.use_empty();
    });

if (allUnused)
  op->erase();
```

但还要再问一个问题：

```text
这个 Operation 有没有 side effect？
```

例如：

```text
store
I/O
call with side effects
```

即使没有 SSA result，也不能因为“没人用”就删除。

所以真正通用 DCE 不应该只靠：

```text
result.use_empty()
```

而应借助 MLIR 的 side-effect interfaces / 现有 canonicalization / DCE 基础设施。

在 tinyIR 教学阶段，先把 pass 限定到：

```text
你明确知道是纯计算的 op
```

例如部分 `arith` op。

---

# 15. Step 12：`BlockArgument` 不是 `OpResult`

课件第 61 页之后开始补另一半 Value：

```text
Value
├── OpResult
└── BlockArgument
```

例如：

```mlir
func.func @foo(%x: i32, %y: i32) {
  ...
}
```

入口块：

```text
^bb0(%x: i32, %y: i32)
```

所以 `%x`、`%y` 是：

```text
BlockArgument
```

不是某个 Operation 的 result。

查询：

```cpp
mlir::Block &entry = func.getBody().front();

mlir::BlockArgument x = entry.getArgument(0);
mlir::BlockArgument y = entry.getArgument(1);

assert(x.getOwner() == &entry);
```

## 15.1 一个非常常见的 bug

错误：

```cpp
mlir::Operation *def = value.getDefiningOp();
def->dump();
```

如果 `value` 是 BlockArgument：

```cpp
value.getDefiningOp()
```

会得到：

```text
nullptr
```

因为它根本不是 Operation 定义的。

所以：

```cpp
if (mlir::Operation *def = value.getDefiningOp()) {
  ...
} else {
  // 很可能是 BlockArgument
}
```

或者先判断 Value 的具体种类。

这也是为什么课件用 `isa_and_present` / typed `getDefiningOp<OpTy>()` 来强调 null-safe 查询。

---

# 16. Step 13：从单 Block 进入真正 CFG

到现在为止，你的很多例子都是：

```text
func
 └── 一个 block
```

但真实 MLIR 可以：

```text
Region
├── ^bb0
├── ^bb1
└── ^bb2
```

测试：

```mlir
module {
  func.func @choose(%c: i1, %a: i32, %b: i32) -> i32 {
    cf.cond_br %c, ^bb1(%a : i32), ^bb2(%b : i32)

  ^bb1(%x: i32):
    cf.br ^bb3(%x : i32)

  ^bb2(%y: i32):
    %z = arith.addi %y, %y : i32
    cf.br ^bb3(%z : i32)

  ^bb3(%r: i32):
    return %r : i32
  }
}
```

这里：

```text
^bb0
 ├── successor -> ^bb1
 └── successor -> ^bb2

^bb1 -> ^bb3
^bb2 -> ^bb3
```

---

# 17. Step 14：Block 在 Region 中也形成链式结构

Operation 在 Block 内是双向链表。

类似地：

```text
Block 在 Region 内也有前后关系
```

可以：

```cpp
mlir::Block *next = block->getNextNode();
mlir::Block *prev = block->getPrevNode();
```

Region：

```cpp
mlir::Region &body = func.getBody();

mlir::Block &first = body.front();
mlir::Block &last  = body.back();
```

遍历：

```cpp
for (mlir::Block &block : body) {
  ...
}
```

重要区别：

```text
Block 的前后顺序
```

不是：

```text
CFG successor / predecessor 关系
```

例如文本里：

```text
^bb1 紧挨着 ^bb2
```

并不意味着：

```text
^bb1 -> ^bb2
```

CFG 边是 terminator operation 的 successor 关系决定的。

---

# 18. Step 15：Region 到底是什么？

Region 不是“另一个 Block”。

Region 是：

```text
Operation 内部对一组 Blocks 的语义分组
```

关系：

```text
Operation
└── Region
    ├── Block
    ├── Block
    └── Block
```

例如：

```mlir
scf.while ...
```

可以有多个 Region：

```text
scf.while
├── before region
└── after region
```

generic API：

```cpp
mlir::Region &r0 = op->getRegion(0);

assert(r0.getParentOp() == op);
```

Region 内：

```cpp
mlir::Block &entry = r0.front();
```

这说明：

```text
Operation contains Region
Region contains Block
Block contains Operation
```

形成递归嵌套。

---

# 19. Step 16：BlockOperand——CFG 版本的 OpOperand

这是当前 `pass.md` 最明显缺的一块之一。

对 SSA Value：

```text
OpOperand = use of Value
```

对 CFG Block：

```text
BlockOperand = use of Block
```

例如：

```mlir
cf.cond_br %c, ^bb1(...), ^bb2(...)
```

这条 terminator “引用”两个 successor block。

generic API：

```cpp
mlir::BlockOperand &bo =
    op->getBlockOperands()[0];

mlir::Block *target = bo.get();

mlir::Operation *owner = bo.getOwner();
unsigned index = bo.getOperandNumber();
```

也可以：

```cpp
mlir::Block *succ = op->getSuccessor(0);
```

可以把二者对照记忆：

```text
SSA use-def:
Value
  ↑
OpOperand
  ↑
Operation

CFG use-def:
Block
  ↑
BlockOperand
  ↑
Operation
```

---

# 20. Step 17：Block 也有 `getUses()` / `getUsers()`

和 Value 一样：

```cpp
for (mlir::BlockOperand &use : block.getUses()) {
  ...
}

for (mlir::Operation *user : block.getUsers()) {
  ...
}
```

典型 user：

```text
cf.br
cf.cond_br
```

同样需要注意：

```text
Block uses 的顺序也不保证稳定
```

并且修改 successor 时也要警惕：

```text
遍历 use-list 的同时修改 use-list
```

这与 SSA `OpOperand` 完全对称。

---

# 21. Step 18：实现 `CFGInspectPass`

创建：

```text
src/passes/CFGInspect.cpp
```

核心：

```cpp
#include "passes/Passes.h"

#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/Block.h"
#include "mlir/IR/Operation.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Pass/PassRegistry.h"
#include "llvm/Support/raw_ostream.h"

namespace {

struct CFGInspectPass
    : mlir::PassWrapper<
          CFGInspectPass,
          mlir::OperationPass<mlir::func::FuncOp>> {

  mlir::StringRef getArgument() const override {
    return "cfg-inspect";
  }

  void runOnOperation() override {
    mlir::func::FuncOp func = getOperation();
    mlir::Region &body = func.getBody();

    unsigned blockIndex = 0;

    for (mlir::Block &block : body) {
      llvm::errs()
          << "\n=== block "
          << blockIndex++
          << " ===\n";

      llvm::errs()
          << "arguments: "
          << block.getNumArguments()
          << "\n";

      for (mlir::BlockArgument arg :
           block.getArguments()) {
        llvm::errs()
            << "  arg #"
            << arg.getArgNumber()
            << ": ";
        arg.dump();
      }

      if (block.empty())
        continue;

      mlir::Operation &terminator = block.back();

      llvm::errs()
          << "terminator: "
          << terminator.getName()
          << "\n";

      llvm::errs()
          << "successors: "
          << terminator.getNumSuccessors()
          << "\n";

      for (unsigned i = 0;
           i < terminator.getNumSuccessors();
           ++i) {
        mlir::Block *succ =
            terminator.getSuccessor(i);

        llvm::errs()
            << "  successor #"
            << i
            << " has "
            << succ->getNumArguments()
            << " block args\n";
      }
    }
  }
};

} // namespace

void tinyir::registerCFGInspectPass() {
  mlir::PassRegistration<CFGInspectPass>();
}
```

运行：

```bash
build/bin/tinyir-opt test/passes/advanced/cfg.mlir \
  --pass-pipeline='builtin.module(func.func(cfg-inspect))'
```

---

# 22. Step 19：课件为什么反复强调“linked list”？

到这里可以总结：

```text
Operation in Block          双向链表
Block in Region             双向链表
OpOperand uses of Value     use-list
BlockOperand uses of Block  use-list
```

这套结构带来的优化目标是：

```text
insert operation        快
erase operation         快
move operation          快
replace uses            与 use 数量成正比
walk IR                 自然
```

不是：

```text
random access by index
count everything repeatedly
```

因此写 pass 时，常见好习惯：

```text
不要频繁“取第 N 个 op”
不要无意义反复 getNumUses() 做完整计数
如果只想知道是否有 use，优先 use_empty()
如果会修改 use-list，使用安全 rewrite API 或 early-inc
```

---

# 23. Step 20：Type——它是 Value 的编译期元数据

课件对 Type 的核心定义：

```text
Type 是 SSA Value 的 compile-time constant metadata
```

例如：

```cpp
mlir::Type t = value.getType();
```

构造：

```cpp
mlir::OpBuilder b(func.getContext());

mlir::Type i16 = b.getI16Type();
mlir::Type i32 = b.getI32Type();
mlir::Type i64 = b.getI64Type();
```

也可以：

```cpp
value.setType(i16);
```

但：

> **能调用 `setType()` 不代表只改这一处就得到合法 IR。**

---

# 24. Step 21：为什么“把 i32 改成 i16”不是一行代码？

例如：

```mlir
func.func @test(%a: i32, %b: i32) -> i32 {
  %0 = arith.addi %a, %b : i32
  return %0 : i32
}
```

如果只：

```cpp
add.getResult().setType(builder.getI16Type());
```

会出现：

```text
function argument 还是 i32
add operands 还是 i32
add result 变成 i16
return/function signature 仍然写 i32
```

整个 IR 的类型系统已经不一致。

完整修改至少涉及：

```text
1. BlockArgument 类型
2. Operation result 类型
3. function_type attribute
4. 可能还有所有依赖该类型的其他 op / attributes
```

这就是为什么真正的类型转换通常不会靠：

```cpp
Value::setType
```

到处手改。

后面 Dialect Conversion 会提供更系统的机制。

---

# 25. Step 22：Type / Attribute 的 uniquing

课件强调：

```text
Type 和 Attribute 通常由 MLIRContext uniqued
```

意思不是：

```text
每调用一次 builder.getI32Type() 都 new 一个对象
```

而更接近：

```text
“相同的结构，在同一个 MLIRContext 中复用 canonical storage”
```

因此：

```cpp
auto a = builder.getI32Type();
auto b = builder.getI32Type();
```

逻辑上是同一个 uniqued Type。

这也是为什么：

```text
MLIRContext
```

不只是“放 dialect registry 的地方”。

它还承担：

```text
Type / Attribute storage
uniquing
部分线程同步
```

等职责。

---

# 26. Step 23：Attribute——Operation 的编译期元数据

Type 更偏：

```text
Value 的类型信息
```

Attribute 更偏：

```text
Operation 的常量元信息
```

例如：

```mlir
%0 = arith.addi %a, %b {foo = 42 : i64} : i32
```

可以：

```cpp
mlir::OpBuilder b(func.getContext());

add->setAttr(
    "foo",
    b.getI64IntegerAttr(42));
```

读取：

```cpp
if (mlir::Attribute attr =
        add->getAttr("foo")) {
  attr.dump();
}
```

删除：

```cpp
add->removeAttr("foo");
```

---

# 27. Step 24：为什么添加 Attribute 不是“一个 map insert”这么简单？

课件第 80–82 页强调性能模型。

Operation 的 attributes 逻辑上形成：

```text
DictionaryAttr
```

而 key/value 又涉及：

```text
StringAttr
Attribute
```

这些对象通常走 MLIRContext uniquing。

因此：

```cpp
op->setAttr(...)
```

可能涉及：

```text
1. lookup / create value Attribute
2. lookup / create StringAttr key
3. 重建 NamedAttribute 数组
4. lookup / create DictionaryAttr
5. 把新 attribute dictionary 设置到 op
6. 多线程下可能涉及 MLIRContext 同步
```

所以在非常热的 compiler path 上：

```text
频繁动态修改大量 attributes
```

不是免费的。

这也是课件接着引出：

```text
Properties
```

的原因。

---

# 28. Step 25：Properties 是什么？

Properties 也是Operation 的 compile-time constant metadata

但一个核心差异是：
Properties 可以直接存储在 Operation 相关 storage 中

因此某些访问不需要每次都经过 MLIRContext uniquing / DictionaryAttr 重建

在实际 dialect 中，Properties 往往由 op 定义自动生成对应 typed accessor。

在当前 tinyIR 阶段你只需要建立边界：

```text
Attribute
    通用、可打印、可字典化、uniqued

Property
    op-specific、直接 storage、更适合某些频繁访问/内部字段
```

此处不建议为了“练 API”硬造一个 property；等你后面在 TableGen 中为自定义 op 定义 property 时再做。

---

# 29. Step 26：Builder、OpBuilder、Rewriter 的分工

课件第 91 页给了一张非常重要的层次图：

```text
Builder
  ↓
OpBuilder
  ↓
PatternRewriter
  ↓
ConversionPatternRewriter
```

理解成：

```text
Builder
    创建 Type / Attribute
    没有 insertion point

OpBuilder
    在 Builder 基础上
    增加 insertion point
    创建并插入 Operation / Block

PatternRewriter / RewriterBase
    在 OpBuilder 基础上
    增加“我要修改现有 IR”的语义化 API

ConversionPatternRewriter
    用于 Dialect Conversion
```

`pass.md` 里直接使用 OpBuilder 是正确的第一步。

但到了真实 rewrite：

```text
replace
erase
inline
merge
clone
```

更推荐进入 rewriter API。

---

# 30. Step 27：Insertion Point 不只是“插入到哪里”

常用：

```cpp
builder.setInsertionPoint(op);       // before op
builder.setInsertionPointAfter(op);

builder.setInsertionPointToStart(block);
builder.setInsertionPointToEnd(block);
```

最重要的语义：

```text
OpBuilder 的 insertion point
=
“下一次 create 的 Operation 插在哪里”
```

不是：

```text
builder 自己属于哪个 block
```

---

# 31. Step 28：`InsertionGuard`——为什么非常重要？

假设一个函数：

```cpp
void helper(mlir::OpBuilder &builder,
            mlir::Operation *somewhereElse) {
  builder.setInsertionPoint(somewhereElse);
  ...
}
```

调用者原本 insertion point 在 A：

```text
A
```

helper 改到了 B：

```text
B
```

如果 helper 返回时不恢复：

```text
调用者后面的 create 会莫名其妙插到 B
```

解决：

```cpp
void helper(mlir::OpBuilder &builder,
            mlir::Operation *somewhereElse) {
  mlir::OpBuilder::InsertionGuard guard(builder);

  builder.setInsertionPoint(somewhereElse);

  // create...
} // guard 析构，自动恢复旧 insertion point
```

这是一种 RAII。

只要一个 helper 会临时改变 builder insertion point，强烈建议先考虑：

```cpp
InsertionGuard
```

---

# 32. Step 29：Builder 最危险的错误——dominance violation

假设：

```mlir
%c0 = arith.constant 0 : i32
%c1 = arith.constant 1 : i32
```

你：

```cpp
builder.setInsertionPoint(c1.getDefiningOp());

auto add =
    mlir::arith::AddIOp::create(
        builder,
        loc,
        c0,
        c1);
```

`setInsertionPoint(c1)` 表示：

```text
插在 %c1 前面
```

于是：

```mlir
%c0 = arith.constant 0 : i32
%r  = arith.addi %c0, %c1 : i32
%c1 = arith.constant 1 : i32
```

`%r` 在 `%c1` 定义前就使用 `%c1`。

这是：

```text
dominance violation
```

所以 Builder 只负责：

```text
按你给的 insertion point 插入
```

它不会自动替你证明：

```text
所有 operands 在这里都可见且支配当前 use
```

---

# 33. Step 30：实现 `BuilderSafetyPass`

创建：

```text
src/passes/BuilderSafety.cpp
```

做一个**合法**例子：

```cpp
#include "passes/Passes.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/Builders.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Pass/PassRegistry.h"

namespace {

struct BuilderSafetyPass
    : mlir::PassWrapper<
          BuilderSafetyPass,
          mlir::OperationPass<mlir::func::FuncOp>> {

  mlir::StringRef getArgument() const override {
    return "builder-safety";
  }

  void runOnOperation() override {
    mlir::func::FuncOp func = getOperation();

    mlir::arith::AddIOp target = nullptr;

    func.walk([&](mlir::arith::AddIOp add) {
      if (!target)
        target = add;
    });

    if (!target)
      return;

    mlir::OpBuilder builder(func.getContext());

    mlir::OpBuilder::InsertionGuard guard(builder);

    builder.setInsertionPointAfter(target);

    mlir::arith::AddIOp::create(
        builder,
        target.getLoc(),
        target.getLhs(),
        target.getRhs());
  }
};

} // namespace

void tinyir::registerBuilderSafetyPass() {
  mlir::PassRegistration<BuilderSafetyPass>();
}
```

注意这里使用：

```cpp
mlir::arith::AddIOp::create(...)
```

而不是旧式：

```cpp
builder.create<mlir::arith::AddIOp>(...)
```

如果你当前 wheel 对 `builder.create` 给出 deprecated warning，这个版本更贴近你课件使用的现代 API。

---

# 34. Step 31：Builder 也可以创建 detached op 吗？

课件第 89–90 页的核心是：

```text
create / build / insertion point
```

三件事不要混。

你已经在 `pass.md` 中学过：

```cpp
OperationState
Operation::create
```

得到 detached op。

typed op 的：

```cpp
OpTy::build(...)
```

本质上只是：

```text
填 OperationState
```

并不自动意味着：

```text
已经插入某个 Block
```

而：

```cpp
OpTy::create(builder, ...)
```

通常会利用 builder 的 insertion point 完成：

```text
create + insert
```

因此看到 API 名时不要只记函数名，要先问：

```text
这个调用最终有没有 parent Block？
```

---

# 35. Step 32：从裸 IR mutation 过渡到 Rewriter

课件列出了一批常见 rewriter convenience API：

```cpp
builder.clone(...);

rewriter.eraseOp(op);
rewriter.replaceAllUsesWith(...);
rewriter.replaceOp(...);
rewriter.replaceOpWithNewOp<...>(...);

rewriter.mergeBlock(...);
rewriter.inlineBlockBefore(...);
```

这些 API 的价值不是“比 `erase()` 少打几个字”。

更重要的是：

```text
rewrite 基础设施可以统一记录和管理 IR mutation
```

这对后续：

```text
PatternRewriter
GreedyPatternRewriteDriver
Dialect Conversion
listener
```

都非常重要。

---

# 36. Step 33：先用 `IRRewriter` 练习，不急着写 Pattern

为了保持学习曲线平滑，可以先用：

```cpp
mlir::IRRewriter
```

而不是一上来定义：

```cpp
OpRewritePattern<...>
```

创建：

```text
src/passes/RewriterDemo.cpp
```

目标：

```mlir
%r = arith.addi %x, %c0 : i32
```

如果 `%c0` 是常量 0，则：

```text
把所有 %r 的 uses 换成 %x
删除 add
```

概念代码：

```cpp
#include "passes/Passes.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Pass/PassRegistry.h"
#include "llvm/ADT/SmallVector.h"

namespace {

struct RewriterDemoPass
    : mlir::PassWrapper<
          RewriterDemoPass,
          mlir::OperationPass<mlir::func::FuncOp>> {

  mlir::StringRef getArgument() const override {
    return "rewriter-demo";
  }

  void runOnOperation() override {
    mlir::func::FuncOp func = getOperation();

    llvm::SmallVector<mlir::arith::AddIOp> worklist;

    func.walk([&](mlir::arith::AddIOp add) {
      worklist.push_back(add);
    });

    mlir::IRRewriter rewriter(func.getContext());

    for (mlir::arith::AddIOp add : worklist) {
      auto rhsConst =
          add.getRhs()
              .getDefiningOp<mlir::arith::ConstantIntOp>();

      if (!rhsConst)
        continue;

      if (rhsConst.value() != 0)
        continue;

      rewriter.replaceOp(add, add.getLhs());
    }
  }
};

} // namespace

void tinyir::registerRewriterDemoPass() {
  mlir::PassRegistration<RewriterDemoPass>();
}
```

如果你当前 MLIR 版本中 `ConstantIntOp` accessor 名字有细微差异，不要硬背这一行；去看当前生成的：

```text
ArithOps.h.inc
```

或 IDE 跳转到 `ConstantIntOp` 定义。

这一讲真正要学的是：

```text
match
  ↓
建立 worklist
  ↓
rewriter.replaceOp(...)
```

而不是记住某个具体 constant op accessor。

---

# 37. Step 34：为什么先收集 worklist，再 rewrite？

你在 `pass.md` 里已经遇到过：

```text
walk 时插入新 op
```

可能影响遍历。

同理：

```text
walk 时 erase 当前 op
```

也必须考虑 traversal invalidation。

最容易理解且适合学习的策略：

```cpp
SmallVector<OpTy> worklist;

func.walk([&](OpTy op) {
  if (match(op))
    worklist.push_back(op);
});

for (OpTy op : worklist) {
  rewrite(op);
}
```

优点：

```text
遍历阶段只读
修改阶段集中进行
```

等你后面真正进入 PatternRewriter，再让 rewrite driver 管理这一层复杂度。

---

# 38. Step 35：什么时候应该开始 `PatternRewriter`？

`pass.md` 中“先不要急着学 PatternRewriter”是对的。

现在你已经补齐：

```text
Operation ownership
Block / Region
use-def
iterator invalidation
RAUW
insertion point
dominance
rewriter
```

这时再学 pattern 才不会变成：

```text
会写 matchAndRewrite
但不知道 rewriter 到底在维护什么
```

下一阶段建议：

```text
RewritePattern
  ↓
OpRewritePattern<OpTy>
  ↓
RewritePatternSet
  ↓
applyPatternsGreedily
  ↓
canonicalization
```

---

# 39. Step 36：Pass failure 不是 `return`

课件第 95 页：

```cpp
void runOnOperation() override {
  mlir::Operation *op = getOperation();

  if (somethingWrong) {
    op->emitError("pass failed");
    signalPassFailure();
    return;
  }
}
```

区别：

```cpp
return;
```

只表示：

```text
这个 C++ 函数结束
```

而：

```cpp
signalPassFailure();
```

表示：

```text
这个 Pass 的执行失败
```

PassManager 会据此处理 pipeline 失败。

所以当你的 pass 检测到：

```text
无法合法继续 transformation
必要 invariant 不满足
```

不要只打印 error 后 `return`。

---

# 40. Step 37：调试 Pass 的三个最重要命令

课件最后一页给出的工具非常实用。

## 40.1 每个 Pass 前打印 IR

```bash
build/bin/tinyir-opt input.mlir \
  --pass-pipeline='...' \
  -mlir-print-ir-before-all
```

## 40.2 每个 Pass 后打印 IR

```bash
build/bin/tinyir-opt input.mlir \
  --pass-pipeline='...' \
  -mlir-print-ir-after-all
```

这是排查：

```text
“到底是哪一个 pass 把 IR 改坏了？”
```

最直接的方法。

## 40.3 临时看未验证 IR

```bash
--verify-each=0
```

正常情况下：

```text
verifier 很重要，应该开着
```

但如果你正在调试一个中间步骤，故意想看：

```text
某个暂时 invalid 的 IR 长什么样
```

可以临时关闭“每步 verify”。

**不要把它当修复 verifier error 的方法。**

---

# 41. Step 38：`dump()` 仍然是最简单有效的武器

课件明确建议：

```cpp
op->dump();
value.dump();
type.dump();
attr.dump();
```

建议你建立一个习惯：

```text
先 dump 实际对象
再猜 API / IR 结构
```

尤其是遇到：

```text
这个 Value 到底是 BlockArgument 还是 OpResult？
这个 Operation 到底有哪些 regions？
这个 successor 到底指哪个 Block？
这个 generated accessor 到底返回什么？
```

时，`dump()` 往往比盯代码猜更快。

---

# 42. Step 39：generated `.h.inc` 是最重要的“本地文档”

课件最后还特别建议看：

```text
generated .h.inc
```

对你的 tinyIR，就是：

```text
build/gen/List_Ops.h.inc
build/gen/List_Types.h.inc
build/gen/Tutorial_Ops.h.inc
build/gen/Tutorial_Types.h.inc
```

为什么重要？

因为它会直接告诉你：

```text
TableGen 到底生成了什么 C++ class
有哪些 getters
有哪些 mutable getters
builder 签名是什么
result / operand accessor 名是什么
type class 有哪些方法
```

以后遇到：

```text
getLhs?
getInput?
getInputs?
getResult?
getOutput?
```

不要猜。

直接：

```bash
grep -n "class .*Op" build/gen/*_Ops.h.inc
```

或者在 VS Code 里跳转定义。

---

# 43. 对照课件的“完整能力清单”

完成本教程后，你至少应该能不看答案解释：

## Operation

```cpp
op->getName()
op->getBlock()
op->getPrevNode()
op->getNextNode()

op->getNumOperands()
op->getOperand(i)
op->getOpOperand(i)

op->getNumResults()
op->getResult(i)

op->getNumRegions()
op->getRegion(i)

op->moveBefore(...)
op->moveAfter(...)
op->erase()
```

## Value / OpResult / BlockArgument

```cpp
value.getType()
value.setType(...)

value.getDefiningOp()
value.getUses()
value.getUsers()
value.use_empty()
value.replaceAllUsesWith(...)

blockArg.getOwner()
blockArg.getArgNumber()
```

## OpOperand

```cpp
use.get()
use.set(...)
use.getOwner()
use.getOperandNumber()
```

## Block

```cpp
block.front()
block.back()
block.begin()
block.end()

block.getArgument(i)
block.getArguments()

block.getPrevNode()
block.getNextNode()

block.getUses()
block.getUsers()
```

## Region

```cpp
region.getParentOp()
region.front()
region.back()

for (Block &b : region)
```

## CFG / BlockOperand

```cpp
op->getNumSuccessors()
op->getSuccessor(i)
op->getBlockOperands()

blockOperand.get()
blockOperand.set(...)
blockOperand.getOwner()
blockOperand.getOperandNumber()
```

## Builder / Rewriter

```cpp
builder.setInsertionPoint(...)
builder.setInsertionPointAfter(...)
builder.setInsertionPointToStart(...)
builder.setInsertionPointToEnd(...)

OpBuilder::InsertionGuard

OpTy::create(builder, ...)
rewriter.eraseOp(...)
rewriter.replaceOp(...)
rewriter.replaceOpWithNewOp<...>(...)
```

---

# 44. 课件中最值得牢记的复杂度直觉

不要死背纳秒数，只记数据结构决定的量级。

| 操作 | 直觉 |
|---|---|
| `op->getResult(i)` | O(1) |
| Block 中取第 i 个 op | O(i) |
| 在已知位置插入 op | 链表局部更新，结构上近似 O(1) |
| 移动一个 op | 链表摘除 + 插入，结构上近似 O(1) |
| `value.use_empty()` | 只看 use-list 是否为空，优于完整计数 |
| RAUW | O(number of uses) |
| 添加 Attribute | 可能随 op attribute 数量增长，并涉及 uniquing |

这张表会影响你以后写编译器 pass 的代码风格。

---

# 45. 五个必须真正理解的“危险区”

## 45.1 dangling Value

```text
erase defining op
↓
继续使用其 OpResult
↓
use-after-free
```

## 45.2 iterator invalidation

```text
遍历 value.getUses()
↓
循环体 use.set(...)
↓
修改正在遍历的 use-list
```

解决：

```text
RAUW
early-inc
worklist
rewriter
```

## 45.3 dominance

```text
插入位置在 operand 定义之前
↓
IR 结构能插进去
↓
SSA 语义非法
```

## 45.4 BlockArgument 没有 defining op

```text
value.getDefiningOp() == nullptr
```

不是异常。

## 45.5 “没有 use”不等于“operation 可以删”

还要考虑：

```text
其他 results
side effects
symbol / region semantics
```

---

# 46. 推荐的实际练习顺序

不要一次把五个 pass 全写完。

建议：

```text
Day 1
InspectIR.cpp
  ↓
typed/generic op
cast
parent block
prev/next

Day 2
UseDef.cpp
  ↓
OpOperand
getUses/getUsers
RAUW
early-inc

Day 3
CFGInspect.cpp
  ↓
BlockArgument
multi-block
Region
successor
BlockOperand

Day 4
BuilderSafety.cpp
  ↓
insertion point
InsertionGuard
dominance

Day 5
RewriterDemo.cpp
  ↓
IRRewriter
replaceOp
worklist
debug flags
signalPassFailure
```

每一天都必须：

```text
写一个 .mlir
运行 tinyir-opt
先看 before
再看 after
故意制造一个错误
理解 verifier 为什么报错
```

---

# 47. Makefile 可以补一个通用 advanced-pass 目标

你现在可以加：

```make
PASS ?= inspect-ir
TEST ?= $(TEST_DIR)/passes/advanced/inspect-ir.mlir

.PHONY: pass

pass: $(BIN)
	@echo "+PASS    $(PASS) on $(TEST)"
	@$(BIN) $(TEST) \
		--pass-pipeline="builtin.module(func.func($(PASS)))"
```

使用：

```bash
make pass \
  PASS=inspect-ir \
  TEST=test/passes/advanced/inspect-ir.mlir
```

或者：

```bash
make pass \
  PASS=use-def-demo \
  TEST=test/passes/advanced/use-def.mlir
```

这样不用为每个 pass 增加新的 Make target。

---

# 48. 最终目录树

完成这一阶段后：

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
│   │   ├── SimplifyAdd.cpp
│   │   ├── InspectIR.cpp
│   │   ├── UseDef.cpp
│   │   ├── CFGInspect.cpp
│   │   ├── BuilderSafety.cpp
│   │   └── RewriterDemo.cpp
│   └── tinyir-opt.cpp
├── test/
│   └── passes/
│       ├── simpleAdd.mlir
│       └── advanced/
│           ├── inspect-ir.mlir
│           ├── use-def.mlir
│           ├── cfg.mlir
│           ├── builder.mlir
│           └── rewriter.mlir
└── build/
    ├── bin/
    │   └── tinyir-opt
    ├── gen/
    ├── lib/
    │   └── libtinyir.a
    └── obj/
        └── passes/
            ├── SimplifyAdd.o
            ├── InspectIR.o
            ├── UseDef.o
            ├── CFGInspect.o
            ├── BuilderSafety.o
            └── RewriterDemo.o
```

---

# 49. 你到这里真正跨过了什么门槛？

`pass.md` 之后，你已经会：

```text
“调用 API 改一条 IR”
```

`pass-advanced.md` 之后，目标应该变成：

```text
我知道为什么这个 API 可以这样改
我知道底层谁拥有谁
我知道 Value 的定义与 use 在哪里
我知道哪些容器是链表
我知道修改遍历对象为什么会失效
我知道 insertion point 为什么可能破坏 dominance
我知道 Block/Region/CFG 和 SSA use-def 是两套互相配合的关系
我知道何时应该从裸 mutation 进入 Rewriter
我知道怎样定位是哪一个 pass 把 IR 改坏
```

这才是这份 **MLIR Fundamentals: C++ IR Data Structures and APIs** 课件真正希望你掌握的东西。

---

# 50. 下一阶段：PatternRewriter / Canonicalization / Dialect Conversion

完成本教程后，下一份教程应该进入：

```text
pass-pattern.md
```

主线建议：

```text
RewritePattern
↓
OpRewritePattern<OpTy>
↓
matchAndRewrite
↓
RewritePatternSet
↓
applyPatternsGreedily
↓
canonicalization patterns
↓
TypeConverter
↓
ConversionTarget
↓
ConversionPatternRewriter
↓
Dialect Conversion
```

此时你再学习 PatternRewriter，前面的：

```text
Operation
Value
OpOperand
Block
Region
use-def
insertion point
dominance
rewriter
```

都会直接变成可复用基础，而不会只是“背 pattern 模板”。

---

# 附录 A：与原 `pass.md` 的边界

保留原 `pass.md` 的价值：

```text
pass.md
=
第一条最小可复现 Pass 学习链
```

不要把所有 advanced 内容硬塞回去，否则第一次学习时认知负担会过大。

建议仓库文档分层：

```text
docs/
├── pass.md
├── pass-advanced.md
└── pass-pattern.md       # 下一阶段
```

三者关系：

```text
pass.md
  “Pass 怎么跑起来？”

pass-advanced.md
  “MLIR IR 在 C++ 里到底怎么工作？”

pass-pattern.md
  “如何用 MLIR 正统 rewrite infrastructure 写可扩展变换？”
```

---

# 附录 B：课件页码映射

便于你一边看 PDF 一边练习：

```text
P3–18
  Operation / Region / Block / BlockArgument /
  OpResult / OpOperand / Value

P20–24
  OperationState
  detached op
  typed build
  OpBuilder create + insert
  insertion point
  InsertionGuard
  destroy vs erase

P25–29
  isa / dyn_cast / cast
  typed op / generic Operation 的传递方式

P30–37
  parent Block
  OpResult
  result 查询复杂度
  OpResult lifetime
  为什么增加 result 需要 new op + RAUW + erase

P38–41
  OpOperand 查询与修改

P42–49
  prev/next operation
  block front/back
  linked-list complexity
  insertion / movement

P50–60
  getUses / getUsers
  RAUW
  early-inc
  RAUW complexity
  use_empty
  multi-result erase safety

P61–65
  BlockArgument
  getDefiningOp 的 null 情况

P66–74
  multiple Blocks
  Region
  BlockOperand
  Block getUses/getUsers
  IR data structure summary

P75–83
  Type
  whole-IR type consistency
  Attribute
  Attribute cost
  Properties

P84–92
  Builder
  OpBuilder
  insertion point
  dominance violation
  PatternRewriter
  rewriter convenience API

P93–96
  Pass pipeline
  signalPassFailure
  print-ir-before/after
  dump
  generated .h.inc
  --verify-each=0
```
