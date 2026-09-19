.section .crt0, "ax"
.global _start
.type _start, %function
_start:
  b _start
  .word __nx_mod0 - _start
  .space 8, 0
  .org 0xF0

.global __nx_mod0
.type __nx_mod0, %object
__nx_mod0:
  .word 0x30444f4d
  .word _DYNAMIC - __nx_mod0
  .word 0
  .word 0
  .word 0
  .word 0
  .word 0
.size __nx_mod0, . - __nx_mod0
