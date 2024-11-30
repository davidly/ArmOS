#pragma once

#include <djl_os.hxx>

#ifdef _MSC_VER

    //#define __inline_perf __declspec(noinline)
    #define __inline_perf __forceinline

#else // MacOS + Linux

    #define __inline_perf

#endif // _MSC_VER

struct Arm64;

// callbacks when instructions are executed

extern void arm64_invoke_svc( Arm64 & cpu );                                               // called when the svc instruction is executed
extern const char * arm64_symbol_lookup( uint64_t address, uint64_t & offset );            // returns the best guess for a symbol name and offset for the address
extern void arm64_hard_termination( Arm64 & cpu, const char *pcerr, uint64_t error_value ); // show an error and exit
extern void arm64_check_ptracenow( Arm64 & cpu );

typedef uint8_t vec16_t[ 16 ];  // int128 would be better

struct Arm64
{
    bool trace_instructions( bool trace );                // enable/disable tracing each instruction
    void end_emulation( void );                           // make the emulator return at the start of the next instruction

    Arm64( vector<uint8_t> & memory, uint64_t base_address, uint64_t start, uint64_t stack_commit, uint64_t top_of_stack )
    {
        memset( this, 0, sizeof( *this ) );
        pc = start;
        stack_size = stack_commit;                 // remember how much of the top of RAM is allocated to the stack
        stack_top = top_of_stack;                  // where the stack started
        regs[ 31 ] = top_of_stack;                 // points at argc with argv, penv, and aux records above it
        base = base_address;                       // lowest valid address in the app's address space, maps to offset 0 in mem
        mem = memory.data();                       // save the pointer, but don't take ownership
        mem_size = memory.size();
        beyond = mem + memory.size();              // addresses beyond and later are illegal
        membase = mem - base;                      // real pointer to the start of the app's memory (prior to offset)
        memset( vec_ones, 0xff, sizeof( vec_ones ) );
    } //Arm64

    uint64_t run( uint64_t max_cycles );
    const char * reg_name( uint64_t reg );
    const char * vreg_name( uint64_t reg );

    union floating
    {
        uint16_t h;
        float f;
        double d;
        vec16_t b16;
    };

    uint64_t regs[ 32 ];            // x0 through x31. x31 is sp. XZR references to x31 are handled in code
    floating vregs[ 32 ];           // v0 through v31
    uint64_t pc;
    uint64_t tpidr_el0;             // thread id, can be set and retrieved by apps via msr/mrs tpidr_el0.
    uint64_t fpcr;                  // floating point control register
    bool fN, fZ, fC, fV;            // negative, zero, unsigned carry or overflow, signed overflow.

    uint64_t cycles_so_far;
    uint8_t * mem;
    uint8_t * beyond;
    uint64_t base;
    uint8_t * membase;              // host pointer to base of vm's memory
    uint64_t stack_size;
    uint64_t stack_top;
    uint64_t mem_size;
    vec16_t vec_zeroes;
    vec16_t vec_ones;

    uint64_t getoffset( uint64_t address )
    {
        return address - base;
    } //getoffset

    uint64_t get_vm_address( uint64_t offset )
    {
        return base + offset;
    } //get_vm_address

    uint64_t host_to_vm_address( void * p )
    {
        return (uint64_t) ( (uint8_t *) p - mem + base );
    } //host_to_vm_address

    uint8_t * getmem( uint64_t offset )
    {
        #ifdef NDEBUG

            return membase + offset;

        #else

            uint8_t * r = membase + offset;

            if ( r >= beyond )
                arm64_hard_termination( *this, "memory reference beyond address space:", offset );

            if ( r < mem )
                arm64_hard_termination( *this, "memory reference prior to address space:", offset );

            return r;

        #endif
    } //getmem

    bool is_address_valid( uint64_t offset )
    {
        uint8_t * r = membase + offset;
        return ( ( r < beyond ) && ( r >= mem ) );  
    } //is_address_valid

    uint64_t getui64( uint64_t o ) { return * (uint64_t *) getmem( o ); }
    uint32_t getui32( uint64_t o ) { return * (uint32_t *) getmem( o ); }
    uint16_t getui16( uint64_t o ) { return * (uint16_t *) getmem( o ); }
    uint8_t getui8( uint64_t o ) { return * (uint8_t *) getmem( o ); }
    float getfloat( uint64_t o ) { return * (float *) getmem( o ); }
    double getdouble( uint64_t o ) { return * (double *) getmem( o ); }

    void setui64( uint64_t o, uint64_t val ) { * (uint64_t *) getmem( o ) = val; }
    void setui32( uint64_t o, uint32_t val ) { * (uint32_t *) getmem( o ) = val; }
    void setui16( uint64_t o, uint16_t val ) { * (uint16_t *) getmem( o ) = val; }
    void setui8( uint64_t o, uint8_t val ) { * (uint8_t *) getmem( o ) = val; }
    void setfloat( uint64_t o, float val ) { * (float *) getmem( o ) = val; }
    void setdouble( uint64_t o, double val ) { * (double *) getmem( o ) = val; }

