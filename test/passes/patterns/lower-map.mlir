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



// 运行输出:
// module {
//   func.func @map_once(%arg0: !list.list<i64>, %arg1: i64) -> !list.list<i64> {
//     %true = arith.constant true
//     %0 = "list.empty"() : () -> !list.list<i64>
//     // scf分为before和after两个region,分别用于描述循环条件和循环体
//     %1:2 = scf.while (%arg2 = %arg0, %arg3 = %0) : (!list.list<i64>, !list.list<i64>) -> (!list.list<i64>, !list.list<i64>) {
//       %2 = "list.is_empty"(%arg2) : (!list.list<i64>) -> i1
//       %3 = arith.xori %2, %true : i1
//       scf.condition(%3) %arg2, %arg3 : !list.list<i64>, !list.list<i64>//这里如果判断%3 = true(继续执行循环体),则还要把arg2和arg3传给循环体作为block arg
//     } do {
//     ^bb0(%arg2: !list.list<i64>, %arg3: !list.list<i64>):
//       %2 = "list.peek_front"(%arg2) : (!list.list<i64>) -> i64
//       %3 = "list.pop_front"(%arg2) : (!list.list<i64>) -> !list.list<i64>
//       %4 = arith.addi %2, %arg1 : i64
//       %5 = "list.push_back"(%arg3, %4) : (!list.list<i64>, i64) -> !list.list<i64>
//       scf.yield %3, %5 : !list.list<i64>, !list.list<i64>//yield操作将两个value再传回before region
//     }
//     return %1#1 : !list.list<i64>//只返回scf op的第二个result
//   }
// }

// 整体数据流:
// before:
//     rem, acc
//       |
//       v
// after:
//     newRem, newAcc
//       |
//       | scf.yield
//       v
// 下一轮 before:
//     rem = newRem
//     acc = newAcc


