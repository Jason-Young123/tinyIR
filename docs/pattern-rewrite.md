# `pattern-rewrite.md`：tinyIR 的 Pattern-based Transformations 手把手教程

> 适用工程：当前 `tinyIR` 项目  
> 前置教程：已经完成 `pass.md` 与 `pass-advanced.md`，能够注册 `OperationPass<func::FuncOp>`，理解 `Operation / Region / Block / Value / OpOperand`、use-def、`OpBuilder`、`IRRewriter`、RAUW 与安全 erase。  
> 对照课件：**MLIR Fundamentals: Pattern-based Transformations**, Matthias Springer, ACM Europe School on MLIR 2026。  
> 本讲义目标：从“我会手工 walk + rewrite IR”继续走到“我会把局部变换写成可组合的 RewritePattern，并用 greedy driver 驱动到 fixed point”，同时在 tinyIR 中实现 `list.from_elements` lowering、`list.map` fusion 与 `list.map` lowering。

---

# 0. 先建立这一讲的整体框架

上一讲我们已经会：

```text
Pass
  ↓
runOnOperation()
  ↓
walk
  ↓
找到 Operation
  ↓
OpBuilder / IRRewriter
  ↓
replace / erase
```

这套方法完全能写变换，但当规则越来越多时，会出现三个问题：

```text
问题 1：每个 pass 都要自己写 walk / worklist / 判断
问题 2：规则之间可能互相触发，需要重复运行
问题 3：多个局部规则混在 runOnOperation() 中，越来越难维护
```

这一讲引入 MLIR 的 Pattern Rewrite 基础设施，把：

```text
“遍历 + 匹配 + 修改”
```

拆成：

```text
RewritePattern
    │
    ├── Match：这个 op 是否满足规则？
    │
    └── Rewrite：满足时怎样改写？
            ↓
RewritePatternSet
            ↓
Rewrite Driver
            ↓
反复应用直到 fixed point
```

本讲的学习路径不是从 API 名字出发，而是从一个完整编译流程逐步搭起来：

```text
Stage A：先理解为什么要分解 List Dialect 中的高层 op
    ↓
Stage B：不用 Pattern，手工 lower list.from_elements
    ↓
Stage C：把同一个规则改写成 OpRewritePattern
    ↓
Stage D：让 Greedy Rewrite Driver 自动反复执行规则
    ↓
Stage E：学习一个真正需要 Region cloning 的优化：合并两个 list.map
    ↓
Stage F：把 list.map lower 成 scf.while
    ↓
Stage G：理解 PatternBenefit、fixed point 与不同 rewrite driver
```

完成之后，你应该能解释：

```text
Pass 和 Pattern 的职责有什么区别？
matchAndRewrite() 为什么失败时绝对不能修改 IR？
PatternRewriter 为什么比普通 OpBuilder 更严格？
applyPatternsGreedily() 为什么会重复应用规则？
为什么 map fusion 应该比 map lowering 有更高 benefit？
为什么 clone Region 内部 IR 时需要 IRMapping？
```

---

# 1. 先把课件中的 List Dialect 看成“一层高层 IR”

课件首先回顾了一组 List Dialect operation：

```text
list.empty
list.is_empty
list.length
list.peek_front
list.pop_front
list.push_back
list.push_front
list.from_elements
list.get_elements
list.range
list.reverse
list.map
```

这层 IR 的意义不是规定底层一定使用 C++ `std::list` 或双向链表。

例如：

```mlir
%r = list.push_back %l, %a
```

只表达：

```text
“产生一个语义上等价于在 l 尾部加入 a 的新 list”
```

至于最后 lower 成：

```text
linked list
array
runtime call
SCF loop
LLVM memory operations
```

是后续 lowering 决定的。

课件还强调这组 List operation 使用 **value semantics**：

```text
输入 list 不被原地修改
操作产生新的 list Value
```

所以：

```mlir
%new = list.push_back %old, %a
```

不是：

```text
直接修改 %old
```

而更像：

```text
%new = f(%old, %a)
```

这与我们前面学过的 SSA Value 模型完全一致。

---

# 2. 从“高层 op”到“低层 op”：先理解 lowering 的含义

这一讲真正的起点不是 Pattern，而是：

> **一个高层 operation 可以由更基础的 operation 组合实现。**

例如：

```mlir
%r = list.from_elements %a, %b, %c
```

可以展开为：

```mlir
%0 = list.empty
%1 = list.push_back %0, %a
%2 = list.push_back %1, %b
%r = list.push_back %2, %c
```

这里发生的是：

```text
一个高层 op
    ↓
多个更基础 op
```

这就是一种 lowering。

再比如：

```mlir
%r = list.range %lb to %ub
```

可以概念上 lower 成：

```text
empty list
    ↓
scf.for
    ↓
每轮 push_back 当前 induction variable
```

而：

```mlir
%r = list.length %l
```

可以概念上 lower 成：

```text
count = 0
rem = l

while (!is_empty(rem)) {
    rem = pop_front(rem)
    count++
}
```

所以本讲不是“学习一种新的 C++ 写法”，而是在学习编译器中非常核心的一件事：

```text
把一种 IR 语义
逐步表达成另一组更基础的 IR 语义
```

---

# 3. 为什么 `list.map` 会自然引出优化？

单个：

```mlir
%r = list.map %l ...
```

如果 lower 成循环，大致需要：

```text
遍历 n 个元素
n 次 peek
n 次 pop
n 次 push
```

如果连续两个 map：

```mlir
%tmp = list.map %l {
  x -> x + 1
}

%r = list.map %tmp {
  x -> x * 2
}
```

直接分别 lower：

```text
第一遍遍历 n 个元素
+
第二遍遍历 n 个元素
```

总共两遍。

但它们可以先融合成：

```mlir
%r = list.map %l {
  x -> (x + 1) * 2
}
```

再 lower。

于是：

```text
两次 map
    ↓ fusion
一次 map
    ↓ lowering
一个循环
```

这就是后面 Pattern Benefit 要解决的问题：

> **如果“map fusion”和“map lowering”都能匹配，我们希望先 fusion，再 lowering。**

现在先把这个目标记住，后面再实现。

---

# 4. 本讲开始前的 tinyIR 工程结构

按照你当前工程，`src/passes/` 已经有类似：

```text
tinyIR/
├── Makefile
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
│   │   ├── CFGInspect.cpp
│   │   ├── InspectIR.cpp
│   │   ├── UseDef.cpp
│   │   └── ...
│   └── tinyir-opt.cpp
└── test/
    └── passes/
```

这一讲新增：

```text
src/passes/
├── ListLowerFromElements.cpp
└── ListSimplify.cpp

test/passes/patterns/
├── from-elements.mlir
├── simplify-from-elements.mlir
├── merge-map.mlir
└── lower-map.mlir
```

并修改：

```text
src/dialect/List.td
src/passes/Passes.h
src/tinyir-opt.cpp
```

你的 Makefile 已经使用：

