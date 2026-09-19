/*===- TableGen'erated file -------------------------------------*- C++ -*-===*\
|*                                                                            *|
|* Dialect Definitions                                                        *|
|*                                                                            *|
|* Automatically generated file, do not edit!                                 *|
|* From: Tutorial.td                                                          *|
|*                                                                            *|
\*===----------------------------------------------------------------------===*/

MLIR_DEFINE_EXPLICIT_TYPE_ID(::mlir::tutorial::TutorialDialect)
namespace mlir::tutorial {


TutorialDialect::TutorialDialect(::mlir::MLIRContext *context)
    : ::mlir::Dialect(getDialectNamespace(), context, ::mlir::TypeID::get<TutorialDialect>())
    
     {
  
  initialize();
}

TutorialDialect::~TutorialDialect() = default;


} // namespace mlir::tutorial
