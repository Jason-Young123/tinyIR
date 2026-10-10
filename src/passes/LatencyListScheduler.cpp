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

// 目前仅支持三种指令, load/addi/muli,各自执行latency可在这里自定义
unsigned gLat1ExecTime = 1;
unsigned gLat5ExecTime = 5;
unsigned gLat10ExecTime = 10;
unsigned gMaxInFlight = 2; // 至多有2条指令可以并行执行

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
  return name == "latency.lat1" || name == "latency.lat5" || name == "latency.lat10";
}

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

// 统计inFlight(已经发射尚未执行完毕)指令数目, 不得超过 maxParallelism
unsigned countInFlight(const std::vector<bool> &issued,
                       const std::vector<unsigned> &finishCycle,
                       unsigned cycle) {
  unsigned count = 0;
  for (unsigned id = 0; id < issued.size(); ++id) {
    if (issued[id] && finishCycle[id] > cycle) //已经发射且预期finishCycle大于当前cycle, 视为inFlight
      ++count;
  }
  return count;
}


// 模拟固定顺序的硬件执行时间, 返回ScheduleResult结构体
ScheduleResult simulateFixedOrder(
    const std::vector<unsigned> &order,
    const std::vector<std::vector<unsigned>> &predecessors,
    const std::vector<unsigned> &execTime) {
  const unsigned n = execTime.size();
  std::vector<bool> issued(n, false);//每个op是否已经发射
  std::vector<unsigned> finishCycle(n, 0);//如果已经发射,则其会在哪个cycle完成

  ScheduleResult result;
  result.order = order;
  result.timing.resize(n);

  unsigned cycle = 0;

  for (unsigned u : order) {//对于原始topo sort中的op按顺序进行发射
    // 首先确定当前cycle这个op是否已经可以发射(如果当前cycle大于所有前继的finishCycle则可以发射)
    unsigned dependencyReadyCycle = 0;
    for (unsigned pred : predecessors[u]) {
      dependencyReadyCycle = std::max(dependencyReadyCycle, finishCycle[pred]);
    }

    while (true) {
      unsigned inFlight = countInFlight(issued, finishCycle, cycle);
      if (inFlight < gMaxInFlight && dependencyReadyCycle <= cycle) // 至多只有两个inFlight指令且cycle满足要求
        break; // 发射
      ++cycle;
    }

    // 更新result
    result.timing[u].issueCycle = cycle;
    result.timing[u].finishCycle = cycle + execTime[u];
    
    // 更新finishCycle和issue状态列表
    finishCycle[u] = result.timing[u].finishCycle;
    issued[u] = true;

    ++cycle;
  }

  unsigned makespan = 0; // 查找最后一个完成的op的finishCycle
  for (unsigned id = 0; id < n; ++id)
    makespan = std::max(makespan, finishCycle[id]);

  result.totalCycles = makespan;
  return result;
}




// 基于当前图结构按照greedy方案寻找一个较佳的topo sort, 返回ScheduleResult结构体
ScheduleResult buildGreedySchedule(
    const std::vector<std::vector<unsigned>> &graph, //邻接表已知
    const std::vector<unsigned> &indegree, // 各个节点入度已知
    const std::vector<unsigned> &execTime, // 各个节点执行时间已知
    const std::vector<unsigned> &priority // 各个节点priority已知
) {
  const unsigned n = graph.size();

  std::vector<unsigned> remainingIndegree = indegree;
  std::vector<unsigned> readyCycle(n, 0); // 每个op至少在第几个cycle可以发射
  std::vector<unsigned> finishCycle(n, 0); // 每个op在第几个cycle执行完毕(如果已经发射的话)
  std::vector<bool> scheduled(n, false); // 每个op是否已经发射

  ScheduleResult result;//result.order存放已经发射且记录时间戳的ops
  result.timing.resize(n);

  unsigned cycle = 0;
  while (result.order.size() < n) { //还没记录满
    unsigned numInFlight = countInFlight(scheduled, finishCycle, cycle); // 在当前cycle, 有多少条指令已经发射、正在执行中

    unsigned selected = n; // 本轮选中可发射的op编号
    if (numInFlight < gMaxInFlight) { // 硬件层面支持发射
      for (unsigned id = 0; id < n; ++id) { // 遍历查询每个op
        if (scheduled[id]) // 已经发射, 直接跳过
          continue;
        if (remainingIndegree[id] != 0) // RAW依赖尚未解除, 跳过
          continue;
        if (readyCycle[id] > cycle) // 还没有ready(RAW已经解除但是前继还在执行中), 跳过
          continue;

        // 如果先后遇到可以发射的op, 则priority越大优先级越高、id越小优先级越高
        if (selected == n || priority[id] > priority[selected] || (priority[id] == priority[selected] && id < selected)) {
          selected = id;
        }
      }
    }

    if (selected != n) { // 本cycle确实有op可以发射
      unsigned u = selected;
      // 更新result
      result.order.push_back(u);
      result.timing[u].issueCycle = cycle;
      result.timing[u].finishCycle = cycle + execTime[u];
      // 更新scheduled和finishCycle列表
      scheduled[u] = true;
      finishCycle[u] = result.timing[u].finishCycle;

      for (unsigned v : graph[u]) { // 把所有后继的readyCycle进行更新
        --remainingIndegree[v];
        readyCycle[v] = std::max(readyCycle[v], finishCycle[u]);
      }
    } 

    ++cycle;
  }

  unsigned makespan = 0;
  for (unsigned id = 0; id < n; ++id)
    makespan = std::max(makespan, finishCycle[id]);

  result.totalCycles = makespan;
  return result;
}