```make
PASS_SRCS := $(wildcard $(PASS_DIR)/*.cpp)
```

所以新增 `.cpp` 后：

```text
不需要再手工往 Makefile 添加每个文件名
```

构建链仍然是：

```text
src/passes/ListSimplify.cpp
    ↓ CXX
build/obj/passes/ListSimplify.o
    ↓ AR
build/lib/libtinyir.a
    ↓ LD
build/bin/tinyir-opt
```

---

# 5. Stage A：先补齐这一讲需要的最小 List IR

## 5.1 为什么先补 Dialect，而不是马上写 Pattern？

Pattern 只能匹配：

```text
已经存在于 IR 中的 operation
```

所以如果当前 tinyIR 的 `List.td` 里还没有：

```text
list.empty
list.push_back
list.from_elements
list.map
...
```

我们必须先把这些“IR 词汇”定义出来。

这一阶段只做：

```text
“允许 parser / C++ typed API 认识这些 op”
```

暂时不实现执行语义，也不写 lowering。

---

## 5.2 修改 `src/dialect/List.td`

保留你原来的：

```text
List_Dialect
List_ListType
List_Op
已有的 list.reverse
```

然后在后面追加下面这组最小 operation 定义：

```tablegen
//===----------------------------------------------------------------------===//
// Operations used by pattern-rewrite.md
//===----------------------------------------------------------------------===//

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
  let results = (outs List_ListType:$result);
  let regions = (region AnyRegion:$body);
}

def List_YieldOp : List_Op<"yield"> {
  let summary = "Yield one mapped element";
  let arguments = (ins AnyInteger:$value);
}
```

### 为什么这里暂时不写 `assemblyFormat`？

这是本讲一个刻意的教学选择。

你现在要学的是：

```text
Pattern
Region cloning
Greedy rewrite
Lowering
```

如果同时加入：

```text
custom parser
custom printer
复杂 assemblyFormat
```

会把注意力分散。

因此这一讲的测试文件优先使用 MLIR 的 **generic operation syntax**：

```mlir
"list.from_elements"(...)
"list.map"(...)
```

虽然看起来没有课件里的 custom syntax 漂亮，但：

```text
它们在 IR 数据结构上完全是同一种 operation
typed C++ accessor 也照常生成
```

等 Pattern 学完，再给这些 op 加 custom assembly format 更合适。

---

# 6. 重新生成 `.inc`，确认 typed API 已经产生

执行：

```bash
make tblgen
```

或者你的正常构建：

```bash
make build
```

应该重新生成：

```text
build/gen/List_Ops.h.inc
build/gen/List_Ops.cpp.inc
```

检查：

```bash
grep -n "class FromElementsOp" build/gen/List_Ops.h.inc
grep -n "class MapOp"          build/gen/List_Ops.h.inc
grep -n "class YieldOp"        build/gen/List_Ops.h.inc
```

你应该能看到生成的：

```text
mlir::list::FromElementsOp
mlir::list::MapOp
mlir::list::YieldOp
```

从这一刻开始：

```text
TableGen record:
    List_FromElementsOp

C++ typed wrapper:
    mlir::list::FromElementsOp

IR operation name:
    list.from_elements
```

三者要对应起来。

---

# 7. 先准备最小测试：`from-elements.mlir`

创建：

```text
test/passes/patterns/from-elements.mlir
```

内容：

```mlir
module {
  func.func @test(%a: i64, %b: i64, %c: i64)
      -> !list.list<i64> {
    %r = "list.from_elements"(%a, %b, %c)
         : (i64, i64, i64) -> !list.list<i64>

    return %r : !list.list<i64>
  }
}
```

先不运行任何 pass：

```bash
make pass \
  TEST=./test/passes/patterns/from-elements.mlir
```

或者直接：

```bash
build/bin/tinyir-opt \
  test/passes/patterns/from-elements.mlir
```

这一轮只验证：

```text
parser
  ↓
List dialect registration
  ↓
FromElementsOp definition
  ↓
printer
```

全部正常。

如果报：

```text
unknown operation 'list.from_elements'
```

先不要写 Pattern。

优先检查：

```text
List.td 是否重新 tblgen
List.cpp 是否 include 新生成的 Ops.cpp.inc
ListDialect::initialize() 是否 addOperations<...>()
tinyir-opt 是否注册 ListDialect
```

---

# 8. Stage B：先不用 Pattern，手工完成第一次 lowering

现在我们先沿用上一讲已经掌握的方式：

```text
Pass
  ↓
walk
  ↓
IRRewriter / OpBuilder
  ↓
replace
```

这样可以明确看到：

> Pattern 基础设施究竟替我们省掉了什么。

---

# 9. 新增 `ListLowerFromElements.cpp`

创建：

```text
src/passes/ListLowerFromElements.cpp
```

完整内容：

```cpp
#include "passes/Passes.h"

#include "dialect/List.h"

#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/Builders.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Pass/PassRegistry.h"

#include "llvm/ADT/SmallVector.h"

namespace {

struct ListLowerFromElementsPass
    : mlir::PassWrapper<
          ListLowerFromElementsPass,
          mlir::OperationPass<mlir::func::FuncOp>> {

  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(
      ListLowerFromElementsPass)

  mlir::StringRef getArgument() const override {
    return "list-lower-from-elements";
  }

  mlir::StringRef getDescription() const override {
    return "Lower list.from_elements to list.empty + list.push_back";
  }

  void runOnOperation() override {
    mlir::func::FuncOp func = getOperation();

    llvm::SmallVector<mlir::list::FromElementsOp> worklist;

    // 第一阶段只找目标，不修改 IR。
    func.walk([&](mlir::list::FromElementsOp op) {
      worklist.push_back(op);
    });

    // 第二阶段集中改写。
    for (mlir::list::FromElementsOp op : worklist) {
      mlir::OpBuilder builder(func.getContext());
      builder.setInsertionPoint(op);

      mlir::Location loc = op.getLoc();
      mlir::Type listType = op.getResult().getType();

      auto empty =
          mlir::list::EmptyOp::create(
              builder,
              loc,
              listType);

      mlir::Value current = empty.getResult();

      for (mlir::Value element : op.getElements()) {
        auto pushed =
            mlir::list::PushBackOp::create(
                builder,
                loc,
                listType,
                current,
                element);

        current = pushed.getResult();
      }

      op.getResult().replaceAllUsesWith(current);
      op->erase();
    }
  }
};

} // namespace

void tinyir::registerListLowerFromElementsPass() {
  mlir::PassRegistration<ListLowerFromElementsPass>();
}
```

---

# 10. 先读懂这份手工 lowering

原始：

```mlir
%r = list.from_elements %a, %b, %c
```

C++：

```cpp
auto empty = EmptyOp::create(...);
mlir::Value current = empty.getResult();
```

对应：

```mlir
%0 = list.empty
```

第一次循环：

```cpp
current = push_back(current, %a)
```

对应：

