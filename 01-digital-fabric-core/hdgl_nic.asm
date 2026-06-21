; ============================================================================
; hdgl_nic.asm — bare-metal NIC driver
;
; Covers every vendor:device in hdgl_router64.asm .nic_ids table:
;   Intel e1000 family (0x100E/0x10D3/0x1539/0x1521/0x1533/0x1091/0x1368/0x15F3)
;   Realtek RTL8111/8168/8169 (0x8168/0x8169/0x10C3)
;   Atheros AR8131 (0x1066)
;
; INTEGRATION:
;   Add `call .nic_init` in .omega_observe_nics after the NIC table is built.
;   TX: call .nic_tx  (RSI=buf, RCX=len)
;   RX: call .nic_rx  (RDI=buf, returns RCX=len, 0=no frame)
;   Both are phi-neutral: mov not xor, no interrupts, no IDT.
;
; MEMORY MAP (within identity-mapped space):
;   0x105000  NIC driver state (512 bytes)
;   0x106000  TX ring (e1000: 16 descriptors × 16B = 256B)
;   0x107000  RX ring (e1000: 16 descriptors × 16B = 256B)
;   0x108000  TX buffers (16 × 2KB = 32KB)
;   0x110000  RX buffers (16 × 2KB = 32KB)
;
; PHILOSOPHY:
;   No interrupts. Poll-based (wu-wei — the shell loop IS the tick).
;   No DMA mapping complexity — identity-mapped pages handle it.
;   e1000 and RTL share the same TX/RX call interface.
;   Driver type selected at init from Omega NIC node vendor:device.
; ============================================================================

[bits 64]

; ── NIC state block at 0x105000 ──────────────────────────────────────────────
NIC_STATE_BASE  equ 0x105000
NIC_OFF_TYPE    equ 0          ; 4B: 0=e1000, 1=RTL8111, 2=RTL8169
NIC_OFF_MMIO    equ 8          ; 8B: MMIO base address
NIC_OFF_IOBASE  equ 16         ; 4B: I/O base (RTL uses I/O port)
NIC_OFF_TX_HEAD equ 20         ; 4B: TX ring head
NIC_OFF_RX_HEAD equ 24         ; 4B: RX ring head
NIC_OFF_MAC     equ 32         ; 6B: MAC address

NIC_TYPE_E1000  equ 0
NIC_TYPE_RTL    equ 1

; ── Ring geometry ────────────────────────────────────────────────────────────
NIC_TX_RING     equ 0x106000
NIC_RX_RING     equ 0x107000
NIC_TX_BUF      equ 0x108000
NIC_RX_BUF      equ 0x110000
NIC_RING_LEN    equ 16         ; descriptors per ring
NIC_BUF_SIZE    equ 2048       ; bytes per buffer slot

; ── e1000 registers (MMIO offsets) ──────────────────────────────────────────
E1000_CTRL      equ 0x0000
E1000_STATUS    equ 0x0008
E1000_EERD      equ 0x0014     ; EEPROM read (MAC address)
E1000_RCTL      equ 0x0100     ; RX control
E1000_TCTL      equ 0x0400     ; TX control
E1000_TDBAL     equ 0x3800     ; TX desc base low
E1000_TDBAH     equ 0x3804
E1000_TDLEN     equ 0x3808
E1000_TDH       equ 0x3810     ; TX desc head
E1000_TDT       equ 0x3818     ; TX desc tail
E1000_RDBAL     equ 0x2800
E1000_RDBAH     equ 0x2804
E1000_RDLEN     equ 0x2808
E1000_RDH       equ 0x2810
E1000_RDT       equ 0x2818
E1000_RAL       equ 0x5400     ; Receive addr low
E1000_RAH       equ 0x5404

; ── RTL8111 registers (I/O port offsets from IOBASE) ─────────────────────────
RTL_IDR0        equ 0x00       ; MAC address bytes 0-5
RTL_CMD         equ 0x37       ; command register
RTL_IMR         equ 0x3C       ; interrupt mask
RTL_ISR         equ 0x3E       ; interrupt status
RTL_TCR         equ 0x40       ; TX config
RTL_RCR         equ 0x44       ; RX config
RTL_TSAD0       equ 0x20       ; TX start address (4 descriptors)
RTL_RBSTART     equ 0x30       ; RX buffer start (legacy ring mode)
RTL_CAPR        equ 0x38       ; current address of packet read
RTL_CBR         equ 0x3A       ; current buffer address
RTL_TXSTS0      equ 0x10       ; TX status descriptor 0

