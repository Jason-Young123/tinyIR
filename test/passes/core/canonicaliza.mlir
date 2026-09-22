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
