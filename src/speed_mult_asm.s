; speed_mult_asm.s
; Calling convention: __fastcall__
; C signature:
;   extern int __fastcall__ speed_mult_asm(int source, int mult);

.export _speed_mult_asm
.importzp c_sp
.import incsp2

; Allocate temporary workspace in RAM to prevent Zero-Page conflicts
.segment "BSS"
val_source: .res 2
val_mult:   .res 2
val_res:    .res 2
val_temp:   .res 2
val_sign:   .res 1

.segment "PROG1"

_speed_mult_asm:
    ; 1. Store 'mult' (passed in AX via __fastcall__)
    sta val_mult
    stx val_mult+1

    ; 2. Fetch 'source' off C stack frame (c_sp)
    ldy #0
    lda (c_sp),y
    sta val_source
    iny
    lda (c_sp),y
    sta val_source+1

    ; 3. Clean up the 2-byte stack parameter (source)
    jsr incsp2

    ; 4. Extract signs (convert operands to positive, track net sign in val_sign)
    lda #0
    sta val_sign

    ; Check source sign
    lda val_source+1
    bpl @source_pos
    sec
    lda #0
    sbc val_source
    sta val_source
    lda #0
    sbc val_source+1
    sta val_source+1
    inc val_sign
@source_pos:

    ; Check mult sign
    lda val_mult+1
    bpl @mult_pos
    sec
    lda #0
    sbc val_mult
    sta val_mult
    lda #0
    sbc val_mult+1
    sta val_mult+1
    lda val_sign
    eor #1
    sta val_sign
@mult_pos:

    ; 5. Calculate val_source * 13  (13 = 8 + 4 + 1)
    ; S2 = S * 2
    lda val_source
    asl A
    sta val_temp
    lda val_source+1
    rol A
    sta val_temp+1

    ; S4 = S2 * 2
    lda val_temp
    asl A
    sta val_res
    lda val_temp+1
    rol A
    sta val_res+1

    ; S8 = S4 * 2 (held in X:Y)
    lda val_res
    asl A
    tay
    lda val_res+1
    rol A
    tax

    ; val_temp = S8 + S4
    clc
    tya
    adc val_res
    sta val_temp
    txa
    adc val_res+1
    sta val_temp+1

    ; val_source = (S8 + S4) + S1
    clc
    lda val_temp
    adc val_source
    sta val_source
    lda val_temp+1
    adc val_source+1
    sta val_source+1

    ; 6. Divide val_source by 32 (Unsigned Shift Right 5)
    ldy #5
@lsr_loop:
    lsr val_source+1
    ror val_source
    dey
    bne @lsr_loop

    ; 7. Multiply (val_source * val_mult) -> val_res
    lda #0
    sta val_res
    sta val_res+1

@mult_loop:
    lsr val_mult+1
    ror val_mult
    bcc @no_add

    clc
    lda val_res
    adc val_source
    sta val_res
    lda val_res+1
    adc val_source+1
    sta val_res+1

@no_add:
    asl val_source
    rol val_source+1

    lda val_mult
    ora val_mult+1
    bne @mult_loop

    ; 8. Apply net sign if calculation was negative
    lda val_sign
    lsr A
    bcc @done           ; Bit 0 clear -> positive result

    ; Negate val_res (0 - val_res)
    sec
    lda #0
    sbc val_res
    sta val_res
    lda #0
    sbc val_res+1
    sta val_res+1

@done:
    ; Return 16-bit integer in AX (A = low byte, X = high byte)
    lda val_res
    ldx val_res+1
    rts
    