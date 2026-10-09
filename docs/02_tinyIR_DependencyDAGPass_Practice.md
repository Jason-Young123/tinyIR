# tinyIR 面试冲刺讲义 02：4～6 小时完成最小 DependencyDAGPass

> 基于你当前上传的 tinyIR 仓库结构设计。  
> 本教程只做一个**最小、完整、可解释、可验证**的 Analysis Pass，不做危险的 Operation Reordering。  
> 完成后你可以在面试中准确地说：  
> **“我基于 MLIR SSA def-use，在 basic block 内构建 operation dependency DAG，并实现 indegree、Kahn topological order 与 dependency level 分析。”**

---

# 0. 当前仓库里我们能复用什么？

你当前仓库已经有：

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
│   ├── BuilderSafety.cpp
│   ├── CFGInspect.cpp
│   ├── Demo.cpp
│   ├── InspectIR.cpp
│   ├── ListLowerFromElements.cpp
│   ├── ListLowering.cpp
│   ├── Passes.h
│   ├── RewriterDemo.cpp
│   └── UseDef.cpp
└── tinyir-opt.cpp
```

已有两个最直接的前置：

```text
UseDef.cpp
    已经会从 mlir::Value 看 uses

CFGInspect.cpp
    已经会遍历 func body 中的 Block
