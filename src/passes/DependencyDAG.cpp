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

struct DependencyDAGPass : mlir::PassWrapper<DependencyDAGPass, mlir::OperationPass<mlir::func::FuncOp>> {
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(DependencyDAGPass)

  mlir::StringRef getArgument() const override {
    return "dependency-dag";
  }

  mlir::StringRef getDescription() const override {
    return "Build a basic-block-local SSA operation dependency DAG";
  }


  // 分析该block内的所有operation, 并构建图描述, 包含每个节点的邻接表(graph)和入度(indegree)
  void analyzeBlock(mlir::Block &block, unsigned blockIndex) {
    std::vector<mlir::Operation *> ops;
    std::unordered_map<mlir::Operation *, unsigned> opToId;
    for (mlir::Operation &op : block) {
      unsigned id = ops.size();
      ops.push_back(&op);
      opToId[&op] = id;
    }

    // Stage1: 遍历ops, 构建图描述
    const unsigned n = ops.size(); // 该block内operation总数
    std::vector<std::vector<unsigned>> graph(n);
    std::vector<unsigned> indegree(n, 0);

    for (unsigned to = 0; to < n; ++to) { // 遍历所有operation
      mlir::Operation *consumer = ops[to]; // 当前的operation记为consumer

      for (mlir::Value operand : consumer->getOperands()) { // 对于该operation中的每一个operand
        mlir::Operation *producer = operand.getDefiningOp(); // 寻找产生这个operator的operation, 该operation就是producer

        if (producer == nullptr) // 该operand不是由某个operation产生的, 比如传入函数的%x等
          continue;
        if (producer->getBlock() != &block) // 该operand的producer不在当前block内, 暂时忽略
          continue;

        auto it = opToId.find(producer); // 对于合法的producer, 寻找其对应op编号
        if (it == opToId.end())
          continue;
        unsigned from = it->second;//这里second即为编号, unsigned int

        auto &successors = graph[from]; // 此后需要操作from对应的successor数组, 把to加入进去
        if (std::find(successors.begin(), successors.end(), to) != successors.end()) {//该op已经存在于successors列表中
          continue;
        }
        successors.push_back(to);
        ++indegree[to]; // to自身入度加1
      }
    }

    // 打印图描述信息
    llvm::errs() << "\n=== dependency DAG: block " << blockIndex << " ===\n";
    llvm::errs() << "operations: " << n << "\n";
    for (unsigned id = 0; id < n; ++id) {
      llvm::errs() << "  op" << id << ": " << ops[id]->getName() << "\n";
    }
    
    llvm::errs() << "edges:\n";//邻接表
    bool hasEdge = false;
    for (unsigned from = 0; from < n; ++from) {
      for (unsigned to : graph[from]) {
        hasEdge = true;
        llvm::errs() << "  op" << from << " -> op" << to << "\n";
      }
    }
    if (!hasEdge)
      llvm::errs() << "  <none>\n";

    llvm::errs() << "indegree:\n";//入度表
    for (unsigned id = 0; id < n; ++id) {
      llvm::errs() << "  op" << id << ": " << indegree[id] << "\n";
    }


    // Stage2: 基于Kanh Algo. 进行环判断和Topo Sort
    std::queue<unsigned> ready;
    std::vector<unsigned> remainingIndegree = indegree;
    std::vector<unsigned> level(n, 0);
    std::vector<unsigned> topoOrder;

    for (unsigned id = 0; id < n; ++id) { // step1: 所有入度为0的节点全都入队; 这里的顺序最终决定了toposort的结果顺序
      if (remainingIndegree[id] == 0)
        ready.push(id);
    }

    while (!ready.empty()) { // step2: 迭代直至队列为空
      unsigned u = ready.front();
      ready.pop();
      topoOrder.push_back(u); // 头部节点出队并放到toposort列表中

      for (unsigned v : graph[u]) { // 该节点的所有successor入度
        level[v] = std::max(level[v], level[u] + 1); // level加一层
        --remainingIndegree[v];

        if (remainingIndegree[v] == 0) // 如果successor入度降为0, 则也放到队列中
          ready.push(v);
      }
    }

    llvm::errs() << "topological order:\n  ";
    for (unsigned i = 0; i < topoOrder.size(); ++i) {
      if (i != 0)
        llvm::errs() << " -> ";
      llvm::errs() << "op" << topoOrder[i];
    }
    llvm::errs() << "\n";

    if (topoOrder.size() != n) {
      llvm::errs() << "cycle detected: yes\n";
      return;
    }
    llvm::errs() << "cycle detected: no\n";

    unsigned maxLevel = 0;
    for (unsigned value : level)
      maxLevel = std::max(maxLevel, value);
    llvm::errs() << "dependency levels:\n";
    for (unsigned currentLevel = 0; currentLevel <= maxLevel; ++currentLevel) {
      llvm::errs() << "  level " << currentLevel << ":";

      for (unsigned id = 0; id < n; ++id) {
        if (level[id] == currentLevel) {
          llvm::errs() << " op" << id;
        }
      }

      llvm::errs() << "\n";
    }
  }




  void runOnOperation() override {
    mlir::func::FuncOp func = getOperation();
    llvm::errs() << "\n========================================\n";
    llvm::errs() << "function: " << func.getName() << "\n";

    unsigned blockIndex = 0;
    for (mlir::Block &block : func.getBody()) {//循环分析每一个block
      analyzeBlock(block, blockIndex++);
    }
  }
};

} // namespace

void tinyir::registerDependencyDAGPass() {
  mlir::PassRegistration<DependencyDAGPass>();
}
