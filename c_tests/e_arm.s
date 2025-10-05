// build on an Arm64 Linux machine using:
//  gcc -o $1 $1.s -march=native -mcpu=native -static
//
// compute the first 192 digits of e
// replicates this C code:
//    #define DIGITS_TO_FIND 200 /*9009*/
//    int main() {
//      int N = DIGITS_TO_FIND;
//      int x = 0;
//      int a[ DIGITS_TO_FIND ];
//      int n;
//
//      for (n = N - 1; n > 0; --n)
//          a[n] = 1;
//
//      a[1] = 2, a[0] = 0;
//      while (N > 9) {
//          n = N--;
//          while (--n) {
//              a[n] = x % n;
//              x = 10 * a[n-1] + x/n;
//          }
//          printf("%d", x);
//      }
//  
//      printf( "\ndone\n" );
//      return 0;
//    }
//
// arm64 on linux calling convention: x19-x28 callee saved
// & a[n] is in x21
// array is in x22
// N is in x23
// x is in x24
// n is in x25
// 10 is in x26

.global _start

.set array_size, 200

.data
  .p2align 4
  .array: .zero 2*array_size

  .p2align 4
  .done_string: .asciz "\ndone\n"
  .number_string: .asciz "%u"

  .print_buf: .zero 20
  .after_print_buf: .zero 1
 
.text
.p2align 2 
_start:
    // remember the caller's stack frame and return address
    sub      sp, sp, #32
    stp      x29, x30, [sp, #16]
    add      x29, sp, #16

    // for (n = N - 1; n > 0; --n)  a[n] = 1;
    // 4 array entries at a time
    adrp     x22, .array
    add      x22, x22, :lo12:.array
    ldr      x0, =( ( array_size * 2 ) - 8 )
    ldr      x1, =0x0001000100010001
  _init_next:
    str      x1, [x22, x0]
    subs     x0, x0, #8
    b.pl     _init_next

    strh     wzr, [x22]         // a[0] = 0
    mov      x0, #2
    strh     w0, [x22, x0 ]     // a[1] = 2

    mov      x26, #10                 
    mov      x23, array_size    // initialize N
    mov      xzr, x24           // initialize x

  _outer:
    cmp      x23, 9
    beq      _loop_done
    sub      x23, x23, #1
    mov      x25, x23
    mov      x21, x22
    add      x21, x21, x25, LSL #1

  _inner:
    udiv     x1, x24, x25
    msub     x0, x1, x25, x24             // x0 = x24 - ( x1 / x25 )
    strh     w0, [x21], #-2               // post-decrement x21 after the store
    ldrh     w0, [x21]
    madd     x24, x0, x26, x1             // x24 = ( x0 * x26 ) + x1
    subs     x25, x25, 1
    b.ne     _inner

  _print_digit:
    mov      x0, x24
    bl       print_uint
    b        _outer

  _loop_done:
    adrp     x0, .done_string
    add      x0, x0, :lo12:.done_string
    bl       put_string

  _main_done:
    mov      x0, 0
    ldp      x29, x30, [sp, #16]
    add      sp, sp, #32

    // don't ret -- just invoke the exit system call
    mov      x8, 93
    mov      x0, 0
    svc      0

.p2align 2 
print_uint: // use this instead of printf because it's much faster
    sub      sp, sp, #32
    stp      x29, x30, [sp, #16]
    add      x29, sp, #16

    adrp     x4, .after_print_buf
    add      x4, x4, :lo12:.after_print_buf
    sub      x1, x4, 1
    mov      x5, '0'
    strb     w5, [x1]
    cmp      x0, 0
    b.eq     _write_buffer
    mov      x10, 10

  _next_digit:
    cmp      x0, 0
    b.eq     _inc_write_buffer
    udiv     x3, x0, x10
    msub     x2, x3, x10, x0
    mov      x0, x3
    add      x2, x2, x5
    strb     w2, [x1], #-1
    b        _next_digit

  _inc_write_buffer:
    add      x1, x1, 1
  _write_buffer:
    sub      x2, x4, x1
    mov      x0, 1
    mov      x8, 64
    svc      0

    mov      x0, 0
    ldp      x29, x30, [sp, #16]
    add      sp, sp, #32
    ret

.p2align 2
put_string:
    sub      sp, sp, #32
    stp      x29, x30, [sp, #16]
    add      x29, sp, #16

    mov      x19, x0
    bl       my_strlen
    mov      x2, x0
    mov      x1, x19
    mov      x0, 1
    mov      x8, 64
    svc      0

    mov      x0, 0
    ldp      x29, x30, [sp, #16]
    add      sp, sp, #32
    ret

.p2align 2
my_strlen:
    mov      x2, #0
  _my_strlen_loop:
    ldrb     w3, [x0, x2]
    cbz      w3, _my_strlen_exit
    add      x2, x2, #1
    b        _my_strlen_loop
  _my_strlen_exit:
    mov      x0, x2
    ret