```mlir
%1 = list.push_back %0, %a
```

第二次：

```mlir
%2 = list.push_back %1, %b
```

第三次：

```mlir
%3 = list.push_back %2, %c
```

最后：

```cpp
op.getResult().replaceAllUsesWith(current);
op->erase();
```

等价于：

```text
原 %r 的 users
    ↓
全部改用 %3
    ↓
删除原 list.from_elements
```

这里所有知识你都已经在 `pass-advanced.md` 学过。

所以这个 pass 是一座桥：

```text
旧知识：walk + RAUW + erase
    ↓
下一步：把它封装成 Pattern
```

---

# 11. 注册手工 lowering pass

修改：

```text
src/passes/Passes.h
```

增加：

```cpp
void registerListLowerFromElementsPass();
```

例如：

```cpp
namespace tinyir {

void registerInspectIRPass();
void registerUseDefPass();
void registerCFGInspectPass();

void registerListLowerFromElementsPass();

} // namespace tinyir
```

再修改：

```text
src/tinyir-opt.cpp
```

增加：

```cpp
tinyir::registerListLowerFromElementsPass();
```

重新构建：

```bash
make build
```

确认：

```bash
build/bin/tinyir-opt --help \
  | grep list-lower-from-elements
```

---

# 12. 运行第一次 lowering

执行：

```bash
make pass \
  TEST=./test/passes/patterns/from-elements.mlir \
  PASS=list-lower-from-elements
```

或者：

```bash
build/bin/tinyir-opt \
  test/passes/patterns/from-elements.mlir \
  --pass-pipeline='builtin.module(func.func(list-lower-from-elements))'
```

输出语义上应该从：

```mlir
%r = "list.from_elements"(%a, %b, %c)
```

变成：

```text
list.empty
   ↓
list.push_back(..., %a)
   ↓
list.push_back(..., %b)
   ↓
list.push_back(..., %c)
```

这一步对应课件的第一个 coding exercise。

---

# 13. Stage C：现在才正式引入 Rewrite Pattern

手工 pass 的核心代码可以抽象成：

```text
找到一个 FromElementsOp
    ↓
判断是否满足某个条件
    ↓
改写
```

RewritePattern 就是把这一个“局部规则”独立出来。

MLIR 中最适合 typed op 的形式：

```cpp
mlir::OpRewritePattern<OpType>
```

比如：

```cpp
mlir::OpRewritePattern<mlir::list::FromElementsOp>
```

意思是：

> 这个 Pattern 的候选根节点就是 `list.from_elements`。

于是你不再需要：

```cpp
func.walk([&](FromElementsOp op) {
    ...
});
```

driver 会替你找到候选 op。

---

# 14. 第一条 Pattern：空 `from_elements` → `empty`

先只解决最简单情况：

```mlir
%r = list.from_elements
```

没有任何 element。

语义上等价于：

```mlir
%r = list.empty
```

这正好适合学习 `matchAndRewrite()`。

---

# 15. 新增 `ListSimplify.cpp`

创建：

```text
src/passes/ListSimplify.cpp
```

先写第一版：

```cpp
#include "passes/Passes.h"

#include "dialect/List.h"

#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Pass/PassRegistry.h"
#include "mlir/Transforms/GreedyPatternRewriteDriver.h"

namespace {

struct ReplaceEmptyFromElements
    : mlir::OpRewritePattern<
          mlir::list::FromElementsOp> {

  using OpRewritePattern::OpRewritePattern;

  mlir::LogicalResult matchAndRewrite(
      mlir::list::FromElementsOp op,
      mlir::PatternRewriter &rewriter) const override {

    // Match 阶段：
    // 只接受没有任何 element 的 from_elements。
    if (!op.getElements().empty()) {
      return rewriter.notifyMatchFailure(
          op,
          "list has elements");
    }

    // Rewrite 阶段：
    // 创建新的 list.empty，
    // 用其 result 替代旧 op 的 result，
    // 并删除旧 op。
    rewriter.replaceOpWithNewOp<
        mlir::list::EmptyOp>(
            op,
            op.getResult().getType());

    return mlir::success();
  }
};

struct ListSimplifyPass
    : mlir::PassWrapper<
          ListSimplifyPass,
          mlir::OperationPass<mlir::func::FuncOp>> {

  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(
      ListSimplifyPass)

  mlir::StringRef getArgument() const override {
    return "list-simplify";
  }

  mlir::StringRef getDescription() const override {
    return "Apply pattern-based transformations to List dialect";
  }

  void runOnOperation() override {
    mlir::RewritePatternSet patterns(&getContext());

    patterns.add<ReplaceEmptyFromElements>(
        patterns.getContext());

    if (mlir::failed(
            mlir::applyPatternsGreedily(
                getOperation(),
                std::move(patterns)))) {
      signalPassFailure();
    }
  }
};

} // namespace

void tinyir::registerListSimplifyPass() {
  mlir::PassRegistration<ListSimplifyPass>();
}
```

---

# 16. `matchAndRewrite()` 的契约

这是本讲最重要的规则之一。

函数：

```cpp
LogicalResult matchAndRewrite(
    FromElementsOp op,
    PatternRewriter &rewriter)
```

有两种结果：

```text
failure
    =
“我没有匹配这个 op”

success
    =
“我匹配了，并且已经实际修改 IR”
```

所以：

```cpp
if (!op.getElements().empty())
  return failure();
```

之前：

```text
绝对不能修改 IR
```

也就是说这种逻辑是错误的：

```cpp
createSomething();

if (laterCheckFailed)
  return failure();   // 错
```

因为返回 `failure` 后，rewrite driver 会认为：

```text
这个 pattern 没有动过 IR
```

但你实际上已经改了。

正确结构：

```text
先做所有 match / legality 检查
    ↓
确认一定要改
    ↓
再开始 rewrite
    ↓
return success
```

---

# 17. 为什么所有修改都要通过 `PatternRewriter`？

上一讲你可以：

```cpp
op->erase();
value.replaceAllUsesWith(...);
```

但进入 Pattern infrastructure 后，优先使用：

```cpp
rewriter.eraseOp(...)
rewriter.replaceOp(...)
rewriter.replaceOpWithNewOp(...)
rewriter.clone(...)
```

原因是 rewrite driver 需要知道：

```text
哪些 op 被新增
哪些 op 被删除
哪些 use 被替换
哪些 op 需要重新加入 worklist
```

所以：

```text
PatternRewriter
=
OpBuilder
+
“让 rewrite infrastructure 感知 mutation”
```

这是为什么：

```cpp
rewriter.replaceOpWithNewOp<EmptyOp>(...)
```

不只是一个“少写两行”的 convenience API。

---

# 18. `replaceOpWithNewOp` 实际完成了什么？

这一句：

```cpp
rewriter.replaceOpWithNewOp<
    mlir::list::EmptyOp>(
        op,
        op.getResult().getType());
```

概念上组合了：

