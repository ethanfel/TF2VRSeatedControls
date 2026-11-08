EXTERN SeatedClassify:PROC
EXTERN SeatedUpdate:PROC
EXTERN SeatedPouch:PROC
EXTERN SeatedReload:PROC
PUBLIC ClassifyThunk
PUBLIC UpdateThunk
PUBLIC PouchThunk
PUBLIC ReloadThunk
SAVE MACRO
    sub rsp, 108h
    .allocstack 108h
    .endprolog
    mov [rsp+40h], rcx
    mov [rsp+48h], rdx
    mov [rsp+50h], r8
    mov [rsp+58h], r9
    mov [rsp+60h], r10
    mov [rsp+68h], r11
    movaps [rsp+70h], xmm0
    movaps [rsp+80h], xmm1
    movaps [rsp+90h], xmm2
    movaps [rsp+0a0h], xmm3
    movaps [rsp+0b0h], xmm4
    movaps [rsp+0c0h], xmm5
ENDM
RESTORE MACRO
    movaps xmm0, [rsp+70h]
    movaps xmm1, [rsp+80h]
    movaps xmm2, [rsp+90h]
    movaps xmm3, [rsp+0a0h]
    movaps xmm4, [rsp+0b0h]
    movaps xmm5, [rsp+0c0h]
    mov rcx, [rsp+40h]
    mov rdx, [rsp+48h]
    mov r8, [rsp+50h]
    mov r9, [rsp+58h]
    mov r10, [rsp+60h]
    mov r11, [rsp+68h]
    add rsp, 108h
    ret
ENDM
.code
ClassifyThunk PROC FRAME
    SAVE
    mov rax, [rsp+108h]
    mov [rsp+20h], rax
    call SeatedClassify
    RESTORE
ClassifyThunk ENDP
UpdateThunk PROC FRAME
    SAVE
    mov rax, [rsp+130h]
    mov [rsp+20h], rax
    mov rax, [rsp+138h]
    mov [rsp+28h], rax
    mov rax, [rsp+140h]
    mov [rsp+30h], rax
    call SeatedUpdate
    RESTORE
UpdateThunk ENDP
PouchThunk PROC FRAME
    SAVE
    mov r9, [rsp+108h]
    call SeatedPouch
    RESTORE
PouchThunk ENDP
ReloadThunk PROC FRAME
    SAVE
    call SeatedReload
    RESTORE
ReloadThunk ENDP
END
