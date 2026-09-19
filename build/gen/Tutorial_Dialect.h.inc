/*===- TableGen'erated file -------------------------------------*- C++ -*-===*\
|*                                                                            *|
|* Dialect Declarations                                                       *|
|*                                                                            *|
|* Automatically generated file, do not edit!                                 *|
|* From: Tutorial.td                                                          *|
|*                                                                            *|
\*===----------------------------------------------------------------------===*/

namespace mlir::tutorial {

/// A minimal dialect for checking the tutorial setup
class TutorialDialect : public ::mlir::Dialect {
  explicit TutorialDialect(::mlir::MLIRContext *context);

  void initialize();
  friend class ::mlir::MLIRContext;
public:
  ~TutorialDialect() override;
  static constexpr ::llvm::StringLiteral getDialectNamespace() {
    return ::llvm::StringLiteral("tutorial");
  }
};

} // namespace mlir::tutorial
MLIR_DECLARE_EXPLICIT_TYPE_ID(::mlir::tutorial::TutorialDialect)