    uint8_t vreg_getui8( uint64_t vreg, uint64_t lowbyte ) { return * (uint8_t *) ( vreg_ptr( vreg, lowbyte ) ); }
    uint16_t vreg_getui16( uint64_t vreg, uint64_t lowbyte ) { return * (uint16_t *) ( vreg_ptr( vreg, lowbyte ) ); }
    uint32_t vreg_getui32( uint64_t vreg, uint64_t lowbyte ) { return * (uint32_t *) ( vreg_ptr( vreg, lowbyte ) ); }
    uint64_t vreg_getui64( uint64_t vreg, uint64_t lowbyte ) { return * (uint64_t *) ( vreg_ptr( vreg, lowbyte ) ); }
    float vreg_getfloat( uint64_t vreg, uint64_t lowbyte ) { return * (float *) ( vreg_ptr( vreg, lowbyte ) ); }
    double vreg_getdouble( uint64_t vreg, uint64_t lowbyte ) { return * (double *) ( vreg_ptr( vreg, lowbyte ) ); }

    void vreg_setui8( uint64_t vreg, uint64_t lowbyte, uint8_t val ) { * (uint8_t *) ( vreg_ptr( vreg, lowbyte ) ) = val; }
    void vreg_setui16( uint64_t vreg, uint64_t lowbyte, uint16_t val ) { * (uint16_t *) ( vreg_ptr( vreg, lowbyte ) ) = val; }
    void vreg_setui32( uint64_t vreg, uint64_t lowbyte, uint32_t val ) { * (uint32_t *) ( vreg_ptr( vreg, lowbyte ) ) = val; }
    void vreg_setui64( uint64_t vreg, uint64_t lowbyte, uint64_t val ) { * (uint64_t *) ( vreg_ptr( vreg, lowbyte ) ) = val; }
    void vreg_setfloat( uint64_t vreg, uint64_t lowbyte, float val ) { * (float *) ( vreg_ptr( vreg, lowbyte ) ) = val; }
    void vreg_setdouble( uint64_t vreg, uint64_t lowbyte, double val ) { * (double *) ( vreg_ptr( vreg, lowbyte ) ) = val; }

    void trace_vregs();
    void force_trace_vregs();

  private:
    enum FPRounding { FPRounding_TIEEVEN, FPRounding_POSINF, FPRounding_NEGINF,  FPRounding_ZERO, FPRounding_TIEAWAY, FPRounding_ODD };
    enum ElementComparisonResult { ecr_lt, ecr_eq, ecr_gt };
    uint64_t op;

    void unhandled( void );

    __inline_perf uint64_t opbits( uint64_t lowbit, uint64_t len )
    {
        uint64_t val = ( op >> lowbit );
        assert( 64 != len ); // the next line of code wouldn't work but there are no callers that do this
        return ( val & ( ( 1ull << len ) - 1 ) );
    } //opbits

    // when inlined, the compiler uses btc for bits. when non-inlined it does the slow thing
    // bits is the 0-based high bit that will be extended to the left.

#ifdef _MSC_VER
    __forceinline
#endif
    uint64_t openai_add_with_carry64( uint64_t x, uint64_t y, bool carry, bool setflags );
    uint64_t add_with_carry64( uint64_t x, uint64_t y, bool carry, bool setflags );
    uint32_t add_with_carry32( uint32_t x, uint32_t y, bool carry, bool setflags );
    uint64_t sub64( uint64_t x, uint64_t y, bool setflags );
    uint32_t sub32( uint32_t x, uint32_t y, bool setflags );
    bool check_conditional( uint64_t cond );
    uint64_t shift_reg64( uint64_t reg, uint64_t shift_type, uint64_t amount );
    uint32_t shift_reg32( uint64_t reg, uint64_t shift_type, uint64_t amount );
    uint64_t reg_or_sp_value( uint64_t x );
    uint64_t extend_reg( uint64_t m, uint64_t extend_type, uint64_t shift );
    uint64_t val_reg_or_zr( uint64_t r );
    const char * render_flags();
    ElementComparisonResult compare_vector_elements( uint8_t * pl, uint8_t * pr, uint64_t width, bool unsigned_compare );
    uint8_t * vreg_ptr( uint64_t reg, uint64_t offset )
    {
        uint8_t * pv = (uint8_t *) vregs[ reg ].b16;
        return pv + offset;
    }
    void zero_vreg( uint64_t reg ) { memset( vreg_ptr( reg, 0 ), 0, sizeof( vec16_t ) ); }
    uint64_t adv_simd_expand_imm( uint64_t op, uint64_t cmode, uint64_t imm8 );
    uint64_t replicate_bytes( uint64_t val, uint64_t byte_len );
    void set_flags_from_double( double result );
    void set_flags_from_nzcv( uint64_t nzcv );
    int64_t double_to_fixed_int64( double d, uint64_t fracbits, FPRounding rounding );
    uint64_t double_to_fixed_uint64( double d, uint64_t fracbits, FPRounding rounding );
    uint32_t double_to_fixed_uint32( double d, uint64_t fracbits, FPRounding rounding );
    int32_t double_to_fixed_int32( double d, uint64_t fracbits, FPRounding rounding );
    double round_double( double d, FPRounding rounding );
    FPRounding fp_decode_rmode( uint64_t rmode );
    FPRounding fp_decode_rm( uint64_t rm );

    void trace_state( void );                  // trace the machine current status
}; //Arm64


