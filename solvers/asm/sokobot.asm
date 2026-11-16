default abs

%define MAX_STATES  1000000
%define HASH_SIZE   4194304
%define MAX_PRIO    8388608
%define INPUT_SIZE  268435456
%define OUT_SIZE    67108864
%define MAX_BOXES   2000
%define INF         1000000
%define BIG         0x40000000

%define ST_PLAYER   0
%define ST_PARENT   4
%define ST_SRC      8
%define ST_DST      12
%define ST_SW       16
%define ST_HNEXT    20
%define ST_BNEXT    24
%define ST_HASH     32
%define ST_BOXES    40

section .rodata align=32
pd_inf:     times 8 dd INF
pd_big:     times 8 dd BIG
dir_chars:  db "udlr"
cpu_msg:    db "sokobot: this CPU needs AVX2 and BMI1", 10
cpu_msg_len equ $ - cpu_msg

section .data
steps:      dq 0, 0, -1, 1
stamp:      dq 2

section .bss
width:      resq 1
height:     resq 1
stride:     resq 1
ncells:     resq 1
ncells32:   resq 1
nb:         resq 1
ng:         resq 1
nbp:        resq 1
nbp4:       resq 1
nbp4n:      resq 1
recsize:    resq 1
st_u:       resq 1
st_v:       resq 1
st_p:       resq 1
map_p:      resq 1
items_p:    resq 1
area:       resq 1

start_player: resq 1
norm_player:  resq 1
new_player:   resq 1
child_hash:   resq 1
pull_dest:    resq 1
cur_h:        resq 1
cur_ref:      resq 1
stand:        resq 1
last_stamp:   resq 1
seed_stamp:   resq 1
min_prio:     resq 1
states_base:  resq 1
states_next:  resq 1
states_limit: resq 1

reach_p:    resq 1
came_p:     resq 1
stampA_p:   resq 1
stampB_p:   resq 1
zob_p:      resq 1
queue_p:    resq 1
goals_p:    resq 1
sboxes_p:   resq 1
tbox_p:     resq 1
cbox_p:     resq 1
dist_p:     resq 1
cost_p:     resq 1
hu_p:       resq 1
hv_p:       resq 1
hp_p:       resq 1
hway_p:     resq 1
hminv_p:    resq 1
hused_p:    resq 1
hused0_p:   resq 1
states_p:   resq 1
hhead_p:    resq 1
bhead_p:    resq 1
out_p:      resq 1

section .text

%macro PUSHALL 0
    push rbx
    push rbp
    push r12
    push r13
    push r14
    push r15
%endmacro
%macro POPALL 0
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbp
    pop rbx
%endmacro

%macro COPY32 2
%%l:
    vmovdqu ymm0, [%1 + rcx]
    vmovdqu [%2 + rcx], ymm0
    add     rcx, 32
    jl      %%l
%endmacro

%macro ALLOC 2
    mov     rdi, %2
    call    alloc
    mov     [%1], rax
%endmacro

global _start
_start:
    call    check_cpu
    mov     edi, INPUT_SIZE
    call    alloc
    mov     rbx, rax
    xor     r12d, r12d
.read:
    mov     edx, INPUT_SIZE
    sub     rdx, r12
    jle     .bad
    xor     eax, eax
    xor     edi, edi
    lea     rsi, [rbx + r12]
    syscall
    test    rax, rax
    jle     .eof
    add     r12, rax
    jmp     .read
.eof:
    cmp     r12, 4
    jb      .bad
    call    clock_us
    mov     r13, rax
    call    read_text
    test    eax, eax
    jnz     .bad

    mov     rdi, [map_p]
    mov     rsi, [area]
    mov     edx, '.'
    call    count_char
    mov     [ng], rax
    mov     rdi, [items_p]
    mov     rsi, [area]
    mov     edx, '$'
    call    count_char
    mov     [nb], rax
    cmp     rax, [ng]
    jne     .bad
    test    rax, rax
    jz      .empty
    cmp     rax, MAX_BOXES
    ja      .bad

    mov     rax, [width]
    add     rax, 2
    mov     [stride], rax
    mov     rcx, [height]
    add     rcx, 2
    imul    rcx, rax
    mov     [ncells], rcx
    call    alloc_all
    call    parse
    call    init_push_distances
    call    search
    mov     r14, rax

    call    clock_us
    sub     rax, r13
    call    print_time
    mov     rcx, [out_p]
    mov     byte [rcx + r14], 10
    lea     rdx, [r14 + 1]
    mov     rsi, rcx
    mov     eax, 1
    mov     edi, 1
    syscall
    jmp     .exit
.empty:
    call    clock_us
    sub     rax, r13
    call    print_time
    mov     byte [rbx], 10
    mov     eax, 1
    mov     edi, 1
    mov     rsi, rbx
    mov     edx, 1
    syscall
.exit:
    mov     eax, 60
    xor     edi, edi
    syscall
.bad:
    mov     eax, 60
    mov     edi, 2
    syscall

clock_us:
    sub     rsp, 16
    mov     eax, 228
    mov     edi, 1
    mov     rsi, rsp
    syscall
    mov     rax, [rsp + 8]
    xor     edx, edx
    mov     ecx, 1000
    div     rcx
    mov     rcx, [rsp]
    imul    rcx, 1000000
    add     rax, rcx
    add     rsp, 16
    ret

