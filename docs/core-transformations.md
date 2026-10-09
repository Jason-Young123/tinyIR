# `core-transformations.md`：tinyIR 的 Canonicalization、Folding 与 CSE 手把手教程

> 适用工程：当前 `tinyIR` 项目  
> 前置教程：已经完成 `pass.md`、`pass-advanced.md`、`pattern-rewrite.md`；已经能编写 `OpRewritePattern`、使用 `PatternRewriter`、`RewritePatternSet`、`PatternBenefit`、`applyPatternsGreedily`，并完成 `list.from_elements` rewrite、`list.map` fusion 与 `list.map -> scf.while` lowering。  
> 对照课件：**Core Transformations in MLIR — I.e. letting MLIR optimize our programs for us!**, Markus Böck / Maximilian Bartel, ACM Europe School on MLIR 2026。  
> 本讲目标：不再为每个 Dialect 手工串联“自己的 simplify pass”，而是把真正属于**规范化（canonicalization）**的规则挂回 Operation 本身；进一步学习比 RewritePattern 更局部、更受限的 `fold()`；最后用 MLIR 自带的 `-cse` 理解“只声明语义属性，就能让通用优化自动作用于自定义 Dialect”的设计方式。

---

# 0. 先建立这一讲的整体框架

上一讲结束时，我们已经能够自己写：

```text
ListSimplifyPass
    ↓
RewritePatternSet
    ├── ReplaceEmptyFromElements
    ├── LowerFromElementsOneStep
    ├── MergeMapPattern
    └── LowerMapPattern
    ↓
applyPatternsGreedily
```

这已经能完成相当复杂的 IR transformation。

但现在出现一个更高层的问题：

```text
如果 List Dialect 有自己的 simplify patterns
Arith Dialect 有自己的 simplify patterns
Func Dialect 也有自己的 simplify patterns
……
```

难道每次都要手工写：

```bash
-list-simplify
-arith-simplify
-func-simplify
-list-simplify
-arith-simplify
...
```

吗？

课件开头正是在指出这个问题：

```text
RewritePatterns are meant to be together!
```

因为很多优化机会并不属于单独某一个 Dialect。

例如：

```text
list.map fusion
    ↓
把两个 Region 合并

arith canonicalization
    ↓
发现 ext + trunc 可以消掉

list canonicalization
    ↓
又产生新的 list 简化机会
```

如果每个 Dialect 的规则都被锁在自己的 Pass 里：

```text
规则之间很难自动互相触发
```

所以这一讲的主线是：

```text
Stage A
把“局部规范化规则”从手写 ListSimplifyPass
迁移到 Operation 自己的 canonicalization hook
        ↓
使用 MLIR 通用 -canonicalize

Stage B
进一步区分：
RewritePattern 能做很多事情，
但某些最局部 simplification 应该使用更弱的 fold()
        ↓
学习 OpFoldResult / FoldAdaptor

Stage C
为了真正实践 constant folding，
给 tinyIR 补齐 list constant 表示：
ListAttr + list.constant + constant materializer
        ↓
实现 list.length / list.is_empty / list.reverse fold

Stage D
学习另一个完全不关心 List 具体语义的通用优化：
Common Subexpression Elimination
        ↓
-cse

Stage E
理解 CSE 为什么只需要：
“这个 op 有没有 side effect？”
        ↓
为自定义 List op 正确声明 Pure / effects

Stage F
进入 CFG 后理解：
CSE 不是“文本一样就删”
而是必须满足 dominance
```

完成本讲后，你应该能够清楚回答：

```text
1. canonicalize 和我们上一讲自己写的 list-simplify 有什么本质区别？
2. 什么样的 RewritePattern 应该注册为 canonicalization？
3. lowering 为什么通常不应该塞进 canonicalize？
4. fold() 和 RewritePattern 为什么不是简单的两种语法？
5. OpFoldResult 返回 Value 和返回 Attribute 分别意味着什么？
6. FoldAdaptor 为什么拿到的是 Attribute，而普通 op accessor 拿到的是 Value？
7. 为什么返回一个 Attribute 后还需要 dialect materializeConstant？
8. CSE 如何判断两个 Operation “等价”？
9. 为什么 CSE 需要 side-effect 信息？
10. 为什么两个文本上一模一样的 op 在不同 CFG branch 中仍可能无法合并？
```

---

# 1. 这一讲与上一讲的关系：不是再造一个新 Pass

先把三层结构摆在一起。

## 1.1 `pattern-rewrite.md` 已经解决的问题

上一讲解决：

```text
“我有一个局部 IR 规则，
怎样把它写成模块化 Pattern，
让 driver 自动匹配和 rewrite？”
```

核心工具：

```cpp
OpRewritePattern<OpTy>
PatternRewriter
RewritePatternSet
applyPatternsGreedily
```

---

## 1.2 本讲的新问题

现在我们继续问：

```text
“这些 Pattern 到底应该由谁注册？”
```

上一讲为了教学方便，我们写：

```cpp
struct ListSimplifyPass {
  void runOnOperation() {
    RewritePatternSet patterns(...);

    patterns.add<
        ReplaceEmptyFromElements,
        LowerFromElementsOneStep,
        MergeMapPattern,
        LowerMapPattern>(...);

    applyPatternsGreedily(...);
  }
};
```

这表示：

```text
Pattern 的生命周期和 ListSimplifyPass 绑定
```

只有用户显式执行：

```text
list-simplify
```

这些规则才存在。

但如果某些规则本质上只是：

```text
“把同一种语义写成统一、简单、规范的 IR”
```

那么它们更应该成为：

```text
Operation 自己的 canonicalization
```

这样 MLIR 通用：

```text
canonicalize
```

Pass 就能把所有 Dialect 的 canonicalization 一起运行。

---

# 2. 本讲开始时的 tinyIR 目录树

我们以 `pattern-rewrite.md` 完成后的工程为起点。

源码层大致是：

```text
tinyIR/
├── .gitignore
├── Makefile
├── requirements.txt
├── src/
│   ├── dialect/
│   │   ├── List.cpp
│   │   ├── List.h
│   │   ├── List.td
│   │   ├── Tutorial.cpp
│   │   ├── Tutorial.h
│   │   └── Tutorial.td
│   ├── passes/
│   │   ├── Passes.h
│   │   ├── BuilderSafety.cpp
│   │   ├── CFGInspect.cpp
│   │   ├── InspectIR.cpp
│   │   ├── ListLowerFromElements.cpp
│   │   ├── ListSimplify.cpp
│   │   ├── RewriterDemo.cpp
│   │   ├── SimplifyAdd.cpp
│   │   └── UseDef.cpp
│   └── tinyir-opt.cpp
└── test/
    └── passes/
        ├── advanced/
        └── patterns/
            ├── from-elements.mlir
            ├── simplify-from-elements.mlir
            ├── merge-map.mlir
            └── lower-map.mlir
```

本讲会逐渐变成：

```text
tinyIR/
├── src/
│   ├── dialect/
│   │   ├── List.td          ← 增加 canonicalizer/folder/attr/constant/effects
│   │   ├── List.h           ← 接入 AttrDef 生成代码
│   │   └── List.cpp         ← canonicalization + fold + materializer
│   │
│   ├── passes/
│   │   ├── ListSimplify.cpp ← 逐步退出历史舞台
│   │   └── ListLowering.cpp ← 只保留真正 lowering 的 Pattern
│   │
│   └── tinyir-opt.cpp
│
└── test/
    └── passes/
        └── core/
            ├── canonicalize.mlir
            ├── fold-structural.mlir
            ├── fold-constant.mlir
            ├── cse-list.mlir
            ├── cse-effects.mlir
            └── cse-cfg.mlir
```

这次目录变化背后的设计原则是：

```text
Dialect/Operation 自己负责：
    “什么是我的 canonical form？”
    “我有哪些局部 fold？”

Pass 负责：
    “我要执行一种明确的 transformation policy / lowering”

MLIR 通用 Pass 负责：
    canonicalize
    cse
    ...
```

---

# 3. Stage A：先理解什么叫 Canonicalization

`canonicalize` 不是“任何看起来像优化的东西都放进去”。

课件给出的核心目标是：

```text
Simplify the entire IR regardless of dialects present
```

它会自动收集：

```text
所有注册为 canonicalization 的 RewritePattern
```

然后统一运行。

所以：

```bash
tinyir-opt input.mlir \
  --pass-pipeline='builtin.module(func.func(canonicalize))'
```

并不是：

```text
“运行一个 Arith pass”
```

或者：

```text
“运行一个 List pass”
```

而是：

```text
canonicalizer
    ↓
看到 arith op
    → 取 arith canonicalization / fold

看到 list op
    → 取 list canonicalization / fold

看到 func op
    → 取 func canonicalization / fold

……
```

这也是“RewritePatterns are meant to be together”的真正含义。

---

# 4. 什么 Pattern 才应该进入 canonicalize？

课件强调：

```text
It’s a Design Choice
```

也就是说没有一个机械的：

```cpp
if (...) canonicalization;
else lowering;
```

但有非常实用的判断准则。

我们把它整理成四个问题。

---

## 4.1 问题一：它是不是让 IR 更“规范”？

例如：

```text
list.reverse(list.reverse(%x))
    ↓
%x
```

显然：

```text
语义完全相同
结构更简单
```

非常适合 canonicalization。

---

## 4.2 问题二：operation 数量是不是通常减少？

例如：

```text
map
 ↓
map
```

融合成：

```text
single map
```

一般更接近 canonical form。

所以：

```text
MergeMapPattern
```

很适合作为 canonicalization。

---

## 4.3 问题三：有没有丢失更高层信息？

这是区分：

```text
canonicalization
vs
lowering
```

最重要的问题之一。

例如：

```text
list.map
    ↓
scf.while
```

语义保持不变，但是：

```text
“这是一个 map”
```

这个高层信息已经消失。

以后如果某个优化器还想：

```text
merge maps
vectorize map
recognize map semantics
```

就更难了。

所以：

```text
LowerMapPattern
```

属于 lowering，而不是 canonicalization。