/* Scheduler Strained by Latency & Parallelism的调度伪代码: 
// 硬件规定
const int execTime_load = 10;
const int execTime_addi = 1;
const int execTime_muli = 5;
const int parallelism = 2;

// 一个有n个operation
vector<vector<unsigned>> graph(n); // 邻接表
vector<unsigned> indegree(n, 0); // 入度
vector<unsigned> priority(n, 0); // 每个operation的priority
vector<unsigned> execTime(n, 0); // 每个operation的execTime

// 预先准备的数据
get_execTime();
create_graph(...);
create_indegree(...);
create_priority(...); //对于任意节点, 其到叶子节点的最长execTime路径之和

// 迭代过程中需要使用的数据
unordered_map<unsigned, unsigned> queue; // 字典, {op: remainingCycle}代表op这个操作还有remainingCycle个时钟周期才可以执行, 即其前序节点还在执行中
ordered_map<unsigned, unsigned> sort; // 字典, {op: remainingCycle}代表op这个操作还有remainingCycle个时钟周期就执行完毕
unordered_map<unsigned, int> record; // 字典, 用于存储入度尚不为0的operation的执行状态

int cycle = 0; //代表已经执行的cycle

for(unsigned id = 0; id < n; ++id) { // step1: 所有入度为0的节点全都入队; 这里的顺序最终决定了toposort的结果顺序
  if (indegree[id] == 0)
    queue.push_back({id, 0});
}


while(!queue.empty()){
  int numOfInFlightInst = sort.count(value > 0);//统计正在执行的指令数目
  int numOfReadyInst = queue.count(value == 0);//统计可以执行的指令数目
  if(numOfInFlightInst < parallelism && numOfReadyInst > 0){
    auto instSelected = select_by_priority(queue); //选中符合要求的且priority最高的inst
    auto instId = instSelected.first;
    queue.erase(instSelected);//从queue中删除
    sort.push_back({instId, execTime(instId) - 1});

    for(auto& successor : graph[instId]){
      indegree[successor] --;
      if(indegree[successor] == 0){
        if(record.count(successor) > 0){ // 已经在record中被记录
          queue.push_back({successor, max(execTime(instId) - 1, record[successor].second)});
          record.erase(successor);
        }
        else{
          queue.push_back({successor, execTime(instId) - 1});
        }
      }
      else{
        record.push_back({successor, execTime(instId) - 1});
      }
    }
  }
  
  ++cycle;
  for(auto& item : queue){
    if(item.second > 0) --item.second;
  }
  for(auto& item : sort){
    if(item.second > 0) --item.second;
  }
  for(auto& item : record) --item.second;
}

while(sort.count(value > 0) > 0){//drain 剩余已经发射还未执行完毕的指令
  ++cyle;
  for(auto& item : sort){
    if(item.second > 0) --item.second;
  }
}

assert(queue.empty());
assert(record.empty());
assert(sort.len() == n);

get_topo_sort(sort);

*/




struct LatencyListSchedulerPass : mlir::PassWrapper<LatencyListSchedulerPass, mlir::OperationPass<mlir::func::FuncOp>> {
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(LatencyListSchedulerPass)

