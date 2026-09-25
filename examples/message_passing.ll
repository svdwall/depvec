; ModuleID = 'examples/message_passing.c'
source_filename = "examples/message_passing.c"
target datalayout = "e-m:o-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-n32:64-S128-Fn32"
target triple = "arm64-apple-macosx14.0.0"

@shared_value = local_unnamed_addr global i32 0, align 4
@ready = local_unnamed_addr global i32 0, align 4
@.str = private unnamed_addr constant [7 x i8] c"depvec\00", section "llvm.metadata"
@.str.1 = private unnamed_addr constant [27 x i8] c"examples/message_passing.c\00", section "llvm.metadata"
@llvm.global.annotations = appending global [2 x { ptr, ptr, ptr, i32, ptr }] [{ ptr, ptr, ptr, i32, ptr } { ptr @message_passing_writer, ptr @.str, ptr @.str.1, i32 7, ptr null }, { ptr, ptr, ptr, i32, ptr } { ptr @message_passing_reader, ptr @.str, ptr @.str.1, i32 12, ptr null }], section "llvm.metadata"

; Function Attrs: mustprogress nofree norecurse nounwind sspstrong willreturn memory(readwrite, argmem: none, inaccessiblemem: none, target_mem0: none, target_mem1: none) uwtable(sync)
define void @message_passing_writer() #0 {
entry:
  store atomic i32 42, ptr @shared_value monotonic, align 4
  store atomic i32 1, ptr @ready release, align 4
  ret void
}

; Function Attrs: nofree norecurse nounwind sspstrong memory(readwrite, argmem: none, inaccessiblemem: none, target_mem0: none, target_mem1: none) uwtable(sync)
define i32 @message_passing_reader() #1 {
entry:
  br label %while.cond

while.cond:                                       ; preds = %while.cond, %entry
  %0 = load atomic i32, ptr @ready acquire, align 4
  %cmp = icmp eq i32 %0, 0
  br i1 %cmp, label %while.cond, label %while.end, !llvm.loop !10

while.end:                                        ; preds = %while.cond
  %1 = load atomic i32, ptr @shared_value monotonic, align 4
  ret i32 %1
}

attributes #0 = { mustprogress nofree norecurse nounwind sspstrong willreturn memory(readwrite, argmem: none, inaccessiblemem: none, target_mem0: none, target_mem1: none) uwtable(sync) "frame-pointer"="all" "no-trapping-math"="true" "stack-protector-buffer-size"="4" "target-cpu"="apple-m1" "target-features"="+aes,+altnzcv,+ccdp,+ccidx,+ccpp,+complxnum,+crc,+dit,+dotprod,+flagm,+fp-armv8,+fp16fml,+fptoint,+fullfp16,+jsconv,+lse,+neon,+pauth,+perfmon,+predres,+ras,+rcpc,+rdm,+sb,+sha2,+sha3,+specrestrict,+ssbs,+v8.1a,+v8.2a,+v8.3a,+v8.4a,+v8a" "zero-call-used-regs"="used-gpr" }
attributes #1 = { nofree norecurse nounwind sspstrong memory(readwrite, argmem: none, inaccessiblemem: none, target_mem0: none, target_mem1: none) uwtable(sync) "frame-pointer"="all" "no-trapping-math"="true" "stack-protector-buffer-size"="4" "target-cpu"="apple-m1" "target-features"="+aes,+altnzcv,+ccdp,+ccidx,+ccpp,+complxnum,+crc,+dit,+dotprod,+flagm,+fp-armv8,+fp16fml,+fptoint,+fullfp16,+jsconv,+lse,+neon,+pauth,+perfmon,+predres,+ras,+rcpc,+rdm,+sb,+sha2,+sha3,+specrestrict,+ssbs,+v8.1a,+v8.2a,+v8.3a,+v8.4a,+v8a" "zero-call-used-regs"="used-gpr" }

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
!10 = distinct !{!10, !11}
!11 = !{!"llvm.loop.mustprogress"}
