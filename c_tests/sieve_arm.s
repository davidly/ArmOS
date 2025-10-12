// build on an Arm64 Linux machine using:
//  gcc -o $1 $1.s -march=native -mcpu=native -static
//
//   #define SIZE 8190
//
//   char flags[SIZE+1];
//
//   int main()
//           {
//           int i,k;
//           int prime,count,iter;
//
//           for (iter = 1; iter <= 10; iter++) {    /* do program 10 times */
//                   count = 0;                      /* initialize prime counter */
//                   for (i = 0; i <= SIZE; i++)     /* set all flags TRUE */
//                           flags[i] = TRUE;
//                   for (i = 0; i <= SIZE; i++) {
//                           if (flags[i]) {         /* found a prime */
//                                   prime = i + i + 3;      /* twice index + 3 */
//                                   for (k = i + prime; k <= SIZE; k += prime)
//                                           flags[k] = FALSE;       /* kill all multiples */
//                                   count++;                /* primes found */
//                                   }
//                           }
//                   }
//           printf("%d primes.\n",count);           /*primes found in 10th pass */
//           return 0;
//           }
//
// k x20
// flags x21
// size x22
// prime x23
// count x24
// i x25
// iter x26

.global _start

.set size, 8190
.set size_full, (size+2)

.data
  .align 8
  .done_string: .asciz " primes.\n"
  .align 8
  .flags: .zero size_full

  .align 8
  .print_buf: .zero 20
  .after_print_buf: .zero 1

.text
.p2align 2 
_start:
    // remember the caller's stack frame and return address
    sub      sp, sp, #32
    stp      x29, x30, [sp, #16]
    add      x29, sp, #16

    mov      x26, 10

  _next_iteration:
    adrp     x21, .flags
    add      x21, x21, :lo12:.flags
    ldr      x1, =( size_full - 8 )
    ldr      x0, =0x0101010101010101

  _finit:
    str      x0, [ x21, x1 ]
    subs     x1, x1, 8
    b.pl     _finit

    ldr      x22, =size
    mov      x24, 0
    mov      x27, x21

  _outer:
    ldrb     w0, [ x27 ], #1
    cmp      w0, 0
    b.eq     _outer

    sub      x25, x27, x21     // figure out i based on how far the pointer in flags progressed
    sub      x25, x25, 1
    cmp      x25, x22
    b.gt     _all_done

    add      x23, x25, 3       // prime = i + 3
    add      x23, x23, x25     // prime += i
    add      x20, x23, x25     // k = prime + i
    cmp      x20, x22          // redundant check to have just one branch in _inner
    b.gt     _inc_count

  _inner:
    strb     wzr, [ x21, x20 ] // flags[ k ] = false
    add      x20, x20, x23     // k += prime
    cmp      x20, x22          // k <= SIZE
    b.le     _inner

  _inc_count:
    add      x24, x24, 1
    b        _outer

  _all_done:
    subs     x26, x26, 1
    b.ne     _next_iteration

    mov      x0, x24
    bl       print_uint

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