print_time:
    sub     rsp, 32
    lea     rdi, [rsp + 31]
    mov     byte [rdi], 10
    xor     edx, edx
    mov     ecx, 1000
    div     rcx
    mov     r8, rax
    mov     rax, rdx
    mov     r9d, 10
    mov     r10d, 3
.frac:
    xor     edx, edx
    div     r9
    add     dl, '0'
    dec     rdi
    mov     [rdi], dl
    dec     r10d
    jnz     .frac
    dec     rdi
    mov     byte [rdi], '.'
    mov     rax, r8
.whole:
    xor     edx, edx
    div     r9
    add     dl, '0'
    dec     rdi
    mov     [rdi], dl
    test    rax, rax
    jnz     .whole
    lea     rdx, [rsp + 32]
    sub     rdx, rdi
    mov     rsi, rdi
    mov     eax, 1
    mov     edi, 1
    syscall
    add     rsp, 32
    ret

read_text:
    PUSHALL
    mov     rsi, rbx
    lea     rdx, [rbx + r12]
    xor     r8d, r8d
    xor     r9d, r9d
    xor     r10d, r10d
    xor     r11d, r11d
.scan:
    cmp     rsi, rdx
    jae     .scan_end
    movzx   eax, byte [rsi]
    inc     rsi
    cmp     al, 10
    je      .eol
    cmp     al, 13
    je      .scan
    inc     r8
    jmp     .scan
.eol:
    inc     r10
    test    r8, r8
    jz      .eol_done
    mov     r11, r10
    cmp     r8, r9
    jbe     .eol_done
    mov     r9, r8
.eol_done:
    xor     r8d, r8d
    jmp     .scan
.scan_end:
    test    r8, r8
    jz      .measured
    inc     r10
    mov     r11, r10
    cmp     r8, r9
    jbe     .measured
    mov     r9, r8
.measured:
    test    r9, r9
    jz      .empty_map
    test    r11, r11
    jz      .empty_map
    mov     [width], r9
    mov     [height], r11
    imul    r11, r9
    mov     [area], r11
    lea     rdi, [r11 * 2]
    call    alloc
    mov     [map_p], rax
    mov     rdi, rax
    mov     r8, [area]
    add     r8, rax
    mov     [items_p], r8
    mov     rsi, rbx
    lea     rdx, [rbx + r12]
    xor     r9d, r9d
.row:
    cmp     r9, [height]
    jae     .done
    xor     r10d, r10d
.cell:
    cmp     r10, [width]
    jae     .rest
    mov     eax, '#'
    cmp     rsi, rdx
    jae     .have
    movzx   ecx, byte [rsi]
    cmp     cl, 10
    je      .have
    cmp     cl, 13
    je      .have
    mov     eax, ecx
    inc     rsi
.have:
    mov     r14d, ' '
    mov     r15d, ' '
    cmp     al, '#'
    je      .wall
    cmp     al, '.'
    je      .goal
    cmp     al, '*'
    je      .goal_box
    cmp     al, '+'
    je      .goal_player
    cmp     al, '$'
    je      .box
    cmp     al, '@'
    je      .player
    jmp     .store
.wall:
    mov     r14d, '#'
    jmp     .store
.goal:
    mov     r14d, '.'
    jmp     .store
.goal_box:
    mov     r14d, '.'
.box:
    mov     r15d, '$'
    jmp     .store
.goal_player:
    mov     r14d, '.'
.player:
    mov     r15d, '@'
.store:
    mov     rax, r9
    imul    rax, [width]
    add     rax, r10
    mov     [rdi + rax], r14b
    mov     [r8 + rax], r15b
    inc     r10
    jmp     .cell
.rest:
    cmp     rsi, rdx
    jae     .next_row
    movzx   eax, byte [rsi]
    inc     rsi
    cmp     al, 10
    jne     .rest
.next_row:
    inc     r9
    jmp     .row
.done:
    xor     eax, eax
    POPALL
    ret
.empty_map:
    mov     eax, 1
    POPALL
    ret

check_cpu:
    push    rbx
    mov     eax, 1
    cpuid
    and     ecx, (1 << 27) | (1 << 28)
    cmp     ecx, (1 << 27) | (1 << 28)
    jne     .no
    xor     ecx, ecx
    xgetbv
    and     eax, 6
    cmp     eax, 6
    jne     .no
    mov     eax, 7
    xor     ecx, ecx
    cpuid
    and     ebx, (1 << 5) | (1 << 3)
    cmp     ebx, (1 << 5) | (1 << 3)
    jne     .no
    pop     rbx
    ret
.no:
    mov     eax, 1
    mov     edi, 2
    lea     rsi, [cpu_msg]
    mov     edx, cpu_msg_len
    syscall
    mov     eax, 60
    mov     edi, 3
    syscall

alloc:
    lea     rsi, [rdi + 4096]
    and     rsi, -4096
    mov     eax, 9
    xor     edi, edi
    mov     edx, 3
    mov     r10d, 0x4022
    mov     r8, -1
    xor     r9d, r9d
    syscall
    cmp     rax, -4096
    ja      .fail
    ret
.fail:
    mov     eax, 60
    mov     edi, 2
    syscall

alloc_huge:
    push    rdi
    call    alloc
    pop     rsi
    mov     rdi, rax
    mov     r9, rax
    mov     edx, 14
    mov     eax, 28
    syscall
    mov     rax, r9
    ret

