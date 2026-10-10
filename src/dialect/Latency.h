#ifndef LATENCY_H
#define LATENCY_H

#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/Dialect.h"
#include "mlir/IR/OpDefinition.h"
#include "mlir/IR/OpImplementation.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"

#include "Latency_Dialect.h.inc"

#define GET_OP_CLASSES
#include "Latency_Ops.h.inc"

#endif // LATENCY_H