```text
1. 在合适位置创建 EmptyOp
2. 把旧 FromElementsOp result 的 uses
   全部改到新 EmptyOp result
3. 删除旧 FromElementsOp
```

也就是上一讲手工写过的：

```text
create
+
RAUW
+
erase
```

现在被统一封装成一次 rewrite。

---

# 19. 准备空列表 Pattern 测试

创建：

```text
test/passes/patterns/simplify-from-elements.mlir
```

内容：

```mlir
module {
  func.func @empty_case() -> !list.list<i64> {
    %r = "list.from_elements"() : () -> !list.list<i64>
    return %r : !list.list<i64>
  }

  func.func @non_empty_case(%a: i64, %b: i64) -> !list.list<i64> {
    %r = "list.from_elements"(%a, %b) : (i64, i64) -> !list.list<i64>
    return %r : !list.list<i64>
  }
}
```

注册：

```cpp
// Passes.h
void registerListSimplifyPass();
```

以及：

```cpp
// tinyir-opt.cpp
tinyir::registerListSimplifyPass();
```

构建：

```bash
make build
```

运行：

```bash
make pass \
  TEST=./test/passes/patterns/simplify-from-elements.mlir \
  PASS=list-simplify
```

第一版 Pattern 只应该改：

```text
@empty_case
```

而：

```text
@non_empty_case
```

暂时保持不变。

这一步非常重要，因为它证明：

```text
driver 找到了候选 op
    ↓
pattern 自己决定 match / failure
```

---

# 20. Stage D：从“一条 Pattern”进入“Pattern 组合”

现在我们希望任何：

```mlir
list.from_elements %a, %b, %c
```

最终都消失。

课件给出一个很适合 greedy rewrite 的思路：

```text
from_elements(a,b,c)
    ↓
from_elements(a,b) + push_back(c)
    ↓
from_elements(a) + push_back(b) + push_back(c)
    ↓
from_elements() + push_back(a) + push_back(b) + push_back(c)
    ↓
empty + push_back(a) + push_back(b) + push_back(c)
```

注意这里并不是“一次 Pattern 直接生成最终所有 op”。

我们故意让一次 rewrite 只做一步：

```text
N elements
    ↓
N-1 elements + push_back(last)
```

为什么？

因为这样能直接展示：

```text
applyPatternsGreedily()
```

如何反复应用 Pattern 直到 fixed point。

---

# 21. 增加第二条 Pattern：每次剥掉最后一个 element

在 `ListSimplify.cpp` 中、`ReplaceEmptyFromElements` 后增加：

```cpp
struct LowerFromElementsOneStep
    : mlir::OpRewritePattern<
          mlir::list::FromElementsOp> {

  using OpRewritePattern::OpRewritePattern;

  mlir::LogicalResult matchAndRewrite(
      mlir::list::FromElementsOp op,
      mlir::PatternRewriter &rewriter) const override {

    mlir::ValueRange elements = op.getElements();

    // 空列表由 ReplaceEmptyFromElements 负责。
    if (elements.empty())
      return mlir::failure();

    mlir::Type listType =
        op.getResult().getType();

    mlir::ValueRange prefix =
        elements.drop_back();

    mlir::Value last =
        elements.back();

    auto shorter =
        mlir::list::FromElementsOp::create(
            rewriter,
            op.getLoc(),
            listType,
            prefix);

    auto pushed =
        mlir::list::PushBackOp::create(
            rewriter,
            op.getLoc(),
            listType,
            shorter.getResult(),
            last);

    rewriter.replaceOp(
        op,
        pushed.getResult());

    return mlir::success();
  }
};
```

然后在：

```cpp
runOnOperation()
```

中改为：

```cpp
patterns.add<
    ReplaceEmptyFromElements,
    LowerFromElementsOneStep>(
        patterns.getContext());
```

---

# 22. 现在理解 `applyPatternsGreedily()` 的 fixed point

输入：

```text
from_elements(a,b,c)
```

第一次：

```text
LowerFromElementsOneStep
    ↓
from_elements(a,b)
+
push_back(...,c)
```

driver 发现：

```text
新产生的 from_elements(a,b)
```

仍然可能匹配 Pattern。

第二次：

```text
from_elements(a)
+
push_back(...,b)
+
push_back(...,c)
```

第三次：

```text
from_elements()
+
push_back(...,a)
+
push_back(...,b)
+
push_back(...,c)
```

然后：

```text
ReplaceEmptyFromElements
```

匹配：

```text
from_elements()
```

最终：

```text
empty
+
push_back(a)
+
push_back(b)
+
push_back(c)
```

此时已经没有任何 Pattern 能继续修改 IR。

这就是：

```text
fixed point
```

即：

> 再跑一轮，也没有任何变化。

---

# 23. 再次运行 `list-simplify`

执行：

```bash
make pass \
  TEST=./test/passes/patterns/from-elements.mlir \
  PASS=list-simplify
```

如果你的 Makefile 已经加入：

```make
-mlir-print-ir-before-all
-mlir-print-ir-after-all
```

可以直接观察：

```text
Before list-simplify
After list-simplify
```

对比。

如果想进一步看 greedy driver：

```bash
build/bin/tinyir-opt \
  test/passes/patterns/from-elements.mlir \
  --pass-pipeline='builtin.module(func.func(list-simplify))' \
  -debug
```

---

# 24. 到这里，Pass 和 Pattern 的职责已经分开

现在：

```text
ListSimplifyPass
```

只负责：

```text
准备 PatternSet
选择 rewrite driver
处理 driver failure
```

真正的局部规则：

```text
ReplaceEmptyFromElements
LowerFromElementsOneStep
```

各自负责：

```text
match + rewrite
```

形成：

```text
Pass
├── Pattern A
├── Pattern B
├── Pattern C
└── Pattern Driver
```

以后新增规则，不需要不断扩大一个巨大的：

```cpp
runOnOperation()
```

---

# 25. Stage E：开始处理带 Region 的 `list.map`

前面的 `from_elements` 没有 Region。

而：

```mlir
list.map
```

内部自己包含一段 IR：

```text
list.map
└── Region
    └── Block
        ├── BlockArgument %elem
        ├── arith.addi ...
        └── list.yield
```

因此 map fusion 不只是：

```text
替换几个 operand
```

而是要：

```text
把两个 Region 中的 IR 合并到一个新 Region
```

这正好连接到 `pass-advanced.md` 学过的：

```text
Region / Block
BlockArgument
clone
IRMapping
```

---

# 26. 创建两个连续 map 的测试文件

创建：

```text
test/passes/patterns/merge-map.mlir
```

内容使用 generic syntax：

```mlir
module {
  func.func @two_maps(
      %l: !list.list<i64>,
      %c1: i64,
      %c2: i64)
      -> !list.list<i64> {

    %tmp = "list.map"(%l) ({
    ^bb0(%elem: i64):
      %g = arith.addi %elem, %c1 : i64
      "list.yield"(%g) : (i64) -> ()
    }) : (!list.list<i64>)
       -> !list.list<i64>

    %r = "list.map"(%tmp) ({
    ^bb0(%elem: i64):
      %g = arith.muli %elem, %c2 : i64
      "list.yield"(%g) : (i64) -> ()
    }) : (!list.list<i64>)
       -> !list.list<i64>

    return %r : !list.list<i64>
  }
}
```