---

## 4.4 问题四：规则会不会收敛？

canonicalizer 会不断寻找新的简化机会。

如果你写两条规则：

```text
A -> B
B -> A
```

那么 canonicalization 永远无法稳定。

所以 canonicalization 应该有某种：

```text
“朝着统一形式前进”
```

的方向。

---

# 5. 先给上一讲的四个 Pattern 分类

上一讲我们有：

```text
ReplaceEmptyFromElements
LowerFromElementsOneStep
MergeMapPattern
LowerMapPattern
```

现在逐个分类。

## `ReplaceEmptyFromElements`

```text
list.from_elements()
    ↓
list.empty
```

属于：

```text
canonicalization
```

原因：

```text
语义不变
表达更直接
没有降低抽象层次
```

---

## `MergeMapPattern`

```text
map(map(x))
    ↓
map(x)
```

属于：

```text
canonicalization
```

原因：

```text
语义不变
减少一个 map
保留 map 高层语义
未来优化机会没有因为 lowering 消失
```

---

## `LowerFromElementsOneStep`

```text
from_elements(a,b,c)
    ↓
from_elements(a,b) + push_back(c)
```

更接近：

```text
lowering/decomposition
```

因为它不是把同一语义规范成更简洁结构，而是在：

```text
把一个较高层 op
逐步展开成较低层 primitive
```

而且 operation 数量会增加。

---

## `LowerMapPattern`

```text
list.map
    ↓
scf.while
```

明确属于：

```text
lowering
```

它会丢失：

```text
map semantics
```

因此不应该注册进通用 `canonicalize`。

最终得到：

```text
canonicalization:
    ReplaceEmptyFromElements
    MergeMapPattern

lowering:
    LowerFromElementsOneStep
    LowerMapPattern
```

这一步非常重要。

从现在开始：

```text
“Pattern 是怎么写的”
```

和：

```text
“这个 Pattern 应该由哪个 transformation driver 使用”
```

是两个不同问题。

---

# 6. 第一次工程改造：为 Operation 打开 canonicalizer hook

课件给出的 ODS 入口是：

```tablegen
let hasCanonicalizer = 1;
```

它的含义不是：

```text
“TableGen 自动帮我生成 canonicalization”
```

而是：

> **请在这个 Operation 的 C++ 类中声明 `getCanonicalizationPatterns(...)`，实现由我自己提供。**

---

# 7. 修改 `src/dialect/List.td`

找到：

```tablegen
def List_FromElementsOp : ...
```

增加：

```tablegen
let hasCanonicalizer = 1;
```

例如：

```tablegen
def List_FromElementsOp : List_Op<"from_elements"> {
  let summary = "Create a list from zero or more SSA values";
  let arguments = (ins Variadic<AnyInteger>:$elements);
  let results = (outs List_ListType:$result);

  // 允许 FromElementsOp 向 canonicalize 注册自己的 patterns。
  let hasCanonicalizer = 1;
}
```

然后找到：

```tablegen
def List_MapOp : ...
```

增加：

```tablegen
let hasCanonicalizer = 1;
```

保留你上一讲已经加入的：

```tablegen
let hasVerifier = 1;
let hasRegionVerifier = 1;
```

因此大致：

```tablegen
def List_MapOp : List_Op<"map"> {
  let summary = "Map a region over all list elements";

  let arguments = (ins List_ListType:$input);
  let results = (outs List_ListType:$result);
  let regions = (region AnyRegion:$body);

  let hasVerifier = 1;
  let hasRegionVerifier = 1;

  // 本讲新增。
  let hasCanonicalizer = 1;
}
```

执行：

```bash
make tblgen
```

或者：

```bash
make build
```

此时 generated `List_Ops.h.inc` 中会出现类似：

```cpp
static void getCanonicalizationPatterns(
    mlir::RewritePatternSet &results,
    mlir::MLIRContext *context);
```

注意：

```text
只有声明
没有实现
```

所以如果你现在直接链接，可能会出现：

```text
undefined reference
```

下一步就是提供实现。

---

# 8. canonicalization Pattern 应该放到哪里？

上一讲：

```text
src/passes/ListSimplify.cpp
```

里面同时放：

```text
Pattern 定义
+
Pass 定义
```

现在 canonicalization 已经变成：

```text
List operation 本身提供的语义能力
```

因此更自然的组织方式是：

```text
src/dialect/List.cpp
```

或者未来工程扩大后：

```text
src/dialect/ListCanonicalization.cpp
```

当前 tinyIR 规模还很小，所以本讲先直接放：

```text
src/dialect/List.cpp
```

这样不会为了几十行代码继续拆文件。

---

# 9. 把 `ReplaceEmptyFromElements` 迁移到 `List.cpp`

这里不是重新学习 Pattern。

我们只是把已经验证过的 Pattern：

```text
从 Pass 私有规则
变成 Operation canonicalization
```

在：

```text
src/dialect/List.cpp
```

顶部确保有：

```cpp
#include "dialect/List.h"

#include "mlir/IR/PatternMatch.h"
```

然后把上一讲已经写好的：

```cpp
struct ReplaceEmptyFromElements
    : mlir::OpRewritePattern<
          mlir::list::FromElementsOp> {
  ...
};
```

**原样移动**到：

```cpp
namespace {
...
} // namespace
```

里。

例如：

```cpp
namespace {

struct ReplaceEmptyFromElements
    : mlir::OpRewritePattern<
          mlir::list::FromElementsOp> {

  using OpRewritePattern::OpRewritePattern;

  mlir::LogicalResult matchAndRewrite(
      mlir::list::FromElementsOp op,
      mlir::PatternRewriter &rewriter) const override {

    if (!op.getElements().empty())
      return mlir::failure();

    rewriter.replaceOpWithNewOp<
        mlir::list::EmptyOp>(
            op,
            op.getResult().getType());

    return mlir::success();
  }
};

} // namespace
```

现在 Pattern 本身完全没变。

改变的是：

```text
谁来注册它
```

---

# 10. 实现 `FromElementsOp::getCanonicalizationPatterns`

继续在：

```text
src/dialect/List.cpp
```

加入：

```cpp
void mlir::list::FromElementsOp::
getCanonicalizationPatterns(
    mlir::RewritePatternSet &results,
    mlir::MLIRContext *context) {

  results.add<ReplaceEmptyFromElements>(
      context);
}
```

逐项理解：

```cpp
FromElementsOp::getCanonicalizationPatterns
```

表示：

```text
“当 canonicalizer 询问 FromElementsOp：
你自己有哪些 canonicalization？”
```

这个函数负责回答。

参数：

```cpp
RewritePatternSet &results
```

表示：

```text
canonicalizer 提供给你的 Pattern 容器
```

你只要把自己的规则放进去：

```cpp
results.add<...>(context);
```

即可。

第二个参数：

```cpp
MLIRContext *context
```

用于构造 Pattern。

---

# 11. 把 `MergeMapPattern` 迁移到 `List.cpp`

同样，把已经在上一讲跑通的：

```cpp
struct MergeMapPattern
    : mlir::OpRewritePattern<mlir::list::MapOp> {
    ...
};
```

从：

```text
src/passes/ListSimplify.cpp
```

移动到：

```text
src/dialect/List.cpp
```

其内部：

```text
producer detection
Region checks
IRMapping
clone producer body
clone consumer body
replace consumer
erase dead producer
```

全部保持不变。

然后增加：

```cpp
void mlir::list::MapOp::
getCanonicalizationPatterns(
    mlir::RewritePatternSet &results,
    mlir::MLIRContext *context) {

  results.add<MergeMapPattern>(
      context);
}
```

现在：

```text
MapOp
   ↓
知道自己的 canonicalization
   ↓
MergeMapPattern
```

而不是：

```text
ListSimplifyPass
   ↓
知道所有规则
```

这是本讲第一个架构变化。

---

# 12. 此时 `ListSimplify.cpp` 里还剩什么？

把 canonicalization 移走以后，原文件只剩：

```text
LowerFromElementsOneStep
LowerMapPattern
ListSimplifyPass
```

但这两个 Pattern 已经被我们明确分类为：

```text
lowering
```

所以：

```text
ListSimplify
```

这个名字已经不准确了。

最干净的做法是：

```text
重命名：
src/passes/ListSimplify.cpp
        ↓
src/passes/ListLowering.cpp
```

在 WSL：

```bash
mv src/passes/ListSimplify.cpp \
   src/passes/ListLowering.cpp
```

Makefile 使用：

```make
PASS_SRCS := $(wildcard $(PASS_DIR)/*.cpp)
```

因此不需要新增编译规则。

---

# 13. 重命名 Pass：`list-simplify` → `list-lowering`

修改：

```text
src/passes/ListLowering.cpp
```

把：

```cpp
struct ListSimplifyPass
```

改成：

```cpp
struct ListLoweringPass
```

TypeID：

```cpp
MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(
    ListLoweringPass)
```

argument：

```cpp
mlir::StringRef getArgument() const override {
  return "list-lowering";
}
```

description：

```cpp
mlir::StringRef getDescription() const override {
  return "Lower high-level List operations";
}
```

PatternSet 现在只保留：

```cpp
patterns.add<LowerFromElementsOneStep>(
    patterns.getContext());

patterns.add<LowerMapPattern>(
    patterns.getContext());
```

这里已经不再需要：

```text
MergeMapPattern benefit = 2
LowerMapPattern benefit = 1
```

因为：

```text
MergeMapPattern
```

已经交给：

```text
canonicalize
```

了。

这反而让 pipeline 设计更明确：

```text
先 canonicalize
    ↓
尽可能保留并优化高层语义

再 list-lowering
    ↓
真正进入更低层 IR
```

---

# 14. 修改 `Passes.h`

原来：

```cpp
void registerListSimplifyPass();
```

改成：

```cpp
void registerListLoweringPass();
```

---

# 15. 修改 `tinyir-opt.cpp`

原来：

```cpp
tinyir::registerListSimplifyPass();
```

改成：

```cpp
tinyir::registerListLoweringPass();
```

