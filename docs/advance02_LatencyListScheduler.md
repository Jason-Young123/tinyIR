# advance02：Latency-Aware List Scheduler

> 基于你本轮上传的 `tinyIR-master(4).zip` 编写。  
> 当前仓库已经有 `DependencyDAGPass`，但还没有 scheduling pass。  
> 本节**不删除、不修改已有 `DependencyDAGPass` 的功能**，而是新增一个独立的 `LatencyListSchedulerPass`，实现 latency-aware heuristic list scheduling。
>
> 本节最终完成：
>
> ```text
> func.func
>   ↓
> SSA def-use
>   ↓
> Dependency DAG
>   ↓
> Kahn cycle check
>   ↓
> Reverse-topo critical priority
>   ↓
> Latency + max-in-flight constrained greedy scheduling
>   ↓
> 输出 scheduled topo order
>   ↓
> 输出每个 op 的 issueCycle / finishCycle
>   ↓
> 和原始顺序比较总 cycles
>   ↓
> 若不退化，则真正 moveBefore() 重排 Operation
> ```

---

# 0. 先明确这一版硬件模型

这一版不是通用 CPU/GPU 模型，而是我们人为定义的简化 target model。

## 0.1 三类 Operation

只允许：

```text
memref.load
arith.addi
arith.muli
```

latency：

```cpp
unsigned gLoadExecTime = 10;
unsigned gAddiExecTime = 1;
unsigned gMuliExecTime = 5;
```

它们放在 `.cpp` 文件顶部，后续可以直接修改。

## 0.2 issue width = 1

每个 cycle 最多 issue 一条新指令。

也就是说即使同时有很多 ready op：

```text
cycle 0 只能选 1 条
cycle 1 最多再选 1 条
...
```

## 0.3 max in-flight = 2

最多允许两条尚未执行完成的 instruction 同时存在。

```cpp
unsigned gMaxInFlight = 2;
```

例如：

```text
cycle 0: issue A, A 还没完成
cycle 1: issue B, A/B 都没完成
```

此时：

```text
numInFlight = 2
```

即使 `C` 完全 ready：

```text
cycle 2 也不能再 issue C
```

必须等 A/B 至少一条执行完成。

所以：

> `issueWidth = 1` 和 `maxInFlight = 2` 是两个不同约束。

## 0.4 RAW 约束

如果：

```text
A -> B
```

A 的 `finishCycle` 尚未到：

```text
B 不能 issue
```

因此 scheduler 同时考虑：

```text
topological legality
+
operand ready time
+
max in-flight resource
```

---

# 1. 为什么这一版不再使用 queue / record / sort 三个 map？

你最初的设计思想是正确的，但：

```text
queue      记录 topo-ready 但可能仍在等 predecessor 的 op
record     记录尚未 indegree=0 节点的剩余等待
sort       记录已经 issue 的 op
```

三个容器都维护 `remainingCycle`，容易出现：

```text
off-by-one
状态重复
更新时机不一致
最终 schedule 顺序丢失
```

这一版改成每个状态一个独立数组：

```cpp
std::vector<unsigned> remainingIndegree;
std::vector<unsigned> readyCycle;
std::vector<unsigned> finishCycle;
std::vector<bool> scheduled;
std::vector<unsigned> order;
```

含义分别是：

```text
remainingIndegree[u]
    u 还有多少 predecessor 尚未被放入静态 schedule

readyCycle[u]
    u 的所有已知 predecessor 中最晚的 finishCycle

finishCycle[u]
    u 如果已经 issue，何时完成

scheduled[u]
    u 是否已经进入最终 schedule

order
    最终真正的 topo/scheduling 顺序
```

这样状态语义非常清晰。

---

# 2. `readyCycle` 为什么初始化为 0？

初始化：

```cpp
std::vector<unsigned> readyCycle(n, 0);
```

定义：

$$
readyCycle(u)
=
\max_{p\in Pred(u)} finishCycle(p)
$$

对于没有 predecessor 的 root：

```text
Pred(u) = empty
```

我们把它视为：

```text
readyCycle = 0
```

因此在：

```text
cycle = 0
```

时，所有：

```text
remainingIndegree == 0
```

的 root 都天然属于候选节点。

没有显式 `queue.push(root)`，而是每个 cycle 动态扫描得到 ready set。

---

# 3. 当前 ready set 的数学定义

当前 cycle 为 `t` 时，一个节点必须同时满足：

```text
尚未 scheduled
remainingIndegree == 0
readyCycle <= t
```

才能真正 issue。

也就是：

$$
Ready(t)
=
\{u\mid
\neg scheduled(u),
remainingIndegree(u)=0,
readyCycle(u)\le t
\}
$$

然后从这些节点中选择：

$$
selected
=
\arg\max_{u\in Ready(t)} priority(u)
$$

若 priority 相同：

```text
原始 op id 更小者优先
```

这样结果是 deterministic 的。

---

# 4. Priority 的定义

我们使用：

> 从当前节点到 DAG leaf 的最长 latency 路径。

数学定义：

$$
Priority(u)
=
ExecTime(u)
+
\max_{u\rightarrow v} Priority(v)
$$

如果 `u` 没有 successor：

$$
Priority(u)=ExecTime(u)
$$

Priority 越高：

> 说明这个节点位于更长的 critical dependency chain 上，更不希望被拖延。

---

# 5. 为什么先 Kahn，再 reverse-topo 算 Priority？

普通 Kahn 从 source 向 leaf：