alloc_all:
    PUSHALL
    mov     rax, [ncells]
    add     rax, 31
    and     rax, -32
    mov     [ncells32], rax
    mov     r12, rax
    lea     rbx, [rax*4]
    mov     rax, [nb]
    add     rax, 7
    and     rax, -8
    mov     [nbp], rax
    lea     r13, [rax*4]
    mov     [nbp4], r13
    mov     rax, r13
    neg     rax
    mov     [nbp4n], rax

    ALLOC   reach_p, r12
    ALLOC   came_p, r12
    ALLOC   stampA_p, rbx
    ALLOC   stampB_p, rbx
    ALLOC   queue_p, rbx
    mov     rdi, [ncells]
    shl     rdi, 3
    call    alloc
    mov     [zob_p], rax
    ALLOC   goals_p, r13
    ALLOC   sboxes_p, r13
    ALLOC   tbox_p, r13
    ALLOC   cbox_p, r13
    mov     rdi, [ncells]
    imul    rdi, r13
    call    alloc
    mov     [dist_p], rax
    mov     rdi, [nb]
    inc     rdi
    imul    rdi, r13
    call    alloc
    mov     [cost_p], rax
    lea     r14, [r13 + 32]
    ALLOC   hu_p, r14
    ALLOC   hv_p, r14
    ALLOC   hp_p, r14
    ALLOC   hway_p, r14
    ALLOC   hminv_p, r14
    ALLOC   hused_p, r14
    ALLOC   hused0_p, r14

    mov     rdi, rax
    mov     ecx, -1
    mov     rdx, [nb]
.pad:
    cmp     rdx, [nbp]
    jge     .keys
    mov     [rdi + rdx*4], ecx
    inc     rdx
    jmp     .pad

.keys:
    mov     rdi, [zob_p]
    mov     rax, 0x9E3779B97F4A7C15
    xor     ecx, ecx
.key:
    mov     rdx, rax
    shl     rdx, 13
    xor     rax, rdx
    mov     rdx, rax
    shr     rdx, 7
    xor     rax, rdx
    mov     rdx, rax
    shl     rdx, 17
    xor     rax, rdx
    mov     [rdi + rcx*8], rax
    inc     rcx
    cmp     rcx, [ncells]
    jl      .key

    lea     rax, [r13 + ST_BOXES]
    mov     [st_u], rax
    add     rax, r13
    mov     [st_v], rax
    add     rax, r13
    mov     [st_p], rax
    add     rax, r13
    mov     [recsize], rax
    mov     rdi, MAX_STATES
    imul    rdi, rax
    call    alloc_huge
    mov     [states_p], rax
    mov     [states_next], rax
    mov     rcx, [recsize]
    mov     rdx, rax
    sub     rdx, rcx
    mov     [states_base], rdx
    mov     rdx, MAX_STATES
    imul    rdx, rcx
    add     rdx, rax
    mov     rsi, [nb]
    shl     rsi, 2
    imul    rsi, rcx
    sub     rdx, rsi
    mov     [states_limit], rdx
    mov     rdi, HASH_SIZE * 4
    call    alloc_huge
    mov     [hhead_p], rax
    ALLOC   bhead_p, MAX_PRIO * 4
    ALLOC   out_p, OUT_SIZE
    POPALL
    ret

count_char:
    xor     eax, eax
.l:
    cmp     [rdi + rsi - 1], dl
    sete    cl
    movzx   ecx, cl
    add     rax, rcx
    dec     rsi
    jnz     .l
    ret

parse:
    PUSHALL
    mov     rax, [stride]
    mov     [steps + 8], rax
    neg     rax
    mov     [steps], rax
    mov     r12, [stampA_p]
    mov     r13, [stampB_p]
    mov     eax, -1
    mov     rdi, r12
    mov     rcx, [ncells32]
    rep stosd
    mov     rdi, r13
    mov     rcx, [ncells32]
    rep stosd
    mov     rsi, [map_p]
    mov     rdx, [items_p]
    mov     r14, [goals_p]
    mov     r15, [sboxes_p]
    xor     ebx, ebx
    xor     ebp, ebp
    xor     r8d, r8d
.row:
    xor     r9d, r9d
.col:
    mov     rax, r8
    imul    rax, [width]
    add     rax, r9
    lea     rcx, [r8 + 1]
    imul    rcx, [stride]
    lea     rcx, [rcx + r9 + 1]
    movzx   r10d, byte [rsi + rax]
    cmp     r10b, '#'
    je      .item
    mov     dword [r12 + rcx*4], 0
    mov     dword [r13 + rcx*4], 0
    cmp     r10b, '.'
    jne     .item
    mov     [r14 + rbx*4], ecx
    inc     rbx
.item:
    movzx   r10d, byte [rdx + rax]
    cmp     r10b, '$'
    jne     .player
    mov     [r15 + rbp*4], ecx
    inc     rbp
    jmp     .next
.player:
    cmp     r10b, '@'
    jne     .next
    mov     [start_player], rcx
.next:
    inc     r9
    cmp     r9, [width]
    jl      .col
    inc     r8
    cmp     r8, [height]
    jl      .row
    POPALL
    ret

