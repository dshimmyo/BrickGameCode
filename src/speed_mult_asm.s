.importzp sp ; sp is a zero page variable defined in an external file
                ; sp stands for evaluation stack pointer
.import incsp2 ; Increment Stack Pointer by 2. . When your C function finishes, it calls incsp2 
                ; to drop/pop 2 bytes off the parameter stack (cleaning up arguments passed from C).

.zeropage ; the following variables in zero page

; note on terminology ; extern int speed_mult_asm(int source, int mult)
; this is a special multiply function where the operand is called delta (source)
; and the multiplier is called speed, but has another * 13 / 32 to fine-tune the speed
DELTA_LOW:  .res 1 ; reserve one byte ; source
DELTA_HIGH: .res 1
SPEED_LOW:  .res 1 ; speed = mult
SPEED_HIGH: .res 1
RES_LOW:    .res 1 ; result
RES_HIGH:   .res 1

.code ; out of zero page, back to code
.segment "PROG1" 
.export _speed_mult_asm ; .export makes visible to c compiler
    ; cc65 automatically prepends an underscore to all C functions

_speed_mult_asm: 
    ; First argument (source): Pushed onto cc65's software stack (sp) before calling the function.
    ; Second argument (mult): Left in A (low byte) and X (high byte)
    ; 1. 'speed' is in A (low) and X (high)
    STA SPEED_LOW
    STX SPEED_HIGH

    ; 2. Grab 'delta' (dx/dy) from the cc65 stack
    LDY #1
    LDA (sp),y    ; Reads high byte from stack (sp + 1)
    TAX           ; Moves high byte into X register
    DEY           ; Decrements Y from 1 down to 0
    LDA (sp),y    ; Reads low byte from stack (sp + 0)
    STA DELTA_LOW  ; Stores low byte
    STX DELTA_HIGH ; Stores high byte

    ; 3. PHASE 1: delta * 13
    ; (num * 8) + (num * 4) + (num * 1)
    
    ; Calculate num * 2
    ASL A ;<<1 low bit shift left
    TAY             ; Y = delta * 2 low
    TXA ; high bit 
    ROL A ;Rotate One Bit Left (shift left and rotates carry bit from previous << into bit 0 of A, high byte)
    TAX             ; X = delta * 2 high
    TYA             ; A = delta * 2 low
    
    ; Calculate num * 4
    ASL A
    STA RES_LOW     ; RES = delta * 4 low
    TXA
    ROL A
    STA RES_HIGH    ; RES = delta * 4 high
    
    ; Calculate num * 8 (from RES)
    LDA RES_LOW
    ASL A
    TAY             ; Y = delta * 8 low
    LDA RES_HIGH
    ROL A
    TAX             ; X = delta * 8 high
    
    ; Final Add: (delta * 8) + (delta * 4) + (delta * 1)
    CLC
    TYA             ; * 8 low
    ADC RES_LOW     ; + * 4 low
    ADC DELTA_LOW   ; + original low
    STA DELTA_LOW
    
    TXA             ; * 8 high
    ADC RES_HIGH    ; + * 4 high
    ADC DELTA_HIGH  ; + original high
    STA DELTA_HIGH

    ; 4. PHASE 2: Divide by 32 (Arithmetic Shift Right 5)
    ; We use CMP #$80 to "smear" the sign bit so negative stays negative
    LDY #5
@asr_loop:
    LDA DELTA_HIGH
    CMP #$80        ; Sets carry if bit 7 is 1 (negative)
    ROR DELTA_HIGH  ; Pulls carry into bit 7
    ROR DELTA_LOW   ; Pulls bit 0 of high into bit 7 of low
    DEY
    BNE @asr_loop
    
    ; 5. PHASE 3: Multiply by 'speed'
    ; Clear RES to act as our product accumulator
    LDA #0
    STA RES_LOW
    STA RES_HIGH

@mult_loop:
    LSR SPEED_HIGH
    ROR SPEED_LOW   ; Check lowest bit of speed
    BCC @no_add
    
    CLC
    LDA RES_LOW
    ADC DELTA_LOW
    STA RES_LOW
    LDA RES_HIGH
    ADC DELTA_HIGH
    STA RES_HIGH

@no_add:
    ASL DELTA_LOW   ; Shift multiplicand left for next bit
    ROL DELTA_HIGH
    
    LDA SPEED_LOW   ; Check if multiplier is now 0
    ORA SPEED_HIGH
    BNE @mult_loop

    ; 6. CLEANUP & RETURN
    LDA RES_LOW
    LDX RES_HIGH
    
    PHA             ; Save result
    TXA
    PHA
    JSR incsp2      ; Pop b->dx/dy from stack
    PLA
    TAX
    PLA
    RTS