```text
source
↓
successor
↓
leaf
```

但 `Priority(u)` 依赖 successor：

```text
u 的 priority
依赖
successor 的 priority
```

所以最方便的是：

```text
Stage 1:
Kahn 得到任意合法 topoOrder

Stage 2:
reverse(topoOrder)

Stage 3:
从 leaf 向 source 做 DP
```

因此：

```text
一次 Kahn
+
一次 reverse-topo DP
+
一次 greedy scheduling
```

即可。

不需要穷举所有 topo order。

---

# 6. 本阶段要改哪些文件？

基于当前仓库，只新增：

```text
src/passes/LatencyListScheduler.cpp
test/passes/latency-list-scheduler.mlir
docs/advance02_LatencyListScheduler.md
```

修改：

```text
src/passes/Passes.h
src/tinyir-opt.cpp
```

不修改：

```text
src/passes/DependencyDAG.cpp
Makefile
其他已有 Pass
```

你当前 Makefile 已经：

```make
PASS_SRCS := $(wildcard $(PASS_DIR)/*.cpp)
```

所以新增 `.cpp` 会自动参与编译。

---

# 7. Step 1：新增测试 IR

新增：

```text
test/passes/latency-list-scheduler.mlir
```

完整内容：

```mlir
module {
  func.func @load_hide(
      %mem: memref<16xi32>,
      %idx: index,
      %k1: i32,
      %k2: i32,
      %x: i32,
      %y: i32,
      %z: i32,
      %w: i32) -> i32 {
    %0 = memref.load %mem[%idx] : memref<16xi32>
    %1 = arith.muli %0, %k1 : i32
    %2 = arith.addi %1, %k2 : i32
    %3 = arith.addi %x, %y : i32
    %4 = arith.muli %3, %z : i32
    %5 = arith.addi %4, %w : i32
    %6 = arith.addi %2, %5 : i32
    return %6 : i32
  }

  func.func @two_mul_chains(
      %a: i32,
      %b: i32,
      %c: i32,
      %d: i32,
      %e: i32,
      %f: i32) -> i32 {
    %0 = arith.muli %a, %b : i32
    %1 = arith.addi %0, %c : i32
    %2 = arith.muli %d, %e : i32
    %3 = arith.addi %2, %f : i32
    %4 = arith.addi %1, %3 : i32
    return %4 : i32
  }
}

```

第一组 `load_hide` 的 DAG：

```text
op0(load) -> op1(mul) -> op2(add) ----\
                                       -> op6(add)
op3(add)  -> op4(mul) -> op5(add) ----/
```

latency：

```text
op0 = 10
op1 = 5
op2 = 1
op3 = 1
op4 = 5
op5 = 1
op6 = 1
```

Priority：

```text
op0 = 17
op1 = 7
op2 = 2
op3 = 8
op4 = 7
op5 = 2
op6 = 1
```

---

# 8. Step 2：修改 `Passes.h`

文件：

```text
src/passes/Passes.h
```

只增加：

```cpp
void registerLatencyListSchedulerPass();
```

修改后的**完整文件**：

```cpp
#ifndef TINYIR_PASSES_H
#define TINYIR_PASSES_H

namespace tinyir { // 在tinyir-opt.cpp中可以直接使用tinyir::

void registerDemoPass(); //为tinyir-opt.cpp提供注册接口; 最小化demo
//各类进阶pass
void registerInspectIRPass();
void registerUseDefPass();
void registerCFGInspectPass();
void registerBuilderSafetyPass();
void registerRewriterDemoPass();
void registerListLowerFromElementsPass();
void registerListLoweringPass();

void registerDependencyDAGPass();
void registerLatencyListSchedulerPass();

} // namespace tinyir

#endif // TINYIR_PASSES_H

```

---

# 9. Step 3：新增核心 Scheduler Pass

新增：

```text
src/passes/LatencyListScheduler.cpp
```

完整文件：