同时 `ListLowering.cpp` 底部：

```cpp
void tinyir::registerListLoweringPass() {
  mlir::PassRegistration<ListLoweringPass>();
}
```

重新：

```bash
make build
```

检查：

```bash
build/bin/tinyir-opt --help \
  | grep list-lowering
```

现在应该找到：

```text
list-lowering
```

而原来的：

```text
list-simplify
```

不再需要存在。

---

# 16. 新建 canonicalization 测试目录

创建：

```bash
mkdir -p test/passes/core
```

现在：

```text
test/passes/
├── advanced/
├── patterns/
└── core/
```

这一讲所有测试都放：

```text
test/passes/core/
```

因为：

```text
canonicalize
fold
cse
```

都属于：

```text
MLIR core transformations
```

而不是某个单独的自定义 pass 教学。

---

# 17. 创建第一个测试：`canonicalize.mlir`

创建：

```text
test/passes/core/canonicalize.mlir
```

先使用我们已经确定能工作的两类 List IR：

```mlir
module {
  // ============================================================
  // Case 1:
  // 空 from_elements 应该规范化为 list.empty。
  // ============================================================

  func.func @empty_from_elements()
      -> !list.list<i64> {

    %r = "list.from_elements"()
         : () -> !list.list<i64>

    return %r : !list.list<i64>
  }


  // ============================================================
  // Case 2:
  // 两个连续 map 应该由 MergeMapPattern 合并。
  // ============================================================

  func.func @two_maps(
      %l: !list.list<i64>,
      %c1: i64,
      %c2: i64)
      -> !list.list<i64> {

    %tmp = "list.map"(%l) ({
    ^bb0(%elem: i64):
      %a = arith.addi %elem, %c1 : i64
      "list.yield"(%a) : (i64) -> ()
    }) : (!list.list<i64>)
       -> !list.list<i64>

    %r = "list.map"(%tmp) ({
    ^bb0(%elem: i64):
      %b = arith.muli %elem, %c2 : i64
      "list.yield"(%b) : (i64) -> ()
    }) : (!list.list<i64>)
       -> !list.list<i64>

    return %r : !list.list<i64>
  }
}
```

---

# 18. 第一次真正运行 MLIR 自带 `canonicalize`

这一次不运行：

```text
list-simplify
```

而直接：

```bash
build/bin/tinyir-opt \
  test/passes/core/canonicalize.mlir \
  --pass-pipeline='builtin.module(func.func(canonicalize))'
```

如果希望看前后 IR：

```bash
build/bin/tinyir-opt \
  test/passes/core/canonicalize.mlir \
  -mlir-print-ir-before-all \
  -mlir-print-ir-after-all \
  --pass-pipeline='builtin.module(func.func(canonicalize))'
```

预期：

```text
empty_from_elements:
    from_elements()
        ↓
    empty
```

以及：

```text
two_maps:
    map
     ↓
    map
        ↓
    one map
```

注意：

```text
你没有显式告诉 canonicalize：
“请运行 MergeMapPattern”
```

它是通过：

```text
MapOp
  ↓
getCanonicalizationPatterns()
  ↓
MergeMapPattern
```

自动找到的。

---

# 19. canonicalization 的真正工程链

这一点要彻底建立起来：

```text
List.td
    ↓
let hasCanonicalizer = 1
    ↓
generated MapOp declaration
    ↓

List.cpp
    ↓
MapOp::getCanonicalizationPatterns(...)
    ↓
results.add<MergeMapPattern>()
    ↓

MLIR built-in canonicalize pass
    ↓
询问当前 IR 中 Operation 的 canonicalization
    ↓
收集多个 Dialect 的 patterns
    ↓
统一运行
```

所以以后：

```text
增加一个 canonicalization
```

通常不需要：

```text
再增加一个新 Pass
```

---

# 20. 为什么把 Pattern 放在 canonicalize 后，跨 Dialect 优化更自然？

课件使用：

```text
list.map
+
arith.extsi
+
arith.trunci
```

作为例子。

两个 map：

```text
map #1:
  i32 -> extsi -> i64

map #2:
  i64 -> trunci -> i32
```

如果：

```text
先 map fusion
```

得到一个 Region：

```text
extsi
↓
trunci
```

接下来：

```text
Arith canonicalization
```

可能发现：

```text
某些 extension/truncation 组合可进一步简化
```

这说明：

```text
List canonicalization
可以暴露 Arith canonicalization

Arith canonicalization
又可以暴露新的 List canonicalization
```

所以：

```text
canonicalize
```

的价值不是“少写命令”这么简单。

它更重要的意义是：

```text
不同 Dialect 的 canonicalization
共享一个 simplification framework
并能够互相创造机会
```

---

# 21. Stage B：为什么有了 RewritePattern 还要 fold()？

现在我们已经会：

```cpp
OpRewritePattern
```

理论上下面所有简化都能写 Pattern：

```text
reverse(reverse(x)) -> x
length(constant_list) -> constant integer
is_empty(constant_list) -> constant bool
```

为什么 MLIR 还设计：

```cpp
fold()
```

？

课件给出的核心理由是：

```text
RewritePattern 很强
甚至可以修改非局部 IR
```

而很多简单 simplification 根本不需要这种能力。

因此 MLIR 采用：

```text
Rule of Least Power
```

也就是：

> **能用更弱、更局部的机制表达，就不要使用更强的机制。**

`fold()` 就是这种受限机制。

---

# 22. fold 与 Pattern 的能力边界

先建立非常重要的区分。

## RewritePattern

可以：

```text
创建新 Operation
删除 Operation
修改其他 Operation
clone Region
移动 IR
做复杂结构变化
```

主要工具：

```cpp
PatternRewriter
```

---

## fold()

更强调：

```text
当前 op 的局部简化
```

典型结果只有：

```text
当前 op -> 已存在的 Value
```

或者：

```text
当前 op -> 一个 constant Attribute
```

所以：

```text
fold
```

更适合：

```text
constant folding
identity simplification
极局部 algebraic simplification
```

如果需要：

```text
主动创建多个普通 Operation
大范围修改 CFG
复杂 Region rewrite
```

就应该回到：

```text
RewritePattern
```

---

# 23. 第一个 fold 不碰 constant：双重 reverse

为了先把：

```text
fold API
```

学清楚，我们暂时不引入 ListAttr。

从最简单规则开始：

```text
list.reverse(list.reverse(%l))
    ↓
%l
```

这里不需要创建任何新 Operation。

最终只是：

```text
当前 ReverseOp 的 result
    ↓
替换成已经存在的 Value %l
```

这正是 `OpFoldResult` 擅长的情况。

---

# 24. 在 `List_ReverseOp` 打开 folder

修改：

```text
src/dialect/List.td
```

找到已有：

```tablegen
def List_ReverseOp : ...
```

加入：

```tablegen
let hasFolder = 1;
```

例如：

```tablegen
def List_ReverseOp : List_Op<"reverse"> {
  let summary = "Reverse a list";

  let arguments = (ins List_ListType:$input);
  let results = (outs List_ListType:$result);

  // 本讲新增。
  let hasFolder = 1;
}
```

重新：

```bash
make tblgen
```

生成的 `ReverseOp` 会声明：

```cpp
OpFoldResult fold(FoldAdaptor adaptor);
```

---

# 25. 在 `List.cpp` 实现第一个 `fold()`

加入：

```cpp
mlir::OpFoldResult
mlir::list::ReverseOp::fold(
    FoldAdaptor adaptor) {

  if (auto producer =
          getInput()
              .getDefiningOp<
                  mlir::list::ReverseOp>()) {

    return producer.getInput();
  }

  return {};
}
```

先不要关注：

```cpp
FoldAdaptor adaptor
```

为什么没有使用。

这个例子是在：

```text
检查 IR 结构
```

而不是：

```text
检查 constant operand
```

这是允许的。

---

# 26. `return producer.getInput()` 到底意味着什么？

原始：

```mlir
%a = "list.reverse"(%l)
     : (!list.list<i64>)
    -> !list.list<i64>

%b = "list.reverse"(%a)
     : (!list.list<i64>)
    -> !list.list<i64>
```

当 canonicalizer 尝试 fold 第二个 reverse：

```cpp
getInput()
```

得到：

```text
%a
```

再：

```cpp
getDefiningOp<ReverseOp>()
```

得到第一个 reverse。

然后：

```cpp
producer.getInput()
```

得到：

```text
%l
```

所以：

```cpp
return producer.getInput();
```

表示：

```text
“当前 ReverseOp 的 result
可以直接用这个已有 Value 替代。”
```

也就是：

```text
%b -> %l
```

之后如果：

```text
第一个 reverse 也变成 dead
```

canonicalizer/DCE 类 simplification 可能继续清理。

---

# 27. `OpFoldResult` 到底是什么？

课件给出的核心定义：

```cpp
OpFoldResult
```

本质可以持有：

```text
Value
或
Attribute
```

所以你可以把它理解成：

```text
OpFoldResult
├── Value
└── Attribute
```

当返回：

```cpp
Value
```

表示：

```text
当前 op 可以被一个已经存在的 SSA Value 替换
```

当返回：

```cpp
Attribute
```

表示：

```text
当前 op 的结果已经能够在编译期确定为一个 constant
```

后者还会引出：

```text
constant materialization
```

我们稍后再做。

---

# 28. 创建 `fold-structural.mlir`

新建：

```text
test/passes/core/fold-structural.mlir
```

内容：

```mlir
module {
  func.func @double_reverse(
      %l: !list.list<i64>)
      -> !list.list<i64> {

    %a = "list.reverse"(%l)
         : (!list.list<i64>)
        -> !list.list<i64>

    %b = "list.reverse"(%a)
         : (!list.list<i64>)
        -> !list.list<i64>

    return %b : !list.list<i64>
  }
}
```

运行：

```bash
build/bin/tinyir-opt \
  test/passes/core/fold-structural.mlir \
  --pass-pipeline='builtin.module(func.func(canonicalize))'
```

预期最终接近：

