# ArmOS
Runs Linux Arm64 binaries on platforms including Windows on x32/AMD64/ARM64, macOS on Apple silicon, Linux on RISC-V64, Arm32, Arm64, AMD64, etc.

ArmOS is a C++ app that can load and run standard/ELF Linux Arm64 binaries on other platforms. It emulates the Arm64 ISA and Linux syscalls sufficiently for many small apps to work.

## Caveats
* Only a subset (perhaps 50%) of Base and SIMD&FP instructions are implemented. Specifically, those instructions the g++, Clang-14, Clang-18, and Rust compilers emit for the test apps in this repo along with their language runtimes. It's not too hard to find new C++ or Rust programs that won't run because the instructions they require aren't implemented.
* Linux emulation is limited to one core with one thread. Syscalls for time, file system, mmap, brk, and other basic services exist, but there is no support for threads, child process creation, networking, and a long list of other basic system services.
* Apps must be linked static; ArmOS doesn't load dependent libraries at runtime. Use -static with ld, clang, or g++. Use -C target-feature=+crt-static for Rust apps.

## Usage

    usage: armos <armos arguments> <elf_executable> <app arguments>

    arguments:   -e     just show information about the elf executable; don't actually run it   
                 -h:X   # of meg for the heap (brk space). 0..1024 are valid. default is 40                 
                 -i     if -t is set, also enables arm64 instruction tracing                 
                 -m:X   # of meg for mmap space. 0..1024 are valid. default is 40                 
                 -p     shows performance information at app exit                 
                 -t     enable debug tracing to armos.log                 
                 -v     used with -e shows verbose information (e.g. symbols)

## Files

    arm64.cxx       Arm64 emulator
    arm64.hxx       Header for Arm64 emulator
    armos.cxx       Main app and Linux emulation
    armos.h         Header for main app and Linux emulation
    djl_os.hxx      Cross-platform utilities
    djltrace.hxx    Tracing to a log file
    djl_con.hxx     Console keyboard and terminal abstractions and utilities
    djl_mmap.hxx    Simplistic helper class for Linux mmap calls
    djl_128.hxx     Helper class for 128-bit integer multiply and divide
    m.bat           builds a debug version of ArmOS on Windows
    mr.bat          builds a release version of ArmOS on Windows
    ms.sh           builds a debug version of ArmOS on Linux
    msr.sh          builds a release version of ArmOS on Linux
    mmac.sh         builds a debug release on macOS
    mrmac.sh        builds a release version on macOS
    runall.bat      runs all the tests on Windows. First copy test binaries from a Linux machine.
    runall.sh       runs all the tests on Linux and macOS. If not on Arm64, first copy test binaries.
    words.txt       used by test apps
    tp.bas          used by the BA test app