```

我们现在增加：

```text
src/passes/DependencyDAG.cpp
test/passes/dependency-dag.mlir
```

并修改：

```text
src/passes/Passes.h
src/tinyir-opt.cpp
```

你的 Makefile 已经使用：

```make
PASS_SRCS := $(wildcard $(PASS_DIR)/*.cpp)
```

因此**新增 `src/passes/DependencyDAG.cpp` 后不需要修改 Makefile**，它会自动参与编译。

---

# 1. Step 1：先明确这个 Pass 做什么、不做什么

## 做什么

对于 `func.func` 的每个 direct basic block：

```text
1. 给 block 内 Operation 编号
2. 对每个 Operation 的 operands 找 defining op
3. 如果 defining op 在同一个 block，建立 producer -> consumer edge
4. 计算 indegree
5. Kahn topological sort
6. 计算 dependency level
7. 打印分析结果
```

## 不做什么

第一版明确不做：

```text
跨 block dataflow
memory alias
side effect dependency
真实 instruction latency
resource scheduling
IR reorder
```

这样可以保证你的实现范围小、逻辑干净，而且面试时知道边界。

---

# 2. Step 2：先准备测试 IR

新增完整文件：

```text
test/passes/dependency-dag.mlir
```

完整内容：

```mlir
module {
  func.func @diamond(
      %a: i32,
      %b: i32,
      %c: i32,
      %d: i32) -> i32 {

    %0 = arith.addi %a, %b : i32
    %1 = arith.muli %0, %c : i32
    %2 = arith.addi %0, %d : i32
    %3 = arith.addi %1, %2 : i32

    return %3 : i32
  }

  func.func @two_roots(
      %a: i32,
      %b: i32,
      %c: i32,
      %d: i32) -> i32 {

    %0 = arith.addi %a, %b : i32
    %1 = arith.muli %c, %d : i32
    %2 = arith.addi %0, %1 : i32

    return %2 : i32
  }

  func.func @multi_block(
      %cond: i1,
      %x: i32,
      %y: i32) -> i32 {

    cf.cond_br %cond, ^bb1(%x : i32), ^bb2(%y : i32)

  ^bb1(%a: i32):
    %0 = arith.addi %a, %a : i32
    cf.br ^merge(%0 : i32)

  ^bb2(%b: i32):
    %1 = arith.muli %b, %b : i32
    cf.br ^merge(%1 : i32)

  ^merge(%v: i32):
    %2 = arith.addi %v, %v : i32
    return %2 : i32
  }
}
```

## 为什么准备三个函数？

`diamond`：

```text
验证经典菱形依赖
验证同层潜在并行节点
```

`two_roots`：

```text
验证一个 block 里可以同时有多个 indegree == 0 节点
```

`multi_block`：

```text
验证当前 Pass 按 block 分析
并让你观察 BlockArgument 不会产生同 block defining op edge
```

---

# 3. Step 3：在 `Passes.h` 添加注册声明

你当前 `Passes.h` 是所有自定义 Pass 暴露给 `tinyir-opt.cpp` 的注册接口。

把完整文件替换为：

```cpp
#ifndef TINYIR_PASSES_H
#define TINYIR_PASSES_H

namespace tinyir {

void registerDemoPass();

void registerInspectIRPass();
void registerUseDefPass();
void registerCFGInspectPass();
void registerDependencyDAGPass();

void registerBuilderSafetyPass();
void registerRewriterDemoPass();
void registerListLowerFromElementsPass();
void registerListLoweringPass();

} // namespace tinyir

#endif // TINYIR_PASSES_H
```

这里唯一真正新增的是：

```cpp
void registerDependencyDAGPass();
```

作用：

```text
DependencyDAG.cpp
    ↓ 定义
tinyir::registerDependencyDAGPass()

Passes.h
    ↓ 声明接口

tinyir-opt.cpp
    ↓ 调用注册
```

---

# 4. Step 4：新增 `DependencyDAG.cpp`

新增：

```text
src/passes/DependencyDAG.cpp
```

完整内容：

```cpp
#include "passes/Passes.h"

#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/Block.h"
#include "mlir/IR/Operation.h"
#include "mlir/IR/Value.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Pass/PassRegistry.h"

#include "llvm/Support/raw_ostream.h"

#include <algorithm>
#include <queue>
#include <unordered_map>
#include <vector>

namespace {

struct DependencyDAGPass
    : mlir::PassWrapper<
          DependencyDAGPass,
          mlir::OperationPass<mlir::func::FuncOp>> {

  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(DependencyDAGPass)

  mlir::StringRef getArgument() const override {
    return "dependency-dag";
  }

  mlir::StringRef getDescription() const override {
    return "Build a basic-block-local SSA operation dependency DAG";
  }

  void analyzeBlock(mlir::Block &block, unsigned blockIndex) {
    std::vector<mlir::Operation *> ops;
    std::unordered_map<mlir::Operation *, unsigned> opToId;

    for (mlir::Operation &op : block) {
      unsigned id = ops.size();
      ops.push_back(&op);
      opToId[&op] = id;
    }

    const unsigned n = ops.size();

    std::vector<std::vector<unsigned>> graph(n);
    std::vector<unsigned> indegree(n, 0);

    for (unsigned to = 0; to < n; ++to) {
      mlir::Operation *consumer = ops[to];

      for (mlir::Value operand : consumer->getOperands()) {
        mlir::Operation *producer = operand.getDefiningOp();

        if (producer == nullptr)
          continue;

        if (producer->getBlock() != &block)
          continue;

        auto it = opToId.find(producer);

        if (it == opToId.end())
          continue;

        unsigned from = it->second;

        auto &successors = graph[from];

        if (std::find(
                successors.begin(),
                successors.end(),
                to) != successors.end()) {
          continue;
        }

        successors.push_back(to);
        ++indegree[to];
      }
    }

    llvm::errs()
        << "\n=== dependency DAG: block "
        << blockIndex
        << " ===\n";

    llvm::errs()
        << "operations: "
        << n
        << "\n";

    for (unsigned id = 0; id < n; ++id) {
      llvm::errs()
          << "  op"
          << id
          << ": "
          << ops[id]->getName()
          << "\n";
    }

    llvm::errs() << "edges:\n";

    bool hasEdge = false;

    for (unsigned from = 0; from < n; ++from) {
      for (unsigned to : graph[from]) {
        hasEdge = true;

        llvm::errs()
            << "  op"
            << from
            << " -> op"
            << to
            << "\n";
      }
    }

    if (!hasEdge)
      llvm::errs() << "  <none>\n";

    llvm::errs() << "indegree:\n";

    for (unsigned id = 0; id < n; ++id) {
      llvm::errs()
          << "  op"
          << id
          << ": "
          << indegree[id]
          << "\n";
    }

    std::queue<unsigned> ready;
    std::vector<unsigned> remainingIndegree = indegree;
    std::vector<unsigned> level(n, 0);
    std::vector<unsigned> topoOrder;

    for (unsigned id = 0; id < n; ++id) {
      if (remainingIndegree[id] == 0)
        ready.push(id);
    }

    while (!ready.empty()) {
      unsigned u = ready.front();
      ready.pop();

      topoOrder.push_back(u);

      for (unsigned v : graph[u]) {
        level[v] =
            std::max(
                level[v],
                level[u] + 1);

        --remainingIndegree[v];

        if (remainingIndegree[v] == 0)
          ready.push(v);
      }
    }

    llvm::errs() << "topological order:\n  ";

    for (unsigned i = 0; i < topoOrder.size(); ++i) {
      if (i != 0)
        llvm::errs() << " -> ";

      llvm::errs()
          << "op"
          << topoOrder[i];
    }

    llvm::errs() << "\n";

    if (topoOrder.size() != n) {
      llvm::errs()
          << "cycle detected: yes\n";
      return;
    }

    llvm::errs()
        << "cycle detected: no\n";

    unsigned maxLevel = 0;

    for (unsigned value : level)
      maxLevel = std::max(maxLevel, value);

    llvm::errs() << "dependency levels:\n";

    for (unsigned currentLevel = 0;
         currentLevel <= maxLevel;
         ++currentLevel) {

      llvm::errs()
          << "  level "
          << currentLevel
          << ":";

      for (unsigned id = 0; id < n; ++id) {
        if (level[id] == currentLevel) {
          llvm::errs()
              << " op"
              << id;
        }
      }

      llvm::errs() << "\n";
    }
  }

  void runOnOperation() override {
    mlir::func::FuncOp func = getOperation();

    llvm::errs()
        << "\n========================================\n";

    llvm::errs()
        << "function: "
        << func.getName()
        << "\n";

    unsigned blockIndex = 0;

    for (mlir::Block &block : func.getBody()) {
      analyzeBlock(
          block,
          blockIndex++);
    }
  }
};

} // namespace

void tinyir::registerDependencyDAGPass() {
  mlir::PassRegistration<DependencyDAGPass>();
}
```

---

# 5. Step 5：逐段理解 `DependencyDAG.cpp`

## 5.1 为什么 Pass 仍然挂在 `func.func`？

```cpp
mlir::OperationPass<mlir::func::FuncOp>
```

表示：

> Pass Manager 每次把一个 `func.func` 交给这个 Pass。

这与你现有：

```text
UseDefPass
CFGInspectPass
InspectIRPass
```

保持一致。

---

## 5.2 为什么自己再遍历 Basic Block？

```cpp
for (mlir::Block &block : func.getBody())
```

因为我们第一版 DAG 的定义域是：

```text
basic-block-local
```

这样可以避开：

```text
跨 block SSA 传递
branch operands
block arguments
loop backedge
```

把问题控制在一个可以当天完成的范围内。

---

## 5.3 为什么先给 Operation 编号？

```cpp
std::vector<mlir::Operation *> ops;
std::unordered_map<mlir::Operation *, unsigned> opToId;
```

MLIR Operation 本身是对象指针。

图算法更喜欢：

```text
0, 1, 2, 3, ...
```

所以建立：

```text
Operation* -> integer id
```

例如：

```text
0 -> arith.addi
1 -> arith.muli
2 -> arith.addi
3 -> arith.addi
4 -> func.return
```

这样后面可以直接使用：

```cpp
vector<vector<unsigned>> graph;
vector<unsigned> indegree;
```

---

# 6. Step 6：最关键的一步——从 Operand 找 Producer

核心代码：

```cpp
for (mlir::Value operand : consumer->getOperands()) {
    mlir::Operation *producer = operand.getDefiningOp();
}
```

逻辑：

```text
consumer
   ↓ 遍历 operands
Value
   ↓ getDefiningOp()
producer
```

如果：

```mlir
%0 = arith.addi %a, %b : i32
%1 = arith.muli %0, %c : i32
```

处理 `muli`：

```text
operand = %0
getDefiningOp()
    ↓
arith.addi
```

于是建边：

```text
addi -> muli
```

---

# 7. Step 7：为什么 `producer == nullptr` 要跳过？

```cpp
if (producer == nullptr)
    continue;
```

因为 BlockArgument 没有普通 defining Operation。

例如：

```mlir
func.func @f(%x: i32) -> i32 {
  %0 = arith.addi %x, %x : i32
  return %0 : i32
}
```

`%x` 是函数入口 block argument。

它可以理解成：

```text
当前 block 外部提供的输入
```

不是某个当前 block operation 的 result。

因此第一版不为它创建 operation vertex。

---

# 8. Step 8：为什么检查 `producer->getBlock() != &block`？

```cpp
if (producer->getBlock() != &block)
    continue;
```

因为本 Pass 明确只构：

```text
same-basic-block dependency DAG
```

如果 producer 不在当前 block，就不纳入当前 graph。

这是一个非常重要的“scope contract”。

面试时你可以明确说：

> 当前 pass 有意限制为 block-local SSA analysis；跨 block dataflow 需要进一步结合 CFG、BlockArgument、dominance 和 fixpoint analysis。

---

# 9. Step 9：为什么去重 edge？

考虑：

```mlir
%0 = arith.addi %a, %b : i32
%1 = arith.addi %0, %0 : i32
```

`%1` 有两个 operand 都来自 `%0`。

如果每个 operand 都加边，会得到：

```text
op0 -> op1
op0 -> op1
```

从 operand-use 数量看这可以视为两条 use edge，但我们这里定义的是：

> operation-level dependency graph

因此只需要：

```text
op0 -> op1
```

一条依赖边。

代码：

```cpp
if (std::find(
        successors.begin(),
        successors.end(),
        to) != successors.end()) {
  continue;
}
```

负责去重。

---

# 10. Step 10：注册 Pass

修改完整文件：

```text
src/tinyir-opt.cpp
```

为：

```cpp
#include "mlir/InitAllDialects.h"
#include "mlir/InitAllExtensions.h"
#include "mlir/InitAllPasses.h"
#include "mlir/Tools/mlir-opt/MlirOptMain.h"

#include "dialect/Tutorial.h"
#include "dialect/List.h"
#include "passes/Passes.h"

int main(int argc, char **argv) {
  mlir::DialectRegistry registry;

  mlir::registerAllDialects(registry);
  mlir::registerAllPasses();
  mlir::registerAllExtensions(registry);

  registry.insert<
      mlir::tutorial::TutorialDialect,
      mlir::list::ListDialect>();

  tinyir::registerDemoPass();
  tinyir::registerInspectIRPass();
  tinyir::registerUseDefPass();
  tinyir::registerCFGInspectPass();
  tinyir::registerDependencyDAGPass();
  tinyir::registerBuilderSafetyPass();
  tinyir::registerRewriterDemoPass();
  tinyir::registerListLowerFromElementsPass();
  tinyir::registerListLoweringPass();

  return mlir::asMainReturnCode(
      mlir::MlirOptMain(
          argc,
          argv,
          "tinyIR optimizer driver\n",
          registry));
}
```

唯一新增的注册行为：

```cpp
tinyir::registerDependencyDAGPass();
```

---

# 11. Step 11：为什么 Makefile 不需要修改？

你当前 Makefile 已经有：

```make
PASS_SRCS := $(wildcard $(PASS_DIR)/*.cpp)
```

所以：

```text
src/passes/DependencyDAG.cpp
```

会自动进入：

```text
PASS_SRCS
↓
PASS_OBJS
↓
libtinyir.a
```

因此直接重新：

```bash
make build
```

即可。

---

# 12. Step 12：编译

在 tinyIR 根目录：

```bash
make clean
make build
```

如果你不希望每次全量 clean，也可以先：

```bash
make build
```

但第一次增加新 Pass 时，为了避免旧对象干扰，我更推荐：

```bash
make clean
make build
```

成功后应得到：

```text
build/bin/tinyir-opt
```

---

# 13. Step 13：运行 Pass

你现有 Makefile 的 `pass` target 支持：

```bash
make pass \
  TEST=test/passes/dependency-dag.mlir \
  PASS=dependency-dag
```

它最终会构造类似：

```text
builtin.module(func.func(dependency-dag))
```

的 pipeline。

因为你的 Pass 类型就是：

```cpp
OperationPass<mlir::func::FuncOp>
```

所以这个 nesting 是匹配的。

---

# 14. Step 14：你应该看到什么？

对于：

```mlir
%0 = arith.addi %a, %b : i32
%1 = arith.muli %0, %c : i32
%2 = arith.addi %0, %d : i32
%3 = arith.addi %1, %2 : i32
return %3 : i32
```

你应当看到类似：

```text
function: diamond

=== dependency DAG: block 0 ===

operations: 5

  op0: arith.addi
  op1: arith.muli
  op2: arith.addi
  op3: arith.addi
  op4: func.return

edges:
  op0 -> op1
  op0 -> op2
  op1 -> op3
  op2 -> op3
  op3 -> op4

indegree:
  op0: 0
  op1: 1
  op2: 1
  op3: 2
  op4: 1

topological order:
  op0 -> op1 -> op2 -> op3 -> op4

cycle detected: no

dependency levels:
  level 0: op0
  level 1: op1 op2
  level 2: op3
  level 3: op4
```

实际打印顺序以你编译环境和当前 IR direct operation 顺序为准，但逻辑结构应该一致。

---

# 15. Step 15：为什么 `return` 也在 DAG 里？

我们的代码：

```cpp
for (mlir::Operation &op : block)
```

会遍历 block 中所有 direct operations，包括 terminator。

因此：

```mlir
return %3 : i32
```

也是节点。

它使用 `%3`，所以得到：

```text
op3 -> return
```

这是合理的：

> return 在数据上依赖最终结果。

如果你以后只想分析“可调度计算 op”，可以再过滤 terminator，但第一版保留它反而更容易看到完整 def-use 链。

---

# 16. Step 16：理解 `two_roots`

测试：

```mlir
%0 = arith.addi %a, %b : i32
%1 = arith.muli %c, %d : i32
%2 = arith.addi %0, %1 : i32
```

图：

```text
op0 ----\
         -> op2
op1 ----/
```

所以：

```text
indegree(op0) = 0
indegree(op1) = 0
```

最开始 ready queue 中会同时有：

```text
op0
op1
```

这非常像真实 scheduler 的：

```text
Ready List
```

只是我们现在还没有 latency/resource priority。

---

# 17. Step 17：理解 `multi_block`

例如：

```mlir
^bb1(%a: i32):
  %0 = arith.addi %a, %a : i32
```

`%a` 是 BlockArgument。

因此：

```cpp
operand.getDefiningOp()
```

不会得到同 block producer。

所以 `%0` 在当前 block-local DAG 中可以成为 root。

这正好帮助你理解：

> CFG edge 上的值传递与单 block def-use analysis 是不同层级的问题。

---

# 18. Step 18：验证“重复 operand 不产生重复 dependency edge”

你可以临时追加：

```mlir
func.func @duplicate_operand(%x: i32) -> i32 {
  %0 = arith.addi %x, %x : i32
  %1 = arith.muli %0, %0 : i32
  return %1 : i32
}
```

对于：

```text
op0 -> op1
```

只应打印一次。

虽然 `%0` 被 `%1` 使用两次，但我们当前 graph 表示：

```text
Operation dependency
```

而不是：

```text
Operand-use multiplicity
```

---

# 19. Step 19：验证 Pass 没有修改 IR

你的 `DependencyDAGPass`：

```text
只读 Operation
构图存在 C++ 临时容器里
打印分析结果
```

没有：

```text
eraseOp
replaceOp
moveBefore
moveAfter
create
```

因此它是 Analysis-style Pass。

运行：

```bash
make pass \
  TEST=test/passes/dependency-dag.mlir \
  PASS=dependency-dag
```

你的 Makefile 已启用：

```text
-mlir-print-ir-before-all
-mlir-print-ir-after-all
```

所以可以观察：

```text
Before IR == After IR
```

这也是一个很好的验证点。

---

# 20. Step 20：为什么这还不是真正的 Scheduler？

现在我们的模型只有：

```text
SSA producer-consumer edge
```

没有：

```text
memory dependency
side effect
alias
operation latency
execution units
register pressure
issue width
barrier
```

所以它回答的是：

> 在当前简化模型下，Operation 之间有哪些显式 SSA dependency？

它不回答：

> 最优机器指令顺序是什么？

这句话面试时一定要主动说出来。

---

# 21. 可选增强 A：打印每条边对应哪个 SSA Value

如果你做完主线还有 30～60 分钟，可以把 edge 输出增强成：

```text
op0 -> op1
```

进一步打印：

```text
via operand #0
```

但这会引入“一个 operation pair 可能通过多个 operands 相连”的输出设计。

在面试前时间紧的情况下，这不是必须。

---

# 22. 可选增强 B：给 Level 加语义解释

当前：

```text
level 0
level 1
level 2
```

你可以在 README 或面试中解释：

```text
level 0:
    没有当前 block 内 producer dependency

level k:
    至少需要经过 k 层 producer-consumer dependency 才能到达
```

它不是 cycle 数，也不是 block depth。

---

# 23. 可选增强 C：Critical Depth

若想再进一小步，可以把当前 unit-latency：

$$
level(v)=1+\max level(u)
$$

解释为：

```text
每个 Operation latency 都假设为 1
```

未来可以推广成：

$$
distance(v)=latency(v)+\max distance(u)
$$

然后得到更接近 scheduler 的 critical path。

但不建议你在这周开始设计真实 latency model。

---

# 24. 最终仓库增改清单

完成后目录新增/修改：

```text
src/
├── passes/
│   ├── DependencyDAG.cpp       # 新增
│   └── Passes.h                # 修改
└── tinyir-opt.cpp              # 修改

test/
└── passes/
    └── dependency-dag.mlir     # 新增
```

Makefile：

```text
不修改
```

原因：

```make
PASS_SRCS := $(wildcard $(PASS_DIR)/*.cpp)
```

自动发现新 `.cpp`。

---

# 25. 一次完整操作流程

```bash
cd ~/Learning-MLIR/tinyIR
```

创建：

```bash
vim src/passes/DependencyDAG.cpp
```

修改：

```bash
vim src/passes/Passes.h
vim src/tinyir-opt.cpp
```

创建测试：

```bash
vim test/passes/dependency-dag.mlir
```

编译：

```bash
make clean
make build
```

执行：

```bash
make pass \
  TEST=test/passes/dependency-dag.mlir \
  PASS=dependency-dag
```

---

# 26. 做完以后 Git 怎么提交

先检查：

```bash
git status
git diff
```

然后：

```bash
git add \
  src/passes/DependencyDAG.cpp \
  src/passes/Passes.h \
  src/tinyir-opt.cpp \
  test/passes/dependency-dag.mlir
```

提交：

```bash
git commit -m "add SSA dependency DAG analysis pass"
```

---

# 27. 面试时 90 秒怎么介绍这个功能

你可以按以下逻辑组织，不建议死背原句：

> tinyIR 前面已经完成了基本的 Dialect、ODS、Rewrite 和 lowering 实践。为了继续理解 compiler analysis，我增加了一个 block-local SSA dependency DAG pass。Pass 会遍历 `func.func` 的每个 basic block，对每个 operation 的 operands 调用 `getDefiningOp()`，把同 block producer 到 consumer 建成有向边，并计算 indegree。随后用 Kahn algorithm 得到 topological order，同时做一个 unit-latency dependency level 分析，用来观察潜在并行节点。  
>
> 当前版本有意只建模 SSA def-use dependency，不直接做 operation reordering，因为真实 scheduling 还必须考虑 memory alias、side effects、resource constraint 和 latency model。下一步可以把它扩展到 critical-path priority 或结合 MLIR memory-effect analysis。

这段回答有三个优点：

```text
你做了什么
为什么这样做
你知道它还缺什么
```

---

# 28. 面试官继续追问时怎么接

## “为什么不用 `result.getUses()` 建图？”

两种都能做。

你的实现选择：

```text
consumer operand
    ↓
getDefiningOp()
    ↓
producer
```

原因：

> 处理每个 consumer 时可以直接收集所有 producer，很自然地计算 consumer indegree。

从 producer 侧：

```text
result
 ↓
getUses()
 ↓
consumer
```

也完全可行。

---

## “为什么使用 `unordered_map<Operation*, unsigned>`？”

因为：

```text
MLIR API 处理的是 Operation*
图算法处理整数 id 更方便
```

因此做一层：

```text
Operation* -> id
```

映射。

---

## “为什么图里要去重？”

一个 consumer 可能多次使用同一个 producer 的 result：

```mlir
%1 = arith.addi %0, %0 : i32
```

如果我们的图定义为 operation-level dependency，则：

```text
producer -> consumer
```

只需要一条边。

---

## “如果 topo sort 检测出 cycle 怎么办？”

在当前合法 single-block SSA def-use 模型里理论上不应出现这种 cycle。

因此：

> cycle check 更多是图算法完整性和 sanity check。

如果分析的是 CFG，则 loop 很正常，不能用同样结论。

---

# 29. 完成标准

今天结束时至少做到：

- `DependencyDAG.cpp` 能参与 build。
- `--dependency-dag` 能被注册。
- `diamond` 测试能打印正确 edges。
- 能解释每条 edge 是怎么从 `operand.getDefiningOp()` 得到的。
- 能解释 indegree。
- 能解释 Kahn topo。
- 能解释 dependency level。
- 能解释为什么当前不能直接做任意 reordering。
- 能解释 DAG 与 CFG 的区别。

如果这些都完成，就不要继续扩功能。

把剩余时间放回：

```text
C++ coding
MLIR 基本概念
Rewrite / Lowering 复盘
CUDA / GPU 算子
```

这比在面试前临时造一个“大而不稳”的 compiler project 更有价值。
