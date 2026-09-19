// 同路径下的头文件
#include "Tutorial.h"

#include "mlir/IR/DialectImplementation.h"

using namespace mlir;
using namespace mlir::tutorial;

#include "Tutorial_Dialect.cpp.inc"

void TutorialDialect::initialize() {
  addOperations<
#define GET_OP_LIST
#include "Tutorial_Ops.cpp.inc"
      >();
}

#define GET_OP_CLASSES
#include "Tutorial_Ops.cpp.inc"
