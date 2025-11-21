declare ptr @get_ptr()

define void @test_func(i64 %val) {
  ret void
}

define i32 @main() {
  %r1 = call ptr @get_ptr()
  call void @test_func(i64 %r1)
  ret i32 0
}