概念结构：

```text
map #1
input = %l
body:
    x -> x + c1
result = %tmp

map #2
input = %tmp
body:
    x -> x * c2
result = %r
```

目标：

```text
一个 map
input = %l
body:
    x
      ↓
    x + c1
      ↓
    * c2
      ↓
    yield
```

---

# 27. 为什么不能直接把两个 Block 拼起来？

第一个 map body：

```text
^bb0(%elem1)
  %g1 = add %elem1
  yield %g1
```

第二个：

```text
^bb0(%elem2)
  %g2 = mul %elem2
  yield %g2
```

如果直接 clone 第二个 body：

```text
%elem2
```

仍然是“第二个旧 Block 的 BlockArgument”。

但融合后我们真正需要：

```text
%elem2
    ↓
改成
第一个 body 的 yield 结果
```

也就是说 clone 的过程中必须维护：

```text
旧 Value
    ↓
新 Value
```

映射。

这就是：

```cpp
mlir::IRMapping
```

的作用。

---

# 28. 增加 `MergeMapPattern`

在 `ListSimplify.cpp` 中增加 include：

```cpp
#include "mlir/IR/IRMapping.h"
```

然后增加 Pattern：

```cpp
struct MergeMapPattern
    : mlir::OpRewritePattern<
          mlir::list::MapOp> {

  using OpRewritePattern::OpRewritePattern;

  mlir::LogicalResult matchAndRewrite(
      mlir::list::MapOp consumer,
      mlir::PatternRewriter &rewriter) const override {

    auto producer =
        consumer.getInput()
            .getDefiningOp<mlir::list::MapOp>();

    if (!producer) {
      return rewriter.notifyMatchFailure(
          consumer,
          "input is not produced by list.map");
    }

    mlir::Region &producerRegion =
        producer.getBody();

    mlir::Region &consumerRegion =
        consumer.getBody();

    if (!llvm::hasSingleElement(producerRegion) ||
        !llvm::hasSingleElement(consumerRegion)) {
      return rewriter.notifyMatchFailure(
          consumer,
          "expected single-block map regions");
    }

    mlir::Block &producerBlock =
        producerRegion.front();

    mlir::Block &consumerBlock =
        consumerRegion.front();

    if (producerBlock.getNumArguments() != 1 ||
        consumerBlock.getNumArguments() != 1) {
      return rewriter.notifyMatchFailure(
          consumer,
          "expected one block argument");
    }

    auto producerYield =
        llvm::dyn_cast<mlir::list::YieldOp>(
            producerBlock.getTerminator());

    auto consumerYield =
        llvm::dyn_cast<mlir::list::YieldOp>(
            consumerBlock.getTerminator());

    if (!producerYield || !consumerYield) {
      return rewriter.notifyMatchFailure(
          consumer,
          "expected list.yield terminators");
    }

    // ------------------------------------------------------------
    // 从这里开始才真正修改 IR。
    // ------------------------------------------------------------

    auto fused =
        mlir::list::MapOp::create(
            rewriter,
            consumer.getLoc(),
            consumer.getResult().getType(),
            producer.getInput());

    mlir::Region &fusedRegion =
        fused.getBody();

    mlir::Block *fusedBlock =
        rewriter.createBlock(
            &fusedRegion,
            fusedRegion.end(),
            {producerBlock.getArgument(0).getType()},
            {consumer.getLoc()});

    rewriter.setInsertionPointToEnd(fusedBlock);

    mlir::IRMapping mapping;

    // 新 map 的 block argument
    // 对应 producer 的 block argument。
    mapping.map(
        producerBlock.getArgument(0),
        fusedBlock->getArgument(0));

    // clone producer body，跳过旧 yield。
    for (mlir::Operation &nested :
         producerBlock.without_terminator()) {
      rewriter.clone(nested, mapping);
    }

    mlir::Value producerResult =
        mapping.lookup(producerYield.getValue());

    // consumer 的 block argument
    // 应该接 producer body 的输出。
    mapping.map(
        consumerBlock.getArgument(0),
        producerResult);

    // clone consumer body，仍然跳过旧 yield。
    for (mlir::Operation &nested :
         consumerBlock.without_terminator()) {
      rewriter.clone(nested, mapping);
    }

    mlir::Value finalResult =
        mapping.lookup(consumerYield.getValue());

    mlir::list::YieldOp::create(
        rewriter,
        consumer.getLoc(),
        finalResult);

    // consumer 的结果由 fused map 替代。
    rewriter.replaceOp(
        consumer,
        fused.getResult());

    // producer 可能还有其他 users。
    // 只有彻底 dead 时才能删除。
    if (producer.getResult().use_empty())
      rewriter.eraseOp(producer);

    return mlir::success();
  }
};
```

---

# 29. `IRMapping` 在这个例子里到底映射什么？

先看 producer：

```text
old producer block arg %elem1
    ↓
new fused block arg %elem
```

所以：

```cpp
mapping.map(
    producerBlock.getArgument(0),
    fusedBlock->getArgument(0));
```

clone：

```text
%g1 = add %elem1, %c1
```

以后：

```text
mapping
```

继续记录：

```text
old %g1
    ↓
new cloned %g1'
```

于是：

```cpp
mapping.lookup(producerYield.getValue())
```

得到：

```text
producer body 在新 fused block 中的结果
```

再把：

```text
consumer old block arg
```

映射到它：

```cpp
mapping.map(
    consumerBlock.getArgument(0),
    producerResult);
```

这样 clone consumer：

```text
mul %elem2, %c2
```

时：

```text
%elem2
```

自然被替换成：

```text
producerResult
```

因此两个 Region 串起来了。

---

# 30. 为什么 producer 不能无条件 erase？

考虑：

```mlir
%tmp = list.map %l ...
%a = list.map %tmp ...
%b = some.other_user %tmp
```

`%tmp` 不只有第二个 map 一个 user。

如果 fusion 后直接：

```cpp
rewriter.eraseOp(producer);
```

会把：

```text
%b
```

的输入定义一起删除。

所以课件特别提醒：

```text
%tmp may have users in addition to the second list.map
```

正确策略：

```cpp
if (producer.getResult().use_empty())
  rewriter.eraseOp(producer);
```

这里正好再次用到了上一讲的 use-def 知识。

---

# 31. 把 `MergeMapPattern` 加入 PatternSet

修改：

```cpp
patterns.add<
    ReplaceEmptyFromElements,
    LowerFromElementsOneStep>(
        patterns.getContext());
```

增加：

```cpp
patterns.add<MergeMapPattern>(
    patterns.getContext(),
    mlir::PatternBenefit(2));
```

暂时只关注：