init_push_distances:
    PUSHALL
    mov     r12, [stampA_p]
    mov     r13, [dist_p]
    mov     r14, [reach_p]
    mov     r15, [queue_p]
    mov     rbx, [sboxes_p]
    mov     rdi, r13
    mov     rcx, [ncells]
    imul    rcx, [nbp]
    mov     eax, -1
    rep stosd
    xor     ebp, ebp
.box:
    mov     eax, [rbx + rbp*4]
    mov     r9, rax
    imul    r9, [nbp4]
    add     r9, r13
    mov     dword [r9 + rbp*4], 0
    mov     byte [r14 + rax], 1
    mov     [r15], eax
    xor     ecx, ecx
    mov     edx, 1
.bfs:
    mov     esi, [r15 + rcx*4]
    inc     rcx
    mov     r9, rsi
    imul    r9, [nbp4]
    add     r9, r13
    mov     r10d, [r9 + rbp*4]
    inc     r10d
    xor     r11d, r11d
.dir:
    mov     rax, [steps + r11*8]
    lea     rdi, [rsi + rax]
    sub     rsi, rax
    cmp     dword [r12 + rdi*4], -1
    je      .skip
    cmp     dword [r12 + rsi*4], -1
    je      .skip
    mov     r9, rdi
    imul    r9, [nbp4]
    add     r9, r13
    cmp     dword [r9 + rbp*4], -1
    jne     .skip
    mov     [r9 + rbp*4], r10d
    mov     byte [r14 + rdi], 1
    mov     [r15 + rdx*4], edi
    inc     rdx
.skip:
    add     rsi, rax
    inc     r11d
    cmp     r11d, 4
    jl      .dir
    cmp     rcx, rdx
    jl      .bfs
    inc     rbp
    cmp     rbp, [nb]
    jl      .box
    POPALL
    ret

%macro VISIT 3
    lea     eax, [%1]
    cmp     [rdx + rax*4], r10d
    jae     %%skip
    mov     [rdx + rax*4], r10d
    mov     [rbx + r12*4], eax
%if %3
    mov     byte [r14 + rax], %2
%endif
    inc     r12
%%skip:
%endmacro

%macro FLOOD 1
    push    rbx
    push    rbp
    push    r12
    push    r13
%if %1
    push    r14
    mov     r14, [came_p]
%endif
    mov     rbx, [queue_p]
    mov     r10, [stamp]
    mov     [last_stamp], r10
    add     qword [stamp], 2
    lea     ecx, [r10 + 1]
    mov     rax, [nb]
.mark:
    mov     ebp, [rsi + rax*4 - 4]
    mov     [rdx + rbp*4], ecx
    dec     rax
    jnz     .mark
    mov     [rdx + rdi*4], r10d
    mov     [rbx], edi
    mov     r13, rdi
    mov     r8, [steps]
    mov     r9, [steps + 8]
    xor     ecx, ecx
    mov     r12d, 1
align 16
.bfs:
    mov     ebp, [rbx + rcx*4]
    inc     rcx
    cmp     rbp, r13
    cmovl   r13, rbp
    VISIT   rbp + r8, 0, %1
    VISIT   rbp + r9, 1, %1
    VISIT   rbp - 1, 2, %1
    VISIT   rbp + 1, 3, %1
    cmp     rcx, r12
    jl      .bfs
    mov     rax, r13
%if %1
    pop     r14
%endif
    pop     r13
    pop     r12
    pop     rbp
    pop     rbx
    ret
%endmacro

flood:      FLOOD 0
flood_dirs: FLOOD 1

hash_boxes:
    mov     rsi, [zob_p]
    mov     rcx, [nb]
    xor     eax, eax
.l:
    mov     edx, [rdi + rcx*4 - 4]
    xor     rax, [rsi + rdx*8]
    dec     rcx
    jnz     .l
    ret

hung_row:
    push    rbx
    push    r12
    push    r13
    push    r14
    push    r15
    mov     r12, rdi
    mov     rsi, [hv_p]
    add     rsi, [nbp4]
    mov     rdi, [hminv_p]
    add     rdi, [nbp4]
    mov     r10, [hused_p]
    add     r10, [nbp4]
    mov     r11, [hway_p]
    add     r11, [nbp4]
    vmovdqa ymm10, [pd_big]
    mov     rax, [hp_p]
    mov     rdx, [nbp4]
    mov     [rax + rdx], r12d
    mov     r13, [nbp]
    mov     rdx, [hused0_p]
    add     rdx, [nbp4]
    mov     rcx, [nbp4n]
.reset:
    vmovdqa [rdi + rcx], ymm10
    vmovdqa ymm0, [rdx + rcx]
    vmovdqa [r10 + rcx], ymm0
    add     rcx, 32
    jl      .reset

align 16
.step:
    mov     rax, [hused_p]
    mov     dword [rax + r13*4], -1
    mov     rax, [hp_p]
    mov     r14d, [rax + r13*4]
    mov     rax, [hu_p]
    vpbroadcastd ymm7, [rax + r14*4]
    vmovd   xmm8, r13d
    vpbroadcastd ymm8, xmm8
    mov     r8, r14
    imul    r8, [nbp4]
    add     r8, [cost_p]
    add     r8, [nbp4]
    vmovdqa ymm12, ymm10
    mov     rcx, [nbp4n]
