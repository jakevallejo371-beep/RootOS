bits 32 ; generate protected mode

section .multiboot
align 4
    dd 0x1BADB002   ; magic number GRUB looks for
    dd 0x00 ; flags
    dd -(0x1BADB002 + 0x00) ; checksum (magic+flags+checksum must sum to 0)

global _start
extern main

section .text
_start:
    mov esp, stack_top  ; move stack pointer to register esp

    call main

    cli

global hang

hang:   ; infinite fallback loop
    hlt
    jmp hang   ; jump to hang if cpu wakes up


extern test_interrupt_handler_output
extern exception_handler

global test_isr

test_isr:
    pushad
    cld
    call test_interrupt_handler_output
    popad
    iretd


global keyboard_isr
extern keyboard_handler

keyboard_isr:
    pushad
    cld
    call keyboard_handler
    popad
    iretd

%macro noERRORcode 1
global isr%1
isr%1:
    push dword 0    ; Push a dummy error code 0
    push dword %1   ; Push the interrupt vector number
    jmp common_sub
%endmacro

%macro ERRORcode 1
global isr%1
isr%1:
    ; Error code is automatically pushed by the CPU here
    push dword %1   ; Push the interrupt vector number
    jmp common_sub
%endmacro


common_sub:
    pushad
    cld
    push eax [esp + 36]
    push ebx [esp + 32]
    call exception_handler
    add esp, 8
    popad
    add esp, 8
    iretd

extern terminal

global recovery

recovery:
    sti
    call terminal
    cli
    jmp hang

; all 32 invocation

noERRORcode 0
noERRORcode 1
noERRORcode 2
noERRORcode 3
noERRORcode 4
noERRORcode 5
noERRORcode 6
noERRORcode 7
ERRORcode 8
noERRORcode 9
ERRORcode 10
ERRORcode 11
ERRORcode 12
ERRORcode 13
ERRORcode 14
noERRORcode 15
noERRORcode 16
ERRORcode 17
noERRORcode 18
noERRORcode 19
noERRORcode 20
ERRORcode 21
noERRORcode 22
noERRORcode 23
noERRORcode 24
noERRORcode 25
noERRORcode 26
noERRORcode 27
noERRORcode 28
noERRORcode 29
ERRORcode 30
noERRORcode 31

section .bss    ; storage allocation
align 16    ; starts the stack on a 16-byte memory boundary
stack_bottom:
    resb 16384  ; reserves 16 kilobytes of raw bytes
stack_top: