#include "passes/Passes.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Pass/PassRegistry.h"
#include "llvm/Support/raw_ostream.h"

namespace {

struct DemoPass
    : mlir::PassWrapper<
          DemoPass,
          mlir::OperationPass<mlir::func::FuncOp> // 这个pass仅针对func.func生效
      > 
{

  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(DemoPass)

  mlir::StringRef getArgument() const override { // pass名称, 会在参数行中调用
    return "demo";
  }

  mlir::StringRef getDescription() const override {
    return "The tinyIR demo pass";
  }

  // 可以识别func.func
  /*void runOnOperation() override {
    mlir::func::FuncOp function = getOperation();
    function.emitRemark("DemoPass is running");
  }*/

  // 可以识别func.func中的arith.addi
  /*void runOnOperation() override {
    mlir::func::FuncOp function = getOperation();
    function.walk([&](mlir::arith::AddIOp add) { // function.walk, 遍历函数内部operation
      mlir::Value lhs = add.getLhs(); // 获取左operand对应的value
      mlir::Value rhs = add.getRhs(); // 获取右operand对应的value
      llvm::errs() << "found arith.addi\n";
      llvm::errs() << "  lhs: ";
      lhs.dump();
      llvm::errs() << "  rhs: ";
      rhs.dump();
      llvm::errs() << "  result type: ";
      add.getResult().getType().dump();
    });
  }*/

  // 手工描述一条operation
  /*void runOnOperation() override {
    mlir::func::FuncOp function = getOperation();
    function.walk([&](mlir::arith::AddIOp add) {
      mlir::OperationState state(add.getLoc(), "arith.addi");
      state.addOperands({
          add.getLhs(),
          add.getRhs()
      });
      state.addTypes(add.getResult().getType());
      mlir::Operation *detached = mlir::Operation::create(state); //创建对象, 这里的detached指向原来func中的arith.addi,且处于游离状态
      detached->dump();
      detached->destroy();
    });
  }*/
  
  // 在func中的arith.addi后面插入一条相同的arith.addi
  /*void runOnOperation() override {
    mlir::func::FuncOp function = getOperation();
    mlir::arith::AddIOp target = nullptr;
    function.walk([&](mlir::arith::AddIOp add) {//寻找第一条addi
      if (!target)
        target = add;
    });
    if (!target) // 没找到addi直接返回
      return;
    mlir::OpBuilder builder(function.getContext()); //获取MLIR Context
    builder.setInsertionPointAfter(target); //将第一条addi的后面作为插入点
    mlir::arith::AddIOp::create(
      builder,
      target.getLoc(),
      target.getLhs(),
      target.getRhs()
    );
  }*/

  // 修改原始的addi %x, %1为addi %x, %x
  /*void runOnOperation() override {
    mlir::func::FuncOp function = getOperation();
    function.walk([&](mlir::arith::AddIOp add) {
      mlir::Value lhs = add.getLhs();
      mlir::OpOperand &rhsUse = add->getOpOperand(1);// 获取operand 1所在的操作数位置
      rhsUse.set(lhs);
    });
  }*/

  // 删除原始func中的无效操作
  /*void runOnOperation() override {
    mlir::func::FuncOp function = getOperation();
    llvm::SmallVector<mlir::Operation *, 4> dead;
    function.walk([&](mlir::arith::ConstantOp constant) {
      if (constant.getResult().use_empty())
        dead.push_back(constant.getOperation());
    });
    for (mlir::Operation *operation : dead)
      operation->erase();
  }*/

  // 
  void runOnOperation() override {
    mlir::func::FuncOp function = getOperation();
    mlir::Region &body = function.getBody();//获取region
    if (body.empty())
      return;
    mlir::Block &entry = body.front();
    for (mlir::Operation &operation : entry)
      operation.dump();
    mlir::OpBuilder builder(function.getContext());
    function.walk([&](mlir::arith::AddIOp add) {
      mlir::Type resultType = add.getResult().getType();
      resultType.dump();
      add->setAttr("tinyir.seen", builder.getUnitAttr());
    });
  }
};

} // namespace

void tinyir::registerDemoPass() {
  mlir::PassRegistration<DemoPass>();
}