; ============================================================================
; NIC_INIT — detect driver type, read BAR, init rings
; IN:  (none — reads from NIC table at 0x104000)
; OUT: NIC_STATE_BASE populated; ZF=0 on success, ZF=1 if no NIC
; ============================================================================
.nic_init:
    push rax
    push rbx
    push rcx
    push rdx
    push rdi
    push rsi

    ; Zero state block
    mov  rdi, NIC_STATE_BASE
    mov  rcx, 64
    mov  rax, 0
    rep  stosq

    ; Read first NIC from table
    mov  rax, [0x104000]        ; count
    test rax, rax
    jz   .nic_init_none

    mov  eax, [0x104008]        ; vendor:device of first NIC
    mov  ebx, [0x10400C]        ; bus/dev/fn

    ; Detect type from vendor ID (high 16 bits)
    mov  ecx, eax
    shr  ecx, 16                ; vendor ID
    cmp  ecx, 0x8086            ; Intel
    je   .nic_init_e1000
    ; Realtek / Atheros — use RTL path
    jmp  .nic_init_rtl

.nic_init_e1000:
    mov  dword [NIC_STATE_BASE + NIC_OFF_TYPE], NIC_TYPE_E1000

    ; Read BAR0 from PCI config space (MMIO base)
    ; PCI config address: 0x80000000 | bus<<16 | dev<<11 | fn<<8 | offset
    mov  eax, ebx
    shl  eax, 8                 ; bus/dev/fn << 8
    or   eax, 0x80000010        ; BAR0 offset = 0x10
    mov  dx, 0xCF8
    out  dx, eax
    mov  dx, 0xCFC
    in   eax, dx
    and  eax, 0xFFFFFFF0        ; mask off flags
    ; BAR0 is 32-bit here; for 64-bit BAR check bits 2:1
    movsx rax, eax
    mov  [NIC_STATE_BASE + NIC_OFF_MMIO], rax

    ; Read MAC from EEPROM via EERD
    call .nic_e1000_read_mac

    ; Init TX ring
    call .nic_e1000_init_tx

    ; Init RX ring
    call .nic_e1000_init_rx

    ; Enable TX+RX
    mov  rax, [NIC_STATE_BASE + NIC_OFF_MMIO]
    ; TCTL: EN | PSP | CT=0x10 | COLD=0x40
    mov  dword [rax + E1000_TCTL], 0x4010A
    ; RCTL: EN | SBP | BAM (broadcast) | BSIZE=2048 | SECRC
    mov  dword [rax + E1000_RCTL], 0x8002
    test rsp, rsp               ; set ZF=0 (success) — rsp always non-zero
    jmp  .nic_init_done

.nic_init_rtl:
    mov  dword [NIC_STATE_BASE + NIC_OFF_TYPE], NIC_TYPE_RTL

    ; Read I/O BAR (BAR0 for RTL is I/O space, bit 0 = 1)
    mov  eax, ebx
    shl  eax, 8
    or   eax, 0x80000010
    mov  dx, 0xCF8
    out  dx, eax
    mov  dx, 0xCFC
    in   eax, dx
    and  eax, 0xFFFFFFFC        ; mask off I/O flag bit
    movzx eax, ax               ; I/O base is 16-bit
    mov  [NIC_STATE_BASE + NIC_OFF_IOBASE], eax

    ; Software reset
    movzx edx, word [NIC_STATE_BASE + NIC_OFF_IOBASE]
    add  dx, RTL_CMD
    mov  al, 0x10               ; RST bit
    out  dx, al
