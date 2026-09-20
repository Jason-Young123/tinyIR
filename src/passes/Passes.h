#ifndef TINYIR_PASSES_H
#define TINYIR_PASSES_H

namespace tinyir {

void registerDemoPass(); //为tinyir-opt.cpp提供注册接口; 最小化demo
//各类进阶pass
void registerInspectIRPass();
void registerUseDefPass();
void registerCFGInspectPass();
void registerBuilderSafetyPass();
void registerRewriterDemoPass();

} // namespace tinyir

#endif // TINYIR_PASSES_H