  mlir::StringRef getArgument() const override {
    return "latency-list-schedule";
  }

  mlir::StringRef getDescription() const override {
    return "Latency-aware greedy DAG scheduling with an in-flight limit";
  }

  void runOnOperation() override {
    // Step1: Basic Check & 遍历ops
    mlir::func::FuncOp func = getOperation();
    llvm::errs() << "\n========================================\n";
    llvm::errs() << "LatencyListScheduler: "
                 << func.getName() << "\n";
    llvm::errs() << "hardware model: lat1=" << gLat1ExecTime
                 << ", lat5=" << gLat5ExecTime
                 << ", lat10=" << gLat10ExecTime
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

    for (mlir::Operation &op : block) { // 记录所有op, 去除terminator, 且确保不出现非法op(load/addi/muli之外的op)
      if (&op == terminator)
        continue;
      if (!isSupportedOp(&op)) {
        llvm::errs() << "scheduler input valid: no\n";
        llvm::errs() << "reason: unsupported operation " << op.getName() << "\n";
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


    // Step2: 创建邻接表等图描述对象
    std::vector<std::vector<unsigned>> graph(n); // 后继列表
    std::vector<std::vector<unsigned>> predecessors(n); // 前继列表
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

        if (std::find(successors.begin(), successors.end(), to) != successors.end()) {
          continue;
        }

        successors.push_back(to);
        predecessors[to].push_back(from);
        ++indegree[to];
      }
    }

    llvm::errs() << "operations:\n";
    for (unsigned id = 0; id < n; ++id) {
      llvm::errs() << "  op" << id << ": " << ops[id]->getName() << ", execTime=" << execTime[id] << "\n";
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


    // Step3: 用Kanh Algo得到一种Topo Order
    std::queue<unsigned> ready;
    std::vector<unsigned> remainingIndegree = indegree;
    std::vector<unsigned> baseTopoOrder;

    for (unsigned id = 0; id < n; ++id) { // 3.1 所有入度为0的节点入队
      if (remainingIndegree[id] == 0)
        ready.push(id);
    }

    while (!ready.empty()) { // 3.2 循环直到队列为空
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


    // Step4: 求解每个节点的priority, 逆向遍历baseTopoOrder
    std::vector<unsigned> priority(n, 0);
    for (auto it = baseTopoOrder.rbegin(); it != baseTopoOrder.rend(); ++it) {
      unsigned u = *it;
      unsigned maxSuccessorPriority = 0;
      for (unsigned v : graph[u]) {
        maxSuccessorPriority = std::max(maxSuccessorPriority, priority[v]);
      }
      priority[u] = execTime[u] + maxSuccessorPriority;
    }
    llvm::errs() << "critical priority:\n";
    for (unsigned id = 0; id < n; ++id)
      llvm::errs() << "  op" << id << ": " << priority[id] << "\n";


    // Step5.1: 测试原始顺序（0 -> 1 -> 2 -> ...）的硬件执行耗时
    std::vector<unsigned> originalOrder;
    for (unsigned id = 0; id < n; ++id)
      originalOrder.push_back(id);
    ScheduleResult original = simulateFixedOrder(originalOrder, predecessors, execTime);
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


    // Step5.2: 测试baseTopoOrder的硬件执行耗时
    ScheduleResult baseTopo = simulateFixedOrder(baseTopoOrder, predecessors, execTime);
    llvm::errs() << "baseTopo order:\n  ";
    printOrder(baseTopo.order, ops);
    llvm::errs() << "baseTopo timestamps:\n";
    for (unsigned id : baseTopo.order) {
      llvm::errs() << "  op" << id
                   << ": issueCycle=" << baseTopo.timing[id].issueCycle
                   << ", finishCycle=" << baseTopo.timing[id].finishCycle
                   << "\n";
    }
    llvm::errs() << "baseTopo total cycles: " << baseTopo.totalCycles << "\n";


    // Step5.3: 调用自定义scheduler进行顺序求解
    ScheduleResult scheduled = buildGreedySchedule(graph, indegree, execTime, priority);
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


    for (unsigned id : scheduled.order)
      ops[id]->moveBefore(terminator);

    llvm::errs() << "IR reordered: yes\n";
  }
};

} // namespace

void tinyir::registerLatencyListSchedulerPass() {
  mlir::PassRegistration<LatencyListSchedulerPass>();
}