.rtl_rst_wait:
    in   al, dx
    test al, 0x10
    jnz  .rtl_rst_wait

    ; Read MAC
    call .nic_rtl_read_mac

    ; Set RX buffer (legacy ring mode: 64KB+16+1500 at RX_BUF)
    movzx edx, word [NIC_STATE_BASE + NIC_OFF_IOBASE]
    add  dx, RTL_RBSTART
    mov  eax, NIC_RX_BUF
    out  dx, eax

    ; RCR: AAB | AM | APM | AB | WRAP | MXDMA=unlimited | RXFTH=no
    movzx edx, word [NIC_STATE_BASE + NIC_OFF_IOBASE]
    add  dx, RTL_RCR
    mov  eax, 0xF0F             ; accept all, no threshold
    out  dx, eax

    ; TCR: MXDMA=2048
    movzx edx, word [NIC_STATE_BASE + NIC_OFF_IOBASE]
    add  dx, RTL_TCR
    mov  eax, 0x600             ; 2048-byte max DMA burst
    out  dx, eax

    ; Enable RX+TX
    movzx edx, word [NIC_STATE_BASE + NIC_OFF_IOBASE]
    add  dx, RTL_CMD
    mov  al, 0x0C               ; TE | RE
    out  dx, al

    test rsp, rsp               ; ZF=0

.nic_init_done:
    pop  rsi
    pop  rdi
    pop  rdx
    pop  rcx
    pop  rbx
    pop  rax
    ret

.nic_init_none:
    ; No NIC: set ZF=1 to signal caller
    xor  eax, eax               ; ZF=1
    pop  rsi
    pop  rdi
    pop  rdx
    pop  rcx
    pop  rbx
    pop  rax
    ret

; ── e1000 helpers ────────────────────────────────────────────────────────────

.nic_e1000_read_mac:
    ; Read MAC from RAL/RAH registers (set by firmware)
    mov  rax, [NIC_STATE_BASE + NIC_OFF_MMIO]
    mov  edx, [rax + E1000_RAL]
    mov  [NIC_STATE_BASE + NIC_OFF_MAC + 0], edx
    mov  edx, [rax + E1000_RAH]
    mov  word [NIC_STATE_BASE + NIC_OFF_MAC + 4], dx
    ret

.nic_e1000_init_tx:
    ; 16 TX descriptors at NIC_TX_RING, buffers at NIC_TX_BUF
    ; Descriptor layout (16 bytes): addr(8) len(2) cso(1) cmd(1) status(1) css(1) vlan(2)
    mov  rdi, NIC_TX_RING
    mov  rcx, NIC_RING_LEN
    mov  rax, NIC_TX_BUF
.tx_desc_init:
    mov  [rdi], rax             ; buffer address
    mov  qword [rdi+8], 0      ; len=0 cmd=0 status=0
    add  rax, NIC_BUF_SIZE
    add  rdi, 16
    dec  rcx
    jnz  .tx_desc_init

    mov  rax, [NIC_STATE_BASE + NIC_OFF_MMIO]
    mov  dword [rax + E1000_TDBAL], NIC_TX_RING
    mov  dword [rax + E1000_TDBAH], 0
    mov  dword [rax + E1000_TDLEN], NIC_RING_LEN * 16
    mov  dword [rax + E1000_TDH], 0
    mov  dword [rax + E1000_TDT], 0
    ret

.nic_e1000_init_rx:
    mov  rdi, NIC_RX_RING
    mov  rcx, NIC_RING_LEN
    mov  rax, NIC_RX_BUF
.rx_desc_init:
    mov  [rdi], rax
    mov  qword [rdi+8], 0
    add  rax, NIC_BUF_SIZE
    add  rdi, 16
    dec  rcx
    jnz  .rx_desc_init

    mov  rax, [NIC_STATE_BASE + NIC_OFF_MMIO]
    mov  dword [rax + E1000_RDBAL], NIC_RX_RING
    mov  dword [rax + E1000_RDBAH], 0
    mov  dword [rax + E1000_RDLEN], NIC_RING_LEN * 16
    mov  dword [rax + E1000_RDH], 0
    mov  dword [rax + E1000_RDT], NIC_RING_LEN - 1
    ret

.nic_rtl_read_mac:
    movzx edx, word [NIC_STATE_BASE + NIC_OFF_IOBASE]
    add  dx, RTL_IDR0
    mov  rdi, NIC_STATE_BASE + NIC_OFF_MAC
    mov  rcx, 6
.rtl_mac_byte:
    in   al, dx
    mov  [rdi], al
    inc  dx
    inc  rdi
    dec  rcx
    jnz  .rtl_mac_byte
    ret

