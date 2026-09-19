#ifndef TUTORIAL_H
#define TUTORIAL_H

// MLIR自带头文件
#include "mlir/Bytecode/BytecodeOpInterface.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/Dialect.h"
#include "mlir/IR/OpDefinition.h"
#include "mlir/IR/OpImplementation.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"

// 自动生成.inc文件
#include "Tutorial_Dialect.h.inc"

#define GET_OP_CLASSES
#include "Tutorial_Ops.h.inc"

#endif // TUTORIAL_H
