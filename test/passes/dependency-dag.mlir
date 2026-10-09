module {
  func.func @diamond(
      %a: i32,
      %b: i32,
      %c: i32,
      %d: i32) -> i32 {

    %0 = arith.addi %a, %b : i32
    %1 = arith.muli %0, %c : i32
    %2 = arith.addi %0, %d : i32
    %3 = arith.addi %1, %2 : i32

    return %3 : i32
  }

  func.func @two_roots(
      %a: i32,
      %b: i32,
      %c: i32,
      %d: i32) -> i32 {

    %0 = arith.addi %a, %b : i32
    %1 = arith.muli %c, %d : i32
    %2 = arith.addi %0, %1 : i32

    return %2 : i32
  }

  func.func @multi_block(
      %cond: i1,
      %x: i32,
      %y: i32) -> i32 {

    cf.cond_br %cond, ^bb1(%x : i32), ^bb2(%y : i32)

  ^bb1(%a: i32):
    %0 = arith.addi %a, %a : i32
    cf.br ^merge(%0 : i32)

  ^bb2(%b: i32):
    %1 = arith.muli %b, %b : i32
    cf.br ^merge(%1 : i32)

  ^merge(%v: i32):
    %2 = arith.addi %v, %v : i32
    return %2 : i32
  }
}


