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

extern void emulator_invoke_svc( Arm64 & cpu );                                               // called when the svc instruction is executed
extern const char * emulator_symbol_lookup( uint64_t address, uint64_t & offset );            // returns the best guess for a symbol name and offset for the address
extern void emulator_hard_termination( Arm64 & cpu, const char *pcerr, uint64_t error_value ); // show an error and exit

typedef struct vec16_t
{
    union
    {
        uint64_t ui64[ 2 ];
        uint32_t ui32[ 4 ];
        uint16_t ui16[ 8 ];
        uint8_t ui8[ 16 ];
        double d[ 2 ];
        float f[ 4 ];
    };
} vec16_t;

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
        memset( &vec_ones, 0xff, sizeof( vec_ones ) );
    } //Arm64

    uint64_t run( void );

    uint64_t regs[ 32 ];            // x0 through x31. x31 is sp. XZR references to x31 are handled in code
    vec16_t vregs[ 32 ];            // v0 through v31
    uint64_t pc;
    uint64_t tpidr_el0;             // thread id, can be set and retrieved by apps via msr/mrs tpidr_el0.
    uint64_t fpcr;                  // floating point control register
    bool fN, fZ, fC, fV;            // negative, zero, unsigned carry or overflow, signed overflow.

    uint8_t * mem;
    uint8_t * beyond;
    uint64_t base;
    uint8_t * membase;              // host pointer to base of vm's memory
    uint64_t stack_size;
    uint64_t stack_top;
    uint64_t mem_size;
    uint64_t cycles;
    vec16_t vec_zeroes;
    vec16_t vec_ones;

    uint64_t getoffset( uint64_t address ) const
    {
        return address - base;
    } //getoffset

    uint64_t get_vm_address( uint64_t offset ) const
    {
        return base + offset;
    } //get_vm_address

    uint64_t host_to_vm_address( void * p ) const
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
                emulator_hard_termination( *this, "memory reference beyond address space:", offset );

            if ( r < mem )
                emulator_hard_termination( *this, "memory reference prior to address space:", offset );

            return r;

        #endif
    } //getmem

    bool is_address_valid( uint64_t offset ) const
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

    void trace_vregs();
    void force_trace_vregs();
    void trace_state( void );                  // trace the machine current status

  private:
    enum FPRounding { FPRounding_TIEEVEN, FPRounding_POSINF, FPRounding_NEGINF,  FPRounding_ZERO, FPRounding_TIEAWAY, FPRounding_ODD };
    enum ElementComparisonResult { ecr_lt, ecr_eq, ecr_gt };
    uint64_t op; // opcode of the current instruction being executed. only need uint32_t, but faster like this given usage.

    void unhandled( void );

    __inline_perf uint64_t opbits( uint64_t lowbit, uint64_t len ) const
    {
        uint64_t val = ( op >> lowbit );
        assert( 64 != len ); // the next line of code wouldn't work but there are no callers that do this
        return ( val & ( ( 1ull << len ) - 1 ) );
    } //opbits

    __inline_perf uint64_t opbit( uint64_t bit ) const
    {
        return ( 1 & ( op >> bit ) );
    } //opbit

    uint64_t add_with_carry64( uint64_t x, uint64_t y, bool carry, bool setflags );
    uint32_t add_with_carry32( uint32_t x, uint32_t y, bool carry, bool setflags );
    uint64_t sub64( uint64_t x, uint64_t y, bool setflags );
    uint32_t sub32( uint32_t x, uint32_t y, bool setflags );
    bool check_conditional( uint64_t cond ) const;
    uint64_t shift_reg64( uint64_t reg, uint64_t shift_type, uint64_t amount );
    uint32_t shift_reg32( uint64_t reg, uint64_t shift_type, uint64_t amount );
    uint64_t extend_reg( uint64_t m, uint64_t extend_type, uint64_t shift, bool fullm = true );
    uint64_t val_reg_or_zr( uint64_t r ) const;
    const char * render_flags() const;
    ElementComparisonResult compare_vector_elements( uint8_t * pl, uint8_t * pr, uint64_t width, bool unsigned_compare );
    uint8_t * vreg_ptr( uint64_t reg, uint64_t offset ) { return offset + (uint8_t *) & ( vregs[ reg ] ); }
    void zero_vreg( uint64_t reg ) { vregs[ reg ] = vec_zeroes; }
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
}; //Arm64


