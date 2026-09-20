#include "passes/Passes.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/Operation.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Pass/PassRegistry.h"
#include "llvm/Support/raw_ostream.h"

namespace {

//进阶pass: Inspect
struct InspectIRPass
    : mlir::PassWrapper<
          InspectIRPass,
          mlir::OperationPass<mlir::func::FuncOp>> {

  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(InspectIRPass)

  mlir::StringRef getArgument() const override {
    return "inspect-ir";
  }

  mlir::StringRef getDescription() const override {
    return "Inspect MLIR Operation/Block/Region structure";
  }


  void runOnOperation() override {
    mlir::func::FuncOp func = getOperation();

    func.walk([&](mlir::Operation *op) {//遍历generic operations
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

      if (auto add = llvm::dyn_cast<mlir::arith::AddIOp>(op)) {
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
