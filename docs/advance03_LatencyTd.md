# advance03：Latency Dialect —— 用自定义 Latency Op 构造任意调度 DAG

> 基于当前最新仓库 `tinyIR-master(5).zip`。  
> 当前状态：
>
> - 已经有 `DependencyDAGPass`
> - 已经有 `LatencyListSchedulerPass`
> - `src/dialect/Latency.td` 已经存在，但还没有 `Latency.h / Latency.cpp`
> - `tinyir-opt.cpp` 尚未注册 `LatencyDialect`
> - `LatencyListScheduler.cpp` 仍然识别 `memref.load / arith.addi / arith.muli`
>
> 本节目标：
>
> ```text
> latency.lat1
> latency.lat5
> latency.lat10
> ```
>
> 三个自定义 Pure SSA Operation 专门用于构造任意 Dependency DAG，并把原本 Scheduler 中：
>
> ```text
> arith.addi  -> latency.lat1
> arith.muli  -> latency.lat5
> memref.load -> latency.lat10
> ```
>
> 完整替换。
>
> 最后会把之前的大型 DAG 图转换成可以直接运行的 MLIR testcase。

---

# 0. 为什么单独做一个 Latency Dialect？

之前为了快速验证 Scheduler，我们把：

```text
memref.load latency = 10
arith.muli latency = 5
arith.addi latency = 1
```

当作三种不同耗时的节点。

但这样会带来一个问题：

```text
memref.load
```

并不是普通的“输入若干 SSA value，输出一个 SSA value”的纯计算节点。

它真正需要：

```text
memref + index
```

因此对于任意 DAG，例如：

```text
muli -> load
```

不能自然地把前一个 `muli` 的结果直接作为 `load` 的普通数据输入。

所以我们现在把 Scheduler 和真实 ISA / Dialect 语义彻底解耦：

```text
latency.lat1
latency.lat5
latency.lat10
```

这些 Operation：

```text
只用于 Dependency DAG / Scheduling 实验
没有真实计算语义
没有 memory side effect
只保留 SSA dependency + latency
```

这样：

> 图中的每一条入边，直接对应一个 SSA operand。

---

# 1. 最终目录变化

完成后新增：

```text
src/dialect/Latency.h
src/dialect/Latency.cpp
```

当前已经存在、需要确认最终内容：

```text
src/dialect/Latency.td
```

需要修改：

```text
src/tinyir-opt.cpp
src/passes/LatencyListScheduler.cpp
test/passes/latency-list-scheduler.mlir
```

不需要修改：

```text
Makefile
src/passes/Passes.h
src/passes/DependencyDAG.cpp
```

原因是当前 Makefile 已经自动收集：

```make
TD_SRCS      := $(wildcard $(DIALECT_DIR)/*.td)
DIALECT_SRCS := $(wildcard $(DIALECT_DIR)/*.cpp)
```

所以：

```text
Latency.td
Latency.cpp
```

都会自动进入 TableGen / C++ build 流程。

---

# 2. Step 1：确认 `Latency.td`

文件：

```text
src/dialect/Latency.td
```

当前最新仓库里已经有这个文件，因此本步骤不是新增。

它的最终完整内容保持为：

```tablegen
#ifndef LATENCY_DIALECT
#define LATENCY_DIALECT

include "mlir/IR/OpBase.td"
include "mlir/Interfaces/SideEffectInterfaces.td"


// 定义Latency Dialect
def Latency_Dialect : Dialect {
  let name = "latency";

  let cppNamespace = "::mlir::latency";

  let summary =
      "Synthetic latency operations for instruction scheduling experiments";

  let description = [{
    The latency dialect provides synthetic pure SSA operations with fixed
    scheduling latencies.

    These operations are not intended to model real ISA instructions.
    They are used to construct arbitrary dependency DAGs and evaluate
    latency-aware scheduling algorithms.
  }];
}


// 定义Latency Dialect中所有Operation的共同基类
class Latency_Op<
    string mnemonic,
    list<Trait> traits = []>
    : Op<Latency_Dialect, mnemonic, traits>;


// latency.lat1
def Latency_Lat1Op
    : Latency_Op<"lat1", [Pure]> {

  let summary =
      "Synthetic operation with execution latency 1";

  let description = [{
    A synthetic pure operation whose scheduler latency is modeled as 1 cycle.

    It accepts zero or more i32 SSA operands and produces one i32 SSA result.
    Each operand represents one incoming dependency edge in the scheduling DAG.

    Examples:

      %0 = latency.lat1 : i32
      %1 = latency.lat1 %0 : i32
      %2 = latency.lat1 %0, %1 : i32
  }];

  let arguments = (ins
    Variadic<I32>:$inputs
  );

  let results = (outs
    I32:$result
  );

  let assemblyFormat =
      "$inputs attr-dict `:` type($result)";
}


// latency.lat5
def Latency_Lat5Op
    : Latency_Op<"lat5", [Pure]> {

  let summary =
      "Synthetic operation with execution latency 5";

  let description = [{
    A synthetic pure operation whose scheduler latency is modeled as 5 cycles.

    It accepts zero or more i32 SSA operands and produces one i32 SSA result.
    Each operand represents one incoming dependency edge in the scheduling DAG.

    Examples:

      %0 = latency.lat5 : i32
      %1 = latency.lat5 %0 : i32
      %2 = latency.lat5 %0, %1 : i32
  }];

  let arguments = (ins
    Variadic<I32>:$inputs
  );

  let results = (outs
    I32:$result
  );

  let assemblyFormat =
      "$inputs attr-dict `:` type($result)";
}


// latency.lat10
def Latency_Lat10Op
    : Latency_Op<"lat10", [Pure]> {

  let summary =
      "Synthetic operation with execution latency 10";

  let description = [{
    A synthetic pure operation whose scheduler latency is modeled as 10 cycles.

    It accepts zero or more i32 SSA operands and produces one i32 SSA result.
    Each operand represents one incoming dependency edge in the scheduling DAG.

    Examples:

      %0 = latency.lat10 : i32
      %1 = latency.lat10 %0 : i32
      %2 = latency.lat10 %0, %1 : i32
  }];

  let arguments = (ins
    Variadic<I32>:$inputs
  );

  let results = (outs
    I32:$result
  );

  let assemblyFormat =
      "$inputs attr-dict `:` type($result)";
}


#endif // LATENCY_DIALECT
```

