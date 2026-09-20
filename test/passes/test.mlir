module {
  func.func @add_one(%x: i32) -> i32 {
    %unused = arith.constant 123 : i32
    %c1 = arith.constant 1 : i32
    %c2 = arith.constant 2 : i32
    %y = arith.addi %x, %c1 : i32
    %z = arith.muli %y, %c2 : i32
    return %z : i32
  }
}
