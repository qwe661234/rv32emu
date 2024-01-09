; ModuleID = 'my_module'
source_filename = "my_module"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128"

define void @start({ i8, { void*, void*, void*, void*, void*, void*, void*, void*, void*, void*, void*, i8 }, [32 x i32], i32 }* %0) {
entry:
  %addr_rs1 = getelementptr inbounds i32, { i8, { void*, void*, void*, void*, void*, void*, void*, void*, void*, void*, void*, i8 }, [32 x i32], i32 }* %0, i32 26
  %addr_rd = getelementptr inbounds i32, { i8, { void*, void*, void*, void*, void*, void*, void*, void*, void*, void*, void*, i8 }, [32 x i32], i32 }* %0, i32 43
  %val_rs1 = load i32, i32* %addr_rs1, align 4
  %add = add i32 %val_rs1, 93
  store i32 %add, i32* %addr_rd, align 4
  %addr_PC = getelementptr inbounds i32, { i8, { void*, void*, void*, void*, void*, void*, void*, void*, void*, void*, void*, i8 }, [32 x i32], i32 }* %0, i32 58
  store i32 124008, i32* %addr_PC, align 4
  %addr_rv = getelementptr inbounds void*, { i8, { void*, void*, void*, void*, void*, void*, void*, void*, void*, void*, void*, i8 }, [32 x i32], i32 }* %0, i32 8
  %func_ecall = load void ({ i8, { void*, void*, void*, void*, void*, void*, void*, void*, void*, void*, void*, i8 }, [32 x i32], i32 }*)*, void** %addr_rv, align 8
  call void %func_ecall({ i8, { void*, void*, void*, void*, void*, void*, void*, void*, void*, void*, void*, i8 }, [32 x i32], i32 }* %0)
  ret void
}