---

# 3. Step 2：理解为什么 Operand 用 `Variadic<I32>`

这里没有写：

```tablegen
I32:$lhs,
I32:$rhs
```

而是：

```tablegen
Variadic<I32>:$inputs
```

因为 Scheduling DAG 中一个节点可能有：

```text
0 个 predecessor
1 个 predecessor
2 个 predecessor
3 个 predecessor
...
```

例如：

```mlir
%0 = latency.lat10 : i32
```

表示：

```text
root
```

没有任何入边。

而：

```mlir
%1 = latency.lat5 %0 : i32
```

表示：

```text
op0 -> op1
```

再比如：

```mlir
%2 = latency.lat1 %0, %1 : i32
```

表示：

```text
op0 ----\
         -> op2
op1 ----/
```

因为 Scheduler 本来就是：

```cpp
for (mlir::Value operand : consumer->getOperands()) {
    mlir::Operation *producer = operand.getDefiningOp();
}
```

所以每个 operand 会自然生成一条：

```text
producer -> consumer
```

依赖边。

---

# 4. Step 3：为什么三个 Op 必须是 `[Pure]`

三种定义：

```tablegen
Latency_Op<"lat1", [Pure]>
Latency_Op<"lat5", [Pure]>
Latency_Op<"lat10", [Pure]>
```

这里：

```text
Pure
```

意味着：

```text
没有 memory effect
没有不可见 side effect
```

当前 Scheduler 最后会：

```cpp
op->moveBefore(...)
```

真正修改 Operation 的静态顺序。

对于这些 synthetic ops：

> 只要保持 SSA producer-consumer 的拓扑顺序，就可以安全交换彼此的位置。

这正是我们想要的实验模型。

---

# 5. Step 4：新增 `Latency.h`

[NEW] 新增文件：

```text
src/dialect/Latency.h
```

完整内容：

```cpp
#ifndef LATENCY_H
#define LATENCY_H

#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/Dialect.h"
#include "mlir/IR/OpDefinition.h"
#include "mlir/IR/OpImplementation.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"

#include "Latency_Dialect.h.inc"

#define GET_OP_CLASSES
#include "Latency_Ops.h.inc"

#endif // LATENCY_H
```

它的组织方式和当前：

```text
Tutorial.h
```

完全一致。

TableGen 会根据：

```text
Latency.td
```

生成：

```text
build/gen/Latency_Dialect.h.inc
build/gen/Latency_Ops.h.inc
```

而这个头文件负责把 generated declarations 包装进正常 C++ 头文件。

---

# 6. Step 5：新增 `Latency.cpp`

[NEW] 新增文件：

```text
src/dialect/Latency.cpp
```

完整内容：

```cpp
#include "Latency.h"

#include "mlir/IR/DialectImplementation.h"

using namespace mlir;
using namespace mlir::latency;

#include "Latency_Dialect.cpp.inc"

void LatencyDialect::initialize() {
  addOperations<
#define GET_OP_LIST
#include "Latency_Ops.cpp.inc"
      >();
}

#define GET_OP_CLASSES
#include "Latency_Ops.cpp.inc"
```

这里最重要的是：

```cpp
void LatencyDialect::initialize()
```

里面：

```cpp
addOperations<
#define GET_OP_LIST
#include "Latency_Ops.cpp.inc"
>();
```

它把：

```text
Latency_Lat1Op
Latency_Lat5Op
Latency_Lat10Op
```

真正注册进：

```text
LatencyDialect
```

---

# 7. Step 6：TableGen 会自动生成哪些文件？

你当前 Makefile：

```make
TD_SRCS := $(wildcard $(DIALECT_DIR)/*.td)
```

