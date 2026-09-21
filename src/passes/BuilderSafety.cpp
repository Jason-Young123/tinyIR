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

  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(BuilderSafetyPass)

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


