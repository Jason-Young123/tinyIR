#include "List.h"

#include "mlir/IR/DialectImplementation.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/IR/IRMapping.h"

#include "llvm/ADT/TypeSwitch.h"

using namespace mlir;
using namespace mlir::list;


//===----------------------------------------------------------------------===//
// Dialect implementation
//===----------------------------------------------------------------------===//
#include "List_Dialect.cpp.inc"
void ListDialect::initialize() {

  addTypes<
#define GET_TYPEDEF_LIST
#include "List_Types.cpp.inc"
      >();

  addOperations<
#define GET_OP_LIST
#include "List_Ops.cpp.inc"
      >();
}

//===----------------------------------------------------------------------===//
// Type implementation
//===----------------------------------------------------------------------===//
#define GET_TYPEDEF_CLASSES
#include "List_Types.cpp.inc"


//===----------------------------------------------------------------------===//
// Operation implementation
//===----------------------------------------------------------------------===//
#define GET_OP_CLASSES
#include "List_Ops.cpp.inc"

//===----------------------------------------------------------------------===//
// Canonicalization Patterns
//===----------------------------------------------------------------------===//
namespace {
// Pattern1:
struct ReplaceEmptyFromElements : mlir::OpRewritePattern<mlir::list::FromElementsOp> {
  using OpRewritePattern::OpRewritePattern;
  mlir::LogicalResult matchAndRewrite(// 具体定义rewritePattern的实现
      mlir::list::FromElementsOp op,
      mlir::PatternRewriter &rewriter) const override {
    // Match 阶段：可能会返回failure, 因此在此之前不能修改IR
    // 只接受没有任何 element 的 from_elements。
    if (!op.getElements().empty()) {
      return rewriter.notifyMatchFailure(
          op,
          "list has elements");
    }
    // Rewrite 阶段：创建新的 list.empty，用其 result 替代旧 op 的 result，并删除旧op
    rewriter.replaceOpWithNewOp<
        mlir::list::EmptyOp>(
            op,
            op.getResult().getType());
    return mlir::success();
  }
};

// Pattern3:
struct MergeMapPattern : mlir::OpRewritePattern<mlir::list::MapOp> {
  using OpRewritePattern::OpRewritePattern;
  mlir::LogicalResult matchAndRewrite(
      mlir::list::MapOp consumer,
      mlir::PatternRewriter &rewriter) const override {
    auto producer = consumer.getInput().getDefiningOp<mlir::list::MapOp>(); // 定位到producer list.map operation
    if (!producer) {
      return rewriter.notifyMatchFailure(
          consumer,
          "input is not produced by list.map");
    }

    mlir::Region &producerRegion = producer.getBody();// 获取producer的region
    mlir::Region &consumerRegion = consumer.getBody();// 获取consumer的region
    if (!llvm::hasSingleElement(producerRegion) ||
        !llvm::hasSingleElement(consumerRegion)) {//确保二者的map region中均只有一个block
      return rewriter.notifyMatchFailure(
          consumer,
          "expected single-block map regions");
    }

    mlir::Block &producerBlock = producerRegion.front();// 获取producer的block
    mlir::Block &consumerBlock = consumerRegion.front();// 获取consumer的block
    if (producerBlock.getNumArguments() != 1 ||
        consumerBlock.getNumArguments() != 1) {// 确保二者均只有一个block argument
      return rewriter.notifyMatchFailure(
          consumer,
          "expected one block argument");
    }

    auto producerYield = llvm::dyn_cast<mlir::list::YieldOp>(producerBlock.getTerminator());
    auto consumerYield = llvm::dyn_cast<mlir::list::YieldOp>(consumerBlock.getTerminator());
    if (!producerYield || !consumerYield) {// 确保二者block都已yield结尾
      return rewriter.notifyMatchFailure(
          consumer,
          "expected list.yield terminators");
    }

    // 开始修改IR
    auto fused =
        mlir::list::MapOp::create(
            rewriter,
            consumer.getLoc(),
            consumer.getResult().getType(),
            producer.getInput()); //将producer的输入作为融合后list.map操作的输入

    mlir::Region &fusedRegion = fused.getBody();
    mlir::Block *fusedBlock =
        rewriter.createBlock(
            &fusedRegion,
            fusedRegion.end(),
            {producerBlock.getArgument(0).getType()},
            {consumer.getLoc()});

    rewriter.setInsertionPointToEnd(fusedBlock);

    mlir::IRMapping mapping;

    // 新 map 的 block argument
    // 对应 producer 的 block argument。
    mapping.map(producerBlock.getArgument(0),fusedBlock->getArgument(0));

    // clone producer body，跳过旧 yield。
    for (mlir::Operation &nested :
         producerBlock.without_terminator()) {
      rewriter.clone(nested, mapping);
    }

    mlir::Value producerResult = mapping.lookup(producerYield.getValue());

    // consumer 的 block argument
    // 应该接 producer body 的输出。
    mapping.map(consumerBlock.getArgument(0), producerResult);

    // clone consumer body，仍然跳过旧 yield。
    for (mlir::Operation &nested :
         consumerBlock.without_terminator()) {
      rewriter.clone(nested, mapping);
    }

    mlir::Value finalResult = mapping.lookup(consumerYield.getValue());//得到最终结果,由consumer产出

    mlir::list::YieldOp::create(//在fusedOp尾部自行添加yield
        rewriter,
        consumer.getLoc(),
        finalResult);

    // consumer 的结果由 fused map 替代。
    rewriter.replaceOp(consumer, fused.getResult());

    // producer 可能还有其他 users。
    // 只有彻底 dead 时才能删除。
    if (producer.getResult().use_empty())
      rewriter.eraseOp(producer);

    return mlir::success();
  }
};

} // namespace


//===----------------------------------------------------------------------===//
// Canonicalization registration
//===----------------------------------------------------------------------===//
// list.from_elements这个op将自己的某个pattern注册到MLIR的通用canonicalizer中
void mlir::list::FromElementsOp::getCanonicalizationPatterns(
    mlir::RewritePatternSet &results,
    mlir::MLIRContext *context) {

  results.add<ReplaceEmptyFromElements>(
      context);
}

// list.map这个op也将自己的某个pattern注册到MLIR的通用canonicalizer中
void mlir::list::MapOp::getCanonicalizationPatterns(
    mlir::RewritePatternSet &results,
    mlir::MLIRContext *context) {

  results.add<MergeMapPattern>(
      context);
}


//===----------------------------------------------------------------------===//
// Folder
//===----------------------------------------------------------------------===//
mlir::OpFoldResult mlir::list::ReverseOp::fold(
    FoldAdaptor adaptor) {
  if (auto producer =
          getInput()
              .getDefiningOp<
                  mlir::list::ReverseOp>()) {

    return producer.getInput();
  }
  return {};
}


//===----------------------------------------------------------------------===//
// Verification
//===----------------------------------------------------------------------===//
//   let hasVerifier = 1;
LogicalResult MapOp::verify() {
  return success();
}

//   let hasRegionVerifier = 1;
LogicalResult MapOp::verifyRegions() {
  return success();
}