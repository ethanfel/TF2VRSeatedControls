// Preserve volatile registers too: the core's optimized internal callers may
// rely on registers that the original routines happen not to clobber.
.text
.macro SAVE
    subq $0x108, %rsp
    .seh_stackalloc 0x108
    .seh_endprologue
    movq %rcx, 0x40(%rsp)
    movq %rdx, 0x48(%rsp)
    movq %r8, 0x50(%rsp)
    movq %r9, 0x58(%rsp)
    movq %r10, 0x60(%rsp)
    movq %r11, 0x68(%rsp)
    movaps %xmm0, 0x70(%rsp)
    movaps %xmm1, 0x80(%rsp)
    movaps %xmm2, 0x90(%rsp)
    movaps %xmm3, 0xa0(%rsp)
    movaps %xmm4, 0xb0(%rsp)
    movaps %xmm5, 0xc0(%rsp)
.endm
.macro RESTORE
    movaps 0x70(%rsp), %xmm0
    movaps 0x80(%rsp), %xmm1
    movaps 0x90(%rsp), %xmm2
    movaps 0xa0(%rsp), %xmm3
    movaps 0xb0(%rsp), %xmm4
    movaps 0xc0(%rsp), %xmm5
    movq 0x40(%rsp), %rcx
    movq 0x48(%rsp), %rdx
    movq 0x50(%rsp), %r8
    movq 0x58(%rsp), %r9
    movq 0x60(%rsp), %r10
    movq 0x68(%rsp), %r11
    addq $0x108, %rsp
    ret
.endm
.globl ClassifyThunk
.seh_proc ClassifyThunk
ClassifyThunk:
    SAVE
    movq 0x108(%rsp), %rax
    movq %rax, 0x20(%rsp)
    call SeatedClassify
    RESTORE
.seh_endproc
.globl PouchThunk
.seh_proc PouchThunk
PouchThunk:
    SAVE
    movq 0x108(%rsp), %r9
    call SeatedPouch
    RESTORE
.seh_endproc
.globl ReloadThunk
.seh_proc ReloadThunk
ReloadThunk:
    SAVE
    call SeatedReload
    RESTORE
.seh_endproc
.globl UpdateThunk
.seh_proc UpdateThunk
UpdateThunk:
    SAVE
    movq 0x130(%rsp), %rax
    movq %rax, 0x20(%rsp)
    movq 0x138(%rsp), %rax
    movq %rax, 0x28(%rsp)
    movq 0x140(%rsp), %rax
    movq %rax, 0x30(%rsp)
    call SeatedUpdate
    RESTORE
.seh_endproc