.pass1:
    vmovdqa ymm0, [r8 + rcx]
    vpsubd  ymm0, ymm0, ymm7
    vpsubd  ymm0, ymm0, [rsi + rcx]
    vmovdqa ymm1, [rdi + rcx]
    vmovdqa ymm2, [r10 + rcx]
    vpcmpgtd ymm3, ymm1, ymm0
    vpandn  ymm3, ymm2, ymm3
    vpblendvb ymm1, ymm1, ymm0, ymm3
    vmovdqa [rdi + rcx], ymm1
    vmovdqa ymm4, [r11 + rcx]
    vpblendvb ymm4, ymm4, ymm8, ymm3
    vmovdqa [r11 + rcx], ymm4
    vpblendvb ymm5, ymm1, ymm10, ymm2
    vpminsd ymm12, ymm12, ymm5
    add     rcx, 32
    jl      .pass1
    vextracti128 xmm13, ymm12, 1
    vpminsd xmm13, xmm13, xmm12
    vpshufd xmm14, xmm13, 0x4E
    vpminsd xmm13, xmm13, xmm14
    vpshufd xmm14, xmm13, 0xB1
    vpminsd xmm13, xmm13, xmm14
    vmovd   r15d, xmm13
    vpbroadcastd ymm14, xmm13
    mov     rcx, [nbp4n]
.pass2:
    vmovdqa ymm1, [rdi + rcx]
    vmovdqa ymm2, [r10 + rcx]
    vpblendvb ymm5, ymm1, ymm10, ymm2
    vpcmpeqd ymm5, ymm5, ymm14
    vmovmskps eax, ymm5
    test    eax, eax
    jnz     .got
    add     rcx, 32
    jmp     .pass2
.got:
    tzcnt   eax, eax
    add     rcx, [nbp4]
    shr     ecx, 2
    lea     ebx, [rcx + rax]
    mov     rcx, [nbp4n]
.pass3:
    vmovdqa ymm2, [r10 + rcx]
    vpand   ymm3, ymm14, ymm2
    vpandn  ymm4, ymm2, ymm14
    vmovdqa ymm0, [rsi + rcx]
    vpsubd  ymm0, ymm0, ymm3
    vmovdqa [rsi + rcx], ymm0
    vmovdqa ymm0, [rdi + rcx]
    vpsubd  ymm0, ymm0, ymm4
    vmovdqa [rdi + rcx], ymm0
    vmovmskps eax, ymm2
    mov     edx, ecx
    add     edx, dword [nbp4]
    shr     edx, 2
.bits:
    test    eax, eax
    jz      .next
    tzcnt   r9d, eax
    blsr    eax, eax
    add     r9d, edx
    mov     r8, [hp_p]
    mov     r9d, [r8 + r9*4]
    mov     r8, [hu_p]
    add     [r8 + r9*4], r15d
    jmp     .bits
.next:
    add     rcx, 32
    jl      .pass3
    mov     rdx, [nbp4]
    mov     r8, [hp_p]
    mov     r9d, [r8 + rdx]
    mov     r8, [hu_p]
    add     [r8 + r9*4], r15d
    sub     [rsi], r15d
    mov     r13, rbx
    mov     rax, [hp_p]
    cmp     dword [rax + r13*4], 0
    jne     .step
.augment:
    mov     rax, [hway_p]
    mov     eax, [rax + r13*4]
    mov     rdx, [hp_p]
    mov     ecx, [rdx + rax*4]
    mov     [rdx + r13*4], ecx
    mov     r13, rax
    cmp     r13, [nbp]
    jne     .augment
    vzeroupper
    pop     r15
    pop     r14
    pop     r13
    pop     r12
    pop     rbx
    ret

hung_total:
    mov     r8, [hp_p]
    mov     r9, [cost_p]
    mov     r10, [nbp]
    xor     eax, eax
    xor     ecx, ecx
.l:
    mov     edx, [r8 + rcx*4]
    imul    rdx, r10
    add     rdx, rcx
    mov     edx, [r9 + rdx*4]
    add     rax, rdx
    inc     rcx
    cmp     rcx, [nb]
    jl      .l
    ret

hungarian:
    push    r12
    mov     rdx, [nbp]
    add     rdx, 8
    xor     eax, eax
    mov     rdi, [hu_p]
    mov     rcx, rdx
    rep stosd
    mov     rdi, [hv_p]
    mov     rcx, rdx
    rep stosd
    mov     rdi, [hp_p]
    mov     rcx, rdx
    rep stosd
    mov     r12d, 1
.row:
    mov     rdi, r12
    call    hung_row
    inc     r12
    cmp     r12, [nb]
    jle     .row
    call    hung_total
    pop     r12
    ret

fill_cost:
    vpcmpeqd ymm5, ymm5, ymm5
    vmovdqa ymm6, [pd_inf]
    mov     r10, [nbp4]
    mov     r8, [dist_p]
    add     r8, r10
    mov     r9, [cost_p]
    lea     r9, [r9 + r10*2]
    xor     edx, edx
.row:
    mov     eax, [rdi + rdx*4]
    imul    rax, r10
    add     rax, r8
    vpxor   ymm8, ymm8, ymm8
    mov     rcx, [nbp4n]
.copy:
    vmovdqa ymm0, [rax + rcx]
    vpcmpeqd ymm2, ymm0, ymm5
    vpandn  ymm3, ymm2, ymm5
    vpor    ymm8, ymm8, ymm3
    vpblendvb ymm0, ymm0, ymm6, ymm2
    vmovdqa [r9 + rcx], ymm0
    add     rcx, 32
    jl      .copy
    vptest  ymm8, ymm8
    jz      .dead
    add     r9, r10
    inc     rdx
    cmp     rdx, [nb]
    jl      .row
    xor     eax, eax
    vzeroupper
    ret
