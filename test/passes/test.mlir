module {
  func.func @add_one(%x: i32) -> i32 {
    %unused = arith.constant 123 : i32
    %c0 = arith.constant 0 : i32
    %c1 = arith.constant 1 : i32
    %c2 = arith.constant 2 : i32
    %r = arith.addi %x, %c0 : i32
    %y = arith.addi %r, %c1 : i32
    %z = arith.muli %r, %c2 : i32
    return %z : i32
  }
}
