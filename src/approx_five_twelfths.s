; --- Register Definitions ---
.zeropage
NUM_LOW:   .res 1
NUM_HIGH:  .res 1
T4_LOW:    .res 1
T4_HIGH:   .res 1
T8_LOW:    .res 1
T8_HIGH:   .res 1

.code
.segment "PROG1" 
.export _approx_five_twelfths

_approx_five_twelfths:
    ; A = low, X = high
    STA NUM_LOW
    STX NUM_HIGH

    ; 1. Calculate num * 4
    ASL A
    TAY             ; Y = num * 2 low
    TXA
    ROL A
    TAX             ; X = num * 2 high
    TYA             ; A = num * 2 low

    ASL A
    STA T4_LOW      ; num * 4 low
    TXA
    ROL A
    STA T4_HIGH     ; num * 4 high

    ; 2. Calculate num * 8 (from T4)
    LDA T4_LOW
    ASL A
    STA T8_LOW      ; num * 8 low
    LDA T4_HIGH
    ROL A
    STA T8_HIGH     ; num * 8 high

    ; 3. The Triple Add: (num*8) + (num*4) + (num*1)
    ; We do this in one chain to save CLCs and STAs
    CLC
    LDA T8_LOW
    ADC T4_LOW
    ADC NUM_LOW
    STA NUM_LOW     ; Result low

    LDA T8_HIGH
    ADC T4_HIGH
    ADC NUM_HIGH
    STA NUM_HIGH    ; Result high

    ; 4. Arithmetic Shift Right (>> 5)
    ; We use CMP #$80 to preserve the sign bit (bit 7)
    LDY #5
@shift_loop:
    LDA NUM_HIGH
    CMP #$80        ; Carry = bit 7 (sign bit)
    ROR NUM_HIGH    ; Rotate right, pulling sign bit in
    ROR NUM_LOW     ; Pull bit 0 of high into bit 7 of low
    DEY
    BNE @shift_loop

    ; Return in A/X
    LDA NUM_LOW
    LDX NUM_HIGH
    RTS