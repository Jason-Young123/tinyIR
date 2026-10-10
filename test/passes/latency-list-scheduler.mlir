module {

  // 简单测试1：
  // 两条独立chain最后汇合，其中第一条从lat10开始
  func.func @latency_hide() -> i32 {

    %0 = latency.lat10 : i32
    %1 = latency.lat5 %0 : i32
    %2 = latency.lat1 %1 : i32

    %3 = latency.lat1 : i32
    %4 = latency.lat5 %3 : i32
    %5 = latency.lat1 %4 : i32

    %6 = latency.lat1 %2, %5 : i32

    return %6 : i32
  }


  // 简单测试2：
  // 两条对称lat5->lat1 chain最后汇合
  func.func @two_lat5_chains() -> i32 {

    %0 = latency.lat5 : i32
    %1 = latency.lat1 %0 : i32

    %2 = latency.lat5 : i32
    %3 = latency.lat1 %2 : i32

    %4 = latency.lat1 %1, %3 : i32

    return %4 : i32
  }


  // 综合测试：
  // 对应24节点大型DAG(pics/dag_24_nodes.png)
  func.func @complex_latency_dag() -> i32 {

    // ---------- Top roots ----------
    %l0 = latency.lat10 : i32
    %c0 = latency.lat1 : i32
    %r0 = latency.lat5 : i32

    // ---------- Upper layer ----------
    %l1 = latency.lat5 %l0 : i32
    %c1 = latency.lat5 %c0 : i32

    // l1在原图中分叉成两个lat1
    %la = latency.lat1 %l1 : i32
    %lb = latency.lat1 %l1 : i32

    // r0分叉成三个节点
    %ra = latency.lat1 %r0 : i32
    %rb = latency.lat5 %r0 : i32
    %rc = latency.lat1 %r0 : i32

    // ---------- Next layer ----------
    %l2 = latency.lat1 %la : i32

    // center vertical chain + right-left branch
    %c2 = latency.lat5 %c1, %ra : i32

    %r10a = latency.lat10 %ra : i32

    // right middle join
    %rjoin = latency.lat1 %rb, %rc : i32

    // ---------- Middle layer ----------
    %l3 = latency.lat10 %l2 : i32

    // 原图这里依赖的是lb(lat1)，不是l1(lat5)
    %c3 = latency.lat5 %c2, %lb : i32

    %r10b = latency.lat10 %rjoin : i32

    // ---------- Lower-middle layer ----------
    %l4 = latency.lat1 %l3 : i32

    // 左侧第二个lat1斜向汇入中央lat1
    %c4 = latency.lat1 %c3, %l2 : i32

    %l5 = latency.lat5 %l4 : i32

    // right-side lat10斜向汇入中央下方lat1
    %c5 = latency.lat1 %c4, %r10a : i32

    // ---------- Lower layer ----------

    // lower-left merge
    %x = latency.lat1 %l5, %c5 : i32

    // lower-right merge
    %r5 = latency.lat5 %c5, %r10b : i32

    // ---------- Final merge ----------
    %out = latency.lat5 %x, %r5 : i32

    return %out : i32
  }

}