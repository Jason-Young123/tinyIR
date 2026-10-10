#ifndef LATENCY_DIALECT
#define LATENCY_DIALECT

include "mlir/IR/OpBase.td"
include "mlir/Interfaces/SideEffectInterfaces.td"


// 定义Latency Dialect
def Latency_Dialect : Dialect {
  let name = "latency";

  let cppNamespace = "::mlir::latency";

  let summary =
      "Synthetic latency operations for instruction scheduling experiments";

  let description = [{
    The latency dialect provides synthetic pure SSA operations with fixed
    scheduling latencies.

    These operations are not intended to model real ISA instructions.
    They are used to construct arbitrary dependency DAGs and evaluate
    latency-aware scheduling algorithms.
  }];
}


// 定义Latency Dialect中所有Operation的共同基类
class Latency_Op<
    string mnemonic,
    list<Trait> traits = []>
    : Op<Latency_Dialect, mnemonic, traits>;


// latency.lat1
def Latency_Lat1Op
    : Latency_Op<"lat1", [Pure]> {

  let summary =
      "Synthetic operation with execution latency 1";

  let description = [{
    A synthetic pure operation whose scheduler latency is modeled as 1 cycle.

    It accepts zero or more i32 SSA operands and produces one i32 SSA result.
    Each operand represents one incoming dependency edge in the scheduling DAG.

    Examples:

      %0 = latency.lat1 : i32
      %1 = latency.lat1 %0 : i32
      %2 = latency.lat1 %0, %1 : i32
  }];

  let arguments = (ins
    Variadic<I32>:$inputs
  );

  let results = (outs
    I32:$result
  );

  let assemblyFormat =
      "$inputs attr-dict `:` type($result)";
}


// latency.lat5
def Latency_Lat5Op
    : Latency_Op<"lat5", [Pure]> {

  let summary =
      "Synthetic operation with execution latency 5";

  let description = [{
    A synthetic pure operation whose scheduler latency is modeled as 5 cycles.

    It accepts zero or more i32 SSA operands and produces one i32 SSA result.
    Each operand represents one incoming dependency edge in the scheduling DAG.

    Examples:

      %0 = latency.lat5 : i32
      %1 = latency.lat5 %0 : i32
      %2 = latency.lat5 %0, %1 : i32
  }];

  let arguments = (ins
    Variadic<I32>:$inputs
  );

  let results = (outs
    I32:$result
  );

  let assemblyFormat =
      "$inputs attr-dict `:` type($result)";
}


// latency.lat10
def Latency_Lat10Op
    : Latency_Op<"lat10", [Pure]> {

  let summary =
      "Synthetic operation with execution latency 10";

  let description = [{
    A synthetic pure operation whose scheduler latency is modeled as 10 cycles.

    It accepts zero or more i32 SSA operands and produces one i32 SSA result.
    Each operand represents one incoming dependency edge in the scheduling DAG.

    Examples:

      %0 = latency.lat10 : i32
      %1 = latency.lat10 %0 : i32
      %2 = latency.lat10 %0, %1 : i32
  }];

  let arguments = (ins
    Variadic<I32>:$inputs
  );

  let results = (outs
    I32:$result
  );

  let assemblyFormat =
      "$inputs attr-dict `:` type($result)";
}


#endif // LATENCY_DIALECT


