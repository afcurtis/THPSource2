BITS 32
org 0x004cac30

Math_Vector3MulScalarOut:
    mov eax, [esp + 0x0c]
    push esi
    mov esi, [esp + 0x0c]
    push edi
    mov eax, [eax]
    mov ecx, [esi]
    mov edx, [esi + 4]
    mov esi, [esi + 8]
    imul ecx, eax
    imul edx, eax
    imul esi, eax
    mov eax, [esp + 0x0c]
    db 0x8b, 0xf8                 ; mov edi, eax
    mov [edi], ecx
    mov [edi + 4], edx
    mov [edi + 8], esi
    pop edi
    pop esi
    ret

    times 2 nop
