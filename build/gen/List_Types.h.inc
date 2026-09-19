/*===- TableGen'erated file -------------------------------------*- C++ -*-===*\
|*                                                                            *|
|* TypeDef Declarations                                                       *|
|*                                                                            *|
|* Automatically generated file, do not edit!                                 *|
|*                                                                            *|
\*===----------------------------------------------------------------------===*/

#ifdef GET_TYPEDEF_CLASSES
#undef GET_TYPEDEF_CLASSES


namespace mlir {
class AsmParser;
class AsmPrinter;
} // namespace mlir
namespace mlir::list {

/// list of integers
/// !list.list is a dynamically-sized sequence of integers of a fixed element type.
/// Example:
///   !list.list<i32>
class ListType;
namespace detail {

struct ListTypeStorage;

} // namespace detail
class ListType : public ::mlir::Type::TypeBase<ListType, ::mlir::Type, detail::ListTypeStorage> {
public:
  using Base::Base;
  static constexpr ::llvm::StringLiteral name = "list.list";
  static constexpr ::llvm::StringLiteral dialectName = "list";
  static ListType get(::mlir::MLIRContext *context, ::mlir::IntegerType elementType);
  static ListType get(::mlir::IntegerType elementType);
  static constexpr ::llvm::StringLiteral getMnemonic() {
    return {"list"};
  }

  static ::mlir::Type parse(::mlir::AsmParser &odsParser);
  void print(::mlir::AsmPrinter &odsPrinter) const;
  ::mlir::IntegerType getElementType() const;
};

} // namespace mlir::list
MLIR_DECLARE_EXPLICIT_TYPE_ID(::mlir::list::ListType)

#endif // GET_TYPEDEF_CLASSES

