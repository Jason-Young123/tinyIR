#ifndef LIST_DIALECT
#define LIST_DIALECT

include "mlir/IR/OpBase.td"
include "mlir/IR/AttrTypeBase.td"
include "mlir/IR/CommonTypeConstraints.td"
include "mlir/IR/OpBase.td"
include "mlir/Interfaces/InferTypeOpInterface.td"


// step1: 定义List_Dialect, 继承Dialect
def List_Dialect : Dialect {
  let name = "list";
  let cppNamespace = "::mlir::list";

  let summary = "Types and operations on lists of integers";
  let description = [{
    The `list` dialect describes immutable, dynamically-sized sequences of
    integers, together with a small set of operations that produce, consume and
    transform them.
  }];

  let useDefaultTypePrinterParser = 1;
}


// step2: 定义List_Type
// 定义所有List类型的共同基类
class List_Type<string name, string typeMnemonic, list<Trait> traits = []>
    : TypeDef<List_Dialect, name, traits> {
  let mnemonic = typeMnemonic;
}

// 具体定义list.list类型; 后续还有可能继续定义list.listTuple等等类型
def List_ListType : List_Type<"List", "list"> { //List为cpp侧名称, list为.mlir侧名称
  let summary = "list of integers";
  let description = [{
    !list.list is a dynamically-sized sequence of integers of a fixed element type.
    Example:
      !list.list<i32>
  }];

  let parameters = (ins "::mlir::IntegerType":$elementType);
  let assemblyFormat = "`<` $elementType `>`";

  let builders = [
    TypeBuilderWithInferredContext<(ins "::mlir::IntegerType":$elementType), [{
      return $_get(elementType.getContext(), elementType);
    }]>
  ];
}


// step3: 定义一些traits, 比如用于Op定义过程中的类型推导等等
class List_ItemTypeMatchesElementType<string listArg, string itemArg>
    : TypesMatchWith<"type of '" # itemArg # "' matches the element type of '"
                     # listArg # "'", listArg, itemArg,
                     "::llvm::cast<::mlir::list::ListType>($_self)"
                     ".getElementType()">;


// step4: 定义一系列Ops
// 定义所有Op的共同基类
class List_Op<string mnemonic, list<Trait> traits = []>
    : Op<List_Dialect, mnemonic, traits>;

// 具体定义Ops
def List_EmptyOp : List_Op<"empty"> {
  let summary = "list without any element";
  let description = [{
    Creates a list that holds no element.
    Example:
    %li = list.empty : !list.list<i32>
  }];

  let results = (outs List_ListType:$result);
  let assemblyFormat = "attr-dict `:` qualified(type($result))";
}

def List_LengthOp : List_Op<"length"> {
  let summary = "number of elements of a list";
  let description = [{
    Returns the number of elements of the operand list.
    Example:
    %len = list.length %li : !list.list<i32> -> i32
  }];

  let arguments = (ins List_ListType:$input);
  let results = (outs I32:$result);

  let assemblyFormat =
      "$input attr-dict `:` qualified(type($input)) `->` type($result)";

  let builders = [
    OpBuilder<(ins "::mlir::Value":$input), [{
      build($_builder, $_state, $_builder.getI32Type(), input);
    }]>
  ];
}

def List_PushBackOp : List_Op<"push_back",
    [AllTypesMatch<["input", "result"]>,
     List_ItemTypeMatchesElementType<"input", "item">]> {
  let summary = "list with an element appended";
  let description = [{
    Returns the operand list with `$item` appended to it.
    Example:
    %longer = list.push_back %li, %item : !list.list<i32>
  }];

  let arguments = (ins List_ListType:$input, AnyInteger:$item);
  let results = (outs List_ListType:$result);

  let assemblyFormat = "$input `,` $item attr-dict `:` qualified(type($input))";

  let builders = [
    OpBuilder<(ins "::mlir::Value":$input, "::mlir::Value":$item), [{
      build($_builder, $_state, input.getType(), input, item);
    }]>
  ];
}


def List_IsEmptyOp : List_Op<"is_empty"> {
  let summary = "check whether a list is empty";
  let description = [{
    Check the emptiness of a list and return bool.
    Example:
    ```mlir
    %is_empty = list.is_empty %li : !list.list<i32> -> i1
    ```
  }];
  let arguments = (ins List_ListType:$input);
  let results = (outs I1:$result);
  let assemblyFormat = "$input attr-dict `:` qualified(type($input)) `->` type($result)";
  let builders = [
      OpBuilder<(ins "::mlir::Value":$input), [{
        build($_builder, $_state, $_builder.getI1Type(), input);
      }]>
    ];
}