所以：

```text
src/dialect/Latency.td
```

会自动参与：

```text
make tblgen
```

根据现有规则会生成：

```text
build/gen/Latency_Dialect.h.inc
build/gen/Latency_Dialect.cpp.inc

build/gen/Latency_Types.h.inc
build/gen/Latency_Types.cpp.inc

build/gen/Latency_Attrs.h.inc
build/gen/Latency_Attrs.cpp.inc

build/gen/Latency_Ops.h.inc
build/gen/Latency_Ops.cpp.inc
```

Latency Dialect 当前没有：

```text
custom type
custom attr
```

所以对应 Type / Attr 文件基本为空，但这不影响构建。

因此：

> Makefile 不需要修改。

---

# 8. Step 7：修改 `tinyir-opt.cpp` 注册 Latency Dialect

[MOD] 修改：

```text
src/tinyir-opt.cpp
```

修改点有两个：

```cpp
// [MOD] 增加Latency头文件
#include "dialect/Latency.h"
```

以及：

```cpp
// [MOD] 把LatencyDialect加入registry
mlir::latency::LatencyDialect
```

修改后的完整文件：

```cpp
#include "mlir/InitAllDialects.h"
#include "mlir/InitAllExtensions.h"
#include "mlir/InitAllPasses.h"
#include "mlir/Tools/mlir-opt/MlirOptMain.h"

#include "dialect/Tutorial.h"
#include "dialect/List.h"
#include "dialect/Latency.h" // [MOD] 新增Latency Dialect
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
      mlir::list::ListDialect,
      mlir::latency::LatencyDialect>(); // [MOD] 注册LatencyDialect

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

# 9. Step 8：为什么不注册 LatencyDialect 就不能解析？

如果 `.td/.h/.cpp` 都有，但：

```cpp
registry.insert<mlir::latency::LatencyDialect>();
```

没有执行，那么：

```mlir
%0 = latency.lat10 : i32
```

对 `tinyir-opt` 来说仍然是未知 Dialect Operation。

所以完整链路必须是：

```text
Latency.td
   ↓ TableGen
Latency_*.inc
   ↓
Latency.h / Latency.cpp
   ↓
LatencyDialect::initialize()
   ↓
