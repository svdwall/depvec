; ModuleID = 'examples/def_use_independent.c'
source_filename = "examples/def_use_independent.c"
target datalayout = "e-m:o-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-n32:64-S128-Fn32"
target triple = "arm64-apple-macosx14.0.0"

@independent_sink = global i32 0, align 4

; Function Attrs: nofree norecurse nounwind sspstrong memory(readwrite, argmem: none, target_mem0: none, target_mem1: none) uwtable(sync)
define range(i32 2, 1) i32 @def_use_independent(i32 noundef %input, i32 noundef %unrelated_input) local_unnamed_addr #0 {
entry:
  %add1 = add i32 %unrelated_input, 9
  store volatile i32 %add1, ptr @independent_sink, align 4, !tbaa !6
  %add = shl i32 %input, 1
  %mul = add i32 %add, 2
  ret i32 %mul
}

attributes #0 = { nofree norecurse nounwind sspstrong memory(readwrite, argmem: none, target_mem0: none, target_mem1: none) uwtable(sync) "frame-pointer"="all" "no-trapping-math"="true" "stack-protector-buffer-size"="4" "target-cpu"="apple-m1" "target-features"="+aes,+altnzcv,+ccdp,+ccidx,+ccpp,+complxnum,+crc,+dit,+dotprod,+flagm,+fp-armv8,+fp16fml,+fptoint,+fullfp16,+jsconv,+lse,+neon,+pauth,+perfmon,+predres,+ras,+rcpc,+rdm,+sb,+sha2,+sha3,+specrestrict,+ssbs,+v8.1a,+v8.2a,+v8.3a,+v8.4a,+v8a" "zero-call-used-regs"="used-gpr" }

!llvm.module.flags = !{!0, !1, !2, !3, !4}
!llvm.ident = !{!5}
!llvm.errno.tbaa = !{!6}

!0 = !{i32 2, !"SDK Version", [2 x i32] [i32 14, i32 4]}
!1 = !{i32 1, !"wchar_size", i32 4}
!2 = !{i32 8, !"PIC Level", i32 2}
!3 = !{i32 7, !"uwtable", i32 1}
!4 = !{i32 7, !"frame-pointer", i32 2}
!5 = !{!"clang version 22.1.8"}
!6 = !{!7, !7, i64 0}
!7 = !{!"int", !8, i64 0}
!8 = !{!"omnipotent char", !9, i64 0}
!9 = !{!"Simple C/C++ TBAA"}
