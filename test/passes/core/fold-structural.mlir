module {
  func.func @double_reverse(
      %l: !list.list<i64>)
      -> !list.list<i64> {

    %a = "list.reverse"(%l)
         : (!list.list<i64>)
        -> !list.list<i64>

    %b = "list.reverse"(%a)
         : (!list.list<i64>)
        -> !list.list<i64>

    return %b : !list.list<i64>
  }
}
