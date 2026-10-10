module {
  func.func @load_hide(
      %mem: memref<16xi32>,
      %idx: index,
      %k1: i32,
      %k2: i32,
      %x: i32,
      %y: i32,
      %z: i32,
      %w: i32) -> i32 {
    %0 = memref.load %mem[%idx] : memref<16xi32>
    %1 = arith.muli %0, %k1 : i32
    %2 = arith.addi %1, %k2 : i32
    %3 = arith.addi %x, %y : i32
    %4 = arith.muli %3, %z : i32
    %5 = arith.addi %4, %w : i32
    %6 = arith.addi %2, %5 : i32
    return %6 : i32
  }

  func.func @two_mul_chains(
      %a: i32,
      %b: i32,
      %c: i32,
      %d: i32,
      %e: i32,
      %f: i32) -> i32 {
    %0 = arith.muli %a, %b : i32
    %1 = arith.addi %0, %c : i32
    %2 = arith.muli %d, %e : i32
    %3 = arith.addi %2, %f : i32
    %4 = arith.addi %1, %3 : i32
    return %4 : i32
  }
}