```text
benefit = 2
```

它的意义下一阶段统一解释。

运行：

```bash
make pass \
  TEST=./test/passes/patterns/merge-map.mlir \
  PASS=list-simplify
```

目标是把两个 `list.map` 变成一个。

---

# 32. Stage F：把 `list.map` lower 成更基础控制流

现在我们已经完成：

```text
map fusion
```

下一步才做：

```text
map lowering
```

课件主方案使用：

```text
scf.while
```

核心状态有两个：

```text
rem
    =
还没有处理的输入 list

acc
    =
已经积累的输出 list
```

每一轮：

```text
is_empty(rem)?
    ↓ no
peek_front(rem)
    ↓
执行原 map body
    ↓
pop_front(rem)
    ↓
push_back(acc, mapped)
    ↓
下一轮
```

这正好和我们前面学过的 loop-carried variables 对应。

---

# 33. 准备 `lower-map.mlir`

创建：

```text
test/passes/patterns/lower-map.mlir
```

内容：

```mlir
module {
  func.func @map_once(
      %l: !list.list<i64>,
      %c1: i64)
      -> !list.list<i64> {

    %r = "list.map"(%l) ({
    ^bb0(%elem: i64):
      %g = arith.addi %elem, %c1 : i64
      "list.yield"(%g) : (i64) -> ()
    }) : (!list.list<i64>)
       -> !list.list<i64>

    return %r : !list.list<i64>
  }
}
```

---

# 34. 增加 LowerMapPattern

先在 `ListSimplify.cpp` 增加：

```cpp
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
```

然后加入：

```cpp
struct LowerMapPattern
    : mlir::OpRewritePattern<
          mlir::list::MapOp> {

  using OpRewritePattern::OpRewritePattern;

  mlir::LogicalResult matchAndRewrite(
      mlir::list::MapOp op,
      mlir::PatternRewriter &rewriter) const override {

    mlir::Region &mapRegion = op.getBody();

    if (!llvm::hasSingleElement(mapRegion))
      return mlir::failure();

    mlir::Block &mapBlock =
        mapRegion.front();

    if (mapBlock.getNumArguments() != 1)
      return mlir::failure();

    auto mapYield =
        llvm::dyn_cast<mlir::list::YieldOp>(
            mapBlock.getTerminator());

    if (!mapYield)
      return mlir::failure();

    mlir::Location loc = op.getLoc();
    mlir::Type listType =
        op.getResult().getType();

    // ------------------------------------------------------------
    // Rewrite starts here.
    // ------------------------------------------------------------

    rewriter.setInsertionPoint(op);

    auto empty =
        mlir::list::EmptyOp::create(
            rewriter,
            loc,
            listType);

    auto trueValue =
        mlir::arith::ConstantIntOp::create(
            rewriter,
            loc,
            1,
            1);

    auto whileOp =
        mlir::scf::WhileOp::create(
            rewriter,
            loc,
            mlir::TypeRange{
                listType,
                listType},
            mlir::ValueRange{
                op.getInput(),
                empty.getResult()});

    // ============================================================
    // before region
    //   (%rem, %acc)
    //       ↓
    //   is_empty(rem)
    //       ↓
    //   condition(!is_empty, rem, acc)
    // ============================================================

    mlir::Region &before =
        whileOp.getBefore();

    mlir::Block *beforeBlock =
        rewriter.createBlock(
            &before,
            before.end(),
            {listType, listType},
            {loc, loc});

    rewriter.setInsertionPointToEnd(beforeBlock);

    mlir::Value rem =
        beforeBlock->getArgument(0);

    mlir::Value acc =
        beforeBlock->getArgument(1);

    auto isEmpty =
        mlir::list::IsEmptyOp::create(
            rewriter,
            loc,
            rewriter.getI1Type(),
            rem);

    auto cond =
        mlir::arith::XOrIOp::create(
            rewriter,
            loc,
            isEmpty.getResult(),
            trueValue.getResult());

    mlir::scf::ConditionOp::create(
        rewriter,
        loc,
        cond.getResult(),
        mlir::ValueRange{rem, acc});

    // ============================================================
    // after region
    //   (%rem2, %acc2)
    //       ↓
    //   peek + pop
    //       ↓
    //   clone 原 map body
    //       ↓
    //   push_back
    //       ↓
    //   yield(newRem, newAcc)
    // ============================================================

    mlir::Region &after =
        whileOp.getAfter();

    mlir::Block *afterBlock =
        rewriter.createBlock(
            &after,
            after.end(),
            {listType, listType},
            {loc, loc});

    rewriter.setInsertionPointToEnd(afterBlock);

    mlir::Value rem2 =
        afterBlock->getArgument(0);

    mlir::Value acc2 =
        afterBlock->getArgument(1);

    mlir::Type elementType =
        mapBlock.getArgument(0).getType();

    auto element =
        mlir::list::PeekFrontOp::create(
            rewriter,
            loc,
            elementType,
            rem2);

    auto rest =
        mlir::list::PopFrontOp::create(
            rewriter,
            loc,
            listType,
            rem2);

    mlir::IRMapping mapping;

    mapping.map(
        mapBlock.getArgument(0),
        element.getResult());

    for (mlir::Operation &nested :
         mapBlock.without_terminator()) {
      rewriter.clone(nested, mapping);
    }

    mlir::Value mapped =
        mapping.lookup(mapYield.getValue());

    auto nextAcc =
        mlir::list::PushBackOp::create(
            rewriter,
            loc,
            listType,
            acc2,
            mapped);

    mlir::scf::YieldOp::create(
        rewriter,
        loc,
        mlir::ValueRange{
            rest.getResult(),
            nextAcc.getResult()});

    // while 返回两个结果：
    // result #0 = 最终 rem
    // result #1 = 最终 acc
    rewriter.replaceOp(
        op,
        whileOp.getResult(1));

    return mlir::success();
  }
};
```

> 注：不同 MLIR revision 的部分 generated builder overload 可能存在轻微签名差异。若你的 2026 wheel 对某个 `::create(...)` 参数报错，优先在 VS Code 中跳转对应 op 的 generated declaration，或查看 wheel 的 `*Ops.h.inc`；**不要改变这段 lowering 的 IR 结构与数据流逻辑。**

---

# 35. 把 LowerMapPattern 加进 PatternSet

现在：

```cpp
patterns.add<MergeMapPattern>(
    patterns.getContext(),
    mlir::PatternBenefit(2));
```

继续增加：

```cpp
patterns.add<LowerMapPattern>(
    patterns.getContext(),
    mlir::PatternBenefit(1));
```

于是同一个：

```text
list.map
```

可能同时匹配：

```text
MergeMapPattern
LowerMapPattern
```

这就正式引出：

```text
PatternBenefit
```

---

# 36. PatternBenefit：为什么 fusion 要比 lowering 优先？

对于：

```text
map(map(x))
```

如果先 LowerMap：

```text
外层 map
    ↓
scf.while
```

那么：

