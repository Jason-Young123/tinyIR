module {
  func.func @two_maps(%l: !list.list<i64>, %c1: i64, %c2: i64) -> !list.list<i64> {

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