## Validation
* I've tested on AMD64 and Arm64 machines running Windows along with AMD64, Arm32, Arm64, and RISC-V64 machines running Linux. I also tested on an M3 macOS 15.0 BuildVersion 24A335.
* The c_tests folder has a number of C and C++ apps that can be built with mall.sh (make all) on an Arm64 Linux machine. I'm sure cross-compilation will work too, though I haven't tested it. These apps are built with various optimization flags: -O0, -O1, -O2, -O3, and -Ofast. Each variation utilizes different Arm64 instructions, which improves test coverage.
* The rust_tests folder has a number of Rust apps that can be built with mall.sh on an Arm64 Linux machine. The apps are built with optimization levels 0, 1, 2, and 3 using -C opt-level=.
* I've also tested with emulators found in my sister repos: NTVAO (Apple 1 + 6502), NTVCM (CP/M 2.2 + 8080/Z80), NTVDM (MS-DOS 3.x + 8086), RVOS (Linux + RISC-V64). ArmOS runs all of these emulators when they are compiled for Arm64 (tested with g++ and clang optimization flags -O2, -O3, and -Ofast). ArmOS also runs itself recursively an arbitrary number of times. RVOS runs ArmOS when it's compiled for RISC-V64. I've validated ArmOS running RVOS running NTVDM running NTVCM running Turbo Pascal for CP/M 2.2; performance isn't great.
* Testing of ArmOS on Windows is done with the Microsoft C++ compiler. On Linux I use g++ and clang. On macOS I use the native clang C++ compiler g++ maps to. All test apps are built on Arm64 Linux using g++, clang, and Rust.
* CoreMark runs with correct results about 50x slower than native speed: [https://github.com/eembc/coremark/tree/main](https://github.com/eembc/coremark/tree/main) Note that the Makefile must be updated to link the binary -static.

## C/C++ Tests
    tcmp        tests comparisons of various integer and floating point types
    e           computes digits of the irrational number e
    printint    prints an integer
    sieve       finds prime numbers
    simple      show arguments and environment variables
    tmuldiv     tests integer multiply and divide of various widths
    tpi         finds digits of the irrational number PI
    pis         finds hexadecimal digits of PI
    ts          tests bit shifting
    tarray      tests array access
    tbits       tests bitwise operations
    trw         tests file I/O using open/read/write/close
    fileops     tests file I/O using fopen/fread/fwrite/fclose
    tmmap       tests mmap, mremap, munmap Linux syscalls using patterns clib uses for its heap
    tsimplef    simple test for floats
    tatomoc     tests some C++ atomic operations and mutexes
    sleeptm     tests high_resolution_clock, nanosleep(), times(), and getrusage()
    tstr        tests various string functions: strlen, strchr, strrchr, memcpy, memcmp, printf
    ttime       tests localtime() and timezones
    tm          tests malloc and free
    glob        tests C++ global variables
    tap         finds digits of the irrational number ap
    ttt         proves you can't win at tic-tac-toe if the opponent is competent
    tf          tests single-precision floating point
    td          tests double-precision floating point
    t           tests math and logical operators on a variety of integer types
    lenum       tests linux filesystem enumeration functions
    tphi        finds digits of the irrational number phi
    terrno      tests errno
    t_setjmp    tests setjmp
    tex         tests C++ exceptions
    an          finds anagrams
    words.txt   list of English words used by various test programs including an
    ba          simplistic BASIC interpreter and compiler (targets 6502, 8080, 8086, x32, x64, arm32, arm64, RISC-V64)
    mm          floating point and integer matrix multiplication
    tp.bas      BASIC test program for BA
    mall.sh     builds the C/C++ test apps in both g++ and clang with 5 different optimization levels
    
## Rust Tests
    e           computes digits of the irrational number e
    ttt         proves you can't win at tic-tac-toe if the opponent is competent
    fileops     tests file I/O
    ato         tests atomic instructions
    tap         finds digits of the irrational number ap
    real        tests floating point operations
    tphi        finds digits of the irrational number ap
    mysort      sorts strings in a text file
    mall.sh     builds the Rust test apps with 4 different optimization levels

## NoClib
The noclib folder has some source files for creating C apps that don't link with clib. This was useful when bootstrapping the emulator because it couldn't yet run the many of instructions clib uses between _start() and main(). There really isn't much utility in this anymore since ArmOS runs clib fine.

    djlclib.h      declares C constants found in standard C headers
    djlclib.c      implements a subset of the C runtime
    noclib.s       implements _start() and a subset of the C runtime
    m.sh           the shell script below to build an app with noclib.

Apps can be built using noclib with a shell script like this:
~~~~
cc -fno-builtin -fsigned-char -Og -S -I . $1.c
cc -fsigned-char -Og -S -I . djlclib.c
as -o noclib.o noclib.s
as -o $1.o $1.s
as -o djlclib.o djlclib.s
ld -static -o $1 $1.o noclib.o djlclib.o
~~~~
    
## FAQ
* Why did you build this? I wanted to learn about Arm64.
* What use is this emulator? Not much beyond a learning tool. Oh, and it's kind of neat to run Arm64 apps on non-Arm64 CPUs.
* Wow, the emulator looks really slow. Why? I strived for clarity rather than performance. This is espcially true for SIMD instructions.
* Will you implement the full instruction set? I currently plan to, if I can find a reasonable way to test it. To date all testing is with actual apps, not with assembly code.
* This is very silly but/so I'd like to help. I would appreciate pull requests to complete the full instruction set, reuse more code, and improve performance.
    
