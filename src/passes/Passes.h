#ifndef TINYIR_PASSES_H
#define TINYIR_PASSES_H

namespace tinyir { // 在tinyir-opt.cpp中可以直接使用tinyir::

void registerDemoPass(); //为tinyir-opt.cpp提供注册接口; 最小化demo
//各类进阶pass
void registerInspectIRPass();
void registerUseDefPass();
void registerCFGInspectPass();
void registerBuilderSafetyPass();
void registerRewriterDemoPass();
void registerListLowerFromElementsPass();
void registerListSimplifyPass();


} // namespace tinyir

#endif // TINYIR_PASSES_H
