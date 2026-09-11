/* cdecl i386; direct one-plane 8x8 write to both ring aliases. */
.text
.globl _dos_direct_tile
_dos_direct_tile:
    pushl %esi
    pushl %edi
    movl 12(%esp),%edi
    movl 16(%esp),%esi
    movl $8,%ecx
1:  movw (%esi),%ax
    movw %ax,(%edi)
    movw %ax,66(%edi)
    addl $2,%esi
    addl $132,%edi
    loop 1b
    popl %edi
    popl %esi
    ret

.globl _dos_copy_span
_dos_copy_span:
    pushl %esi
    pushl %edi
    movl 12(%esp),%edi
    movl 16(%esp),%esi
    movl 20(%esp),%edx
    movl %edx,%ecx
    shrl $2,%ecx
    cld
    rep movsl
    movl %edx,%ecx
    andl $3,%ecx
    rep movsb
    popl %edi
    popl %esi
    ret