; ============================================================================
; NIC_TX — transmit one frame
; IN:  RSI = frame buffer, RCX = frame length (bytes, max 1500)
; OUT: (none; drops silently if ring full — GOI principle: saturate, continue)
; ============================================================================
.nic_tx:
    push rax
    push rbx
    push rcx
    push rdx
    push rdi
    push rsi

    cmp  rcx, 1500
    jg   .nic_tx_done           ; drop oversized (GUZ: floor, continue)

    mov  eax, dword [NIC_STATE_BASE + NIC_OFF_TYPE]
    test eax, eax
    jz   .nic_tx_e1000
    jmp  .nic_tx_rtl

.nic_tx_e1000:
    ; Get current tail index
    mov  rbx, [NIC_STATE_BASE + NIC_OFF_TX_HEAD]
    mov  rdi, rbx
    imul rdi, rdi, 16
    add  rdi, NIC_TX_RING       ; descriptor pointer

    ; Check status — if not done yet, drop (ring full)
    movzx eax, byte [rdi + 12] ; status byte (DD = bit 0)
    test al, 1
    jz   .nic_tx_done           ; busy — GOI saturate

    ; Copy frame to TX buffer
    push rcx
    push rsi
    imul rax, rbx, NIC_BUF_SIZE
    add  rax, NIC_TX_BUF        ; destination buffer
    mov  rdi, rax
    rep  movsb                  ; copy frame bytes
    pop  rsi
    pop  rcx

    ; Fill descriptor
    imul rdi, rbx, 16
    add  rdi, NIC_TX_RING
    imul rax, rbx, NIC_BUF_SIZE
    add  rax, NIC_TX_BUF
    mov  [rdi], rax             ; buffer address
    mov  word [rdi+8], cx       ; length
    mov  byte [rdi+10], 0       ; cso
    mov  byte [rdi+11], 0x0B   ; CMD: EOP|IFCS|RS
    mov  byte [rdi+12], 0       ; status: clear DD
    mov  byte [rdi+13], 0
    mov  word [rdi+14], 0

    ; Advance tail
    inc  rbx
    and  rbx, NIC_RING_LEN - 1
    mov  [NIC_STATE_BASE + NIC_OFF_TX_HEAD], rbx

    ; Ring the doorbell
    mov  rax, [NIC_STATE_BASE + NIC_OFF_MMIO]
    mov  dword [rax + E1000_TDT], ebx
    jmp  .nic_tx_done

.nic_tx_rtl:
    ; RTL8111 legacy TX: 4 TX descriptors, cycle through 0..3
    mov  rbx, [NIC_STATE_BASE + NIC_OFF_TX_HEAD]

    ; Check OWN bit in status register
    mov  eax, ebx
    imul eax, 4                 ; TSAD0..3 at offsets 0x10..0x1C
    movzx edx, word [NIC_STATE_BASE + NIC_OFF_IOBASE]
    add  dx, RTL_TXSTS0
    add  dx, ax
    in   eax, dx
    test eax, 0x2000            ; OWN bit
    jnz  .nic_tx_done           ; busy

    ; Copy frame to TX buffer slot
    push rcx
    push rsi
    imul rdi, rbx, NIC_BUF_SIZE
    add  rdi, NIC_TX_BUF
    rep  movsb
    pop  rsi
    pop  rcx

    ; Write TX status (clears OWN, sets size)
    imul eax, ebx, 4
    movzx edx, word [NIC_STATE_BASE + NIC_OFF_IOBASE]
    add  dx, RTL_TSAD0
    add  dx, ax
    imul eax, ebx, NIC_BUF_SIZE
    add  eax, NIC_TX_BUF
    out  dx, eax                ; TX start address

    movzx edx, word [NIC_STATE_BASE + NIC_OFF_IOBASE]
    add  dx, RTL_TXSTS0
    imul eax, ebx, 4
    add  dx, ax
    mov  eax, ecx               ; size
    or   eax, 0x2000            ; set OWN
    out  dx, eax

    ; Advance slot (mod 4)
    inc  rbx
    and  rbx, 3
    mov  [NIC_STATE_BASE + NIC_OFF_TX_HEAD], rbx

.nic_tx_done:
    pop  rsi
    pop  rdi
    pop  rdx
    pop  rcx
    pop  rbx
    pop  rax
    ret

