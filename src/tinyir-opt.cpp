#include "mlir/InitAllDialects.h"
#include "mlir/InitAllExtensions.h"
#include "mlir/InitAllPasses.h"
#include "mlir/Tools/mlir-opt/MlirOptMain.h"

#include "dialect/Tutorial.h"
#include "dialect/List.h"
#include "passes/Passes.h" //用于提供注册函数

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

  // 自定义pass
  tinyir::registerDemoPass();
  tinyir::registerInspectIRPass();
  tinyir::registerUseDefPass();
  tinyir::registerCFGInspectPass();
  tinyir::registerBuilderSafetyPass();
  tinyir::registerRewriterDemoPass();
  tinyir::registerListLowerFromElementsPass();
  tinyir::registerListSimplifyPass();

  return mlir::asMainReturnCode(
      mlir::MlirOptMain(
          argc,
          argv,
          "tinyIR optimizer driver\n",
          registry));
}