```cpp
#include "passes/Passes.h"

#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/Block.h"
#include "mlir/IR/Operation.h"
#include "mlir/IR/Value.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Pass/PassRegistry.h"

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/raw_ostream.h"

#include <algorithm>
#include <queue>
#include <unordered_map>
#include <vector>

namespace {

unsigned gLoadExecTime = 10;
unsigned gAddiExecTime = 1;
unsigned gMuliExecTime = 5;
unsigned gMaxInFlight = 2;

struct TimingRecord {
  unsigned issueCycle = 0;
  unsigned finishCycle = 0;
};

struct ScheduleResult {
  std::vector<unsigned> order;
  std::vector<TimingRecord> timing;
  unsigned totalCycles = 0;
};

bool isSupportedOp(mlir::Operation *op) {
  llvm::StringRef name = op->getName().getStringRef();
  return name == "memref.load" || name == "arith.addi" ||
         name == "arith.muli";
}

unsigned getExecTime(mlir::Operation *op) {
  llvm::StringRef name = op->getName().getStringRef();
  if (name == "memref.load")
    return gLoadExecTime;
  if (name == "arith.addi")
    return gAddiExecTime;
  if (name == "arith.muli")
    return gMuliExecTime;
  return 0;
}

void printOrder(const std::vector<unsigned> &order,
                const std::vector<mlir::Operation *> &ops) {
  for (unsigned i = 0; i < order.size(); ++i) {
    if (i != 0)
      llvm::errs() << " -> ";
    unsigned id = order[i];
    llvm::errs() << "op" << id << "(" << ops[id]->getName() << ")";
  }
  llvm::errs() << "\n";
}

unsigned countInFlight(const std::vector<bool> &issued,
                       const std::vector<unsigned> &finishCycle,
                       unsigned cycle) {
  unsigned count = 0;
  for (unsigned id = 0; id < issued.size(); ++id) {
    if (issued[id] && finishCycle[id] > cycle)
      ++count;
  }
  return count;
}

ScheduleResult simulateFixedOrder(
    const std::vector<unsigned> &order,
    const std::vector<std::vector<unsigned>> &predecessors,
    const std::vector<unsigned> &execTime) {
  const unsigned n = execTime.size();
  std::vector<bool> issued(n, false);
  std::vector<unsigned> finishCycle(n, 0);

  ScheduleResult result;
  result.order = order;
  result.timing.resize(n);

  unsigned cycle = 0;

  for (unsigned u : order) {
    unsigned dependencyReadyCycle = 0;
    for (unsigned pred : predecessors[u]) {
      dependencyReadyCycle =
          std::max(dependencyReadyCycle, finishCycle[pred]);
    }

    while (true) {
      unsigned inFlight = countInFlight(issued, finishCycle, cycle);
      if (inFlight < gMaxInFlight && dependencyReadyCycle <= cycle)
        break;
      ++cycle;
    }

    result.timing[u].issueCycle = cycle;
    result.timing[u].finishCycle = cycle + execTime[u];
    finishCycle[u] = result.timing[u].finishCycle;
    issued[u] = true;

    ++cycle;
  }

  unsigned makespan = 0;
  for (unsigned id = 0; id < n; ++id)
    makespan = std::max(makespan, finishCycle[id]);

  result.totalCycles = makespan;
  return result;
}

ScheduleResult buildGreedySchedule(
    const std::vector<std::vector<unsigned>> &graph,
    const std::vector<unsigned> &indegree,
    const std::vector<unsigned> &execTime,
    const std::vector<unsigned> &priority) {
  const unsigned n = graph.size();

  std::vector<unsigned> remainingIndegree = indegree;
  std::vector<unsigned> readyCycle(n, 0);
  std::vector<unsigned> finishCycle(n, 0);
  std::vector<bool> scheduled(n, false);

  ScheduleResult result;
  result.timing.resize(n);

  unsigned cycle = 0;

  while (result.order.size() < n) {
    unsigned numInFlight =
        countInFlight(scheduled, finishCycle, cycle);

    unsigned selected = n;

    if (numInFlight < gMaxInFlight) {
      for (unsigned id = 0; id < n; ++id) {
        if (scheduled[id])
          continue;
        if (remainingIndegree[id] != 0)
          continue;
        if (readyCycle[id] > cycle)
          continue;

        if (selected == n || priority[id] > priority[selected] ||
            (priority[id] == priority[selected] && id < selected)) {
          selected = id;
        }
      }
    }

    if (selected != n) {
      unsigned u = selected;
      scheduled[u] = true;
      result.order.push_back(u);

      result.timing[u].issueCycle = cycle;
      result.timing[u].finishCycle = cycle + execTime[u];
      finishCycle[u] = result.timing[u].finishCycle;

      for (unsigned v : graph[u]) {
        --remainingIndegree[v];
        readyCycle[v] = std::max(readyCycle[v], finishCycle[u]);
      }
    } else if (numInFlight == 0) {
      bool hasTopoReady = false;
      for (unsigned id = 0; id < n; ++id) {
        if (!scheduled[id] && remainingIndegree[id] == 0) {
          hasTopoReady = true;
          break;
        }
      }
      if (!hasTopoReady)
        break;
    }

    ++cycle;
  }

  unsigned makespan = 0;
  for (unsigned id = 0; id < n; ++id)
    makespan = std::max(makespan, finishCycle[id]);

  result.totalCycles = makespan;
  return result;
}

struct LatencyListSchedulerPass
    : mlir::PassWrapper<LatencyListSchedulerPass,
                        mlir::OperationPass<mlir::func::FuncOp>> {
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(
      LatencyListSchedulerPass)

  mlir::StringRef getArgument() const override {
    return "latency-list-schedule";
  }

  mlir::StringRef getDescription() const override {
    return "Latency-aware greedy DAG scheduling with an in-flight limit";
  }

  void runOnOperation() override {
    mlir::func::FuncOp func = getOperation();

    llvm::errs() << "\n========================================\n";
    llvm::errs() << "LatencyParallelismStrainedScheduler: "
                 << func.getName() << "\n";
    llvm::errs() << "hardware model: load=" << gLoadExecTime
                 << ", addi=" << gAddiExecTime
                 << ", muli=" << gMuliExecTime
                 << ", issueWidth=1"
                 << ", maxInFlight=" << gMaxInFlight << "\n";

    if (func.getBody().empty()) {
      llvm::errs() << "scheduler input valid: no\n";
      llvm::errs() << "reason: function has no body\n";
      return;
    }

    if (!func.getBody().hasOneBlock()) {
      llvm::errs() << "scheduler input valid: no\n";
      llvm::errs() << "reason: only single-block functions are supported\n";
      return;
    }

    mlir::Block &block = func.getBody().front();
    mlir::Operation *terminator = block.getTerminator();

    std::vector<mlir::Operation *> ops;
    std::unordered_map<mlir::Operation *, unsigned> opToId;

    for (mlir::Operation &op : block) {
      if (&op == terminator)
        continue;

      if (!isSupportedOp(&op)) {
        llvm::errs() << "scheduler input valid: no\n";
        llvm::errs() << "reason: unsupported operation " << op.getName()
                     << "\n";
        return;
      }

      unsigned id = ops.size();
      ops.push_back(&op);
      opToId[&op] = id;
    }

    const unsigned n = ops.size();

    if (n == 0) {
      llvm::errs() << "scheduler input valid: yes\n";
      llvm::errs() << "reason: no schedulable operations\n";
      return;
    }

    std::vector<std::vector<unsigned>> graph(n);
    std::vector<std::vector<unsigned>> predecessors(n);
    std::vector<unsigned> indegree(n, 0);
    std::vector<unsigned> execTime(n, 0);

    for (unsigned id = 0; id < n; ++id)
      execTime[id] = getExecTime(ops[id]);

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

        if (std::find(successors.begin(), successors.end(), to) !=
            successors.end()) {
          continue;
        }

        successors.push_back(to);
        predecessors[to].push_back(from);
        ++indegree[to];
      }
    }

    llvm::errs() << "operations:\n";
    for (unsigned id = 0; id < n; ++id) {
      llvm::errs() << "  op" << id << ": " << ops[id]->getName()
                   << ", execTime=" << execTime[id] << "\n";
    }

    llvm::errs() << "edges:\n";
    bool hasEdge = false;
    for (unsigned from = 0; from < n; ++from) {
      for (unsigned to : graph[from]) {
        hasEdge = true;
        llvm::errs() << "  op" << from << " -> op" << to << "\n";
      }
    }
    if (!hasEdge)
      llvm::errs() << "  <none>\n";

    llvm::errs() << "indegree:\n";
    for (unsigned id = 0; id < n; ++id)
      llvm::errs() << "  op" << id << ": " << indegree[id] << "\n";

    std::queue<unsigned> ready;
    std::vector<unsigned> remainingIndegree = indegree;
    std::vector<unsigned> baseTopoOrder;

    for (unsigned id = 0; id < n; ++id) {
      if (remainingIndegree[id] == 0)
        ready.push(id);
    }

    while (!ready.empty()) {
      unsigned u = ready.front();
      ready.pop();
      baseTopoOrder.push_back(u);

      for (unsigned v : graph[u]) {
        --remainingIndegree[v];
        if (remainingIndegree[v] == 0)
          ready.push(v);
      }
    }

    if (baseTopoOrder.size() != n) {
      llvm::errs() << "scheduler input valid: no\n";
      llvm::errs() << "cycle detected: yes\n";
      return;
    }

    llvm::errs() << "scheduler input valid: yes\n";
    llvm::errs() << "cycle detected: no\n";
    llvm::errs() << "base topological order:\n  ";
    printOrder(baseTopoOrder, ops);

    std::vector<unsigned> priority(n, 0);

    for (auto it = baseTopoOrder.rbegin(); it != baseTopoOrder.rend(); ++it) {
      unsigned u = *it;
      unsigned maxSuccessorPriority = 0;

      for (unsigned v : graph[u]) {
        maxSuccessorPriority =
            std::max(maxSuccessorPriority, priority[v]);
      }

      priority[u] = execTime[u] + maxSuccessorPriority;
    }

    llvm::errs() << "critical priority:\n";
    for (unsigned id = 0; id < n; ++id)
      llvm::errs() << "  op" << id << ": " << priority[id] << "\n";

    std::vector<unsigned> originalOrder;
    for (unsigned id = 0; id < n; ++id)
      originalOrder.push_back(id);

    ScheduleResult original =
        simulateFixedOrder(originalOrder, predecessors, execTime);

    llvm::errs() << "original order:\n  ";
    printOrder(original.order, ops);
    llvm::errs() << "original timestamps:\n";
    for (unsigned id : original.order) {
      llvm::errs() << "  op" << id
                   << ": issueCycle=" << original.timing[id].issueCycle
                   << ", finishCycle=" << original.timing[id].finishCycle
                   << "\n";
    }
    llvm::errs() << "original total cycles: " << original.totalCycles << "\n";

    ScheduleResult scheduled =
        buildGreedySchedule(graph, indegree, execTime, priority);

    if (scheduled.order.size() != n) {
      llvm::errs() << "internal error: scheduler did not schedule all ops\n";
      return;
    }

    llvm::errs() << "scheduled topological order:\n  ";
    printOrder(scheduled.order, ops);
    llvm::errs() << "scheduled timestamps:\n";
    for (unsigned id : scheduled.order) {
      llvm::errs() << "  op" << id
                   << ": issueCycle=" << scheduled.timing[id].issueCycle
                   << ", finishCycle=" << scheduled.timing[id].finishCycle
                   << "\n";
    }
    llvm::errs() << "scheduled total cycles: " << scheduled.totalCycles << "\n";

    long long cycleDelta =
        static_cast<long long>(original.totalCycles) -
        static_cast<long long>(scheduled.totalCycles);
    llvm::errs() << "cycle delta (original - scheduled): " << cycleDelta
                 << "\n";

    if (scheduled.totalCycles > original.totalCycles) {
      llvm::errs() << "IR reordered: no\n";
      llvm::errs() << "reason: heuristic schedule is worse than original\n";
      return;
    }

    if (scheduled.order == originalOrder) {
      llvm::errs() << "IR reordered: no\n";
      llvm::errs() << "reason: scheduled order equals original order\n";
      return;
    }

    for (unsigned id : scheduled.order)
      ops[id]->moveBefore(terminator);

    llvm::errs() << "IR reordered: yes\n";
  }
};

} // namespace

void tinyir::registerLatencyListSchedulerPass() {
  mlir::PassRegistration<LatencyListSchedulerPass>();
}

```

