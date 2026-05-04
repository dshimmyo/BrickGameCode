; --- Imports from C ---
.import _currentBrickWidth
.import _currentNumBricksH
.importzp sp
.import incsp1

; --- Local Scratchpad ---
.zeropage
ROW:        .res 1
COL:        .res 1
DIVISOR:    .res 1
TEMP:       .res 1
RES_L:      .res 1
RES_H:      .res 1

; --- Constants ---
BRICK_TOP    = 16  
BRICK_LEFT   = 8   
BRICK_HEIGHT = 8   ; Power of 2 allows for the Shift Flex

.code
.export _get_brick_index_asm

_get_brick_index_asm:
    ; 1. 'y' is in A (cc65 fastcall). Calculate Row.
    SEC
    SBC #BRICK_TOP
    
    ; Shift Flex: y / 8
    LSR A
    LSR A
    LSR A
    STA ROW

    ; 2. Grab 'x' from the cc65 stack
    LDY #0
    LDA (sp),y      ; A = x
    SEC
    SBC #BRICK_LEFT
    
    ; 3. Calculate Col: A = (x - LEFT) / currentBrickWidth
    LDX _currentBrickWidth
    STX DIVISOR
    JSR local_div_8  ; Returns quotient in A
    STA COL

    ; 4. Index = (ROW * currentNumBricksH) + COL
    LDA ROW
    LDX _currentNumBricksH
    JSR local_mul_8  ; Returns Low in A, High in X
    
    CLC
    ADC COL          ; Add the column offset to the Low byte
    BCC @no_carry
    INX              ; Increment High byte if Low byte overflowed
@no_carry:

    ; 5. Cleanup & Return
    PHA              ; Save Low byte
    TXA
    PHA              ; Save High byte
    JSR incsp1       ; Pop the 1-byte 'x' from C stack
    PLA
    TAX              ; Result High in X
    PLA              ; Result Low in A
    RTS

; --- Helper: 8-bit Division (A / DIVISOR) ---
local_div_8:
    LDY #0
    STY TEMP
    LDY #8
@div_loop:
    ASL A           ; Shift A left, bit 7 goes to Carry
    ROL TEMP        ; Rotate Carry into bit 0 of TEMP
    PHA             ; Save A
    LDA TEMP
    CMP DIVISOR     ; Can we subtract?
    BCC @div_skip
    SBC DIVISOR
    STA TEMP
    PLA
    ORA #$01        ; Set quotient bit
    PHA
@div_skip:
    PLA
    DEY
    BNE @div_loop
    RTS

; --- Helper: 8-bit Multiplication (A * X) ---
; Returns Low Byte in A, High Byte in X
local_mul_8:
    STA TEMP        ; Multiplicand
    STX RES_L       ; Multiplier
    LDA #0          ; Clear result high byte
    LDY #8
@mul_loop:
    LSR RES_L       ; Shift Multiplier right, bit 0 -> Carry
    BCC @mul_skip
    CLC
    ADC TEMP        ; Add multiplicand to high byte
@mul_skip:
    ; 16-bit shift: Result High (A) -> Result Low (RES_L)
    ROR A           ; Carry (from ADC or LSR) -> bit 7 of A
    ROR RES_L       ; bit 0 of A -> bit 7 of RES_L
    DEY
    BNE @mul_loop
    
    ; Logic: After 8 loops, the lower 8 bits of the product 
    ; are in RES_L and the upper 8 bits are in A.
    TAX             ; High byte to X
    LDA RES_L       ; Low byte to A
    RTS