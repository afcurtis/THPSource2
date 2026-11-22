BITS 32
org 0x004cabb0

Math_Vector3AddOut:
    mov eax, [esp + 8]
    push esi
    push edi
    mov edi, [esp + 0x14]
    mov ecx, [eax]
    mov edx, [edi]
    mov esi, [edi + 4]
    db 0x03, 0xca                 ; add ecx, edx
    mov edx, [eax + 4]
    db 0x03, 0xd6                 ; add edx, esi
    mov esi, [eax + 8]
    mov eax, [edi + 8]
    db 0x03, 0xf0                 ; add esi, eax
    mov eax, [esp + 0x0c]
    db 0x8b, 0xf8                 ; mov edi, eax
    mov [edi], ecx
    mov [edi + 4], edx
    mov [edi + 8], esi
    pop edi
    pop esi
    ret

    times 15 nop
