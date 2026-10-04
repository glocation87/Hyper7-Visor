.code

extern h7_handle_exit : proc

PUSHGPRS macro
    push    rax
    push    rcx
    push    rdx
    push    rbx
    push    -1          ; rsp placeholder
    push    rbp
    push    rsi
    push    rdi
    push    r8
    push    r9
    push    r10
    push    r11
    push    r12
    push    r13
    push    r14
    push    r15
endm

POPGPRS macro
    pop     r15
    pop     r14
    pop     r13
    pop     r12
    pop     r11
    pop     r10
    pop     r9
    pop     r8
    pop     rdi
    pop     rsi
    pop     rbp
    pop     rbx        ; eats the rsp placeholder
    pop     rbx
    pop     rdx
    pop     rcx
    pop     rax
endm

;
; void __stdcall h7_launch_vm(ULONG64 host_rsp)
;
; rcx = pointer to the top of h7_vcpu host stack
;       where top.guest_vmcb_pa sits at [rsp+0]
;
h7_launch_vm proc frame
    mov     rsp, rcx

_loop:
    mov     rax, [rsp]          ; guest vmcb pa
    vmload  rax
    vmrun   rax
    vmsave  rax

    .pushframe
    PUSHGPRS                    ; rsp -> h7_gp_regs on stack

    mov     rdx, rsp            ; arg2 = &gp_regs
    mov     rcx, [rsp + 8*18]   ; arg1 = vcpu->top.self (past 16 regs + 2 slots)

    sub     rsp, 60h
    movaps  xmmword ptr [rsp+20h], xmm0
    movaps  xmmword ptr [rsp+30h], xmm1
    movaps  xmmword ptr [rsp+40h], xmm2
    movaps  xmmword ptr [rsp+50h], xmm3
    .endprolog

    call    h7_handle_exit

    movaps  xmm3, xmmword ptr [rsp+50h]
    movaps  xmm2, xmmword ptr [rsp+40h]
    movaps  xmm1, xmmword ptr [rsp+30h]
    movaps  xmm0, xmmword ptr [rsp+20h]
    add     rsp, 60h

    test    al, al
    POPGPRS
    jnz     _bail
    jmp     _loop

_bail:
    ; rcx = rsp, rbx = nrip (set by the exit handler)
    mov     rsp, rcx
    jmp     rbx
h7_launch_vm endp


h7_read_cs proc
    mov     ax, cs
    ret
h7_read_cs endp

h7_read_ss proc
    mov     ax, ss
    ret
h7_read_ss endp

h7_read_ds proc
    mov     ax, ds
    ret
h7_read_ds endp

h7_read_es proc
    mov     ax, es
    ret
h7_read_es endp

h7_read_rflags proc
    pushfq
    pop     rax
    ret
h7_read_rflags endp

h7_read_rsp proc
    mov     rax, rsp
    add     rax, 8
    ret
h7_read_rsp endp

h7_read_rip proc
    mov     rax, [rsp]
    ret
h7_read_rip endp

h7_sgdt proc
    sgdt    [rcx]
    ret
h7_sgdt endp

end