tinyir-opt registry.insert<LatencyDialect>()
```

缺一不可。

---

# 10. Step 9：修改 `LatencyListScheduler.cpp`

现在 Scheduler 仍然使用：

```text
memref.load
arith.addi
arith.muli
```

本步骤全部改为：

```text
latency.lat10
latency.lat1
latency.lat5
```

同时为了语义清晰，把：

```text
gLoadExecTime
gAddiExecTime
gMuliExecTime
```

改为：

```text
gLat10ExecTime
gLat1ExecTime
gLat5ExecTime
```

[MOD] 修改后的完整文件：

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

// [MOD] Scheduler现在只处理三种synthetic Latency Op
unsigned gLat1ExecTime = 1;
unsigned gLat5ExecTime = 5;
unsigned gLat10ExecTime = 10;
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

// [MOD] 只允许latency.lat1 / lat5 / lat10
bool isSupportedOp(mlir::Operation *op) {
  llvm::StringRef name = op->getName().getStringRef();

  return name == "latency.lat1" ||
         name == "latency.lat5" ||
         name == "latency.lat10";
}

// [MOD] 根据Latency Op类型获得执行latency
unsigned getExecTime(mlir::Operation *op) {
  llvm::StringRef name = op->getName().getStringRef();

  if (name == "latency.lat1")
    return gLat1ExecTime;

  if (name == "latency.lat5")
    return gLat5ExecTime;

  if (name == "latency.lat10")
    return gLat10ExecTime;

  return 0;
}

// 打印最终得到的topo sort
void printOrder(
    const std::vector<unsigned> &order,
    const std::vector<mlir::Operation *> &ops) {

  for (unsigned i = 0; i < order.size(); ++i) {
    if (i != 0)
      llvm::errs() << " -> ";

    unsigned id = order[i];

    llvm::errs()
        << "op"
        << id
        << "("
        << ops[id]->getName()
        << ")";
  }

  llvm::errs() << "\n";
}

// 统计已经发射但尚未执行完成的op数量
unsigned countInFlight(
    const std::vector<bool> &issued,
    const std::vector<unsigned> &finishCycle,
    unsigned cycle) {

  unsigned count = 0;

  for (unsigned id = 0; id < issued.size(); ++id) {
    if (issued[id] &&
        finishCycle[id] > cycle) {
      ++count;
    }
  }

  return count;
}

// 模拟一个已经给定的静态顺序
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
          std::max(
              dependencyReadyCycle,
              finishCycle[pred]);
    }

    while (true) {
      unsigned inFlight =
          countInFlight(
              issued,
              finishCycle,
              cycle);

      if (inFlight < gMaxInFlight &&
          dependencyReadyCycle <= cycle) {
        break;
      }

      ++cycle;
    }

    result.timing[u].issueCycle = cycle;
    result.timing[u].finishCycle =
        cycle + execTime[u];

    finishCycle[u] =
        result.timing[u].finishCycle;

    issued[u] = true;

    ++cycle;
  }

  unsigned makespan = 0;

  for (unsigned id = 0; id < n; ++id) {
    makespan =
        std::max(
            makespan,
            finishCycle[id]);
  }

  result.totalCycles = makespan;
  return result;
}

// 基于critical priority进行greedy list scheduling
ScheduleResult buildGreedySchedule(
    const std::vector<std::vector<unsigned>> &graph,
    const std::vector<unsigned> &indegree,
    const std::vector<unsigned> &execTime,
    const std::vector<unsigned> &priority) {

  const unsigned n = graph.size();

  std::vector<unsigned> remainingIndegree =
      indegree;

  std::vector<unsigned> readyCycle(n, 0);
  std::vector<unsigned> finishCycle(n, 0);
  std::vector<bool> scheduled(n, false);

  ScheduleResult result;
  result.timing.resize(n);

  unsigned cycle = 0;

  while (result.order.size() < n) {
    unsigned numInFlight =
        countInFlight(
            scheduled,
            finishCycle,
            cycle);

    unsigned selected = n;

    if (numInFlight < gMaxInFlight) {
      for (unsigned id = 0;
           id < n;
           ++id) {

        if (scheduled[id])
          continue;

        if (remainingIndegree[id] != 0)
          continue;

        if (readyCycle[id] > cycle)
          continue;

        if (selected == n ||
            priority[id] > priority[selected] ||
            (priority[id] == priority[selected] &&
             id < selected)) {
          selected = id;
        }
      }
    }

    if (selected != n) {
      unsigned u = selected;

      result.order.push_back(u);

      result.timing[u].issueCycle =
          cycle;

      result.timing[u].finishCycle =
          cycle + execTime[u];

      scheduled[u] = true;

      finishCycle[u] =
          result.timing[u].finishCycle;

      for (unsigned v : graph[u]) {
        --remainingIndegree[v];

        readyCycle[v] =
            std::max(
                readyCycle[v],
                finishCycle[u]);
      }
    }

    ++cycle;
  }

  unsigned makespan = 0;

  for (unsigned id = 0; id < n; ++id) {
    makespan =
        std::max(
            makespan,
            finishCycle[id]);
  }

  result.totalCycles = makespan;
  return result;
}

struct LatencyListSchedulerPass
    : mlir::PassWrapper<
          LatencyListSchedulerPass,
          mlir::OperationPass<
              mlir::func::FuncOp>> {

  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(
      LatencyListSchedulerPass)

  mlir::StringRef getArgument() const override {
    return "latency-list-schedule";
  }

  mlir::StringRef getDescription() const override {
    return "Latency-aware greedy DAG scheduling with an in-flight limit";
  }

  void runOnOperation() override {
    mlir::func::FuncOp func =
        getOperation();

    llvm::errs()
        << "\n========================================\n";

    llvm::errs()
        << "LatencyListScheduler: "
        << func.getName()
        << "\n";

    // [MOD] 打印新的Synthetic Latency硬件模型
    llvm::errs()
        << "hardware model: "
        << "lat1="
        << gLat1ExecTime
        << ", lat5="
        << gLat5ExecTime
        << ", lat10="
        << gLat10ExecTime
        << ", issueWidth=1"
        << ", maxInFlight="
        << gMaxInFlight
        << "\n";

    if (func.getBody().empty()) {
      llvm::errs()
          << "scheduler input valid: no\n";

      llvm::errs()
          << "reason: function has no body\n";

      return;
    }

    if (!func.getBody().hasOneBlock()) {
      llvm::errs()
          << "scheduler input valid: no\n";

      llvm::errs()
          << "reason: only single-block functions are supported\n";

      return;
    }

    mlir::Block &block =
        func.getBody().front();

    mlir::Operation *terminator =
        block.getTerminator();

    std::vector<mlir::Operation *> ops;

    std::unordered_map<
        mlir::Operation *,
        unsigned> opToId;

    for (mlir::Operation &op : block) {
      if (&op == terminator)
        continue;

      if (!isSupportedOp(&op)) {
        llvm::errs()
            << "scheduler input valid: no\n";

        llvm::errs()
            << "reason: unsupported operation "
            << op.getName()
            << "\n";

        return;
      }

      unsigned id = ops.size();

      ops.push_back(&op);
      opToId[&op] = id;
    }

    const unsigned n = ops.size();

    if (n == 0) {
      llvm::errs()
          << "scheduler input valid: yes\n";

      llvm::errs()
          << "reason: no schedulable operations\n";

      return;
    }

    // Step2: 构造Dependency DAG
    std::vector<std::vector<unsigned>>
        graph(n);

    std::vector<std::vector<unsigned>>
        predecessors(n);

    std::vector<unsigned>
        indegree(n, 0);

    std::vector<unsigned>
        execTime(n, 0);

    for (unsigned id = 0;
         id < n;
         ++id) {
      execTime[id] =
          getExecTime(ops[id]);
    }

    for (unsigned to = 0;
         to < n;
         ++to) {

      mlir::Operation *consumer =
          ops[to];

      for (mlir::Value operand :
           consumer->getOperands()) {

        mlir::Operation *producer =
            operand.getDefiningOp();

        if (producer == nullptr)
          continue;

        if (producer->getBlock() !=
            &block) {
          continue;
        }

        auto it =
            opToId.find(producer);

        if (it == opToId.end())
          continue;

        unsigned from =
            it->second;

        auto &successors =
            graph[from];

        if (std::find(
                successors.begin(),
                successors.end(),
                to) !=
            successors.end()) {
          continue;
        }

        successors.push_back(to);
        predecessors[to].push_back(from);

        ++indegree[to];
      }
    }

    llvm::errs()
        << "operations:\n";

    for (unsigned id = 0;
         id < n;
         ++id) {

      llvm::errs()
          << "  op"
          << id
          << ": "
          << ops[id]->getName()
          << ", execTime="
          << execTime[id]
          << "\n";
    }

    llvm::errs()
        << "edges:\n";

    bool hasEdge = false;

    for (unsigned from = 0;
         from < n;
         ++from) {

      for (unsigned to :
           graph[from]) {

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

    llvm::errs()
        << "indegree:\n";

    for (unsigned id = 0;
         id < n;
         ++id) {

      llvm::errs()
          << "  op"
          << id
          << ": "
          << indegree[id]
          << "\n";
    }

    // Step3: 标准Kahn，检测cycle并得到一个base topo order
    std::queue<unsigned> ready;

    std::vector<unsigned>
        remainingIndegree =
            indegree;

    std::vector<unsigned>
        baseTopoOrder;

    for (unsigned id = 0;
         id < n;
         ++id) {

      if (remainingIndegree[id] == 0)
        ready.push(id);
    }

    while (!ready.empty()) {
      unsigned u =
          ready.front();

      ready.pop();

      baseTopoOrder.push_back(u);

      for (unsigned v :
           graph[u]) {

        --remainingIndegree[v];

        if (remainingIndegree[v] == 0)
          ready.push(v);
      }
    }

    if (baseTopoOrder.size() != n) {
      llvm::errs()
          << "scheduler input valid: no\n";

      llvm::errs()
          << "cycle detected: yes\n";

      return;
    }

    llvm::errs()
        << "scheduler input valid: yes\n";

    llvm::errs()
        << "cycle detected: no\n";

    llvm::errs()
        << "base topological order:\n  ";

    printOrder(
        baseTopoOrder,
        ops);

    // Step4: reverse-topo DP计算critical priority
    std::vector<unsigned>
        priority(n, 0);

    for (auto it =
             baseTopoOrder.rbegin();
         it != baseTopoOrder.rend();
         ++it) {

      unsigned u = *it;

      unsigned maxSuccessorPriority = 0;

      for (unsigned v :
           graph[u]) {

        maxSuccessorPriority =
            std::max(
                maxSuccessorPriority,
                priority[v]);
      }

      priority[u] =
          execTime[u] +
          maxSuccessorPriority;
    }

    llvm::errs()
        << "critical priority:\n";

    for (unsigned id = 0;
         id < n;
         ++id) {

      llvm::errs()
          << "  op"
          << id
          << ": "
          << priority[id]
          << "\n";
    }

    // Step5.1: 原始IR静态顺序
    std::vector<unsigned>
        originalOrder;

    for (unsigned id = 0;
         id < n;
         ++id) {

      originalOrder.push_back(id);
    }

    ScheduleResult original =
        simulateFixedOrder(
            originalOrder,
            predecessors,
            execTime);

    llvm::errs()
        << "original order:\n  ";

    printOrder(
        original.order,
        ops);

    llvm::errs()
        << "original timestamps:\n";

    for (unsigned id :
         original.order) {

      llvm::errs()
          << "  op"
          << id
          << ": issueCycle="
          << original.timing[id].issueCycle
          << ", finishCycle="
          << original.timing[id].finishCycle
          << "\n";
    }

    llvm::errs()
        << "original total cycles: "
        << original.totalCycles
        << "\n";

    // Step5.2: 普通Kahn topo顺序
    ScheduleResult baseTopo =
        simulateFixedOrder(
            baseTopoOrder,
            predecessors,
            execTime);

    llvm::errs()
        << "baseTopo order:\n  ";

    printOrder(
        baseTopo.order,
        ops);

    llvm::errs()
        << "baseTopo timestamps:\n";

    for (unsigned id :
         baseTopo.order) {

      llvm::errs()
          << "  op"
          << id
          << ": issueCycle="
          << baseTopo.timing[id].issueCycle
          << ", finishCycle="
          << baseTopo.timing[id].finishCycle
          << "\n";
    }

    llvm::errs()
        << "baseTopo total cycles: "
        << baseTopo.totalCycles
        << "\n";

    // Step5.3: latency-aware greedy list scheduler
    ScheduleResult scheduled =
        buildGreedySchedule(
            graph,
            indegree,
            execTime,
            priority);

    if (scheduled.order.size() != n) {
      llvm::errs()
          << "internal error: scheduler did not schedule all ops\n";

      return;
    }

    llvm::errs()
        << "scheduled topological order:\n  ";

    printOrder(
        scheduled.order,
        ops);

    llvm::errs()
        << "scheduled timestamps:\n";

    for (unsigned id :
         scheduled.order) {

      llvm::errs()
          << "  op"
          << id
          << ": issueCycle="
          << scheduled.timing[id].issueCycle
          << ", finishCycle="
          << scheduled.timing[id].finishCycle
          << "\n";
    }

    llvm::errs()
        << "scheduled total cycles: "
        << scheduled.totalCycles
        << "\n";

    for (unsigned id :
         scheduled.order) {

      ops[id]->moveBefore(
          terminator);
    }

    llvm::errs()
        << "IR reordered: yes\n";
  }
};

} // namespace

void tinyir::registerLatencyListSchedulerPass() {
  mlir::PassRegistration<
      LatencyListSchedulerPass>();
}
```

