#ifndef TUTORIAL_DIALECT
#define TUTORIAL_DIALECT

include "mlir/IR/OpBase.td"

include "mlir/Interfaces/SideEffectInterfaces.td"

def Tutorial_Dialect : Dialect {
  let name = "tutorial";
  // dialect 在 MLIR 中的名字是 tutorial。
  // 它参与组成 operation 的完整名字，例如 tutorial.constant。

  let cppNamespace = "::mlir::tutorial";
  let summary = "A minimal dialect for checking the tutorial setup";
}

// 定义通用tutorial这个dialect的op
class Tutorial_Op<string mnemonic, list<Trait> traits = []> : Op<Tutorial_Dialect, mnemonic, traits>;

def Tutorial_ConstantOp : Tutorial_Op<"constant", [Pure]> {
  let summary = "Produce a constant i32 value";

  let arguments = (ins I32Attr:$value); // I32Attr 表示一个 32 位 signless integer attribute，来自 OpBase.td。

  let results = (outs I32:$result);

  let assemblyFormat = "$value attr-dict `:` type($result)";
}

#endif // TUTORIAL_DIALECT