.dead:
    mov     eax, 1
    vzeroupper
    ret

heuristic:
    call    fill_cost
    test    eax, eax
    jnz     .dead
    call    hungarian
    cmp     rax, INF
    jle     .done
.dead:
    mov     eax, INF
.done:
    ret

heuristic_warm:
    push    rbx
    push    r12
    push    r13
    push    r14
    mov     rbx, rsi
    mov     r12, rdx
    mov     r13, rcx
    mov     r14, rdi
    call    fill_cost
    test    eax, eax
    jnz     .dead
    xor     r9d, r9d
.find_b:
    cmp     [r14 + r9*4], r13d
    je      .got_b
    inc     r9
    jmp     .find_b
.got_b:
    mov     r13, r9
    mov     rdx, [nbp]
    add     rdx, 8
    xor     eax, eax
    mov     rdi, [hu_p]
    mov     rcx, rdx
    rep stosd
    mov     rdi, [hv_p]
    mov     rcx, rdx
    rep stosd
    mov     rdi, [hp_p]
    mov     rcx, rdx
    rep stosd
    mov     rsi, rbx
    add     rsi, [st_v]
    add     rsi, [nbp4]
    mov     rdi, [hv_p]
    add     rdi, [nbp4]
    mov     rcx, [nbp4n]
    COPY32  rsi, rdi
    mov     r10, rbx
    add     r10, [st_u]
    mov     rdi, [hu_p]
    xor     ecx, ecx
.rows:
    cmp     rcx, r12
    je      .rows_next
    xor     edx, edx
    cmp     rcx, r12
    seta    dl
    mov     rax, rcx
    sub     rax, rdx
    xor     edx, edx
    cmp     rax, r13
    setae   dl
    add     rax, rdx
    mov     edx, [r10 + rcx*4]
    mov     [rdi + rax*4 + 4], edx
.rows_next:
    inc     rcx
    cmp     rcx, [nb]
    jl      .rows
    mov     r10, rbx
    add     r10, [st_p]
    mov     rsi, [hp_p]
    xor     ecx, ecx
.cols:
    mov     eax, [r10 + rcx*4]
    dec     rax
    cmp     rax, r12
    je      .cols_next
    xor     edx, edx
    cmp     rax, r12
    seta    dl
    sub     rax, rdx
    xor     edx, edx
    cmp     rax, r13
    setae   dl
    add     rax, rdx
    inc     rax
    mov     [rsi + rcx*4], eax
.cols_next:
    inc     rcx
    cmp     rcx, [nb]
    jl      .cols
    lea     rdi, [r13 + 1]
    call    hung_row
    call    hung_total
    cmp     rax, INF
    jle     .done
.dead:
    mov     eax, INF
.done:
    pop     r14
    pop     r13
    pop     r12
    pop     rbx
    ret

store_duals:
    mov     rdx, [nbp4]
    mov     rcx, [nbp4n]
    mov     rsi, [hu_p]
    lea     rsi, [rsi + rdx + 4]
    lea     rax, [rdi + rdx]
    add     rax, [st_u]
    COPY32  rsi, rax
    mov     rcx, [nbp4n]
    mov     rsi, [hv_p]
    add     rsi, rdx
    lea     rax, [rdi + rdx]
    add     rax, [st_v]
    COPY32  rsi, rax
    mov     rcx, [nbp4n]
    mov     rsi, [hp_p]
    add     rsi, rdx
    lea     rax, [rdi + rdx]
    add     rax, [st_p]
    COPY32  rsi, rax
    vzeroupper
    ret

find_state:
    mov     rax, rdx
    and     eax, HASH_SIZE - 1
    mov     r8, [hhead_p]
    mov     eax, [r8 + rax*4]
    mov     r9, [states_base]
    mov     r10, rsi
    add     r10, [nbp4]
.chain:
    test    eax, eax
    jz      .done
    lea     r11, [r9 + rax*8]
    cmp     [r11 + ST_HASH], rdx
    jne     .next
    cmp     [r11 + ST_PLAYER], edi
    jne     .next
    mov     rcx, [nbp4n]
    lea     r8, [r11 + ST_BOXES]
    add     r8, [nbp4]
.cmp:
    vmovdqu ymm0, [r8 + rcx]
    vpcmpeqd ymm0, ymm0, [r10 + rcx]
    vpmovmskb esi, ymm0
    cmp     esi, -1
    jne     .next
    add     rcx, 32
    jl      .cmp
    jmp     .done
.next:
    mov     eax, [r11 + ST_HNEXT]
    jmp     .chain
.done:
    vzeroupper
    ret

add_state:
    mov     r8, [states_next]
    mov     rax, [recsize]
    add     rax, r8
    mov     [states_next], rax
    mov     rax, r8
    sub     rax, [states_base]
    shr     rax, 3
    mov     [r8 + ST_PLAYER], edi
    mov     [r8 + ST_HASH], rdx
    mov     rcx, [nbp4n]
    add     rsi, [nbp4]
    lea     r9, [r8 + ST_BOXES]
    add     r9, [nbp4]
    COPY32  rsi, r9
    mov     r9, [hhead_p]
    mov     ecx, edx
    and     ecx, HASH_SIZE - 1
    mov     r10d, [r9 + rcx*4]
    mov     [r8 + ST_HNEXT], r10d
    mov     [r9 + rcx*4], eax
    mov     rdx, r8
    vzeroupper
    ret