```mlir
func.func @double_reverse(
    %arg0: !list.list<i64>)
    -> !list.list<i64> {
  return %arg0 : !list.list<i64>
}
```

这一步建立了一个很重要的事实：

> `canonicalize` 不只运行 `getCanonicalizationPatterns()`；它也会使用 Operation 的 `fold()`。

---

# 29. Stage C：开始进入真正 Constant Folding

现在我们想实现课件练习：

```text
list.length
list.is_empty
list.reverse
```

对 constant list 做编译期计算。

例如：

```text
constant list = [10,20,30]
    ↓
list.length
    ↓
arith.constant 3
```

这要求编译器首先能够表达：

```text
“一个编译期已知的 list”
```

也就是：

```text
Attribute
```

---

# 30. 课件默认有 `ListAttr`，但当前 tinyIR 需要补这一层

课件在 constant-folding 部分直接使用：

```text
list::ListAttribute
list.constant
ListDialect::materializeConstant
```

这说明课件配套工程在这一阶段已经准备好了：

```text
ListAttr
ConstantOp
constant materializer
```

而我们前面的 tinyIR 教程为了控制复杂度，并没有提前引入自定义 Attribute。

所以这一讲需要补齐这块基础设施。

我们不跳过它，因为否则：

```text
OpFoldResult 返回 List constant Attribute
```

就无法完整落地。

---

# 31. 新的构建链：第一次为 List Dialect 生成 AttrDef

之前你已经有：

```text
List.td
  ↓ tblgen
List_Dialect.*
List_Types.*
List_Ops.*
```

现在增加：

```text
List.td
  ↓ tblgen
List_Attrs.h.inc
List_Attrs.cpp.inc
```

新的关系：

```text
List.td
├── Dialect
├── TypeDef
├── AttrDef     ← 新增
└── OpDef
```

---

# 32. 在 `List.td` 增加 Attribute 基类

确保顶部有：

```tablegen
include "mlir/IR/OpBase.td"
include "mlir/IR/AttrTypeBase.td"
```

然后在 `List_Dialect` 定义之后增加：

```tablegen
class List_Attr<
    string name,
    string mnemonic,
    list<Trait> traits = []>
    : AttrDef<
          List_Dialect,
          name,
          traits> {

  let mnemonic = mnemonic;
}
```

它和你之前写过的：

```tablegen
List_Type
List_Op
```

是同一种思路。

也就是：

```text
List_Op
    = List Dialect 中 Operation 的公共模板

List_Type
    = List Dialect 中 Type 的公共模板

List_Attr
    = List Dialect 中 Attribute 的公共模板
```

---

# 33. 定义最小 `List_ListAttr`

继续在：

```text
src/dialect/List.td
```

加入：

```tablegen
def List_ListAttr
    : List_Attr<"List", "list"> {

  let summary =
      "Compile-time constant list";

  // 为了让 tinyIR 的实现尽量简单，
  // ListAttr 内部使用一个 builtin ArrayAttr
  // 保存每个 constant element。
  let parameters = (ins
      "mlir::ArrayAttr":$elements);

  let assemblyFormat =
      "`<` $elements `>`";
}
```

这意味着：

```text
ListAttr
```

本身仍然是一个：

```text
MLIR Attribute
```

内部 payload 是：

```text
ArrayAttr
```

例如语义上：

```text
ListAttr {
    elements = [
        IntegerAttr(1),
        IntegerAttr(2),
        IntegerAttr(3)
    ]
}
```

文本形式大致可以打印成：

```mlir
#list.list<[1 : i64, 2 : i64, 3 : i64]>
```

不同 MLIR revision 的具体 pretty-print 细节可能略有不同，但结构是：

```text
#list.list< ArrayAttr >
```

---

# 34. 修改 Makefile：生成 AttrDef

你当前 `tblgen` 已经生成：

```text
Dialect
Ops
Types
```

现在为 `List.td` 再增加：

```bash
mlir-tblgen \
  -gen-attrdef-decls \
  ... \
  src/dialect/List.td \
  -o build/gen/List_Attrs.h.inc

mlir-tblgen \
  -gen-attrdef-defs \
  ... \
  src/dialect/List.td \
  -o build/gen/List_Attrs.cpp.inc
```

如果你当前 Makefile 使用统一变量，可以增加：

```make
$(GEN_DIR)/List_Attrs.h.inc: $(DIALECT_DIR)/List.td
	@mkdir -p $(GEN_DIR)
	@$(TBLGEN) \
		-gen-attrdef-decls \
		-I $(DIALECT_DIR) \
		-I $(MLIR_INCLUDE_DIR) \
		$< \
		-o $@

$(GEN_DIR)/List_Attrs.cpp.inc: $(DIALECT_DIR)/List.td
	@mkdir -p $(GEN_DIR)
	@$(TBLGEN) \
		-gen-attrdef-defs \
		-I $(DIALECT_DIR) \
		-I $(MLIR_INCLUDE_DIR) \
		$< \
		-o $@
```

如果你的 `tblgen` 使用 stamp target，则把这两条命令放进原来的：

```text
$(TBLGEN_STAMP)
```

配方即可。

目标是：

```bash
make tblgen
```

之后：

```text
build/gen/
├── List_Attrs.h.inc
├── List_Attrs.cpp.inc
├── List_Dialect.h.inc
├── List_Dialect.cpp.inc
├── List_Ops.h.inc
├── List_Ops.cpp.inc
├── List_Types.h.inc
└── List_Types.cpp.inc
```

---

# 35. 修改 `List.h`：暴露生成的 Attribute 类

在：

```text
src/dialect/List.h
```

确保包含：

```cpp
#include "mlir/IR/Attributes.h"
#include "mlir/IR/Dialect.h"
#include "mlir/IR/Types.h"
```

然后和 TypeDef / OpDef 类似加入：

```cpp
#define GET_ATTRDEF_CLASSES
#include "List_Attrs.h.inc"
```

一个典型顺序可以是：

```cpp
#ifndef TINYIR_LIST_H
#define TINYIR_LIST_H

#include "mlir/IR/Attributes.h"
#include "mlir/IR/Dialect.h"
#include "mlir/IR/Types.h"

#include "List_Dialect.h.inc"

#define GET_TYPEDEF_CLASSES
#include "List_Types.h.inc"

#define GET_ATTRDEF_CLASSES
#include "List_Attrs.h.inc"

#define GET_OP_CLASSES
#include "List_Ops.h.inc"

#endif
```

具体 include 相对路径仍以你当前：

```text
-I build/gen
```

的配置为准。

---

# 36. 修改 `List.cpp`：注册 Attribute

在：

```text
src/dialect/List.cpp
```

加入生成实现：

```cpp
#define GET_ATTRDEF_CLASSES
#include "List_Attrs.cpp.inc"
```

然后在：

```cpp
ListDialect::initialize()
```

中增加：

```cpp
addAttributes<
#define GET_ATTRDEF_LIST
#include "List_Attrs.cpp.inc"
>();
```

最终 initialize 的结构类似：

```cpp
void mlir::list::ListDialect::initialize() {

  addTypes<
#define GET_TYPEDEF_LIST
#include "List_Types.cpp.inc"
  >();

  addAttributes<
#define GET_ATTRDEF_LIST
#include "List_Attrs.cpp.inc"
  >();

  addOperations<
#define GET_OP_LIST
#include "List_Ops.cpp.inc"
  >();
}
```

注意：

```text
GET_ATTRDEF_CLASSES
```

负责：

```text
生成类实现
```

而：

```text
GET_ATTRDEF_LIST
```

用于：

```text
把这些 Attribute 注册到 Dialect
```

这和你之前 TypeDef / OpDef 的模式是一致的。

---

# 37. 先只验证 ListAttr 能生成

执行：

```bash
make tblgen
```

检查：

```bash
grep -n "class ListAttr" \
  build/gen/List_Attrs.h.inc
```

如果能找到：

```text
class ListAttr
```

说明：

```text
TableGen
    ↓
AttrDef
    ↓
C++ wrapper
```

这条链已经通。

再：

```bash
make build
```

确保：

```text
List.cpp
```

成功编译。

这一步还没有 constant folding。

我们的目标只是：

```text
先确保 Attribute infrastructure 独立正确
```

---

# 38. 定义 `list.constant`

有了：

```text
ListAttr
```

下一步需要一个真正产生：

```text
!list.list<...>
```

SSA Value 的 constant-like op。

修改：

```text
src/dialect/List.td
```

加入：

```tablegen
def List_ConstantOp
    : List_Op<
          "constant",
          [ConstantLike, Pure]> {

  let summary =
      "Materialize a compile-time constant list";

  // 这是 Attribute，不是 SSA operand。
  let arguments = (ins
      List_ListAttr:$value);

  let results = (outs
      List_ListType:$result);

  // constant op 自己也可以 fold：
  // fold 时直接返回其 constant Attribute。
  let hasFolder = 1;
}
```

这里两个 trait 要理解。

```text
ConstantLike
```

告诉 MLIR：

```text
这是一个 constant-like operation
```

而：

```text
Pure
```

表示：

```text
没有 memory side effect
并且可以安全视为纯计算
```

后面的 CSE 还会继续用到这个信息。

如果当前 `List.td` 没有 side-effect trait 定义可用，增加：

```tablegen
include "mlir/Interfaces/SideEffectInterfaces.td"
```

---

# 39. 为什么 `value` 写在 `arguments` 里，却不是 Value operand？

ODS 的：

```tablegen
let arguments = (ins ...)
```

不只可以描述：

```text
SSA operands
```

也可以描述：

```text
attributes / properties
```

这里：

```tablegen
List_ListAttr:$value
```

描述的是：

```text
operation 的 constant payload attribute
```

所以 `list.constant` 逻辑上是：

```text
0 SSA operands
1 ListAttr
1 SSA result
```

符合 constant-like operation 的典型结构。

---

# 40. 实现 `ConstantOp::fold`

修改：

```text
src/dialect/List.cpp
```

加入：

```cpp
mlir::OpFoldResult
mlir::list::ConstantOp::fold(
    FoldAdaptor adaptor) {

  return getValue();
}
```

