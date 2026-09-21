func.func @choose(%c: i1, %a: i32, %b: i32) -> i32 {
  cf.cond_br %c, ^bb1(%a : i32), ^bb2(%b : i32)

^bb1(%x: i32):
  cf.br ^bb3(%x : i32)

^bb2(%y: i32):
  %z = arith.addi %y, %y : i32
  cf.br ^bb3(%z : i32)

^bb3(%r: i32):
  return %r : i32
}