---

# 11. Step 10：这一修改对 Scheduler 算法本身有没有影响？

没有。

原来：

```text
memref.load -> 10
arith.muli  -> 5
arith.addi  -> 1
```

现在：

```text
latency.lat10 -> 10
latency.lat5  -> 5
latency.lat1  -> 1
```

变化的只有：

```text
Operation identity
```

以下核心逻辑全部保持不变：

```text
SSA def-use 建图
Kahn cycle check
reverse-topo priority
readyCycle
finishCycle
maxInFlight
greedy selection
issue timestamp
moveBefore
```

所以这是一个非常干净的模型解耦：

```text
Scheduler algorithm
```

不再依赖：

```text
memref / arith 的真实语义
```

---

# 12. Step 11：修改 Scheduler testcase

[MOD] 修改：

```text
test/passes/latency-list-scheduler.mlir
```

这里保留前面两个小测试，并增加：

```text
@complex_latency_dag
```

大型综合测试。

修改后的完整文件：

```mlir
module {

  // 简单测试1：
  // 两条独立chain最后汇合，其中第一条从lat10开始
  func.func @latency_hide() -> i32 {

    %0 = latency.lat10 : i32
    %1 = latency.lat5 %0 : i32
    %2 = latency.lat1 %1 : i32

    %3 = latency.lat1 : i32
    %4 = latency.lat5 %3 : i32
    %5 = latency.lat1 %4 : i32

    %6 = latency.lat1 %2, %5 : i32

    return %6 : i32
  }


  // 简单测试2：
  // 两条对称lat5->lat1 chain最后汇合
  func.func @two_lat5_chains() -> i32 {

    %0 = latency.lat5 : i32
    %1 = latency.lat1 %0 : i32

    %2 = latency.lat5 : i32
    %3 = latency.lat1 %2 : i32

    %4 = latency.lat1 %1, %3 : i32

    return %4 : i32
  }


  // 综合测试：
  // 对应之前绘制的23节点大型DAG
  func.func @complex_latency_dag() -> i32 {

    // ---------- Top roots ----------
    %l0 = latency.lat10 : i32
    %c0 = latency.lat1 : i32
    %r0 = latency.lat5 : i32

    // ---------- Upper layer ----------
    %l1 = latency.lat5 %l0 : i32
    %c1 = latency.lat5 %c0 : i32

    %ra = latency.lat1 %r0 : i32
    %rb = latency.lat5 %r0 : i32
    %rc = latency.lat1 %r0 : i32

    // ---------- Next layer ----------
    %l2 = latency.lat1 %l1 : i32

    // center node receives center vertical edge + right-left branch
    %c2 = latency.lat5 %c1, %ra : i32

    %r10a = latency.lat10 %ra : i32

    // right middle join
    %rjoin = latency.lat1 %rb, %rc : i32

    // ---------- Middle layer ----------
    %l3 = latency.lat1 %l2 : i32

    // center receives vertical center edge + cross edge from left lat5
    %c3 = latency.lat5 %c2, %l1 : i32

    %r10b = latency.lat10 %rjoin : i32

    // ---------- Lower-middle layer ----------
    %l4 = latency.lat10 %l3 : i32

    // center receives vertical edge + cross edge from left lat1
    %c4 = latency.lat1 %c3, %l3 : i32

    %l5 = latency.lat1 %l4 : i32

    // center receives right-side lat10 dependency
    %c5 = latency.lat1 %c4, %r10a : i32

    // ---------- Lower layer ----------
    %l6 = latency.lat5 %l5 : i32

    // lower-left merge
    %x = latency.lat1 %l6, %c5 : i32

    // lower-right merge
    %r5 = latency.lat5 %c5, %r10b : i32

    // ---------- Final merge ----------
    %out = latency.lat5 %x, %r5 : i32

    return %out : i32
  }
}
```

