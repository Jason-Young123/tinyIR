#include "passes/Passes.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Pass/PassRegistry.h"
#include "llvm/Support/raw_ostream.h"


namespace {
struct UseDefPass : mlir::PassWrapper<UseDefPass, mlir::OperationPass<mlir::func::FuncOp>> {

  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(UseDefPass)	

  mlir::StringRef getArgument() const override {
    return "use-def";//命令行参数(--pass-pipeline)
  }	

  void runOnOperation() override {
    mlir::func::FuncOp func = getOperation();//定位到func.func

    // walk函数的输入参数为lambda函数, 且需要有参数(这里是mlir::arith::AddIOp add), 这样内部迭代器才会遍历寻找该func内对应的operation
    // [&]代表会使用到lambda函数外部的变量,比如全局计数器等,这里没有使用,可以简化为[]
    func.walk([&](mlir::arith::AddIOp add) {//对于其中的所有arith.addi operation
      mlir::Value result = add.getResult();//对于typed op获取result
      llvm::errs() << "\nvalue: ";
      result.dump();

      for(mlir::OpOperand &use : result.getUses()){//遍历后续所有使用该result的OpOperand, 可能出现在同一个operation中
        llvm::errs()
            << "  user = "
            << use.getOwner()->getName()
            << ", operand #"
            << use.getOperandNumber()
            << "\n";
      }
    });
  } 
};

}


void tinyir::registerUseDefPass() {
  mlir::PassRegistration<UseDefPass>();
}



