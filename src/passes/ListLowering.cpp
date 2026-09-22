#include "passes/Passes.h"

#include "dialect/List.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/SCF/IR/SCF.h"

#include "mlir/IR/IRMapping.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Pass/PassRegistry.h"
#include "mlir/Transforms/GreedyPatternRewriteDriver.h"


// 以下pattern不再涉及canonicalization
namespace {

// 定义RewriterPattern 2: lowering 
struct LowerFromElementsOneStep : mlir::OpRewritePattern<mlir::list::FromElementsOp> {
  using OpRewritePattern::OpRewritePattern;
  mlir::LogicalResult matchAndRewrite(
      mlir::list::FromElementsOp op,
      mlir::PatternRewriter &rewriter) const override {
    mlir::ValueRange elements = op.getElements(); //用于variadic operand,即数量可变/不确定的op
    // 空列表由 ReplaceEmptyFromElements 负责。
    if (elements.empty()) {
      return mlir::failure();
    }
    mlir::Type listType = op.getResult().getType();
    mlir::ValueRange prefix = elements.drop_back(); // 获取前n-1个operand
    mlir::Value last = elements.back();// 单独列出最后一个operand
    auto shorter =
        mlir::list::FromElementsOp::create(// list.from_element(前n-1个operand)
            rewriter,
            op.getLoc(),
            listType,
            prefix);
    auto pushed =
        mlir::list::PushBackOp::create(// list.push_back(最后一个operand)
            rewriter,
            op.getLoc(),
            listType,
            shorter.getResult(),
            last);
    rewriter.replaceOp(// 将op的所有use替换为pushed.result的value,并删除op
        op,
        pushed.getResult());
    return mlir::success();
  }
};

// 定义RewritePattern 4: lowering
struct LowerMapPattern : mlir::OpRewritePattern<mlir::list::MapOp> {
  using OpRewritePattern::OpRewritePattern;
  mlir::LogicalResult matchAndRewrite(
      mlir::list::MapOp op,
      mlir::PatternRewriter &rewriter) const override {

    // list.map内部基本结构检查
    mlir::Region &mapRegion = op.getBody();
    if (!llvm::hasSingleElement(mapRegion))
      return mlir::failure();

    mlir::Block &mapBlock = mapRegion.front();
    if (mapBlock.getNumArguments() != 1)
      return mlir::failure();

    auto mapYield =
        llvm::dyn_cast<mlir::list::YieldOp>(
            mapBlock.getTerminator());
    if (!mapYield)
      return mlir::failure();

    mlir::Location loc = op.getLoc();
    mlir::Type listType =
        op.getResult().getType();

    // ------------------------------------------------------------
    // Rewrite starts here.
    // ------------------------------------------------------------

    rewriter.setInsertionPoint(op);

    auto empty =
        mlir::list::EmptyOp::create(
            rewriter,
            loc,
            listType);

    auto trueValue =
        mlir::arith::ConstantIntOp::create(
            rewriter,
            loc,
            1,
            1);

    auto whileOp =
        mlir::scf::WhileOp::create(
            rewriter,
            loc,
            mlir::TypeRange{listType, listType},
            mlir::ValueRange{op.getInput(), empty.getResult()});

    // ============================================================
    // before region
    //   (%rem, %acc)
    //       ↓
    //   is_empty(rem)
    //       ↓
    //   condition(!is_empty, rem, acc)
    // ============================================================

    mlir::Region &before =
        whileOp.getBefore();

    mlir::Block *beforeBlock =
        rewriter.createBlock(
            &before,
            before.end(),
            {listType, listType},
            {loc, loc});

    rewriter.setInsertionPointToEnd(beforeBlock);

    mlir::Value rem =
        beforeBlock->getArgument(0);

    mlir::Value acc =
        beforeBlock->getArgument(1);

    auto isEmpty =
        mlir::list::IsEmptyOp::create(
            rewriter,
            loc,
            rewriter.getI1Type(),
            rem);

    auto cond =
        mlir::arith::XOrIOp::create(
            rewriter,
            loc,
            isEmpty.getResult(),
            trueValue.getResult());

    mlir::scf::ConditionOp::create(
        rewriter,
        loc,
        cond.getResult(),
        mlir::ValueRange{rem, acc});

    // ============================================================
    // after region
    //   (%rem2, %acc2)
    //       ↓
    //   peek + pop
    //       ↓
    //   clone 原 map body
    //       ↓
    //   push_back
    //       ↓
    //   yield(newRem, newAcc)
    // ============================================================

    mlir::Region &after =
        whileOp.getAfter();

    mlir::Block *afterBlock =
        rewriter.createBlock(
            &after,
            after.end(),
            {listType, listType},
            {loc, loc});

    rewriter.setInsertionPointToEnd(afterBlock);

    mlir::Value rem2 =
        afterBlock->getArgument(0);

    mlir::Value acc2 =
        afterBlock->getArgument(1);

    mlir::Type elementType =
        mapBlock.getArgument(0).getType();

    auto element =
        mlir::list::PeekFrontOp::create(
            rewriter,
            loc,
            elementType,
            rem2);

    auto rest =
        mlir::list::PopFrontOp::create(
            rewriter,
            loc,
            listType,
            rem2);

    mlir::IRMapping mapping;

    mapping.map(
        mapBlock.getArgument(0),
        element.getResult());

    for (mlir::Operation &nested :
         mapBlock.without_terminator()) {
      rewriter.clone(nested, mapping);
    }

    mlir::Value mapped =
        mapping.lookup(mapYield.getValue());

    auto nextAcc =
        mlir::list::PushBackOp::create(
            rewriter,
            loc,
            listType,
            acc2,
            mapped);

    mlir::scf::YieldOp::create(
        rewriter,
        loc,
        mlir::ValueRange{
            rest.getResult(),
            nextAcc.getResult()});

    // while 返回两个结果：
    // result #0 = 最终 rem
    // result #1 = 最终 acc
    rewriter.replaceOp(
        op,
        whileOp.getResult(1));

    return mlir::success();
  }
};




// 定义pass
struct ListLoweringPass
    : mlir::PassWrapper<
          ListLoweringPass,
          mlir::OperationPass<mlir::func::FuncOp>> {

  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(ListLoweringPass)

  mlir::StringRef getArgument() const override {
    return "list-lowering";//命令行passpp名称
  }

  mlir::StringRef getDescription() const override {
    return "Lower high-level List operations w.o. canonicalization";
  }

  void getDependentDialects(
      mlir::DialectRegistry &registry) const override {

    registry.insert<
        mlir::list::ListDialect,
        mlir::arith::ArithDialect,
        mlir::scf::SCFDialect>();
  }

  void runOnOperation() override {
    mlir::RewritePatternSet patterns(&getContext());

  //ReplaceEmptyFromElements和LowerFromElementsOneStep这两个pattern优先级不重要
  /*patterns.add<
    ReplaceEmptyFromElements,
    LowerFromElementsOneStep>(
        patterns.getContext());

  // MergeMapPattern优先级更高
  patterns.add<MergeMapPattern>(
    patterns.getContext(),
    mlir::PatternBenefit(2));

  // LowerMapPattern优先级更低
  patterns.add<LowerMapPattern>(
    patterns.getContext(),
    mlir::PatternBenefit(1));*/

    patterns.add<LowerFromElementsOneStep>(
    patterns.getContext());

    patterns.add<LowerMapPattern>(
    patterns.getContext());

    if (mlir::failed(
            mlir::applyPatternsGreedily(
                getOperation(),
                std::move(patterns)))) {
      signalPassFailure();
    }
  }
};

} // namespace



void tinyir::registerListLoweringPass() {
  mlir::PassRegistration<ListLoweringPass>();
}




