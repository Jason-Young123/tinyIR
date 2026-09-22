module {
  func.func @empty_case() -> !list.list<i64> {
    %r = "list.from_elements"() : () -> !list.list<i64>
    return %r : !list.list<i64>
  }

  func.func @non_empty_case(%a: i64, %b: i64) -> !list.list<i64> {
    %r = "list.from_elements"(%a, %b) : (i64, i64) -> !list.list<i64>
    return %r : !list.list<i64>
  }
}
