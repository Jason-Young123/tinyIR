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


def List_EmptyOp : List_Op<"empty"> {
  let summary = "Create an empty list";
  let results = (outs List_ListType:$result);
}

def List_PushBackOp : List_Op<"push_back"> {
  let summary = "Append one element and return a new list";
  let arguments = (ins
    List_ListType:$input,
    AnyInteger:$element
  );
  let results = (outs List_ListType:$result);
}

def List_FromElementsOp : List_Op<"from_elements"> {
  let summary = "Create a list from zero or more SSA values";
  let arguments = (ins Variadic<AnyInteger>:$elements);
  let results = (outs List_ListType:$result);
}

def List_IsEmptyOp : List_Op<"is_empty"> {
  let summary = "Test whether a list is empty";
  let arguments = (ins List_ListType:$input);
  let results = (outs I1:$result);
}

def List_LengthOp : List_Op<"length"> {
  let summary = "Return the list length";
  let arguments = (ins List_ListType:$input);
  let results = (outs I32:$result);
}

def List_PeekFrontOp : List_Op<"peek_front"> {
  let summary = "Read the first element";
  let arguments = (ins List_ListType:$input);
  let results = (outs AnyInteger:$result);
}

def List_PopFrontOp : List_Op<"pop_front"> {
  let summary = "Return a list without the first element";
  let arguments = (ins List_ListType:$input);
  let results = (outs List_ListType:$result);
}

def List_MapOp : List_Op<"map"> {
  let summary = "Map a region over all list elements";
  let arguments = (ins List_ListType:$input);
  let hasVerifier = 1;
  let hasRegionVerifier = 1;
  let results = (outs List_ListType:$result);
  let regions = (region AnyRegion:$body);
}

def List_YieldOp : List_Op<"yield", [Terminator]> {
  let summary = "Yield one mapped element";
  let arguments = (ins AnyInteger:$value);
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


#endif // LIST_DIALECT