```text
MergeMapPattern
```

已经看不到“两层相邻 list.map”结构了。

fusion 机会丢失。

所以希望：

```text
MergeMapPattern
benefit = 2

LowerMapPattern
benefit = 1
```

让 rewrite driver 倾向于：

```text
先 fusion
再 lowering
```

整个优化顺序：

```text
map #1
   ↓
map #2

先 merge
   ↓

single map

再 lower
   ↓

scf.while
```

这正是课件最后一个 coding exercise 特别提醒：

```text
list.map merge pattern should have a higher benefit
```

的原因。

---

# 37. 运行 `lower-map.mlir`

先单个 map：

```bash
make pass \
  TEST=./test/passes/patterns/lower-map.mlir \
  PASS=list-simplify
```

最终 IR 中：

```text
list.map
```

应该消失，取而代之的是：

```text
list.empty
arith.constant true
scf.while
├── before region
│   ├── list.is_empty
│   ├── arith.xori
│   └── scf.condition
└── after region
    ├── list.peek_front
    ├── list.pop_front
    ├── 原 map body clone
    ├── list.push_back
    └── scf.yield
```

注意：

```text
list.map 被 lower 了
```

但：

```text
list.empty
list.is_empty
list.peek_front
list.pop_front
list.push_back
```

还存在。

这很正常。

编译器 lowering 通常不是：

```text
高层 IR
    ↓ 一步
LLVM IR
```

而是：

```text
高层语义
    ↓
较低层语义
    ↓
更低层语义
    ↓
最终目标
```

---

# 38. 对 `merge-map.mlir` 再运行一次

现在同一个 `list-simplify` 同时拥有：

```text
MergeMapPattern benefit 2
LowerMapPattern benefit 1
```

运行：

```bash
make pass \
  TEST=./test/passes/patterns/merge-map.mlir \
  PASS=list-simplify
```

预期思想：

```text
两个 map
    ↓
先 fusion
    ↓
一个 map
    ↓
再 lower
    ↓
一个 scf.while
```

而不是：

```text
两个 map
    ↓
两个 scf.while
```

这就是 pattern-based optimization 比简单“按文件顺序写几个 rewrite”更有价值的地方。

---

# 39. Greedy driver 为什么叫 Greedy？

当前：

```cpp
applyPatternsGreedily(...)
```

会：

```text
发现可匹配 op
    ↓
选择 pattern
    ↓
rewrite
    ↓
新产生/受影响的 op 重新进入候选
    ↓
继续
    ↓
直到 fixed point
```

所以它不是：

```text
只 walk IR 一次
```

而是在不断消费新的 rewrite 机会。

这对：

```text
from_elements(a,b,c)
```

特别明显，因为一次 rewrite 又产生新的：

```text
from_elements(a,b)
```

如果只 walk 一次，未必会继续处理新生成的 op。

---

# 40. 课件中的三类 rewrite driver

到这里再看课件最后的 driver 分类就非常自然了。

## 40.1 `walkAndApplyPatterns`

思想：

```text
walk IR 一次
对每个 op 尝试 patterns
```

适合：

```text
明确只需要单次扫描
不依赖新 rewrite 再触发 rewrite
```

## 40.2 `applyPatternsGreedily`

思想：

```text
反复应用 patterns
直到 fixed point
```

适合：

```text
canonicalization
局部 simplification
规则之间互相触发
```

我们本讲主要使用它。

## 40.3 `applyPartialConversion / applyFullConversion`

这已经进入：

```text
Dialect Conversion
```

它们不仅问：

```text
“能不能 rewrite？”
```

还会引入：

```text
legal dialect
illegal op
ConversionTarget
TypeConverter
```

这是下一阶段，而不是现在一次塞进来。

---

# 41. Best Practice：什么时候不需要 Pattern？

学完 Pattern 后，很容易形成：“所有变换都应该写 Pattern”，这是错误的；
如果任务只是：

```cpp
func.walk([&](SomeOp op) {
  ...
});
```

而且满足：
只扫描一次
规则之间不会递归触发
没有 complicated replacement bookkeeping


那么walk通常更简单。

所以选择顺序可以记成：

```text
简单只读/单次变换
    ↓
walk

多个局部 rewrite / fixed point
    ↓
RewritePattern + greedy driver

需要 legality / dialect lowering / type conversion
    ↓
Dialect Conversion
```

不要为了“显得高级”而使用更复杂基础设施。

---

# 42. Best Practice：success 当且仅当真的修改 IR

Pattern 中：

```cpp
return success();
```

的含义不是：

```text
“条件检查成功”
```

而是：

```text
“这次 pattern 已经修改了 IR”
```

如果你：

```cpp
if (condition)
  return success();
```

但没有 rewrite，greedy driver 可能误以为：

```text
IR 又发生变化
```

从而产生错误的重试行为，极端情况下甚至无法收敛。

所以牢牢记住：

```text
failure
    =
没有修改 IR

success
    =
确实修改 IR
```

---

# 43. Best Practice：rewrite 后 IR 应该仍然可验证

每一次 Pattern 应该尽量完成一个：

```text
结构完整
类型合法
Region 完整
terminator 完整
```

的 rewrite。

例如创建：

```text
scf.while
```

时不能：

```text
先创建空 region
return success
```

期待“下一个 pattern 再补”。

Pattern 应该把这次 rewrite 所需的结构完整建好。

这也是为什么我们在 LowerMapPattern 里一次性创建：

```text
before block
condition
after block
yield
```

然后才：

```cpp
return success();
```

---

# 44. 调试 Pattern 的推荐方式

## 44.1 看 pass 前后 IR

```bash
build/bin/tinyir-opt input.mlir \
  --pass-pipeline='builtin.module(func.func(list-simplify))' \
  -mlir-print-ir-before-all \
  -mlir-print-ir-after-all
```

## 44.2 使用 `-debug`

```bash
build/bin/tinyir-opt input.mlir \
  --pass-pipeline='builtin.module(func.func(list-simplify))' \
  -debug
```

如果当前 wheel 是 assertions/debug-enabled build，通常能看到更丰富的 pattern / driver 信息。

## 44.3 先拆小测试

不要一上来测试：

```text
from_elements
+
double map
+
map lowering
+
其他 pass
```

而要分别：

```text
from-elements.mlir
simplify-from-elements.mlir
merge-map.mlir
lower-map.mlir
```

每个文件只验证一个概念。

---

# 45. 建议给 Makefile 保留通用 `PASS`

你现在的思路可以继续使用：

```make
TEST ?= $(TEST_DIR)/passes/patterns/from-elements.mlir
PASS ?=

PASSFLAGS := -mlir-print-ir-before-all \
             -mlir-print-ir-after-all

ifneq ($(strip $(PASS)),)
PASSFLAGS += --pass-pipeline='builtin.module(func.func($(PASS)))'
endif
```

对应 target：