下面逐段解释这个文件。

---

# 10. Step 4：先理解硬件参数

顶部：

```cpp
unsigned gLoadExecTime = 10;
unsigned gAddiExecTime = 1;
unsigned gMuliExecTime = 5;
unsigned gMaxInFlight = 2;
```

后续修改硬件模型时，只需要改这里。

例如：

```cpp
unsigned gLoadExecTime = 20;
unsigned gAddiExecTime = 1;
unsigned gMuliExecTime = 3;
unsigned gMaxInFlight = 4;
```

就可以测试另一个 target model。

注意：

> 这些数字是教学模型，不是 AMD 某个真实 GPU 的精确 ISA latency。

---

# 11. Step 5：为什么要同时记录 issueCycle 和 finishCycle？

结构：

```cpp
struct TimingRecord {
  unsigned issueCycle = 0;
  unsigned finishCycle = 0;
};
```

你要求的“每个 op 被调度的 cycle 时间戳”，在这里就是：

```text
issueCycle
```

例如：

```text
op3: issueCycle=1
```

表示：

> op3 在 cycle 1 被 scheduler 发射。

同时记录：

```text
finishCycle
```

因为 RAW consumer 判断何时能执行，需要 producer 的完成时间。

若：

```text
issueCycle = 2
execTime = 5
```