含义非常直接：

```text
“如果有人问我这个 constant op 的 compile-time value 是什么，
就是我的 value Attribute。”
```

这里：

```cpp
getValue()
```

返回：

```text
ListAttr
```

它能够隐式构造成：

```text
OpFoldResult(Attribute)
```

---

# 41. 为什么还需要 `materializeConstant`？

这是 constant folding 最容易缺的一环。

假设：

```text
list.length(constant_list)
```

fold 计算得到：

```text
IntegerAttr(3)
```

但是 IR 中不能直接把：

```text
Attribute
```

接到：

```text
func.return
arith.addi
```

这些 SSA operand 上。

IR 需要的是：

```text
Value
```

所以：

```text
fold()
    ↓
返回 Attribute
    ↓
canonicalizer 知道“结果是 constant”
    ↓
Dialect::materializeConstant()
    ↓
创建一个 constant-like Operation
    ↓
得到 SSA Value
```

这条链要牢记：

```text
Attribute
≠
SSA Value
```

`materializeConstant` 就是：

```text
Attribute -> constant Operation -> Value
```

的桥梁。

---

# 42. 在 `List_Dialect` 打开 constant materializer

修改：

```text
src/dialect/List.td
```

在：

```tablegen
def List_Dialect : Dialect {
```

中增加：

```tablegen
let hasConstantMaterializer = 1;
```

课件还要求：

```text
List dialect 有些 op fold 后得到 Integer/Bool constant
```

这些 constant 最自然地由：

```text
arith.constant
```

物化。

所以增加：

```tablegen
let dependentDialects = [
  "::mlir::arith::ArithDialect"
];
```

大致：

```tablegen
def List_Dialect : Dialect {
  let name = "list";
  let cppNamespace = "::mlir::list";

  let hasConstantMaterializer = 1;

  let dependentDialects = [
    "::mlir::arith::ArithDialect"
  ];
}
```

---

# 43. 实现 `ListDialect::materializeConstant`

在：

```text
src/dialect/List.cpp
```

确保：

```cpp
#include "mlir/Dialect/Arith/IR/Arith.h"
```

然后实现：

```cpp
mlir::Operation *
mlir::list::ListDialect::materializeConstant(
    mlir::OpBuilder &builder,
    mlir::Attribute value,
    mlir::Type type,
    mlir::Location loc) {

  // ------------------------------------------------------------
  // Case 1:
  // fold 得到的是 ListAttr。
  // 那么创建我们自己的 list.constant。
  // ------------------------------------------------------------

  if (auto listValue =
          llvm::dyn_cast<
              mlir::list::ListAttr>(value)) {

    return mlir::list::ConstantOp::create(
        builder,
        loc,
        type,
        listValue);
  }

  // ------------------------------------------------------------
  // Case 2:
  // List operation 也可能产生 i1 / i32 等 builtin integer result。
  //
  // 例如：
  //
  //   list.is_empty -> i1
  //   list.length   -> i32
  //
  // 它们 fold 后分别可能返回：
  //
  //   BoolAttr
  //   IntegerAttr
  //
  // 这类 constant 交给 Arith Dialect 物化。
  // ------------------------------------------------------------

  return mlir::arith::ConstantOp::materialize(
      builder,
      value,
      type,
      loc);
}
```

这里用的是现代 MLIR `Dialect::materializeConstant` 完整签名：

```cpp
OpBuilder &
Attribute
Type
Location
```

---

# 44. materializer 的完整数据流

例如：

```text
list.length(%constantList)
```

假设常量 list：

```text
[10,20,30]
```

`LengthOp::fold()` 返回：

```text
IntegerAttr(3)
```

然后：

```text
ListDialect::materializeConstant
```

看到：

```text
value = IntegerAttr
```

不是：

```text
ListAttr
```

所以走：

```cpp
arith::ConstantOp::materialize(...)
```

最终 IR：

```mlir
%c3 = arith.constant 3 : i32
```

而如果：

```text
list.reverse(constant list)
```

fold 返回：

```text
ListAttr([30,20,10])
```

materializer 看到：

```text
ListAttr
```

于是创建：

```text
list.constant
```

这正好展示了：

```text
同一个 Dialect 的 fold
可以返回不同种类的 Attribute
```

而 materializer 负责把它们变成合适的 constant op。

---

# 45. FoldAdaptor：为什么不是直接看 `getInput()`？

现在终于进入：

```cpp
FoldAdaptor
```

假设 op：

```text
list.length %input
```

普通 accessor：

```cpp
getInput()
```

返回：

```text
Value
```

它回答：

```text
“运行时 SSA 数据流里，
我的 operand 接的是哪个 Value？”
```

但 constant folding 真正关心：

```text
“这个 Value 在编译期是否已经知道 constant Attribute？”
```

所以 ODS 为 fold 自动生成：

```text
FoldAdaptor
```

它像 operation operand 的一面“constant 镜子”。

可以粗略理解：

```text
Operation accessor:

getInput()
    ↓
Value


FoldAdaptor:

adaptor.getInput()
    ↓
Attribute or null
```

如果 operand 不是编译期 constant：

```text
adaptor.getInput() == null
```

如果 operand 是 constant：

```text
adaptor.getInput() == constant Attribute
```

---

# 46. 实现 `list.length` constant folding

首先在：

```text
List.td
```

找到：

```tablegen
List_LengthOp
```

增加：

```tablegen
let hasFolder = 1;
```

然后 `List.cpp`：

```cpp
mlir::OpFoldResult
mlir::list::LengthOp::fold(
    FoldAdaptor adaptor) {

  auto constant =
      llvm::dyn_cast_if_present<
          mlir::list::ListAttr>(
              adaptor.getInput());

  if (!constant)
    return {};

  int64_t length =
      constant.getElements().size();

  return mlir::IntegerAttr::get(
      getResult().getType(),
      length);
}
```

一步一步看。

---

# 47. `dyn_cast_if_present` 在这里解决什么？

```cpp
adaptor.getInput()
```

有两种情况：

```text
constant operand
    ↓
Attribute

non-constant operand
    ↓
null
```

我们又希望它必须是：

```text
ListAttr
```

所以：

```cpp
dyn_cast_if_present<ListAttr>(...)
```

同时做：

```text
null-safe
+
type-safe cast
```

如果不是 constant：

```text
constant == null
```

于是：

```cpp
return {};
```

表示：

```text
fold 失败
当前 op 保持原样
```

---

# 48. 为什么 LengthOp fold 返回 `IntegerAttr` 而不是创建 arith.constant？

因为：

```text
fold()
```

不应该随便拿 Builder 创建普通 Operation。

它只告诉 folder：

```text
“这个结果的 constant 值是 length”
```

即：

```cpp
return IntegerAttr(...)
```

至于：

```text
怎样在 IR 中表示这个 constant？
```

交给：

```text
materializeConstant
```

这体现了很清晰的职责分工：

```text
fold()
    =
计算 compile-time value

materializeConstant()
    =
把 Attribute 变成 constant-like Operation
```

---

# 49. 实现 `list.is_empty` constant folding

`List.td`：

```tablegen
let hasFolder = 1;
```

`List.cpp`：

```cpp
mlir::OpFoldResult
mlir::list::IsEmptyOp::fold(
    FoldAdaptor adaptor) {

  auto constant =
      llvm::dyn_cast_if_present<
          mlir::list::ListAttr>(
              adaptor.getInput());

  if (!constant)
    return {};

  bool isEmpty =
      constant.getElements().empty();

  return mlir::BoolAttr::get(
      getContext(),
      isEmpty);
}
```

数据流：

```text
ListAttr
    ↓
elements.empty()
    ↓
bool
    ↓
BoolAttr
    ↓
materializeConstant
    ↓
arith.constant true/false
```

---

# 50. 给 `list.reverse` 增加 constant folding

我们之前的：

```cpp
ReverseOp::fold(...)
```

只做：

```text
reverse(reverse(x))
    ↓
x
```

现在同一个 fold 可以同时处理：

```text
结构 simplification
+
constant folding
```

改成：

```cpp
mlir::OpFoldResult
mlir::list::ReverseOp::fold(
    FoldAdaptor adaptor) {

  // ============================================================
  // 规则 1：
  // reverse(reverse(x)) -> x
  //
  // 这是结构 simplification，
  // 不要求 operand 是 constant。
  // ============================================================

  if (auto producer =
          getInput()
              .getDefiningOp<
                  mlir::list::ReverseOp>()) {

    return producer.getInput();
  }

  // ============================================================
  // 规则 2：
  // reverse(constant-list)
  //   -> constant reversed-list
  // ============================================================

  auto constant =
      llvm::dyn_cast_if_present<
          mlir::list::ListAttr>(
              adaptor.getInput());

  if (!constant)
    return {};

  llvm::SmallVector<mlir::Attribute>
      reversed;

  for (mlir::Attribute attr :
       llvm::reverse(
           constant.getElements())) {
    reversed.push_back(attr);
  }

  mlir::ArrayAttr array =
      mlir::ArrayAttr::get(
          getContext(),
          reversed);

  return mlir::list::ListAttr::get(
      getContext(),
      array);
}
```

如果你的 LLVM revision 对：

```cpp
llvm::reverse(...)
```

range 的 include 有要求，加入：

```cpp
#include "llvm/ADT/STLExtras.h"
```

---

# 51. 创建 constant folding 测试

创建：

```text
test/passes/core/fold-constant.mlir
```

为了避免手写 custom `list.constant` syntax 与当前生成器存在差异，我们先使用 generic syntax。

假设 `ListAttr` 打印为：

```text
#list.list<[1 : i64, 2 : i64, 3 : i64]>
```

测试：

```mlir
module {
  func.func @length_of_constant() -> i32 {

    %l = "list.constant"()
         {value = #list.list<
             [1 : i64, 2 : i64, 3 : i64]>}
         : () -> !list.list<i64>

    %len = "list.length"(%l)
           : (!list.list<i64>) -> i32

    return %len : i32
  }


  func.func @empty_constant() -> i1 {

    %l = "list.constant"()
         {value = #list.list<[]>}
         : () -> !list.list<i64>

    %empty = "list.is_empty"(%l)
             : (!list.list<i64>) -> i1

    return %empty : i1
  }
}
```