push_bucket:
    mov     eax, MAX_PRIO - 1
    cmp     rsi, rax
    cmova   rsi, rax
    mov     rdx, [states_base]
    lea     rdx, [rdx + rdi*8]
    mov     r8, [bhead_p]
    mov     eax, [r8 + rsi*4]
    mov     [rdx + ST_BNEXT], eax
    mov     [r8 + rsi*4], edi
    mov     rax, [min_prio]
    cmp     rsi, rax
    cmovb   rax, rsi
    mov     [min_prio], rax
    ret

is_start:
    xor     eax, eax
    cmp     rdi, [norm_player]
    jne     .done
    mov     r8, [sboxes_p]
    add     r8, [nbp4]
    add     rsi, [nbp4]
    mov     rcx, [nbp4n]
.cmp:
    vmovdqu ymm0, [rsi + rcx]
    vpcmpeqd ymm0, ymm0, [r8 + rcx]
    vpmovmskb edx, ymm0
    cmp     edx, -1
    jne     .no
    add     rcx, 32
    jl      .cmp
    mov     eax, 1
.no:
    vzeroupper
.done:
    ret

search:
    PUSHALL
    mov     qword [min_prio], MAX_PRIO
    mov     rbp, [states_base]
    mov     rdi, [start_player]
    mov     rsi, [sboxes_p]
    mov     rdx, [stampB_p]
    call    flood
    mov     [norm_player], rax
    mov     rdi, [goals_p]
    call    heuristic
    cmp     rax, INF
    jge     .none
    lea     r12, [rax*4]

    mov     rax, [stamp]
    mov     [seed_stamp], rax
    mov     rdx, [stampA_p]
    mov     rsi, [goals_p]
    mov     rcx, [nb]
.pre:
    mov     edi, [rsi + rcx*4 - 4]
    mov     [rdx + rdi*4], eax
    dec     rcx
    jnz     .pre
    xor     r13d, r13d
.seed:
    cmp     r13, [ncells]
    jge     .loop
    mov     rdx, [stampA_p]
    mov     eax, [rdx + r13*4]
    cmp     eax, dword [seed_stamp]
    jae     .seed_next
    mov     rdi, r13
    mov     rsi, [goals_p]
    call    flood
    mov     rbx, rax
    mov     rdi, [goals_p]
    call    hash_boxes
    mov     rdx, rax
    mov     rdi, rbx
    mov     rsi, [goals_p]
    call    add_state
    mov     r14, rax
    mov     rdi, rdx
    call    store_duals
    mov     rdi, rbx
    mov     rsi, [goals_p]
    call    is_start
    test    eax, eax
    jnz     .none
    mov     rdi, r14
    mov     rsi, r12
    call    push_bucket
.seed_next:
    inc     r13
    jmp     .seed

.loop:
    mov     rax, [states_next]
    cmp     rax, [states_limit]
    jae     .none
    mov     r12, [min_prio]
    mov     rdx, [bhead_p]
.scan:
    cmp     r12, MAX_PRIO
    jge     .none
    mov     eax, [rdx + r12*4]
    test    eax, eax
    jnz     .pop
    inc     r12
    jmp     .scan
.pop:
    mov     [min_prio], r12
    mov     [cur_ref], rax
    lea     rbx, [rbp + rax*8]
    mov     ecx, [rbx + ST_BNEXT]
    mov     [rdx + r12*4], ecx
    mov     rcx, [nbp4n]
    lea     rsi, [rbx + ST_BOXES]
    add     rsi, [nbp4]
    mov     rdi, [tbox_p]
    add     rdi, [nbp4]
    COPY32  rsi, rdi
    mov     edi, [rbx + ST_PLAYER]
    mov     rsi, [tbox_p]
    mov     rdx, [stampA_p]
    call    flood
    mov     r12, [last_stamp]
    xor     r13d, r13d
.box:
    cmp     r13, [nb]
    jge     .loop
    xor     r14d, r14d
.dir:
    mov     r8, [tbox_p]
    mov     ecx, [r8 + r13*4]
    mov     rax, [steps + r14*8]
    lea     rdx, [rcx + rax]
    mov     r9, [stampA_p]
    cmp     [r9 + rdx*4], r12d
    jne     .next
    mov     r9, [reach_p]
    cmp     byte [r9 + rdx], 0
    je      .next
    lea     r15, [rdx + rax]
    mov     r9, [stampB_p]
    cmp     dword [r9 + r15*4], -1
    je      .next
    mov     rcx, [nb]
.free:
    cmp     [r8 + rcx*4 - 4], r15d
    je      .next
    dec     rcx
    jnz     .free

    mov     [pull_dest], rdx
    mov     rcx, [nbp4n]
    mov     rsi, r8
    add     rsi, [nbp4]
    mov     rdi, [cbox_p]
    add     rdi, [nbp4]
    COPY32  rsi, rdi
    mov     rsi, [cbox_p]
    mov     [rsi + r13*4], edx
    mov     ecx, 1