则：

$$
finishCycle=2+5=7
$$

---

# 12. Step 6：`countInFlight()` 在做什么？

函数：

```cpp
unsigned countInFlight(...)
```

判断：

```cpp
issued[id] && finishCycle[id] > cycle
```

即：

> 已经 issue，但当前 cycle 还没有完成。

例如当前：

```text
cycle = 4
```

某 op：

```text
issue = 1
finish = 6
```

则：

```text
finish > cycle
```

所以它仍然 in-flight。

---

# 13. Step 7：为什么还要 `simulateFixedOrder()`？

Scheduler 不仅输出新顺序，还应该回答：

> 新顺序到底比原顺序快多少？

因此我们把原始：

```text
op0 op1 op2 ... opN
```

也按照完全相同的硬件模型模拟一遍：

```text
issue width = 1
max in-flight = 2
RAW completion constraint
```

然后得到：

```text
original issueCycle
original finishCycle
original total cycles
```

这样优化前后才公平可比。

---

# 14. Step 8：建图逻辑与上一阶段保持一致

仍然使用：

```cpp
mlir::Operation *producer =
    operand.getDefiningOp();
```

建立：

```text
producer -> consumer
```

为了防止：

```mlir
%1 = arith.addi %0, %0 : i32
```

让同一个 producer 对同一个 consumer 重复计边，仍然使用：

```cpp
std::find(successors.begin(), successors.end(), to)
```

进行去重。

同时额外维护：

```cpp
predecessors[to].push_back(from);
```

因为：

```text
graph
```

适合从 producer 找 successor，

而：

```text
predecessors
```

适合模拟固定顺序时计算 consumer 的 operand-ready 时间。

---

# 15. Step 9：Kahn 在这里首先只是做合法性检查

这一轮：

```cpp
std::queue<unsigned> ready;
```

使用标准 Kahn。

目的：

```text
1. cycle check
2. 得到 baseTopoOrder
```

不是最终 schedule。

如果：

```cpp
baseTopoOrder.size() != n
```

说明图没有成功消费全部节点：

```text
cycle detected
```

此时直接退出，不进行 IR reorder。

---

# 16. Step 10：reverse-topo 计算 critical priority

核心：

```cpp
for (auto it = baseTopoOrder.rbegin();
     it != baseTopoOrder.rend();
     ++it) {
  ...
}
```

对于每个 `u`：

```cpp
priority[u] =
    execTime[u] +
    maxSuccessorPriority;
```

例如：

```text
load(10) -> mul(5) -> add(1)
```

则：

$$
P(add)=1
$$

$$
P(mul)=5+1=6
$$

$$
P(load)=10+6=16
$$

---

# 17. Step 11：Greedy Scheduler 的核心状态

进入：

```cpp
buildGreedySchedule(...)
```

后初始化：

```cpp
std::vector<unsigned> remainingIndegree = indegree;
std::vector<unsigned> readyCycle(n, 0);
std::vector<unsigned> finishCycle(n, 0);
std::vector<bool> scheduled(n, false);
```

它们分别回答：

```text
remainingIndegree:
还有多少 predecessor 尚未进入 schedule？

readyCycle:
所有 predecessor 最晚何时完成？

finishCycle:
该 op 何时执行完成？

scheduled:
是否已经进入最终 topo order？
```

---

# 18. Step 12：第一轮如何从初始 indegree=0 节点中选择？

初始：

```text
cycle = 0
scheduled = false
readyCycle = 0
remainingIndegree = indegree
```

扫描：

```cpp
if (scheduled[id])
  continue;

if (remainingIndegree[id] != 0)
  continue;

if (readyCycle[id] > cycle)
  continue;
```

所以所有：

```text
indegree == 0
```

的 root 都会通过。

因为它们：

```text
readyCycle = 0
cycle = 0
```

最后通过：

```cpp
priority[id] > priority[selected]
```

选 priority 最大的 root。

因此这里虽然没有显式：