---

# 13. Step 12：大型 DAG 与 MLIR 的对应关系

为了方便核对，把图按区域标号。

## 左侧链

图中：

```text
10
↓
5
↓
1
↓
1
↓
10
↓
1
↓
5
```

对应：

```mlir
%l0 = latency.lat10 : i32
%l1 = latency.lat5 %l0 : i32
%l2 = latency.lat1 %l1 : i32
%l3 = latency.lat1 %l2 : i32
%l4 = latency.lat10 %l3 : i32
%l5 = latency.lat1 %l4 : i32
%l6 = latency.lat5 %l5 : i32
```

---

## 中央链

图中：

```text
1
↓
5
↓
5
↓
5
↓
1
↓
1
```

对应基础 vertical chain：

```mlir
%c0 = latency.lat1 : i32
%c1 = latency.lat5 %c0 : i32
%c2 = latency.lat5 %c1, %ra : i32
%c3 = latency.lat5 %c2, %l1 : i32
%c4 = latency.lat1 %c3, %l3 : i32
%c5 = latency.lat1 %c4, %r10a : i32
```

这里多出来的 operand：

```text
%ra
%l1
%l3
%r10a
```

就是图中的跨区域依赖边。

---

## 右上子图

图中：

```text
             5
          /  |  \
         1   5   1
        / \   \ /
       ...10   1
               ↓
              10
```