> 如果你当前 AttrDef 的自动 printer 对 `ArrayAttr` 的文本形式与这里略有不同，先运行一个只包含 `list.constant` 的最小文件，让 `tinyir-opt` 打印一次 canonical textual form，再把测试输入改成 printer 实际输出的形式。不要为了匹配示例去改 fold 逻辑。

运行：

```bash
build/bin/tinyir-opt \
  test/passes/core/fold-constant.mlir \
  --pass-pipeline='builtin.module(func.func(canonicalize))'
```

预期语义：

```text
length([1,2,3])
    ↓
3

is_empty([])
    ↓
true
```

输出应出现相应：

```text
arith.constant
```

而原来的：

```text
list.length
list.is_empty
```

消失。

---

# 52. 从运行时角度看 constant fold 到底发生了什么

以：

```text
list.length(%l)
```

为例：

```text
canonicalize
    ↓
尝试 fold LengthOp
    ↓
FoldAdaptor 发现 %l 来自 constant-like op
    ↓
adaptor.getInput()
    =
ListAttr([1,2,3])
    ↓
LengthOp::fold
    ↓
IntegerAttr(3)
    ↓
canonicalizer 需要 SSA Value
    ↓
询问 ListDialect::materializeConstant
    ↓
不是 ListAttr
    ↓
转交 arith::ConstantOp::materialize
    ↓
%c3 = arith.constant 3 : i32
    ↓
替换原 LengthOp result
```

这条链是本讲最重要的流程之一。

---

# 53. fold 与 Pattern 的判断规则

到这里可以建立真正实用的选择顺序：

```text
我要做一个局部 simplification
        ↓
能不能用 fold 表达？
        │
        ├── 能
        │    ↓
        │   fold()
        │
        └── 不能
             ↓
        RewritePattern
```

典型：

```text
constant folding
    → fold

double reverse -> input
    → fold

需要创建多个普通 ops
    → RewritePattern

map fusion + clone Region
    → RewritePattern

map -> scf.while
    → RewritePattern / lowering
```

课件的结论可以压缩为：

> **fold when you can；需要 PatternRewriter 的能力时再使用 RewritePattern。**

---

# 54. Stage D：从局部规范化进入 CSE

到目前为止，我们的 simplification 都知道具体语义：

```text
ReverseOp::fold
知道 reverse 的数学/列表语义

MergeMapPattern
知道 map 的语义
```

现在课件引入一种完全不同的优化：

```text
Common Subexpression Elimination
```

简称：

```text
CSE
```

核心思想：

> 如果之前已经存在一个**等价 Operation**，而且它的 result 在这里可合法使用，那么不要重新计算，直接复用旧 result。

---

# 55. 最简单的 CSE 例子

原始：

```mlir
%a = arith.addi %x, %y : i32
%b = arith.addi %x, %y : i32
%r = arith.muli %a, %b : i32
```

如果两个 add 真正等价：

```text
%b
```

没有必要重新计算。

CSE 可以变成：

```mlir
%a = arith.addi %x, %y : i32
%r = arith.muli %a, %a : i32
```

第二个 `addi` 被删除。

注意：

```text
这不是 algebraic simplification
```

CSE 不需要理解：

```text
加法是什么
```

它只需要判断：

```text
“这两个 Operation 的表达式身份是否等价？”
```

---

# 56. CSE 用什么信息判断“等价”？

课件给出的 simplified key 包括：

```text
operation name
operands
attributes
result types
```

例如：

```text
name:
    arith.addi

operands:
    %x, %y

attributes:
    ...

result types:
    i32
```

通常：

```text
location
```

不用于表达式等价判断。

所以：

```text
同一个 op name
+
同样 operands
+
同样 attrs
+
同样 result types
```

才是候选公共子表达式。

但还不够。

还必须继续检查：

```text
side effects
dominance
```

---

# 57. 为什么 side effect 是 CSE 的生命线？

看：

```text
memref.alloc
```

即使两条：

```mlir
%a = memref.alloc() : memref<16xi32>
%b = memref.alloc() : memref<16xi32>
```

从：

```text
op name
operands
attributes
result type
```

看起来都一样。

但绝不能：

```text
%b -> %a
```

因为：

```text
两次 allocation
```

语义上就是两个不同对象。

也就是说：

```text
alloc 有 effect
```

所以不能按“表达式相同”合并。

---

# 58. CSE 为什么适合展示 MLIR 的抽象能力？

对于自定义 op：

```text
CSE 不需要专门写：
if (isa<ListReverseOp>) ...
if (isa<ListPushBackOp>) ...
```

它真正关心：

```text
这个 Operation 是否有 side effects？
```

所以：

```text
Dialect 作者
    ↓
正确声明 effect

通用 CSE pass
    ↓
自动工作
```

这是 MLIR interface/trait 设计非常重要的一面：

```text
Transformation
不一定依赖具体 op 类型

它可以依赖：
traits
interfaces
effects
```

---

# 59. 先给一个 List op 正确声明 `Pure`

上一讲最开始已经规定：

```text
List operations 使用 value semantics
不原地修改 list
```

对于真正没有 memory effects 的简单 list op，我们可以明确声明：

```tablegen
[Pure]
```

例如 `ReverseOp`：

```tablegen
def List_ReverseOp
    : List_Op<
          "reverse",
          [Pure]> {

  let arguments = (ins
      List_ListType:$input);

  let results = (outs
      List_ListType:$result);

  let hasFolder = 1;
}
```

同理：

```text
list.empty
list.length
list.is_empty
list.peek_front
list.pop_front
list.push_back
list.from_elements
list.constant
```

如果你的语义保证：

```text
没有隐藏 I/O
没有修改外部内存
没有 mutable state
```

都可以声明 pure。

---

# 60. 不要机械地给所有 op 加 `Pure`

这里特别注意：

```text
list.map
```

内部有 Region。

如果你的 Dialect 允许：

```mlir
list.map %l {
  ...
  某个有 side effect 的 operation
  ...
}
```

那么简单地给整个 `MapOp` 标：

```text
Pure
```

可能是不正确的。

所以：

```text
Pure
```

不是：

```text
“为了让 CSE 优化更多，所以随便加”
```

而是一项语义承诺：

> **这个 operation 真的没有 observable side effects。**

本讲为了安全实践 CSE：

```text
先用 ReverseOp
```

作为自定义 pure op 示例。

---

# 61. 创建 `cse-list.mlir`

新建：

```text
test/passes/core/cse-list.mlir
```

内容：

```mlir
module {
  func.func @duplicate_reverse(
      %l: !list.list<i64>)
      -> (!list.list<i64>,
          !list.list<i64>) {

    %a = "list.reverse"(%l)
         : (!list.list<i64>)
        -> !list.list<i64>

    %b = "list.reverse"(%l)
         : (!list.list<i64>)
        -> !list.list<i64>

    return %a, %b
        : !list.list<i64>,
          !list.list<i64>
  }
}
```

这里故意返回两个结果。

否则如果：

```text
%b 根本没人使用
```

你很难区分：

```text
是 CSE 合并了
还是其他 dead-code simplification 删除了
```

现在 `%a`、`%b` 都是 observable result。

---

# 62. 运行 CSE

执行：

```bash
build/bin/tinyir-opt \
  test/passes/core/cse-list.mlir \
  --pass-pipeline='builtin.module(func.func(cse))'
```

如果 `ReverseOp` 正确声明为：

```text
Pure
```

输出应接近：

```mlir
func.func @duplicate_reverse(
    %arg0: !list.list<i64>)
    -> (!list.list<i64>,
        !list.list<i64>) {

  %0 = "list.reverse"(%arg0)
       : (!list.list<i64>)
      -> !list.list<i64>

  return %0, %0
      : !list.list<i64>,
        !list.list<i64>
}
```

原来的：

```text
第二个 reverse
```

已经被消掉。

CSE 做的概念操作是：

```text
发现 %b operation 与 %a operation 等价
    ↓
%b 的所有 uses
    ↓ RAUW
%a
    ↓
erase 第二个 op
```

你之前学过的：

```text
RAUW + erase
```

再次出现，只不过这次由 MLIR 通用 Pass 自动完成。

---

# 63. CSE 的 simplified algorithm

课件给出的直觉算法可以写成：

```text
seen = {}

按顺序 walk operations:

    key = (
        op name,
        operands,
        attributes,
        result types
    )

    if key 已经出现：
        reuse old op results
        erase current op

    else:
        seen[key] = current op
```

如果只考虑 straight-line block：

```text
Time   O(n)
Memory O(n)
```

这是理解 CSE 的第一层心智模型。

真实 MLIR CSE 还要处理：

```text
CFG
dominance
side effects
memory barriers
```

后面马上进入。

---

# 64. Stage E：用 memref 看 side effect barrier

创建：

```text
test/passes/core/cse-effects.mlir
```

内容：

```mlir
module {
  func.func @load_effect(
      %m: memref<1xi32>,
      %newValue: i32)
      -> (i32, i32, i32) {

    %c0 = arith.constant 0 : index

    // ----------------------------------------------------------
    // 两次相同 load，中间没有 write。
    // CSE 有机会复用。
    // ----------------------------------------------------------

    %a = memref.load %m[%c0]
         : memref<1xi32>

    %b = memref.load %m[%c0]
         : memref<1xi32>

    // ----------------------------------------------------------
    // store 修改了同一片 memory。
    // ----------------------------------------------------------

    memref.store %newValue,
                 %m[%c0]
        : memref<1xi32>

    // ----------------------------------------------------------
    // 文本仍然是同样的 load，
    // 但它不能再复用 store 之前的值。
    // ----------------------------------------------------------

    %c = memref.load %m[%c0]
         : memref<1xi32>

    return %a, %b, %c
        : i32, i32, i32
  }
}
```

运行：