```cpp
queue.push(root);
```

但逻辑上等价于每轮动态构造 ready set。

---

# 19. Step 13：为什么在 issue 时就减 successor 的 indegree？

选中：

```text
u
```

之后立即：

```cpp
--remainingIndegree[v];
```

注意这里的 indegree 语义是：

> predecessor 是否已经进入**静态 schedule**。

并不是：

> predecessor 是否已经执行完成。

所以 producer 一旦已经被排在 consumer 前面，它对应的 topological edge 就可以被“消费”。

但 consumer 仍不能立即执行，因为：

```cpp
readyCycle[v] =
    max(
        readyCycle[v],
        finishCycle[u]);
```

继续保留真实 RAW latency 约束。

这正好把：

```text
topo-ready
```

和：

```text
operand-ready
```

分开。

---

# 20. Step 14：`readyCycle` 如何取代原来的 `record`？

假设：

```text
A ----\
       -> C
B ----/
```

A：

```text
finish = 10
```

则：

```cpp
readyCycle[C] = max(0, 10);
```

得到：

```text
10
```

B：

```text
finish = 7
```

再更新：

```cpp
readyCycle[C] = max(10, 7);
```

仍然：

```text
10
```

因此：

$$
readyCycle(C)
=
\max(finish(A), finish(B))
$$

完全不需要额外 `record` map。

---

# 21. Step 15：如何施加 `maxInFlight = 2`？

每个 cycle：

```cpp
unsigned numInFlight =
    countInFlight(
        scheduled,
        finishCycle,
        cycle);
```

只有：

```cpp
numInFlight < gMaxInFlight
```

时，才允许选新 op。

因此：

```text
issue width = 1
```

来自：

> 每个 while iteration 最多选一个 `selected`。

而：

```text
max in-flight = 2
```

来自：

> 已经没完成的 op 达到 2 时，不再 issue 新 op。

二者是独立限制。

---

# 22. Step 16：Greedy 到底怎么选？

当前所有候选必须满足：

```text
!scheduled
remainingIndegree == 0
readyCycle <= cycle
```

然后：

```text
priority 最大者优先
```

如果 priority 相同：

```text
原始 op id 小者优先
```

这是一个：

```text
critical-path-first list scheduler
```

它不是 exact scheduler。

也不保证全局最优。

但复杂度远低于枚举所有 topo order。

---

# 23. Step 17：为什么本版本在没有 ready op 时可以直接 `cycle++`？

可能出现：

```text
topo 上已经有候选
但 predecessor 结果还没 ready
```

或者：

```text
已经有 2 个 in-flight op
资源已满
```

此时：

```text
selected == n
```

不 issue 新指令。

循环尾部：

```cpp
++cycle;
```

相当于硬件 stall 一个 cycle。

对于教学版，这是最直观的 cycle-accurate 模拟。

后续工程化时，可以进一步做 event jump：

```text
直接跳到最近一次 finish event
```

避免逐 cycle 空转。

---

# 24. Step 18：为什么总周期不是 `cycle`？

schedule 循环退出时：

> 所有 op 都已经 issue。

但最后一条或若干条 op 可能还在执行。

所以最终：

```cpp
makespan =
    max(finishCycle[id]);
```

数学上：

$$
Makespan
=
\max_i finishCycle(i)
$$

这才是整个 schedule 的总执行周期。

---

# 25. Step 19：`load_hide` 的预期结果

原始顺序：

```text
op0 -> op1 -> op2 -> op3 -> op4 -> op5 -> op6
```

在：

```text
load=10
addi=1
muli=5
maxInFlight=2
issueWidth=1
```

模型下：

```text
op0 issue=0   finish=10
op1 issue=10  finish=15
op2 issue=15  finish=16
op3 issue=16  finish=17
op4 issue=17  finish=22
op5 issue=22  finish=23
op6 issue=23  finish=24
```

所以：

```text
original total cycles = 24
```

Greedy scheduler 得到：

```text
op0 -> op3 -> op4 -> op5 -> op1 -> op2 -> op6
```

时间戳：

```text
op0 issue=0   finish=10
op3 issue=1   finish=2
op4 issue=2   finish=7
op5 issue=7   finish=8
op1 issue=10  finish=15
op2 issue=15  finish=16
op6 issue=16  finish=17
```

所以：

```text
scheduled total cycles = 17
```

也就是：

```text
24 -> 17
```

---

# 26. Step 20：为什么 `op5` 会在 `op1` 前？

当：

```text
op0
op3
op4
```

都已经 schedule 后，

`op1` 的：

```text
remainingIndegree = 0
```

但：

```text
readyCycle[op1] = 10
```

因为它依赖 load。

而 `op5`：

```text
readyCycle[op5] = 7
```

因此：

```text
cycle 7
```

时只有 `op5` 真正 operand-ready。

所以先 issue：

```text
op5
```

然后继续等 load。

这正是在用 independent work 隐藏长 latency。

---

# 27. Step 21：输出的“timestamp”怎么看？

最终日志：

```text
scheduled timestamps:
  op0: issueCycle=0, finishCycle=10
  op3: issueCycle=1, finishCycle=2
  ...
```

其中：

```text
issueCycle
```

就是你要求的：

> op 被调度/发射的 cycle 时间戳。

而：

```text
finishCycle
```

用于：

> 解释 RAW dependency 和总 makespan。

因此面试时可以画成时间轴：

```text
cycle 0 : issue op0
cycle 1 : issue op3
cycle 2 : issue op4
...
```

