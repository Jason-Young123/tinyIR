//module {
//  %0 = tutorial.constant 42 : i32
//}


// 套函数版本, 需要在Tutorial-opt.cpp中注册函数
func.func @main() -> i32 {
    %0 = tutorial.constant 42 : i32
    func.return %0 : i32
}

