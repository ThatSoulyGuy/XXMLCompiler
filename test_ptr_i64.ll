define void @test_func(i64 %val) {
  ret void
}

define i32 @main() {
  %1 = inttoptr i64 42 to ptr
  call void @test_func(ptr %1)
  ret i32 0
}