对应：

```mlir
%r0 = latency.lat5 : i32

%ra = latency.lat1 %r0 : i32
%rb = latency.lat5 %r0 : i32
%rc = latency.lat1 %r0 : i32

%r10a = latency.lat10 %ra : i32

%rjoin = latency.lat1 %rb, %rc : i32
%r10b = latency.lat10 %rjoin : i32
```

同时：

```text
%ra -> %c2
%r10a -> %c5
```

通过中央 op 的额外 operands 表达。

---

## 底部两个 merge

左下：

```text
%l6 ----\
         -> %x(lat1)
%c5 ----/
```

右下：

```text
%c5 -----\
          -> %r5(lat5)
%r10b ---/
```

最后：

```text
%x ----\
        -> %out(lat5)
%r5 ---/
```

对应：

```mlir
%x = latency.lat1 %l6, %c5 : i32
%r5 = latency.lat5 %c5, %r10b : i32
%out = latency.lat5 %x, %r5 : i32
```

---

# 14. Step 13：为什么 textual MLIR 顺序本身必须先是合法 SSA 顺序？

虽然 Scheduler 会重排 Operation，但输入 MLIR 本身首先要通过 parser / verifier。

所以：

```text
consumer
```

不能写在：

```text
producer
```

之前并引用一个还没定义的 SSA value。

因此大图 testcase 不是简单按照：

```text
左区域写完
中央区域写完
右区域写完
```

而是按照一个已经合法的 topological progression 写出来。

例如：

```text
%ra
```

必须先定义，

才能在：

```mlir
%c2 = latency.lat5 %c1, %ra : i32
```

中使用。

---

# 15. Step 14：重新构建

执行：

```bash
make clean
make build
```

重点观察 TableGen：

```text
+TBLGEN src/dialect/Latency.td --> build/gen/Latency_Dialect.h.inc
...
+TBLGEN src/dialect/Latency.td --> build/gen/Latency_Ops.cpp.inc
```

以及 C++：

```text
+CXX src/dialect/Latency.cpp
```

如果这两部分都成功，说明：

```text
Latency.td
→ generated .inc
→ Latency.cpp
```

这条链已经跑通。

---

# 16. Step 15：先单独验证 Dialect parser

在真正跑 Scheduler 前，可以先执行：

```bash
build/bin/tinyir-opt \
  test/passes/latency-list-scheduler.mlir \
  --mlir-disable-threading
```

如果：

```text
latency.lat1
latency.lat5
latency.lat10
```

都能正常 parse + print，

说明 Dialect 注册已经正确。

如果报：

```text
unknown custom operation 'latency.lat5'
```

优先检查：

```text
Latency.h
Latency.cpp
tinyir-opt.cpp registry.insert
```

---

# 17. Step 16：运行 Scheduler

执行：

```bash
make pass \
  TEST=test/passes/latency-list-scheduler.mlir \
  PASS=latency-list-schedule
```

Scheduler 应该依次分析：

```text
latency_hide
two_lat5_chains
complex_latency_dag
```

大型 DAG 应该输出大量：

```text
operations
edges
indegree
priority
original order
baseTopo order
scheduled topological order
scheduled timestamps
```

这正是我们想要的综合测试。

---

# 18. Step 17：预期看到的 Operation 名字

修改成功后，日志里不应该再出现：

```text
memref.load
arith.addi
arith.muli
```

应该只出现：

```text
latency.lat1
latency.lat5
latency.lat10
```

例如：

```text
operations:
  op0: latency.lat10, execTime=10
  op1: latency.lat1, execTime=1
  op2: latency.lat5, execTime=5
  ...
```

hardware model 也应该变成：

