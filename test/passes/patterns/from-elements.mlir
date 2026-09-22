module {
  func.func @test(%a: i64, %b: i64, %c: i64)
      -> !list.list<i64> {
    %r = "list.from_elements"(%a, %b, %c)
         : (i64, i64, i64) -> !list.list<i64>

    return %r : !list.list<i64>
  }
}