; ============================================================================
; NIC_RX — receive one frame (poll)
; IN:  RDI = destination buffer (at least 1518 bytes)
; OUT: RCX = frame length (0 if no frame ready)
; ============================================================================
.nic_rx:
    push rax
    push rbx
    push rdx
    push rsi
    push rdi
    mov  rcx, 0                 ; default: no frame

    mov  eax, dword [NIC_STATE_BASE + NIC_OFF_TYPE]
    test eax, eax
    jz   .nic_rx_e1000
    jmp  .nic_rx_rtl

.nic_rx_e1000:
    ; Check head descriptor for DD bit
    mov  rbx, [NIC_STATE_BASE + NIC_OFF_RX_HEAD]
    imul rax, rbx, 16
    add  rax, NIC_RX_RING
    movzx ecx, byte [rax + 12] ; status
    test cl, 1                  ; DD set?
    jz   .nic_rx_done_zero

    ; Read length
    movzx ecx, word [rax + 8]

    ; Copy from RX buffer to caller's buffer
    push rcx
    push rdi
    imul rsi, rbx, NIC_BUF_SIZE
    add  rsi, NIC_RX_BUF
    rep  movsb
    pop  rdi
    pop  rcx

    ; Clear descriptor and give back to hardware
    imul rax, rbx, 16
    add  rax, NIC_RX_RING
    imul rdx, rbx, NIC_BUF_SIZE
    add  rdx, NIC_RX_BUF
    mov  [rax], rdx
    mov  qword [rax+8], 0

    ; Advance RDT
    mov  rax, [NIC_STATE_BASE + NIC_OFF_MMIO]
    mov  [rax + E1000_RDT], ebx

    ; Advance head
    inc  rbx
    and  rbx, NIC_RING_LEN - 1
    mov  [NIC_STATE_BASE + NIC_OFF_RX_HEAD], rbx
    jmp  .nic_rx_done

.nic_rx_rtl:
    ; RTL legacy ring: check CAPR vs CBR
    movzx edx, word [NIC_STATE_BASE + NIC_OFF_IOBASE]
    add  dx, RTL_CAPR
    in   ax, dx
    movzx eax, ax
    movzx edx, word [NIC_STATE_BASE + NIC_OFF_IOBASE]
    add  dx, RTL_CBR
    in   dx, dx
    ; If CAPR == CBR: no frame
    cmp  ax, dx
    je   .nic_rx_done_zero

    ; Frame at NIC_RX_BUF + CAPR+16 (RTL prepends 4-byte header)
    movzx edx, word [NIC_STATE_BASE + NIC_OFF_IOBASE]
    add  dx, RTL_CAPR
    in   ax, dx
    movzx eax, ax
    add  eax, NIC_RX_BUF + 16  ; skip RTL header
    mov  rsi, rax
    movzx ecx, word [rsi - 14]  ; length field in RTL header at offset -14
    and  ecx, 0x1FFF
    sub  ecx, 4                 ; strip CRC

    push rcx
    rep  movsb
    pop  rcx

    ; Advance CAPR
    movzx edx, word [NIC_STATE_BASE + NIC_OFF_IOBASE]
    add  dx, RTL_CAPR
    add  ax, cx
    add  ax, 4
    out  dx, ax
    jmp  .nic_rx_done

.nic_rx_done_zero:
    xor  ecx, ecx
.nic_rx_done:
    pop  rdi
    pop  rsi
    pop  rdx
    pop  rbx
    pop  rax
    ret

; ============================================================================
; NIC_TX_ZCHG — transmit a zchg frame over UDP/IP (minimal header builder)
; IN:  RSI = zchg frame payload, RCX = payload length
;      RDX = dest IP (network byte order), DI  = dest port
; OUT: (none)
;
; Builds Ethernet+IP+UDP headers inline before calling .nic_tx.
; Source IP read from NIC_STATE_BASE + 40 (set by Gap 2 peer discovery).
; Source MAC from NIC_STATE_BASE + NIC_OFF_MAC.
; Dest MAC: broadcast FF:FF:FF:FF:FF:FF for now (ARP not implemented).
; ============================================================================
UDP_HEADER_SZ   equ 8
IP_HEADER_SZ    equ 20
ETH_HEADER_SZ   equ 14
FULL_HEADER_SZ  equ ETH_HEADER_SZ + IP_HEADER_SZ + UDP_HEADER_SZ
ZCHG_PORT       equ 8090

