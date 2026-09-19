#ifndef LIST_H
#define LIST_H

// MLIR自带头文件
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/Dialect.h"
#include "mlir/IR/OpDefinition.h"
#include "mlir/IR/OpImplementation.h"
#include "mlir/Interfaces/InferTypeOpInterface.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"

// 自动生成.inc文件
#include "List_Dialect.h.inc"

#define GET_TYPEDEF_CLASSES
#include "List_Types.h.inc"

#define GET_OP_CLASSES
#include "List_Ops.h.inc"

#endif // LIST_H
