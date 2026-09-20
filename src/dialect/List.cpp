#include "List.h"

#include "mlir/IR/DialectImplementation.h"
#include "llvm/ADT/TypeSwitch.h"

using namespace mlir;
using namespace mlir::list;

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

#define GET_TYPEDEF_CLASSES
#include "List_Types.cpp.inc"

#define GET_OP_CLASSES
#include "List_Ops.cpp.inc"

// 对应let hasVerifier = 1
LogicalResult MapOp::verify() {
  return success();
}

// 对应let hasRegionVerifier = 1
LogicalResult MapOp::verifyRegions() {
  return success();
}
