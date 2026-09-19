#include "mlir/InitAllDialects.h"
#include "mlir/InitAllExtensions.h"
#include "mlir/InitAllPasses.h"
#include "mlir/Tools/mlir-opt/MlirOptMain.h"

#include "dialect/Tutorial.h"
#include "dialect/List.h"

int main(int argc, char **argv) {
  mlir::DialectRegistry registry;

  // MLIR自带dialect
  mlir::registerAllDialects(registry);

  // MLIR自带pass
  mlir::registerAllPasses();

  // MLIR extensions
  mlir::registerAllExtensions(registry);

  // 本项目自定义dialect
  registry.insert<
      mlir::tutorial::TutorialDialect,
      mlir::list::ListDialect>();

  return mlir::asMainReturnCode(
      mlir::MlirOptMain(
          argc,
          argv,
          "tinyIR optimizer driver\n",
          registry));
}
