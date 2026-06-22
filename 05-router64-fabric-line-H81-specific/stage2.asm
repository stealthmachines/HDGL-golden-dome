; HDGL Stage2 — A20, PM, LM, load runtime64 at 0x9000, jump
[bits 16]
[org 0x7E00]

    cli
    mov ax, 0
    mov ss, ax
    mov sp, 0x7C00
    mov ds, ax
    mov es, ax
    sti

    ; BIOS/MBR hands the boot drive through in DL (the far jmp from the MBR
    ; doesn't touch it). Same defensive latch as mbr.asm's fix: the COM1
    ; calls below don't currently clobber DL (none of them pre-load dx
    ; before calling .tx16, unlike the original MBR bug), but latch it
    ; explicitly anyway so this isn't fragile against future edits.
    mov [boot_drive], dl

    ; COM1: "S2\r\n"
    mov al, 0x53 & 0xFF
    call .tx16
    mov al, 0x32
    call .tx16
    mov al, 0x0D
    call .tx16
    mov al, 0x0A
    call .tx16

    ; Enable A20 via port 0x92
    in  al, 0x92
    or  al, 0x02
    out 0x92, al

    ; Load runtime64: 64 sectors from canonical LBA 2 to physical 0x9000
    ; Canonical disk layout (Part XII DISK_IMAGE): sector 0=MBR, sector 1=
    ; stage2 (this file, exactly 1 sector), sectors 2..65=runtime64 (64
    ; sectors). LBA 2 = CHS cylinder 0, head 0, sector 3 (1-indexed).
    mov bx, 0x9000     ; load address
    mov cl, 3          ; sector 3 (1-based) = LBA 2
    mov ch, 0
    mov dh, 0
    mov dl, [boot_drive] ; restore boot drive
    mov ah, 0x02
    mov al, 64         ; read all 64 sectors of runtime64 (32768 bytes)
    int 0x13
    jc  .disk_err

    ; COM1: "RT\r\n" (runtime loaded)
    mov al, 0x52
    call .tx16
    mov al, 0x54
    call .tx16
    mov al, 0x0D
    call .tx16
    mov al, 0x0A
    call .tx16

    ; Load GDT and enter protected mode
    lgdt [.gdt_ptr]
    mov eax, cr0
    or  eax, 1
    mov cr0, eax
    jmp 0x08:.pm32

.disk_err:
    mov al, 0x45   ; 'E'
    call .tx16
    jmp $

.tx16:
    push dx
    push ax
    mov dx, 0x3FD
.tw:
    in al, dx
    test al, 0x20
    jz .tw
    pop ax
    mov dx, 0x3F8
    out dx, al
    pop dx
    ret

[bits 32]
.pm32:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov esp, 0x7C00

    ; COM1 "PM"
    mov al, 0x50
    call .tx32
    mov al, 0x4D
    call .tx32
    mov al, 0x0D
    call .tx32
    mov al, 0x0A
    call .tx32

    ; Page tables at 0x1000..0x3FFF
    mov edi, 0x1000
    mov ecx, 0x3000 / 4
    mov eax, 0
    rep stosd

    mov dword [0x1000], 0x2003   ; PML4[0] -> PDPT
    mov dword [0x2000], 0x3003   ; PDPT[0] -> PD
    ; Map 0..10MB as 2MB pages
    mov dword [0x3000], 0x000083
    mov dword [0x3008], 0x200083
    mov dword [0x3010], 0x400083
    mov dword [0x3018], 0x600083
    mov dword [0x3020], 0x800083

    ; PAE
    mov eax, cr4
    or  eax, 0x20
    mov cr4, eax
    mov eax, 0x1000
    mov cr3, eax

    ; EFER.LME
    mov ecx, 0xC0000080
    rdmsr
    or  eax, 0x100
    wrmsr

    ; Paging on -> LM active
    mov eax, cr0
    or  eax, 0x80000001
    mov cr0, eax
    jmp 0x18:.lm64

[bits 64]
.lm64:
    mov ax, 0x20
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov rsp, 0x9F000

    ; COM1 "LM"
    mov rdx, 0x3F8
    mov al, 0x4C
    call .tx64
    mov al, 0x4D
    call .tx64
    mov al, 0x0D
    call .tx64
    mov al, 0x0A
    call .tx64

    jmp 0x9000

.tx64:
    push rdx
    push rax
    mov rdx, 0x3FD
.t64w:
    in al, dx
    test al, 0x20
    jz .t64w
    pop rax
    mov rdx, 0x3F8
    out dx, al
    pop rdx
    ret

[bits 32]
.tx32:
    push edx
    push eax
    mov edx, 0x3FD
.t32w:
    in al, dx
    test al, 0x20
    jz .t32w
    pop eax
    mov edx, 0x3F8
    out dx, al
    pop edx
    ret

align 8
.gdt:
    dq 0
    dq 0x00CF9A000000FFFF   ; 0x08 code32
    dq 0x00CF92000000FFFF   ; 0x10 data32
    dq 0x00AF9A000000FFFF   ; 0x18 code64
    dq 0x00AF92000000FFFF   ; 0x20 data64
.gdt_end:
.gdt_ptr:
    dw .gdt_end - .gdt - 1
    dd .gdt

boot_drive: db 0

times (1*512)-($-$$) db 0