```bash
build/bin/tinyir-opt \
  test/passes/core/cse-effects.mlir \
  --pass-pipeline='builtin.module(func.func(cse))'
```

观察：

```text
第一、第二个 load
```

可能被合并。

但：

```text
store 后面的 load
```

必须保留。

原因：

```text
store
    ↓
改变 memory state
    ↓
后续 load 的值可能不同
```

所以：

```text
文本一样
≠
语义上可以 CSE
```

---

# 65. `Pure` 到底承诺了什么？

在这份教程里，可以先把 `Pure` 记成：

```text
这个 op：
    没有 memory effects
    并且可安全作为纯计算理解
```

于是 CSE 可以更大胆地：

```text
hash / compare / reuse
```

而像：

```text
memref.alloc
store
I/O
unknown-effect call
```

不能这样处理。

所以 side-effect declaration 不是：

```text
优化 hint
```

而是：

```text
IR semantics 的组成部分
```

声明错了会导致：

```text
优化器合法地按照你的错误承诺
生成语义错误的程序
```

---

# 66. Stage F：为什么 dominance 决定跨 Block CSE？

即使两个 Operation：

```text
完全等价
没有 side effect
```

也不代表一定能合并。

还要问：

> **旧 definition 的 result 能不能合法地在新位置使用？**

这就是：

```text
dominance
```

再次出现。

---

# 67. 创建 `cse-cfg.mlir`

新建：

```text
test/passes/core/cse-cfg.mlir
```

内容：

```mlir
module {
  func.func @branch_cse(
      %cond: i1,
      %x: i32)
      -> i32 {

    %c2 = arith.constant 2 : i32

    cf.cond_br %cond,
               ^bb1,
               ^bb2

  ^bb1:
    %a = arith.muli %x, %c2 : i32
    cf.br ^bb3

  ^bb2:
    %b = arith.muli %x, %c2 : i32
    cf.br ^bb3

  ^bb3:
    %c = arith.muli %x, %c2 : i32
    return %c : i32
  }
}
```

这里三个：

```text
arith.muli %x, %c2
```

从文本和 operands 看：

```text
完全一样
```

但 CFG：

```text
           entry
          /     \
       bb1       bb2
        |         |
        \        /
           bb3
```

---

# 68. 为什么 bb1 和 bb2 的 muli 不能互相复用？

`%a` 定义在：

```text
bb1
```

但执行路径可能是：

```text
entry -> bb2
```

这时：

```text
bb1 根本没有执行
```

所以 `%a` 在 bb2 中不存在可用定义。

也就是：

```text
bb1 不 dominate bb2
```

同理：

```text
bb2 不 dominate bb1
```

因此这两个不能简单 CSE。

---

# 69. 为什么 bb3 也不能直接复用 `%a` 或 `%b`？

进入 bb3 有两条路径：

```text
entry -> bb1 -> bb3
entry -> bb2 -> bb3
```

如果你把：

```text
%c
```

替换成：

```text
%a
```

那么走：

```text
entry -> bb2 -> bb3
```

时：

```text
%a 没有定义
```

所以：

```text
%a 不 dominate bb3
```

同理 `%b` 也不 dominate bb3。

因此：

```text
三个 muli 都可能保留
```

---

# 70. 什么时候跨 Block 可以复用？

如果等价 expression 在：

```text
entry block
```

定义：

```text
entry
  |
  | definition D
  |
  +------+
  |      |
 bb1    bb2
  |      |
  +------+
     |
    bb3
```

那么 entry 中的 D：

```text
dominate bb1
dominate bb2
dominate bb3
```

因此后面 block 的相同纯 expression 才可能复用它。

这和 `pass-advanced.md` 讲过的：

```text
Value 必须由 dominance 合法位置定义
```

完全是同一个规则。

---

# 71. canonicalize 和 cse 是什么关系？

它们不是谁替代谁。

## canonicalize

主要依赖：

```text
Operation 自己提供的：
fold()
canonicalization patterns
```

它知道：

```text
具体 op 语义
```

例如：

```text
reverse(reverse(x)) -> x
```

CSE 根本不懂这个语义。

---

## cse

主要利用：

```text
结构等价
side effects
dominance
```

它不需要理解：

```text
reverse 到底是什么意思
```

只要知道：

```text
两个 reverse expression 等价
而且 pure
而且 dominating
```

就可以复用。

所以实际 pipeline 中经常同时出现：

```text
canonicalize
cse
```

---

# 72. 组合运行 canonicalize + cse

可以：

```bash
build/bin/tinyir-opt \
  test/passes/core/canonicalize.mlir \
  --pass-pipeline='builtin.module(func.func(canonicalize,cse))'
```

流程：

```text
输入 IR
    ↓
canonicalize
    ├── fold
    ├── List canonicalization
    ├── Arith canonicalization
    └── 其他 Dialect canonicalization
    ↓
CSE
    └── 删除重复纯表达式
    ↓
输出 IR
```

有时还可以：

```text
canonicalize
↓
cse
↓
canonicalize
```

因为：

```text
CSE 删除结构
可能暴露新的 fold/canonicalization
```

但不要机械地无限串 Pass。

什么时候重复、重复几次，应由整个 compiler pipeline 的设计决定。

---

# 73. 为什么课件强调“性能不是 canonicalization 的首要判断标准”？

这是一个容易误解的点。

Canonicalization 的目标是：

```text
统一 IR 形式
减少冗余
提高后续 analysis / optimization 的稳定性
```

它并不是：

```text
“只要最终机器码更快，就一定应该放 canonicalize”
```

例如：

```text
list.map -> scf.while
```

也许最终代码性能很好。

但它仍然是：

```text
lowering
```

因为：

```text
高层信息丢失
```

所以判断：

```text
canonicalization or lowering
```

主要看：

```text
IR abstraction / information / canonical form
```

而不是直接看运行时间。

---

# 74. 把本讲的文件变化再整理一次

完成 canonicalization 迁移后：

```text
src/dialect/List.cpp
```

现在承担：

```text
Dialect initialization
Type/Attr/Op generated implementation
canonicalization Pattern 定义
getCanonicalizationPatterns()
fold()
materializeConstant()
```

而：

```text
src/passes/ListLowering.cpp
```

只承担：

```text
LowerFromElementsOneStep
LowerMapPattern
list-lowering Pass
```

这个边界非常重要：

```text
Dialect-side canonical knowledge
vs
Pipeline-specific lowering policy
```

---

# 75. 完成本讲后的目录树

建议最终：

```text
tinyIR/
├── .gitignore
├── Makefile
├── requirements.txt
├── src/
│   ├── dialect/
│   │   ├── List.cpp
│   │   ├── List.h
│   │   ├── List.td
│   │   ├── Tutorial.cpp
│   │   ├── Tutorial.h
│   │   └── Tutorial.td
│   │
│   ├── passes/
│   │   ├── Passes.h
│   │   ├── BuilderSafety.cpp
│   │   ├── CFGInspect.cpp
│   │   ├── InspectIR.cpp
│   │   ├── ListLowerFromElements.cpp
│   │   ├── ListLowering.cpp
│   │   ├── RewriterDemo.cpp
│   │   ├── SimplifyAdd.cpp
│   │   └── UseDef.cpp
│   │
│   └── tinyir-opt.cpp
│
└── test/
    └── passes/
        ├── advanced/
        ├── patterns/
        └── core/
            ├── canonicalize.mlir
            ├── fold-structural.mlir
            ├── fold-constant.mlir
            ├── cse-list.mlir
            ├── cse-effects.mlir
            └── cse-cfg.mlir
```

构建目录新增：

```text
build/gen/
├── List_Attrs.h.inc
└── List_Attrs.cpp.inc
```

其余仍遵循：

```text
*.td
↓ tblgen
*.inc
↓ C++
*.o
↓ ar
libtinyir.a
↓ ld
tinyir-opt
```

---

# 76. 从输入文件到输出文件：完整运行流程

以：

```bash
build/bin/tinyir-opt \
  test/passes/core/fold-constant.mlir \
  --pass-pipeline='builtin.module(func.func(canonicalize,cse))'
```

为例：

```text
fold-constant.mlir
    ↓ parser

builtin.module
└── func.func
    ├── list.constant
    ├── list.length
    └── ...

    ↓ PassManager

canonicalize
    │
    ├── 查询 op.fold()
    │
    ├── 查询 op.getCanonicalizationPatterns()
    │
    ├── LengthOp::fold()
    │       ↓
    │   IntegerAttr
    │       ↓
    │   ListDialect::materializeConstant()
    │       ↓
    │   arith.constant
    │
    └── 反复简化到 canonicalization fixed point

    ↓

cse
    │
    ├── 查 expression identity
    ├── 查 effects
    ├── 查 dominance
    └── RAUW + erase duplicate op

    ↓

MLIR printer

    ↓

stdout
```

注意：

```text
canonicalize.cpp
cse.cpp
```

都不是我们自己写的。

我们做的是：

```text
把 Dialect 语义信息补充完整
```

然后：

```text
MLIR core transformations 自动工作
```

---

# 77. 这一讲真正比上一讲多学了什么？

上一讲：

```text
“我会自己写 transformation。”
```

本讲：

```text
“我知道如何把 Dialect 的语义能力接入 MLIR 通用 transformation infrastructure。”
```

这是很重要的一次提升。

因为大型编译器不可能：

```text
每增加一个 Dialect
就重新给所有优化 Pass 写一遍专用版本
```

真正可扩展的方式是：

```text
Dialect
提供：
    canonicalization
    fold
    effects
    traits/interfaces

Core MLIR
提供：
    canonicalize
    CSE
    generic infrastructure
```

---

# 78. 本讲必须真正掌握的三层抽象

第一层：

```text
RewritePattern
```

你告诉 MLIR：

```text
“这一种 IR 结构可以怎样 rewrite。”
```

---

第二层：

```text
fold / canonicalization hooks
```

你告诉 MLIR：

```text
“我的 Operation 自己有哪些局部标准化知识。”
```

---

第三层：

```text
traits / effects / dominance
```

你告诉通用 transformation：

```text
“不要理解我的具体业务语义，
只根据抽象性质也可以安全优化。”
```

CSE 就属于第三层。

---

# 79. 一个很实用的 transformation 选择树

以后设计新优化，可以先问：

```text
我要做什么？
    |
    +-- 只是观察 IR？
    |      ↓
    |     walk
    |
    +-- 当前 op 可以直接被现有 Value
    |   或 constant Attribute 替代？
    |      ↓
    |     fold()
    |
    +-- 是局部 canonical rewrite，
    |   需要创建/删除普通 op？
    |      ↓
    |     RewritePattern
    |     + register as canonicalization
    |
    +-- 是明确 lowering，
    |   会降低 abstraction / 丢失高层信息？
    |      ↓
    |     lowering Pass / later Dialect Conversion
    |
    +-- 是通用冗余消除，
        只依赖 operation identity/effects/dominance？
           ↓
          CSE 等 core transformation
```

这个选择树比背 API 更重要。

---

# 80. 常见错误 1：把 lowering 注册进 canonicalize

例如把：

```text
LowerMapPattern
```

放进：

```cpp
MapOp::getCanonicalizationPatterns()
```

技术上可能能跑。

但设计上会造成：

```text
用户只想 canonicalize
    ↓
高层 map 突然全部消失
    ↓
SCF while 出现
    ↓
后续 map-level optimization 再也做不了
```

这就是：

```text
信息丢失
```

所以：

```text
能放进去
≠
应该放进去
```

---

# 81. 常见错误 2：fold 成功却没有真的简化

fold 的成功含义和 Pattern 类似：

```text
你确实提供了一个 replacement
```

如果不能 fold：

```cpp
return {};
```

不要：

```text
返回一个“看起来有效但实际等价于原 op”的结果
```

否则可能破坏 convergence。

---

# 82. 常见错误 3：在 fold 里偷偷做复杂非局部 mutation

例如：

```cpp
OpFoldResult MyOp::fold(...) {
  someOtherOp.erase();
  createThreeOps();
  moveBlock(...);
  ...
}
```

这违背 fold 的设计边界。

如果你的 transformation 需要：

```text
PatternRewriter
clone
复杂 create
erase unrelated op
```

应该使用：

```text
RewritePattern
```

---

# 83. 常见错误 4：Attribute 与 Value 再次混淆

这次你会同时看到：

```text
Value
Attribute
OpFoldResult
```

记住：

```text
Value
=
运行时/SSA 数据流中的节点

Attribute
=
编译期已知、不可变的 IR 元数据/常量表示

OpFoldResult
=
fold 的返回容器：
Value or Attribute
```

例如：

```text
%l
```

是：

```text
Value
```

而：

```text
#list.list<[1,2,3]>
```

是：

```text
Attribute
```

`list.constant` 把：

```text
Attribute
```

物化为：

```text
Value
```

---

# 84. 常见错误 5：把 CSE 理解成文本去重

CSE 绝对不是：

```text
两行文本长得一样
    ↓
删一行
```

还必须考虑：

```text
operands 是否是同一 SSA Value
attributes 是否相同
result types 是否相同
是否有 side effect
旧 definition 是否 dominate 新位置
memory state 是否被修改
```

所以：

```text
文本相同
```

只是最表面的直觉。

---

# 85. 推荐的实际练习顺序

不要一次把所有改动都贴进去。

建议严格按下面顺序跑。

```text
Step 1
给 FromElementsOp / MapOp 加 hasCanonicalizer
把两个 canonical patterns 移到 List.cpp
运行 canonicalize.mlir

目的：
理解 Pattern 的“归属”发生变化


Step 2
把 ListSimplify.cpp 改造成 ListLowering.cpp

目的：
把 canonicalization 与 lowering
在工程结构上真正分开


Step 3
给 ReverseOp 加 hasFolder
只实现 double reverse fold

目的：
先学 fold API，
暂时不碰 constant infrastructure


Step 4
增加 ListAttr AttrDef
生成 List_Attrs.*.inc

目的：
让编译器能表示 compile-time constant list


Step 5
增加 list.constant
实现 materializeConstant

目的：
打通 Attribute -> constant Operation -> Value


Step 6
实现 length / is_empty constant fold

目的：
真正使用 FoldAdaptor


Step 7
扩展 reverse constant fold

目的：
一个 fold 同时处理结构 simplification
和 constant folding


Step 8
给 ReverseOp 正确声明 Pure
运行 cse-list.mlir

目的：
观察通用 CSE 自动作用于自定义 Dialect


Step 9
运行 cse-effects.mlir

目的：
理解 effect barrier


Step 10
运行 cse-cfg.mlir

目的：
理解 dominance 对 CSE 的约束
```

每一步都遵守：

```text
改一个概念
↓
make build
↓
跑一个最小 .mlir
↓
看 before / after
↓
确认为什么变化
↓
再进入下一步
```

---

# 86. 本讲结束后的自测题

完成后，不看讲义回答：

```text
1. 为什么不应该继续靠：
   list-simplify -> arith-simplify -> list-simplify
   这种 Pass 串联做通用 canonicalization？

2. hasCanonicalizer = 1 到底生成了什么？
   它有没有自动生成 Pattern？

3. getCanonicalizationPatterns 是谁调用的？

4. MergeMapPattern 为什么适合 canonicalize，
   LowerMapPattern 为什么不适合？

5. ReverseOp::fold 返回 producer.getInput()
   时有没有创建新 Operation？

6. OpFoldResult 为什么能同时装 Value 和 Attribute？

7. FoldAdaptor::getInput()
   和 op.getInput()
   各自返回的概念是什么？

8. list.length fold 返回 IntegerAttr 后，
   为什么不能直接结束？
   materializeConstant 做了什么？

9. 为什么 CSE 可以优化一个它完全不了解语义的
   list.reverse？

10. 为什么给有 side effect 的 op 错误声明 Pure
    会造成 miscompile？

11. 为什么两个 branch 中完全相同的 muli
    不能简单合并？

12. canonicalize 和 cse 分别利用了哪类信息？
```

如果这些都能独立解释，本讲的核心已经掌握。

---

# 87. 课件与本讲章节映射

```text
课件 P2–P4
RewritePatterns should work together
不同 Dialect Pattern 互相暴露优化机会
        ↓
本讲 §1–§5、§20

课件 P5–P6
canonicalize
hasCanonicalizer
getCanonicalizationPatterns
        ↓
本讲 §6–§19

课件 P7–P9
什么是 canonicalization？
canonicalization vs lowering
Exercise 1
        ↓
本讲 §3–§15

课件 P10
RewritePattern 很强且较 verbose
locality 的价值
        ↓
本讲 §21–§22

课件 P11–P12
hasFolder
OpFoldResult
Value / Attribute replacement
        ↓
本讲 §23–§29

课件 P13–P15
constant representation
ConstantLike
materializeConstant
FoldAdaptor
        ↓
本讲 §30–§50

课件 P16
Exercise 2:
length / is_empty / reverse fold
        ↓
本讲 §46–§51

课件 P17
fold vs RewritePattern
Rule of least power
        ↓
本讲 §53、§79

课件 P18–P20
CSE
operation identity
effects
basic algorithm
        ↓
本讲 §54–§65

课件 P21–P22
upstream CSE
side-effect examples
        ↓
本讲 §64–§65

课件 P23
CSE + CFG + dominance
        ↓
本讲 §66–§70
```

---

# 88. 本阶段最核心 API 速查

```cpp
// ============================================================
// Canonicalization
// ============================================================

static void getCanonicalizationPatterns(
    mlir::RewritePatternSet &results,
    mlir::MLIRContext *context);

results.add<MyPattern>(context);


// ============================================================
// Folding
// ============================================================

mlir::OpFoldResult
MyOp::fold(FoldAdaptor adaptor);

return mlir::Value(...);
return mlir::Attribute(...);
return {};  // cannot fold


// ============================================================
// FoldAdaptor
// ============================================================

adaptor.getInput();

llvm::dyn_cast_if_present<MyAttr>(
    adaptor.getInput());


// ============================================================
// Constant materialization
// ============================================================

mlir::Operation *
MyDialect::materializeConstant(
    mlir::OpBuilder &builder,
    mlir::Attribute value,
    mlir::Type type,
    mlir::Location loc);


// ============================================================
// Built-in transformations
// ============================================================

canonicalize
cse
```

---

# 89. 最后把这一阶段压缩成一张图

```text
                         tinyIR Dialect
                              │
          ┌───────────────────┼────────────────────┐
          │                   │                    │
          ▼                   ▼                    ▼
 canonicalization          fold()             effects/traits
 patterns                    │                    │
          │                   │                    │
          │             ┌─────┴─────┐              │
          │             │           │              │
          │           Value      Attribute          │
          │                         │               │
          │                         ▼               │
          │              materializeConstant        │
          │                         │               │
          ▼                         ▼               ▼
     canonicalize              canonicalize         CSE
          │                         │               │
          └───────────────┬─────────┘               │
                          │                         │
                          ▼                         ▼
                    canonical IR           remove redundancy
                          │                         │
                          └──────────┬──────────────┘
                                     ▼
                              cleaner MLIR
```

这张图就是本讲真正应该留下的心智模型。

---

# 90. 下一阶段

这份课件结束后，你已经完成：

```text
Pass
↓
C++ IR API
↓
Pattern Rewrite
↓
Canonicalization
↓
Folding
↓
CSE
```

下一阶段真正自然的问题是：

```text
“如果我不只是想简化 IR，
而是要求某些高层 Operation / Dialect
必须彻底消失，
怎样系统地保证 lowering 完成？”
```

这时就会进入：

```text
Dialect Conversion
↓
ConversionTarget
↓
Legal / Illegal Dialect
↓
ConversionPattern
↓
TypeConverter
↓
applyPartialConversion
↓
applyFullConversion
```

到这里：

```text
pattern-rewrite.md
```

里的：

```text
“局部 rewrite”
```

会正式升级为：

```text
“有 legality 约束的系统 lowering”
```

这将是下一章最自然的主题。
