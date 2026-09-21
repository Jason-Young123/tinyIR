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

  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(RewriterDemoPass)

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
      auto rhsConst = add.getRhs().getDefiningOp<mlir::arith::ConstantIntOp>();//如果rhs确实是%c0等常量才会返回非nullptr

      if (!rhsConst)//说明rhs为%y等变量,不符合要求
        continue;

      if (rhsConst.value() != 0)//rhs虽然为常量但不是%c0,也不符合要求
        continue;

      rewriter.replaceOp(add, add.getLhs());//找到add的所有use,将其替换为add.lhs,最后删除这条add
    }
  }
};

} // namespace

void tinyir::registerRewriterDemoPass() {
  mlir::PassRegistration<RewriterDemoPass>();
}




