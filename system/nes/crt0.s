; crt0.s - CC65 startup code for NES
; This file is the entry point for the NES ROM

; ========================================================================
; NES Header (included in final ROM)
; ========================================================================

        .segment "HEADER"
        .byte "NES", $1A      ; iNES signature
        .byte 2               ; 2 x 16KB PRG ROM
        .byte 1               ; 1 x 8KB CHR ROM
        .byte $01             ; Mapper 0, horizontal mirroring
        .byte $00             ; Mapper 0, no CHR RAM
        .byte $00, $00, $00, $00  ; Reserved

; ========================================================================
; Reset Vector Handler
; ========================================================================

        .segment "STARTUP"
        .code

        .proc   _start
        ; Disable interrupts
        SEI
        CLD
        
        ; Set up stack pointer
        LDX     #$FF
        TXS
        
        ; Clear zero page
        LDA     #$00
        LDX     #$00
clear_zp:
        STA     $00,X
        INX
        BNE     clear_zp
        
        ; Clear RAM (0x0100-0x07FF)
        LDA     #$00
        LDX     #$08
clear_ram:
        STA     $0100,X
        DEX
        BNE     clear_ram
        
        ; Clear sprite RAM (0x0200-0x02FF)
        LDX     #$00
clear_oam:
        STA     $0200,X
        INX
        BNE     clear_oam
        
        ; Wait for PPU to be ready
        BIT     $2002
wait_vblank_1:
        BIT     $2002
        BPL     wait_vblank_1
        
        ; Clear VRAM
        LDA     #$20
        STA     $2006
        LDA     #$00
        STA     $2006
        LDX     #$00
clear_vram:
        STA     $2007
        INX
        BNE     clear_vram
        
        ; Second pass for safety
        LDX     #$04
clear_vram_2:
        STA     $2007
        DEX
        BNE     clear_vram_2
        
        ; Enable NMI
        CLI
        
        ; Jump to main C code
        JMP     _main
        
        .endproc

; ========================================================================
; NMI Handler
; ========================================================================

        .segment "NMI"
        .code

        .proc   nmi_handler
        ; Save registers
        PHA
        TXA
        PHA
        TYA
        PHA
        
        ; Increment frame counter
        INC     _NmiFrameCounter
        
        ; Process sprites (DMA)
        LDA     #$02
        STA     $4014
        
        ; Call NMI handler in C if defined
        LDA     _nmi_handler
        ORA     _nmi_handler + 1
        BEQ     nmi_done
        
        JSR     _nmi_handler
        
nmi_done:
        ; Restore registers
        PLA
        TAY
        PLA
        TAX
        PLA
        
        RTI
        .endproc

; ========================================================================
; Reset Vector
; ========================================================================

        .segment "VECTORS"
        .word    nmi_handler     ; NMI vector
        .word    _start         ; Reset vector  
        .word    irq_handler    ; IRQ/BRK vector

; ========================================================================
; IRQ Handler
; ========================================================================

        .segment "IRQ"
        .code

        .proc   irq_handler
        RTI
        .endproc

; ========================================================================
; Uninitialized Data
; ========================================================================

        .segment "BSS"

        .global _NmiFrameCounter
_NmiFrameCounter: .res 1
