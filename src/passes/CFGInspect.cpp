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

  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(CFGInspectPass)

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
