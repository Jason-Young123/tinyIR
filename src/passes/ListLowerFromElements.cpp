#include "passes/Passes.h"

#include "dialect/List.h"

#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/Builders.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Pass/PassRegistry.h"

#include "llvm/ADT/SmallVector.h"

namespace {

struct ListLowerFromElementsPass
    : mlir::PassWrapper<
          ListLowerFromElementsPass,
          mlir::OperationPass<mlir::func::FuncOp>> {

  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(ListLowerFromElementsPass)

  mlir::StringRef getArgument() const override {
    return "list-lower-from-elements";
  }

  mlir::StringRef getDescription() const override {
    return "Lower list.from_elements to list.empty + list.push_back";
  }

  void runOnOperation() override {
    mlir::func::FuncOp func = getOperation();

    llvm::SmallVector<mlir::list::FromElementsOp> worklist;

    // 第一阶段只找目标，不修改 IR。
    func.walk([&](mlir::list::FromElementsOp op) {
      worklist.push_back(op);
    });

    // 第二阶段集中改写。
    for (mlir::list::FromElementsOp op : worklist) {
      mlir::OpBuilder builder(func.getContext());
      builder.setInsertionPoint(op);

      mlir::Location loc = op.getLoc();
      mlir::Type listType = op.getResult().getType();

      auto empty = mlir::list::EmptyOp::create( // 开始lowering,第一步加入list.empty
              builder,
              loc,
              listType);

      mlir::Value current = empty.getResult();

      for (mlir::Value element : op.getElements()) {//循环向list中push元素
        auto pushed =
            mlir::list::PushBackOp::create(
                builder,
                loc,
                listType,
                current,
                element);

        current = pushed.getResult();
      }

      // 这两步是否可以合并为rewriter
      op.getResult().replaceAllUsesWith(current);
      op->erase();
    }
  }
};

} // namespace

void tinyir::registerListLowerFromElementsPass() {
  mlir::PassRegistration<ListLowerFromElementsPass>();
}
