#include "Latency.h"

#include "mlir/IR/DialectImplementation.h"

using namespace mlir;
using namespace mlir::latency;

#include "Latency_Dialect.cpp.inc"

void LatencyDialect::initialize() {
  addOperations<
#define GET_OP_LIST
#include "Latency_Ops.cpp.inc"
      >();
}

#define GET_OP_CLASSES
#include "Latency_Ops.cpp.inc"