```make
.PHONY: pass

pass: $(BIN)
	@echo "+PASS    $(TEST) --> $(PASS)"
	@$(BIN) $(TEST) $(PASSFLAGS)
```

于是：

```bash
make pass \
  TEST=./test/passes/patterns/merge-map.mlir \
  PASS=list-simplify
```

成为这一阶段最常用的实验命令。

---

# 46. 这一讲完成后的目录树

最终源码层建议：

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
│   │   ├── CFGInspect.cpp
│   │   ├── InspectIR.cpp
│   │   ├── UseDef.cpp
│   │   ├── ListLowerFromElements.cpp
│   │   └── ListSimplify.cpp
│   └── tinyir-opt.cpp
└── test/
    └── passes/
        ├── ...
        └── patterns/
            ├── from-elements.mlir
            ├── simplify-from-elements.mlir
            ├── merge-map.mlir
            └── lower-map.mlir
```

构建后新增：

```text
build/obj/passes/
├── ListLowerFromElements.o
└── ListSimplify.o
```

并继续被归档进：

```text
build/lib/libtinyir.a
```

最后链接：

```text
build/bin/tinyir-opt
```

---

# 47. 从文件角度复盘整个执行流程

以：

```bash
make pass \
  TEST=./test/passes/patterns/merge-map.mlir \
  PASS=list-simplify
```

为例。

## 编译阶段

```text
src/dialect/List.td
    ↓ mlir-tblgen
build/gen/List_Ops*.inc
    ↓

src/dialect/List.cpp
src/passes/ListSimplify.cpp
    ↓ c++

build/obj/dialect/List.o
build/obj/passes/ListSimplify.o
    ↓ ar

build/lib/libtinyir.a
    ↓ link

build/bin/tinyir-opt
```

## 运行阶段

```text
merge-map.mlir
    ↓ parser

builtin.module
└── func.func
    ├── list.map
    └── list.map
          ↓

PassManager
    ↓
ListSimplifyPass::runOnOperation()
    ↓
RewritePatternSet
    ├── FromElements patterns
    ├── MergeMapPattern
    └── LowerMapPattern
          ↓
applyPatternsGreedily
          ↓

先尝试高 benefit 的 map fusion
          ↓
single list.map
          ↓
LowerMapPattern
          ↓
scf.while
          ↓
printer
          ↓
stdout
```

---

# 48. 这一讲和 `pass-advanced.md` 的知识连接

这不是一套全新的知识。

几乎每个 Pattern API 都来自上一讲已经学过的底层概念：

```text
PatternRewriter::replaceOp
    ↓
RAUW + erase

PatternRewriter::clone
    ↓
Operation clone

IRMapping
    ↓
old Value / Block → new Value / Block

MergeMapPattern
    ↓
Region / Block / BlockArgument

producer.getResult().use_empty()
    ↓
use-def chain

scf.while
    ↓
Operation contains Region
Region contains Block
Block has BlockArguments

PatternBenefit
    ↓
多个合法 rewrite 之间的选择
```

所以学习顺序应该是：

```text
先理解底层 IR
    ↓
再理解 Pattern infrastructure
```

而不是反过来背 `matchAndRewrite()` 模板。

---

# 49. 本讲必须能独立回答的 10 个问题

完成后，建议你不用看文档回答：

```text
1. RewritePattern 和 Pass 各自负责什么？
2. OpRewritePattern<FromElementsOp> 为什么不需要自己 walk 找 op？
3. matchAndRewrite 返回 failure 前为什么不能修改 IR？
4. PatternRewriter 和普通 OpBuilder 的区别是什么？
5. replaceOpWithNewOp 内部概念上做了哪些事情？
6. applyPatternsGreedily 的 fixed point 是什么意思？
7. from_elements 的递归 lowering 为什么能依赖 greedy driver？
8. 合并两个 map 为什么需要 IRMapping？
9. producer map 为什么不能无条件 erase？
10. 为什么 MergeMapPattern 的 benefit 应高于 LowerMapPattern？
```

如果这 10 个问题都能清楚解释，说明你已经真正掌握这份课件的核心。

---

# 50. 下一阶段应该学什么？

完成本讲后，路线自然进入：

```text
Pattern Rewrite
    ↓
Canonicalization
    ↓
Dialect Conversion
    ↓
ConversionTarget
    ↓
TypeConverter
    ↓
ConversionPatternRewriter
    ↓
多级 Lowering
```

下一份讲义建议命名：

```text
dialect-conversion.md
```

因为此时你已经会：

```text
“某条 op 可以怎样 rewrite”
```

下一步要解决的是：

> **如何声明某一层 IR 中哪些 op 必须消失、哪些 dialect 合法，以及如何系统完成整层 lowering。**

---

# 附录 A：课件内容与本讲义章节映射

```text
课件 P2–P5
    List Dialect 回顾
    from_elements 的分解
        ↓
    本讲 §1–§2

课件 P6–P15
    push_back 复杂度
    get_elements
    range
    length
    scf.for / scf.while
        ↓
    本讲 §2、§32–§37

课件 P16–P25
    list.map lowering
    double map complexity
    map fusion
        ↓
    本讲 §3、§25–§38

课件 P26
    Coding Exercise 1:
    hand-written lowering of from_elements
        ↓
    本讲 §8–§12

课件 P27–P33
    RewritePattern infrastructure
    matchAndRewrite
    PatternRewriter
    RewritePatternSet
    applyPatternsGreedily
        ↓
    本讲 §13–§24

课件 P34
    recursive from_elements rewrite
        ↓
    本讲 §20–§23

课件 P35
    merge two list.map
    clone + IRMapping
        ↓
    本讲 §25–§31

课件 P36
    lower list.map
    merge pattern higher benefit
        ↓
    本讲 §32–§38

课件 P37
    rewrite drivers
        ↓
    本讲 §39–§40

课件 P38
    best practices
        ↓
    本讲 §41–§44
```

---

# 附录 B：本阶段最核心的 C++ API

```cpp
// Pattern
mlir::OpRewritePattern<OpTy>
mlir::LogicalResult
mlir::success()
mlir::failure()

// Rewriter
mlir::PatternRewriter
rewriter.notifyMatchFailure(...)
rewriter.replaceOp(...)
rewriter.replaceOpWithNewOp<...>(...)
rewriter.eraseOp(...)
rewriter.clone(...)
rewriter.createBlock(...)

// Pattern set / driver
mlir::RewritePatternSet
patterns.add<...>(...)
mlir::applyPatternsGreedily(...)

// Clone / value remapping
mlir::IRMapping
mapping.map(...)
mapping.lookup(...)

// Benefit
mlir::PatternBenefit

// Region / block
op.getBody()
region.front()
block.getArgument(...)
block.without_terminator()
block.getTerminator()
```

把这些 API 串起来，不要孤立记忆：

```text
Pattern
    ↓
matchAndRewrite
    ↓
PatternRewriter
    ↓
clone / replace / erase
    ↓
RewritePatternSet
    ↓
Greedy Driver
    ↓
fixed point
```
