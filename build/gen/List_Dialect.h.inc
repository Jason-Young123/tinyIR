/*===- TableGen'erated file -------------------------------------*- C++ -*-===*\
|*                                                                            *|
|* Dialect Declarations                                                       *|
|*                                                                            *|
|* Automatically generated file, do not edit!                                 *|
|* From: List.td                                                              *|
|*                                                                            *|
\*===----------------------------------------------------------------------===*/

namespace mlir::list {

/// Types and operations on lists of integers
/// The `list` dialect describes immutable, dynamically-sized sequences of
/// integers, together with a small set of operations that produce, consume and
/// transform them.
class ListDialect : public ::mlir::Dialect {
  explicit ListDialect(::mlir::MLIRContext *context);

  void initialize();
  friend class ::mlir::MLIRContext;
public:
  ~ListDialect() override;
  static constexpr ::llvm::StringLiteral getDialectNamespace() {
    return ::llvm::StringLiteral("list");
  }

  /// Parse a type registered to this dialect.
  ::mlir::Type parseType(::mlir::DialectAsmParser &parser) const override;

  /// Print a type registered to this dialect.
  void printType(::mlir::Type type,
                 ::mlir::DialectAsmPrinter &os) const override;
};

} // namespace mlir::list
MLIR_DECLARE_EXPLICIT_TYPE_ID(::mlir::list::ListDialect)