---

# 28. Step 22：为什么还要加“不退化保护”？

Greedy heuristic 不保证全局最优。

甚至在某些图上：

```text
scheduled.totalCycles
```

可能比：

```text
original.totalCycles
```

更差。

所以真正修改 IR 前：

```cpp
if (scheduled.totalCycles > original.totalCycles) {
  ...
  return;
}
```

只在自己的 cost model 预测：

```text
不退化
```

时才应用 transformation。

这是非常合理的 compiler engineering 思路。

---

# 29. Step 23：真正重排 MLIR Operation

最终：

```cpp
for (unsigned id : scheduled.order)
  ops[id]->moveBefore(terminator);
```

因为 `scheduled.order` 始终满足：

```text
remainingIndegree == 0 才能被选
```

所以它一定是合法 topo order。

因此：

```text
producer
```

仍然在：

```text
consumer
```

之前。

同时：

```text
terminator
```

从一开始就没有参与 scheduling，始终留在 block 最后。

---

# 30. Step 24：修改 `tinyir-opt.cpp`

文件：

```text
src/tinyir-opt.cpp
```

增加：

```cpp
tinyir::registerLatencyListSchedulerPass();
```

修改后的**完整文件**：

```cpp
#include "mlir/InitAllDialects.h"
#include "mlir/InitAllExtensions.h"
#include "mlir/InitAllPasses.h"
#include "mlir/Tools/mlir-opt/MlirOptMain.h"

#include "dialect/Tutorial.h"
#include "dialect/List.h"
#include "passes/Passes.h" //用于提供注册函数

int main(int argc, char **argv) {
  mlir::DialectRegistry registry;

  // MLIR自带dialect
  mlir::registerAllDialects(registry);

  // MLIR自带pass
  mlir::registerAllPasses();

  // MLIR extensions
  mlir::registerAllExtensions(registry);

  // 本项目自定义dialect
  registry.insert<
      mlir::tutorial::TutorialDialect,
      mlir::list::ListDialect>();

  // 自定义pass
  tinyir::registerDemoPass();
  tinyir::registerInspectIRPass();
  tinyir::registerUseDefPass();
  tinyir::registerCFGInspectPass();
  tinyir::registerBuilderSafetyPass();
  tinyir::registerRewriterDemoPass();
  tinyir::registerListLowerFromElementsPass();
  tinyir::registerListLoweringPass();

  tinyir::registerDependencyDAGPass();
  tinyir::registerLatencyListSchedulerPass();

  return mlir::asMainReturnCode(
      mlir::MlirOptMain(
          argc,
          argv,
          "tinyIR optimizer driver\n",
          registry));
}

```

---

# 31. Step 25：Makefile 不修改

当前 Makefile 已经自动收集：

```make
PASS_SRCS := $(wildcard $(PASS_DIR)/*.cpp)
```

因此无需新增 `.cpp` 文件名。

而且你已经有：

```make
PASSFLAGS := --mlir-disable-threading ...
```

这样多个 `func.func` 的 `llvm::errs()` 输出不会并行交错。

---

# 32. Step 26：重新编译

仓库根目录：

```bash
make clean
make build
```

应该能看到类似：

```text
+CXX src/passes/LatencyListScheduler.cpp
```

---

# 33. Step 27：运行新 Pass

执行：

```bash
make pass \
  TEST=test/passes/latency-list-scheduler.mlir \
  PASS=latency-list-schedule
```

pipeline 会成为：

```text
builtin.module(
  func.func(
    latency-list-schedule
  )
)
```

---

# 34. Step 28：重点检查哪些输出？

对于每个函数，你应该看到：

```text
hardware model
operations
edges
indegree
cycle detected
base topological order
critical priority
original order
original timestamps
original total cycles
scheduled topological order
scheduled timestamps
scheduled total cycles
cycle delta
IR reordered
```

最重要的是：

```text
scheduled topological order
```

以及：

```text
scheduled timestamps
```

因为前者说明：

> scheduler 最终选择了什么静态顺序。

后者说明：

> 每条指令究竟在哪个 cycle 发射。

---

# 35. 为什么这个 Pass 仍然叫 heuristic？

因为它的每一步都是：

```text
当前所有可执行节点
↓
选 priority 最大者
↓
不回溯
```

所以属于 greedy list scheduling。

它没有：

```text
枚举所有完整 topo order
```

因此不能保证：

```text
global optimum
```

准确的面试说法：

> This is a latency-aware, resource-constrained heuristic list scheduler using critical-path priority.

---

# 36. 复杂度

建 DAG：

$$
O(V+E)
$$

Kahn cycle check：

$$
O(V+E)
$$

Reverse-topo priority：

$$
O(V+E)
$$

当前教学版每 cycle 扫描所有节点：

$$
O(C\cdot V)
$$

其中 `C` 是模拟总 cycle 数。

所以整体大致：

$$
O(V+E+C\cdot V)
$$

它不是 factorial search。

后续可以通过：

```text
ready heap
pending heap
event queue
```

进一步优化。

---

# 37. 当前模型与工业 Scheduler 的差距

当前有：

```text
single basic block
SSA RAW dependency
load/addi/muli
fixed latency
issue width = 1
max in-flight = 2
critical-path greedy
```

真实工业 scheduler 还会考虑：

```text
不同 execution units
instruction throughput
resource occupancy
memory alias
store/load ordering
register pressure
branch/control dependency
multi-issue
target hazard
cache behavior
loop-carried dependence
```