```text
hardware model:
lat1=1,
lat5=5,
lat10=10,
issueWidth=1,
maxInFlight=2
```

---

# 19. Step 18：为什么现在的大图测试比原来的 `load_hide` 更有价值？

原来的 DAG 只有：

```text
两条chain
最后一次join
```

大型 DAG 同时包含：

```text
3个root
多次branch
多次join
跨区域dependency
不同latency
多条critical-path候选
maxInFlight资源竞争
```

所以 Scheduler 会频繁面对：

```text
多个op同时topo-ready
多个op同时operand-ready
但maxInFlight有限
```

这时候：

```text
priority
```

才真正有明显选择作用。

---

# 20. Step 19：Latency 数字和 Priority 不要混淆

例如：

```text
latency.lat10
```

只是：

$$
ExecTime=10
$$

但它的 priority 是：

$$
Priority(u)
=
ExecTime(u)
+
\max_{u\rightarrow v}Priority(v)
$$

所以一个：

```text
latency.lat1
```

如果下面挂着非常长的依赖链，

完全可能：

```text
priority(lat1 root)
>
priority(lat10 short branch)
```

这正是大图最值得观察的现象之一。

---

# 21. Step 20：当前模型现在变得更加干净

修改前：

```text
Scheduler
↓
借用 memref.load / arith.addi / arith.muli
↓
既承担真实MLIR语义
又承担虚构latency模型
```

修改后：

```text
Latency Dialect
↓
只负责构造synthetic SSA DAG

LatencyListScheduler
↓
只负责dependency + latency + resource scheduling
```

职责完全分离。

这也是面试时很容易解释的 compiler engineering 思路：

> 为了隔离 scheduling algorithm 和真实 ISA semantics，我定义了一个 synthetic latency dialect，用纯 SSA operations 表达任意 dependency DAG；scheduler 只读取 op class 对应的 latency，不再依赖 memref 或 arithmetic semantics。

---

# 22. Step 21：最终相关目录树

完成后：

```text
src/
├── dialect/
│   ├── Latency.td
│   ├── Latency.h
│   ├── Latency.cpp
│   ├── Tutorial.td
│   ├── Tutorial.h
│   ├── Tutorial.cpp
│   ├── List.td
│   ├── List.h
│   └── List.cpp
│
├── passes/
│   ├── DependencyDAG.cpp
│   ├── LatencyListScheduler.cpp
│   ├── Passes.h
│   └── ...
│
└── tinyir-opt.cpp

test/
└── passes/
    ├── dependency-dag.mlir
    └── latency-list-scheduler.mlir
```

---

# 23. Step 22：建议 Git commit

确认：

```bash
git status
git diff
```

然后：

```bash
git add \
  src/dialect/Latency.td \
  src/dialect/Latency.h \
  src/dialect/Latency.cpp \
  src/tinyir-opt.cpp \
  src/passes/LatencyListScheduler.cpp \
  test/passes/latency-list-scheduler.mlir \
  docs/advance03_LatencyTd.md
```

提交：

```bash
git commit -m "add synthetic latency dialect for DAG scheduling"
```

---

# 24. 面试时完整故事

可以这样讲：

> 我最开始用 `memref.load`、`arith.muli` 和 `arith.addi` 分别模拟 10、5、1 cycle latency，但在构造复杂 DAG 时发现真实 `load` 有 memref/index 语义，不适合表达任意 dependency edge。
>
> 所以后来我单独定义了一个 synthetic `latency` dialect，包含 `latency.lat1`、`latency.lat5` 和 `latency.lat10` 三种 Pure SSA operation。每个 op 接受 variadic i32 operands，因此一个 operand 就对应 dependency DAG 的一条 incoming edge。
>
> Scheduler 只需要从 SSA def-use 建图，再根据 op type 得到 synthetic latency。这样 dependency-graph construction、latency model 和真实 memory semantics 完全解耦，我就可以直接用 MLIR 表达比较复杂的 multi-root / branch / join DAG 来验证 list scheduling、critical priority 和 max-in-flight resource constraint。

---

# 25. 最终自测

确保可以回答：

1. 为什么不能继续长期借用 `memref.load` 表示 latency=10？
2. 为什么 Latency Op 用 `Variadic<I32>`？
3. 一个 SSA operand 如何对应 DAG 的一条 edge？
4. 为什么三个 synthetic op 都标记 `[Pure]`？
5. `.td` 为什么不能单独让 Dialect 生效？
6. `Latency.h` 和 generated `.inc` 是什么关系？
7. `Latency.cpp::initialize()` 做了什么？
8. 为什么 `tinyir-opt.cpp` 还必须 `registry.insert<LatencyDialect>()`？
9. Makefile 为什么无需修改？
10. Scheduler 的核心算法为什么完全不用变化？
11. 大图为什么必须先以一个合法 SSA topo order 写进 MLIR？
12. `execTime` 和 `critical priority` 有什么区别？
13. 为什么 synthetic dialect 比借用真实 arithmetic/memory op 更适合验证 scheduler？
