; setRangeOptASM_prog1.s
; Calling convention: __fastcall__
; C signature:
;   int __fastcall__ setRangeOptASM_prog1(unsigned char input, unsigned char inMin, unsigned char inMax);

.export _setRangeOptASM_prog1
.importzp c_sp, ptr2, tmp1, tmp2, tmp3, tmp4
.import _accumulator
.import addysp

.segment "PROG1"

_setRangeOptASM_prog1:
    ; 1. Under __fastcall__, rightmost parameter (inMax) is passed in A
    sta tmp4          ; tmp4 = inMax

    ; 2. Fetch remaining parameters from C stack frame
    ;    c_sp+0 = inMin
    ;    c_sp+1 = input
    ldy #0
    lda (c_sp),y
    sta ptr2          ; ptr2 = inMin

    iny               ; Y = 1
    lda (c_sp),y
    sta tmp1          ; tmp1 = input

    ; 3. Clean up the 2 stack parameters (addysp modifies A)
    ldy #2
    jsr addysp

    ; ----------------------------------------------------
    ; _accumulator += (target - _accumulator) >> 1
    ; target = input << 8  (high byte = tmp1, low byte = $00)
    ; ----------------------------------------------------
    lda #0            ; target low byte ($00)
    sec
    sbc _accumulator
    tax               ; X = low byte diff

    lda tmp1          ; target high byte
    sbc _accumulator+1 ; A = high byte diff

    ; Arithmetic shift right by 1 on 16-bit signed difference
    cmp #$80          ; Sets Carry if diff is negative
    ror A             ; A = high byte diff >> 1
    sta tmp2          ; tmp2 = high byte diff >> 1
    txa               ; A = low byte diff
    ror A             ; ROR rotates Carry into bit 7
    tax               ; X = low byte diff >> 1

    ; Add 16-bit shifted difference back to _accumulator directly
    txa               ; A = low byte diff >> 1
    clc
    adc _accumulator
    sta _accumulator  ; Update _accumulator low byte

    lda tmp2          ; A = high byte diff >> 1
    adc _accumulator+1
    sta _accumulator+1 ; Update _accumulator high byte

    ; Preserve smoothedInput (_accumulator >> 8) in tmp3
    sta tmp3

    ; ----------------------------------------------------
    ; Bounds Checking
    ; ----------------------------------------------------
    lda tmp3          ; smoothedInput
    cmp ptr2          ; smoothedInput < inMin?
    bcc return_zero

    cmp tmp4          ; smoothedInput > inMax?
    beq @in_range     ; equality is valid
    bcs return_127

@in_range:
    ; Divisor: range = (inMax - inMin)
    lda tmp4
    sec
    sbc ptr2
    beq return_zero   ; Safety check: prevent divide-by-zero
    sta tmp4          ; tmp4 = divisor (range)

    ; Delta = (smoothedInput - inMin)
    lda tmp3
    sec
    sbc ptr2          ; A = delta

    ; Multiply 127 * delta -> 16-bit product in (tmp2:tmp1)
    jsr mult127

    ; Divide 16-bit dividend (tmp2:tmp1) by 8-bit divisor (tmp4)
    ; Returns quotient low byte in A
    jsr div16_8_inline

    ldx #0            ; Return high byte = 0
    rts

return_zero:
    lda #0
    ldx #0
    rts

return_127:
    lda #127
    ldx #0
    rts

; ----------------------------------------------------
; Helper: 127 * A -> (tmp2:tmp1)
; Computes (128 * A) - A
; ----------------------------------------------------
mult127:
    sta tmp1          ; tmp1 = delta
    
    lsr A             ; High byte of (A * 128) is (A >> 1)
    sta tmp2          ; tmp2 = high byte of (delta * 128)
    
    lda tmp1
    ror A             ; Bit 0 of delta rotated into bit 7 of low byte
    and #$80
    
    ; Subtract delta (tmp1) from (delta * 128)
    sec
    sbc tmp1
    sta tmp1          ; tmp1 = low byte of product
    
    lda tmp2
    sbc #0
    sta tmp2          ; tmp2 = high byte of product
    rts

; ----------------------------------------------------
; Helper: 16-bit by 8-bit unsigned division
; Divides (tmp2:tmp1) by tmp4
; Returns quotient low byte in A
; ----------------------------------------------------
div16_8_inline:
    lda #0
    ldx #16
@div_loop:
    asl tmp1
    rol tmp2
    rol A
    bcs @div_sub      ; IF Carry set, A_real >= 256 (> tmp4). MUST subtract!
    cmp tmp4
    bcc @div_next
@div_sub:
    sbc tmp4          ; Carry is 1 in both paths (bcs or cmp >= tmp4)
    inc tmp1
@div_next:
    dex
    bne @div_loop
    lda tmp1
    rts
    