.sort:
    cmp     rcx, [nb]
    jge     .sorted
    mov     eax, [rsi + rcx*4]
    mov     rdi, rcx
.shift:
    test    rdi, rdi
    jz      .put
    mov     r8d, [rsi + rdi*4 - 4]
    cmp     r8d, eax
    jbe     .put
    mov     [rsi + rdi*4], r8d
    dec     rdi
    jmp     .shift
.put:
    mov     [rsi + rdi*4], eax
    inc     rcx
    jmp     .sort
.sorted:
    mov     r8, [tbox_p]
    mov     ecx, [r8 + r13*4]
    mov     r9, [zob_p]
    mov     rax, [rbx + ST_HASH]
    xor     rax, [r9 + rcx*8]
    mov     rdx, [pull_dest]
    xor     rax, [r9 + rdx*8]
    mov     [child_hash], rax
    mov     ecx, eax
    and     ecx, HASH_SIZE - 1
    mov     r8, [hhead_p]
    prefetcht0 [r8 + rcx*4]
    mov     rdi, r15
    mov     rsi, [cbox_p]
    mov     rdx, [stampB_p]
    call    flood
    mov     [new_player], rax
    mov     rdi, rax
    mov     rsi, [cbox_p]
    mov     rdx, [child_hash]
    call    find_state
    test    eax, eax
    jnz     .next
    mov     rdi, [cbox_p]
    mov     rsi, rbx
    mov     rdx, r13
    mov     rcx, [pull_dest]
    call    heuristic_warm
    cmp     rax, INF
    jge     .next
    mov     [cur_h], rax
    mov     rdi, [new_player]
    mov     rsi, [cbox_p]
    mov     rdx, [child_hash]
    call    add_state
    mov     r15, rax
    mov     rdi, rdx
    call    store_duals
    lea     rdx, [rbp + r15*8]
    mov     rax, [cur_ref]
    mov     [rdx + ST_PARENT], eax
    mov     r8, [tbox_p]
    mov     ecx, [r8 + r13*4]
    mov     rax, [pull_dest]
    mov     [rdx + ST_SRC], ecx
    mov     [rdx + ST_DST], eax
    mov     r9d, [rbx + ST_SW]
    cmp     dword [rbx + ST_PARENT], 0
    je      .sw
    cmp     [rbx + ST_DST], ecx
    je      .sw
    inc     r9d
.sw:
    mov     [rdx + ST_SW], r9d
    mov     rax, [cur_h]
    lea     rax, [rax*4]
    lea     r9, [r9 + r9*2]
    mov     rdi, r15
    mov     rsi, rax
    call    push_bucket
    mov     rdi, [new_player]
    mov     rsi, [cbox_p]
    call    is_start
    test    eax, eax
    jnz     .found
.next:
    inc     r14
    cmp     r14, 4
    jl      .dir
    inc     r13
    jmp     .box

.found:
    mov     rdi, r15
    call    to_moves
    jmp     .out
.none:
    xor     eax, eax
.out:
    POPALL
    ret

to_moves:
    PUSHALL
    mov     rbx, [states_base]
    lea     rbx, [rbx + rdi*8]
    mov     rcx, [nbp4n]
    mov     rsi, [sboxes_p]
    add     rsi, [nbp4]
    mov     rdi, [tbox_p]
    add     rdi, [nbp4]
    COPY32  rsi, rdi
    mov     r12, [start_player]
    mov     rbp, [out_p]
    xor     r15d, r15d
.trace:
    cmp     dword [rbx + ST_PARENT], 0
    je      .done
    mov     r13d, [rbx + ST_SRC]
    mov     r14d, [rbx + ST_DST]
    lea     rax, [r14*2]
    sub     rax, r13
    mov     [stand], rax
    mov     rdi, r12
    mov     rsi, [tbox_p]
    mov     rdx, [stampB_p]
    call    flood_dirs
    mov     rsi, [came_p]
    mov     rax, [stand]
    lea     r8, [rbp + r15]
    mov     r9, r8
.back:
    cmp     rax, r12
    je      .reverse
    movzx   edx, byte [rsi + rax]
    movzx   ecx, byte [dir_chars + rdx]
    mov     [r9], cl
    inc     r9
    sub     rax, [steps + rdx*8]
    jmp     .back
.reverse:
    mov     r15, r9
    sub     r15, rbp
    dec     r9
.swap:
    cmp     r8, r9
    jae     .push
    mov     al, [r8]
    mov     cl, [r9]
    mov     [r8], cl
    mov     [r9], al
    inc     r8
    dec     r9
    jmp     .swap
.push:
    mov     rax, r13
    sub     rax, r14
    xor     ecx, ecx
.find_dir:
    cmp     rax, [steps + rcx*8]
    je      .got_dir
    inc     rcx
    jmp     .find_dir
.got_dir:
    movzx   edx, byte [dir_chars + rcx]
    mov     [rbp + r15], dl
    inc     r15
    mov     r8, [tbox_p]
    xor     ecx, ecx
.find_box:
    cmp     [r8 + rcx*4], r14d
    je      .move_box
    inc     rcx
    jmp     .find_box
.move_box:
    mov     [r8 + rcx*4], r13d
    mov     r12, r14
    mov     eax, [rbx + ST_PARENT]
    mov     rbx, [states_base]
    lea     rbx, [rbx + rax*8]
    jmp     .trace
.done:
    mov     rax, r15
    POPALL
    ret
