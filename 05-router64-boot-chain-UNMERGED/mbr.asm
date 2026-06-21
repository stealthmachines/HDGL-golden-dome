; HDGL MBR — loads stage2 from sector 1, jumps to it
; Native to HDGL: Omega state machine bootstraps here
[bits 16]
[org 0x7C00]

start:
    cli
    mov ax, 0
    mov ss, ax
    mov sp, 0x7C00
    mov ds, ax
    mov es, ax
    sti

    ; BIOS hands us the boot drive number in DL at entry.
    ; The COM1 banner code below clobbers DX (it sets dx=0x3F8/0x3FD for
    ; the UART and never restores the original DL), so we must latch the
    ; boot drive into memory now and restore it right before int 0x13.
    mov [boot_drive], dl

    ; Print banner on COM1 (9600 8N1)
    mov dx, 0x3F8      ; COM1 data
    mov al, 0x41       ; 'A'
    call .com1_tx
    mov al, 0x58       ; 'X'
    call .com1_tx
    mov al, 0x49       ; 'I'
    call .com1_tx
    mov al, 0x4F       ; 'O'
    call .com1_tx
    mov al, 0x4D       ; 'M'
    call .com1_tx
    mov al, 0x0D
    call .com1_tx
    mov al, 0x0A
    call .com1_tx

    ; Load sector 1 (stage2 only — canonical layout: sector 0=MBR,
    ; sector 1=stage2, sectors 2..65=runtime64; see Part XII DISK_IMAGE)
    mov ah, 0x02       ; INT 13h read
    mov al, 1          ; sectors to read — stage2 is exactly 1 sector
    mov ch, 0          ; cylinder 0
    mov cl, 2          ; start sector 2 (1-indexed = LBA 1)
    mov dh, 0          ; head 0
    mov dl, [boot_drive] ; restore boot drive (DX was clobbered by COM1 banner)
    mov bx, 0x7E00     ; load address
    int 0x13
    jc  .disk_error

    ; Jump to stage2
    jmp 0x0000:0x7E00

.disk_error:
    mov al, 0x45       ; 'E'
    call .com1_tx
    jmp $

.com1_tx:
    push dx
    push ax
    mov dx, 0x3FD      ; LSR
.tx_wait:
    in al, dx
    test al, 0x20
    jz .tx_wait
    pop ax
    mov dx, 0x3F8
    out dx, al
    pop dx
    ret

boot_drive: db 0

times 510-($-$$) db 0
dw 0xAA55
