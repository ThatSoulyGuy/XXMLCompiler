; ModuleID = 'Phase2Test.ll'
source_filename = "Phase2Test.ll"
target datalayout = "e-m:w-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-windows-msvc19.44.35219"

declare ptr @xxml_malloc(i64)

declare void @xxml_free(ptr)

declare ptr @xxml_memcpy(ptr, ptr, i64)

declare ptr @xxml_memset(ptr, i32, i64)

declare ptr @xxml_ptr_read(ptr)

declare void @xxml_ptr_write(ptr, ptr)

declare i8 @xxml_read_byte(ptr)

declare void @xxml_write_byte(ptr, i8)

declare ptr @Integer_Constructor(i64)

declare i64 @Integer_getValue(ptr)

declare ptr @Integer_add(ptr, ptr)

declare ptr @Integer_sub(ptr, ptr)

declare ptr @Integer_mul(ptr, ptr)

declare ptr @Integer_div(ptr, ptr)

declare i1 @Integer_eq(ptr, ptr)

declare i1 @Integer_ne(ptr, ptr)

declare i1 @Integer_lt(ptr, ptr)

declare i1 @Integer_le(ptr, ptr)

declare i1 @Integer_gt(ptr, ptr)

declare i1 @Integer_ge(ptr, ptr)

declare i64 @Integer_toInt64(ptr)

declare ptr @String_Constructor(ptr)

declare ptr @String_FromCString(ptr)

declare ptr @String_toCString(ptr)

declare i64 @String_length(ptr)

declare ptr @String_concat(ptr, ptr)

declare i1 @String_equals(ptr, ptr)

declare void @String_destroy(ptr)

declare ptr @Bool_Constructor(i1)

declare i1 @Bool_getValue(ptr)

declare ptr @Bool_and(ptr, ptr)

declare ptr @Bool_or(ptr, ptr)

declare ptr @Bool_not(ptr)

declare void @Console_print(ptr)

declare void @Console_printLine(ptr)

declare void @Console_printInt(i64)

declare void @Console_printBool(i1)

declare void @xxml_exit(i32)

declare void @exit(i32)

define i32 @main() {
  %r0 = alloca ptr, align 8
  %r1 = call ptr @Integer_Constructor(i64 10)
  store ptr %r1, ptr %r0, align 8
  %r2 = alloca ptr, align 8
  %r3 = call ptr @Integer_Constructor(i64 5)
  store ptr %r3, ptr %r2, align 8
  call void @exit(i32 0)
  ret i32 0
}