所以这个项目应该描述为：

> 一个小型、可解释、可验证的 resource-constrained list scheduler。

不要描述成：

> 完整工业 backend scheduler。

---

# 38. 为什么暂时不支持 `store`？

当前 DAG 来自：

```text
SSA def-use
```

但是：

```text
store
```

的 memory dependency 不一定表现为 SSA producer-consumer edge。

例如：

```text
store %x, %A
load %A
```

第二条 load 与 store 有真实 memory ordering，但 SSA graph 不一定直接体现。

因此加入 store 前，需要考虑：

```text
MemoryEffects
AliasAnalysis
Memory Dependence
```

这是当前模型刻意留下的边界。

---

# 39. 面试时建议这样讲完整故事

可以按下面顺序讲：

> 第一阶段我先实现了一个 block-local DependencyDAGPass，从 MLIR SSA operand 的 `getDefiningOp()` 建立 producer-consumer edge，并通过 Kahn algorithm 做 cycle check 和 topological analysis。
>
> 第二阶段我发现普通 topological sort 只保证 dependency legality，不保证具体硬件模型下的调度效率，所以实现了一个 latency-aware heuristic list scheduler。
>
> 我把 load/add/mul latency 配成 10/1/5 cycles，硬件设为 single-issue，同时最多允许两个 instruction in flight。先用 Kahn 检查 DAG 并获得一个 base topo order，再反向做 longest-path DP 得到每个节点的 critical priority。
>
> 真正 scheduling 时，我维护 `remainingIndegree`、`readyCycle` 和 `finishCycle`：`remainingIndegree==0` 表示 topologically ready，而 `readyCycle<=currentCycle` 才表示 operand 真正 ready；同时检查当前 in-flight 数量。每个 cycle 在所有合法候选中 greedy 选择 critical priority 最大的 operation，并记录其 issue/finish timestamp。
>
> 最后我用同一个 timing model 模拟原始顺序和新顺序。如果 heuristic 不退化，就通过 MLIR `moveBefore()` 真正重排 Operation。

---

# 40. 面试官最值得追问的几个点

## Q1：为什么 indegree 在 issue 时就减？

因为它描述的是：

```text
static scheduling dependency
```

producer 已经被放在 consumer 前面后，topological edge 就被消费。

真实 completion constraint 由：

```text
readyCycle
```

继续保存。

## Q2：为什么 `remainingIndegree==0` 仍然不一定能执行？

因为 predecessor 可能已经 schedule，但还没执行完。

所以还要求：

```text
readyCycle <= currentCycle
```

## Q3：为什么 priority 要 reverse topo？

因为：

$$
Priority(u)
=
ExecTime(u)+\max Priority(successor)
$$

必须先知道 successor 的 priority。

## Q4：为什么不保证最优？

因为：

```text
每一步只做 local greedy decision
```

且不回溯。

## Q5：为什么记录 issueCycle？

因为 static order 本身不够表达真实 timing。

issue timestamp 可以直接解释：

```text
stall
latency hiding
resource occupancy
```

## Q6：`parallelism=2` 和 dual issue 一样吗？

不一样。

本模型：

```text
issueWidth = 1
maxInFlight = 2
```

表示一周期最多发一条，但可以同时有两条未完成指令。

---

# 41. 最终目录结构

完成后相关目录：

```text
tinyIR/
├── src/
│   ├── passes/
│   │   ├── DependencyDAG.cpp
│   │   ├── LatencyListScheduler.cpp
│   │   ├── Passes.h
│   │   └── ...
│   └── tinyir-opt.cpp
├── test/
│   └── passes/
│       ├── dependency-dag.mlir
│       └── latency-list-scheduler.mlir
└── docs/
    ├── 02_tinyIR_DependencyDAGPass_Practice.md
    └── advance02_LatencyListScheduler.md
```

---

# 42. 建议提交

确认：

```bash
git status
git diff
```

然后：

```bash
git add \
  src/passes/LatencyListScheduler.cpp \
  src/passes/Passes.h \
  src/tinyir-opt.cpp \
  test/passes/latency-list-scheduler.mlir \
  docs/advance02_LatencyListScheduler.md
```

提交：

```bash
git commit -m "add latency parallelism constrained scheduler"
```

---

# 43. 最终自测

不看代码，确保可以回答：

1. `remainingIndegree` 的语义是什么？
2. `readyCycle` 为什么初始化为 0？
3. 第一轮如何隐式得到所有初始 indegree=0 的节点？
4. topo-ready 和 operand-ready 有何区别？
5. 为什么 successor 的 indegree 在 producer issue 时就减？
6. `readyCycle[v] = max(readyCycle[v], finishCycle[u])` 为什么正确？
7. issue width=1 与 maxInFlight=2 有何区别？
8. `Priority(u)` 为什么必须 reverse-topo 计算？
9. 为什么 greedy schedule 一定仍是合法 topo order？
10. 为什么它不一定是全局最优？
11. 为什么最终 makespan 要取所有 finishCycle 的最大值？
12. 为什么要同时记录 issueCycle 和 finishCycle？
13. 为什么当前不支持 store？
14. 为什么真正 reorder 前还要和 original cycles 比较？
15. `moveBefore(terminator)` 为什么不会移动 return？
16. 如果以后要更工业化，应该如何加入 execution unit、throughput、alias 和 register pressure？

如果这 16 个问题都能讲清楚，这个功能就已经形成了一个完整的小型 Compiler Scheduling 项目闭环。
