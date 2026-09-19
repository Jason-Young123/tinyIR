// RUN: tutorial-opt --split-input-file %s | FileCheck %s

func.func @empty() -> !list.list<i32> {
  %0 = list.empty : !list.list<i32>
  return %0 : !list.list<i32>
}

// CHECK-LABEL: func.func @empty
// CHECK: %[[LIST:.*]] = list.empty : !list.list<i32>
// CHECK: return %[[LIST]] : !list.list<i32>

// -----

func.func @push_back(%item: i32) -> !list.list<i32> {
  %empty = list.empty : !list.list<i32>
  %list = list.push_back %empty, %item : !list.list<i32>
  return %list : !list.list<i32>
}

// CHECK-LABEL: func.func @push_back
// CHECK: %[[EMPTY:.*]] = list.empty : !list.list<i32>
// CHECK: %[[LIST:.*]] = list.push_back %[[EMPTY]], %{{.*}} : !list.list<i32>
// CHECK: return %[[LIST]] : !list.list<i32>

// -----

func.func @length(%list: !list.list<i32>) -> i32 {
  %length = list.length %list : !list.list<i32> -> i32
  return %length : i32
}

// CHECK-LABEL: func.func @length
// CHECK: %[[LENGTH:.*]] = list.length %{{.*}} : !list.list<i32> -> i32
// CHECK: return %[[LENGTH]] : i32



func.func @is_empty(%list : !list.list<i32>) -> i1 {
  %is_empty = list.is_empty %list : !list.list<i32> -> i1
  return %is_empty : i1
}


func.func @print(%list: !list.list<i32>) {
  list.print %list : !list.list<i32>
  return
}


func.func @range(%lower: i32, %upper : i32) -> !list.list<i32> {
  %lo = list.range %lower to %upper : !list.list<i32>
  return %lo : !list.list<i32>
}


func.func @reverse(%li : !list.list<i32>) -> !list.list<i32> {
  %lo = list.reverse %li : !list.list<i32> -> !list.list<i32>
  return %lo : !list.list<i32>
}


func.func @pop_front(%li : !list.list<i32>) -> !list.list<i32> {
  %lo = list.pop_front %li : !list.list<i32>
  func.return %lo : !list.list<i32>
}


func.func @peek_front(%li : !list.list<i32>) -> i32 {
  %item = list.peek_front %li : !list.list<i32>
  func.return %item : i32
}

func.func @push_front(%li : !list.list<i32>, %item : i32) -> !list.list<i32> {
  %lo = list.push_front %li, %item : !list.list<i32>
  func.return %lo : !list.list<i32>
}




func.func @map(%li : !list.list<i32>) -> !list.list<i32> {
  %lo = list.map %li : !list.list<i32> -> !list.list<i32> with {
    ^bb0(%elem : i32):
      %one = arith.constant 1 : i32
      %out = arith.addi %elem, %one : i32
      list.yield %out : i32
  }
  func.return %lo : !list.list<i32>
}