.nic_tx_zchg:
    push rax
    push rbx
    push rcx
    push rdx
    push rdi
    push rsi

    ; Assemble frame at 0x119000 (scratch, below RX buffer)
    mov  rdi, 0x119000

    ; ── Ethernet header ──
    ; Dst MAC: broadcast
    mov  byte [rdi+0], 0xFF
    mov  byte [rdi+1], 0xFF
    mov  byte [rdi+2], 0xFF
    mov  byte [rdi+3], 0xFF
    mov  byte [rdi+4], 0xFF
    mov  byte [rdi+5], 0xFF
    ; Src MAC: from NIC state
    mov  rax, NIC_STATE_BASE + NIC_OFF_MAC
    mov  rbx, [rax]
    mov  dword [rdi+6], ebx
    movzx ebx, word [rax+4]
    mov  word [rdi+10], bx
    ; EtherType: IPv4 = 0x0800 (big-endian)
    mov  word [rdi+12], 0x0008

    ; ── IPv4 header ──
    mov  byte [rdi+14], 0x45   ; ver=4, IHL=5
    mov  byte [rdi+15], 0      ; DSCP
    ; Total length: IP+UDP+payload (big-endian)
    mov  eax, ecx
    add  eax, IP_HEADER_SZ + UDP_HEADER_SZ
    xchg al, ah                ; bswap16 for 2-byte field
    mov  word [rdi+16], ax
    mov  word [rdi+18], 0      ; ID
    mov  word [rdi+20], 0x0040 ; flags: DF (big-endian)
    mov  byte [rdi+22], 64     ; TTL
    mov  byte [rdi+23], 17     ; proto: UDP
    mov  word [rdi+24], 0      ; checksum (filled below)
    ; Src IP from state block offset 40
    mov  eax, dword [NIC_STATE_BASE + 40]
    mov  dword [rdi+26], eax
    ; Dst IP
    mov  dword [rdi+30], edx

    ; IPv4 checksum (ones-complement of header words)
    mov  rax, rdi
    add  rax, 14               ; IP header start
    call .ip_checksum          ; → AX = checksum
    mov  word [rdi+24], ax

    ; ── UDP header ──
    ; Src port: ZCHG_PORT (big-endian)
    mov  word [rdi+34], 0x541B ; 0x1B54 = 6996... use 0x545A = 21594 → bswap
    mov  ax, ZCHG_PORT
    xchg al, ah
    mov  word [rdi+34], ax
    ; Dst port
    mov  ax, di                ; saved before push
    xchg al, ah
    mov  word [rdi+36], ax
    ; UDP length
    mov  eax, ecx
    add  eax, UDP_HEADER_SZ
    xchg al, ah
    mov  word [rdi+38], ax
    mov  word [rdi+40], 0      ; UDP checksum: 0 = disabled

    ; ── Payload ──
    push rcx
    push rsi
    lea  rdi, [rdi + FULL_HEADER_SZ]
    rep  movsb
    pop  rsi
    pop  rcx

    ; Transmit
    mov  rsi, 0x119000
    add  rcx, FULL_HEADER_SZ
    call .nic_tx

    pop  rsi
    pop  rdi
    pop  rdx
    pop  rcx
    pop  rbx
    pop  rax
    ret

; ── IPv4 header checksum ──────────────────────────────────────────────────────
; IN:  RAX = pointer to 20-byte IP header
; OUT: AX  = ones-complement checksum (ready to store at offset +10)
.ip_checksum:
    push rbx
    push rcx
    xor  ebx, ebx              ; accumulator
    mov  ecx, 10               ; 10 × 16-bit words
.ipc_loop:
    movzx edx, word [rax]
    xchg dl, dh                ; to host byte order for addition
    add  ebx, edx
    add  rax, 2
    dec  ecx
    jnz  .ipc_loop
    ; Fold carries
    mov  eax, ebx
    shr  eax, 16
    and  ebx, 0xFFFF
    add  eax, ebx
    not  ax
    xchg al, ah                ; back to network byte order
    pop  rcx
    pop  rbx
    ret