//单输入无输出操作
def List_PrintOp : List_Op<"print"> {
  let summary = "print the elements of a list";
  let description = [{
    Print the elements of a list to terminal.
    Example:
    ```mlir
    list.print %li : !list.list<i32>
    ```
  }];
  let arguments = (ins List_ListType:$input);
  let assemblyFormat = "$input attr-dict `:` qualified(type($input))";
  //不需要builder:
  // - 该 operation 只有一个输入 input；
  // - 没有结果，因此不需要额外构造结果类型；
  // - ODS 会自动生成通用 builder，可以直接通过操作数构造它
}


// 利用arguments中的类型简化assemblyFormat中的类型声明
def List_RangeOp : List_Op<"range"> {
  let summary = "construct a list via upper and lower bounds";
  let description = [{
    Construct a list from lower to upper bounds.
    Example:
    ```mlir
    %lo = list.range %lower to %upper : !list.list<i32>
    ```
  }];
  let arguments = (ins I32:$lower, I32:$upper);
  let results = (outs List_ListType:$result);
  let assemblyFormat = "$lower `to` $upper attr-dict `:` qualified(type($result))";
}


// 利用traits简化assemblyFormat中的类型声明
def List_ReverseOp : List_Op<"reverse"> {
  let summary = "reverse the elements in a list";
  let description = [{
    Reverse the content of a list.
    Example:
    ```mlir
    %lo = list.reverse %li : !list.list<i32> -> !list.list<i32>
    ```
  }];
  let arguments = (ins List_ListType:$input);
  let results = (outs List_ListType:$result);
  let assemblyFormat = "$input attr-dict `:` qualified(type($input)) `->` qualified(type($result))";
}



def List_PopFrontOp : List_Op<"pop_front", [AllTypesMatch<["input", "result"]>]> {
  let summary = "pop out the head element of a list";
  let description = [{
    Pop out the head element of a list and return the remaining list.
    Example:
    %item = list.pop_front %li : !list.list<i32>
  }];
  let arguments = (ins List_ListType:$input);
  let results = (outs List_ListType:$result);
  let assemblyFormat = "$input attr-dict `:` qualified(type($input))";
}


def List_PeekFrontOp : List_Op<"peek_front", [List_ItemTypeMatchesElementType<"input", "item">]> {
  let summary = "Return the head element of a list";
  let description = [{
    %item = list.peek_front %li : !list.list<i32>
  }];
  let arguments = (ins List_ListType:$input);
  let results = (outs AnyInteger:$item);
  let assemblyFormat = "$input attr-dict `:` qualified(type($input))";
}

def List_PushFrontOp : List_Op<"push_front", [AllTypesMatch<["li", "lo"]>, List_ItemTypeMatchesElementType<"li", "item">]> {
  let summary = "Add a new item at the head of a list";
  let description = [{
    %lo = list.push_front %li, %item : !list.list<i32>
  }];
  let arguments = (ins List_ListType:$li, AnyInteger:$item);
  let results = (outs List_ListType:$lo);
  let assemblyFormat = "$li `,` $item attr-dict `:` qualified(type($li))";
}


def List_MapOp : List_Op<"map"> {
  let summary = "Apply a computation to each element of a list.";
  let description = [{
    %lo = list.map %li : !list.list<i32> -> !list.list<i64> with {
      ...
    }
  }];
  let hasVerifier = 1;
  let hasRegionVerifier = 1;
  let arguments = (ins List_ListType:$li);
  let regions = (region SizedRegion<1>:$body); //仅包含一个block的region
  let results = (outs List_ListType:$lo);
  let assemblyFormat = "$li `:` qualified(type($li)) `->` qualified(type($lo)) `with` $body attr-dict";
}



def List_YieldOp : List_Op<"yield", [Terminator, HasParent<"::mlir::list::MapOp">]> {
  let summary = "Terminate a list.map operation";
  let description = [{
    list.yield %out : i64
  }];
  let arguments = (ins AnyInteger:$yielded);
  let assemblyFormat = "$yielded attr-dict `:` type($yielded)";
}


#endif // LIST_DIALECT
