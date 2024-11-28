/*
    This is a simplistic Arm64/ARMv8 emulator.
    Only physical memory is supported.
    Only Base and SIMD&FP instructions are supported.
    Many instructions (70%?) are not yet implemented.
    I only implement instructions g++ or Rust compilers emit or use in their runtimes.

    Written by David Lee in October 2024

    Useful: https://developer.arm.com/
            https://mariokartwii.com/armv8/ch16.html
*/

#include <stdint.h>
#include <memory.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <math.h>
#include <chrono>

#include <djl_128.hxx>
#include <djltrace.hxx>

#include "arm64.hxx"

using namespace std;
using namespace std::chrono;

#if defined( __GNUC__ ) && !defined( __APPLE__)
#pragma GCC diagnostic ignored "-Wformat="
#endif

static uint32_t g_State = 0;

const uint32_t stateTraceInstructions = 1;
const uint32_t stateEndEmulation = 2;

bool Arm64::trace_instructions( bool t )
{
    bool prev = ( 0 != ( g_State & stateTraceInstructions ) );
    if ( t )
        g_State |= stateTraceInstructions;
    else
        g_State &= ~stateTraceInstructions;
    return prev;
} //trace_instructions

void Arm64::end_emulation() { g_State |= stateEndEmulation; }

void Arm64::set_flags_from_nzcv( uint64_t nzcv )
{
    fN = ( 0 != ( nzcv & 8 ) );
    fZ = ( 0 != ( nzcv & 4 ) );
    fC = ( 0 != ( nzcv & 2 ) );
    fV = ( 0 != ( nzcv & 1 ) );
} //set_flags_from_nzcv

uint64_t count_bits( uint64_t x )
{
    uint64_t count = 0;
    while ( 0 != x )
    {
        if ( x & 1 )
            count++;
        x >>= 1;
    }
    return count;
} //count_bits

int32_t double_to_fixed_int32( double d, uint64_t fracbits, uint64_t rmode )
{
    if ( 3 == rmode )
        return (int32_t) floor( d * ( 1 << fracbits ) );

    return (int32_t) round( d * ( 1 << fracbits ) );
} //double_to_fixed_int32

uint32_t double_to_fixed_uint32( double d, uint64_t fracbits, uint64_t rmode )
{
    if ( 3 == rmode )
        return (uint32_t) floor( d * ( 1 << fracbits ) );

    return (uint32_t) round( d * ( 1 << fracbits ) );
} //double_to_fixed_uint32

int64_t double_to_fixed_int64( double d, uint64_t fracbits, uint64_t rmode )
{
    if ( 3 == rmode )
        return (int64_t) floor( d * ( 1 << fracbits ) );

    return (int64_t) round( d * ( 1 << fracbits ) );
} //double_to_fixed_int64

uint64_t double_to_fixed_uint64( double d, uint64_t fracbits, uint64_t rmode )
{
    if ( 3 == rmode )
        return (uint64_t) floor( d * ( 1 << fracbits ) );

    return (uint64_t) round( d * ( 1 << fracbits ) );
} //double_to_fixed_uint64

uint64_t get_bit( uint64_t x, uint64_t bit_number )
{
    return ( ( x >> bit_number ) & 1 );
} //get_bit

uint64_t plaster_bit( uint64_t x, uint64_t bit_number, uint64_t bit_val )
{
    uint64_t mask = ~ ( 1ull << bit_number );
    uint64_t plastered_bit = ( bit_val << bit_number );
    return ( ( x & mask ) | plastered_bit );
} //plaster_bit

uint64_t gen_bitmask( uint64_t n )
{
  if ( 0 == n )
      return 0;

  return ( ~0ull ) >> ( 64ull - n );
} //gen_bitmask

uint64_t get_elem_bits( uint64_t val, uint64_t c, uint64_t container_size )
{
    uint64_t mask = gen_bitmask( container_size );
    return ( val & ( mask << ( c * 8 ) ) );
} //get_elem_bits

uint64_t reverse_bytes( uint64_t val, uint64_t n )
{
    uint64_t result = 0;
    uint64_t sw = n / 8;

    for ( uint64_t s = 0; s < sw; s++ )
    {
        uint64_t r = get_elem_bits( val, s, 8 );
        r >>= ( s * 8 );
        result |= ( r << ( 8 * ( ( sw - 1 ) - s ) ) );
    }

    return result;
} //reverse_elems

uint64_t get_bits( uint64_t x, uint64_t lowbit, uint64_t len )
{
    uint64_t val = ( x >> lowbit );
    if ( 64 == len )
        return val;
    return ( val & ( ( 1ull << len ) - 1 ) );
} //get_bits

uint64_t one_bits( uint64_t bits )
{
    if ( 64 == bits )
        return ~ 0ull;

    return ( ( 1ull << bits ) - 1 );
} //one_bits

uint64_t zero_extend( uint64_t x, uint64_t bits )
{
    return ( x & one_bits( bits ) );
} //zero_extend

uint64_t replicate_bits( uint64_t val, uint64_t len )
{
    if ( 0 != val )
        return one_bits( len );
    return 0;
} //replicate_bits

int64_t sign_extend( uint64_t x, uint64_t high_bit )
{
    x &= ( 1ull << ( high_bit + 1 ) ) - 1; // clear bits above the high bit since they may contain noise
    const int64_t m = 1ull << high_bit;
    return ( x ^ m ) - m;
} //sign_extend

uint32_t sign_extend32( uint32_t x, uint32_t high_bit )
{
    x &= ( 1u << ( high_bit + 1 ) ) - 1; // clear bits above the high bit since they may contain noise
    const int32_t m = ( (uint32_t) 1 ) << high_bit;
    return ( x ^ m ) - m;
} //sign_extend32

uint64_t plaster_bits( uint64_t val, uint64_t bits, uint64_t len, uint64_t low_position )
{
    uint64_t low_ones = ( low_position > 0 ) ? one_bits( low_position ) : 0;
    uint64_t high_ones = one_bits( 64 - low_position - len );
    high_ones <<= ( low_position + len );
    uint64_t with_hole = ( val & high_ones ) | ( val & low_ones );
    return ( with_hole | ( bits << low_position ) );
} //plaster_bits

uint64_t lowest_set_bit_nz( uint64_t x )
{
    uint64_t mask = 1;
    for ( uint64_t i = 0; i < 64; i++ )
    {
        if ( x & mask )
            return i;
        mask <<= 1;
    }

    assert( false ); // _nz means this shoudn't hit
    return 64;
} //lowest_set_bit_nz

uint64_t highest_set_bit_nz( uint64_t x )
{
    uint64_t mask = 1ull << 63;
    for ( uint64_t i = 64; i > 0; i-- )
    {
        if ( x & mask )
            return i - 1;
        mask >>= 1;
    }

    assert( false ); // _nz means this shoudn't hit
    return 0;
} //highest_set_bit_nz

uint64_t low_bits( uint64_t x, uint64_t num )
{
    return ( x & one_bits( num ) );
} //low_bits

const char * Arm64::render_flags()
{
    static char ac[ 5 ] = {0};

    ac[ 0 ] = fN ? 'N' : 'n';
    ac[ 1 ] = fZ ? 'Z' : 'z';
    ac[ 2 ] = fC ? 'C' : 'c';
    ac[ 3 ] = fV ? 'V' : 'v';

    return ac;
} //render_flags

const char * reg_or_sp( uint64_t x, bool xregs )
{
    assert( x <= 31 );
    static char ac[ 10 ];
    if ( 31 == x )
        strcpy( ac, "sp" );
    else
        snprintf( ac, _countof( ac ), "%c%llu", xregs ? 'x' : 'w', x );
    return ac;
} //reg_or_sp

const char * reg_or_sp2( uint64_t x, bool xregs )
{
    assert( x <= 31 );
    static char ac[ 10 ];
    if ( 31 == x )
        strcpy( ac, "sp" );
    else
        snprintf( ac, _countof( ac ), "%c%llu", xregs ? 'x' : 'w', x );
    return ac;
} //reg_or_sp2

const char * reg_or_sp3( uint64_t x, bool xregs )
{
    assert( x <= 31 );
    static char ac[ 10 ];
    if ( 31 == x )
        strcpy( ac, "sp" );
    else
        snprintf( ac, _countof( ac ), "%c%llu", xregs ? 'x' : 'w', x );
    return ac;
} //reg_or_sp3

const char * reg_or_zr( uint64_t x, bool xregs )
{
    assert( x <= 31 );
    static char ac[ 10 ];
    if ( 31 == x )
        snprintf( ac, _countof( ac ), "%czr", xregs ? 'x' : 'w' );
    else
        snprintf( ac, _countof( ac ), "%c%llu", xregs ? 'x' : 'w', x );
    return ac;
} //reg_or_zr

const char * reg_or_zr2( uint64_t x, bool xregs )
{
    assert( x <= 31 );
    static char ac[ 10 ];
    if ( 31 == x )
        snprintf( ac, _countof( ac ), "%czr", xregs ? 'x' : 'w' );
    else
        snprintf( ac, _countof( ac ), "%c%llu", xregs ? 'x' : 'w', x );
    return ac;
} //reg_or_zr2

const char * reg_or_zr3( uint64_t x, bool xregs )
{
    assert( x <= 31 );
    static char ac[ 10 ];
    if ( 31 == x )
        snprintf( ac, _countof( ac ), "%czr", xregs ? 'x' : 'w' );
    else
        snprintf( ac, _countof( ac ), "%c%llu", xregs ? 'x' : 'w', x );
    return ac;
} //reg_or_zr3

const char * reg_or_zr4( uint64_t x, bool xregs )
{
    assert( x <= 31 );
    static char ac[ 10 ];
    if ( 31 == x )
        snprintf( ac, _countof( ac ), "%czr", xregs ? 'x' : 'w' );
    else
        snprintf( ac, _countof( ac ), "%c%llu", xregs ? 'x' : 'w', x );
    return ac;
} //reg_or_zr4

uint64_t Arm64::val_reg_or_zr( uint64_t r )
{
    if ( 31 == r )
        return 0;
    return regs[ r ];
} //val_reg_or_zr

Arm64::ElementComparisonResult Arm64::compare_vector_elements( uint8_t * pl, uint8_t * pr, uint64_t width, bool unsigned_compare )
{
    assert( width >= 1 && width <= 16 );

    if ( 1 == width && unsigned_compare )
    {
        uint8_t l = *pl;
        uint8_t r = *pr;
        if ( l < r )
            return ecr_lt;
        else if ( l == r )
            return ecr_eq;
        return ecr_gt;
    }
    else
        unhandled();

    assert( false );
    return ecr_eq;
} //compare_vector_elements

const char * get_ld1_vector_T( uint64_t size, uint64_t Q )
{
    if ( 0 == size )
    {
        if ( 0 == Q )
            return "8b";
        else
            return "16b";
    }

    if ( 1 == size )
    {
        if ( 0 == Q )
            return "4h";
        else
            return "8h";
    }

    if ( 2 == size )
    {
        if ( 0 == Q )
            return "2s";
        else
            return "4s";
    }

    if ( 3 == size )
    {
        if ( 0 == Q )
            return "1d";
        else
            return "2d";
    }

    return "UNKNOWN";
} //get_ld1_vector_T

const char * get_vector_T( uint64_t imm5, uint64_t Q )
{
    if ( 1 == ( 1 & imm5 ) )
    {
        if ( 0 == Q )
            return "8b";
        else
            return "16b";
    }

    if ( 2 == ( 3 & imm5 ) )
    {
        if ( 0 == Q )
            return "4h";
        else
            return "8h";
    }

    if ( 4 == ( 7 & imm5 ) )
    {
        if ( 0 == Q )
            return "2s";
        else
            return "4s";
    }

    if ( 8 == ( 0xf & imm5 ) )
    {
        if ( 0 == Q )
            return "RESERVED";
        else
            return "2d";
    }

    return "RESERVED";
} //get_vector_T

const char * get_sshr_vector_T( uint64_t immh, uint64_t Q )
{
    if ( 1 == immh )
    {
        if ( 0 == Q )
            return "8b";
        else
            return "16b";
    }

    if ( 2 == ( 0xe & immh ) )
    {
        if ( 0 == Q )
            return "4h";
        else
            return "8h";
    }

    if ( 4 == ( 0xc & immh ) )
    {
        if ( 0 == Q )
            return "2s";
        else
            return "4s";
    }

    if ( 8 == ( 8 & immh ) )
    {
        if ( 0 == Q )
            return "RESERVED";
        else
            return "2d";
    }

    return "RESERVED";
} //get_sshr_vector_T

uint64_t Arm64::extend_reg( uint64_t m, uint64_t extend_type, uint64_t shift )
{
    uint64_t x = ( 31 == m ) ? 0 : regs[ m ];

    switch( extend_type )
    {
        case 0: { x &= 0xff; break; }                 // UXTB 
        case 1: { x &= 0xffff; break; }               // UXTH
        case 2: { x &= 0xffffffff; break; }           // LSL/UXTW
        case 3: { break; }                            // UXTX 
        case 4: { x = sign_extend( x, 7 ); break; }   // SXTB 
        case 5: { x = sign_extend( x, 15 ); break; }  // SXTH 
        case 6: { x = sign_extend( x, 31 ); break; }  // SXTW 
        case 7: { break; }                            // SXTX 
        default: unhandled();
    }

    x <<= shift;
    return x;
} //extend_reg

const char * shift_type( uint64_t x )
{
    if ( 0 == x )
        return "lsl";
    if ( 1 == x )
        return "lsr";
    if ( 2 == x )
        return "asr";
    if ( 3 == x )
        return "ror";

    return "UNKNOWN_SHIFT";
} //shift_type

const char * extend_type( uint64_t x )
{
    if ( 0 == x )
        return "UXTB";
    if ( 1 == x )
        return "UXTH";
    if ( 2 == x )
        return "UXTW";
    if ( 3 == x )
        return "LSL | UXTW";
    if ( 4 == x )
        return "SXTB";
    if ( 5 == x )
        return "SXTH";
    if ( 6 == x )
        return "SXTW";
    if ( 7 == x )
        return "SXTX";
    return "UNKNOWN_EXTEND";
} //extend_type

uint64_t Arm64::replicate_bytes( uint64_t val, uint64_t byte_len )
{
    uint64_t mask = one_bits( byte_len * 8 );
    uint64_t pattern = ( val & mask );
    uint64_t repeat = 8 / byte_len;
    uint64_t result = 0;
    for ( uint64_t x = 0; x < repeat; x++ )
        result |= ( pattern << ( x * byte_len * 8) );

    //tracer.Trace( "replicate bytes val %#llx byte_len %lld. mask %#llx, pattern %#llx, repeat %lld, result %#llx\n", val, byte_len, mask, pattern, repeat, result );
    return result;
} //replicate_bytes

uint64_t Arm64::adv_simd_expand_imm( uint64_t operand, uint64_t cmode, uint64_t imm8 )
{
    //tracer.Trace( "operand %#llx, cmode %#llx, imm8 %#llx\n", operand, cmode, imm8 );
    uint64_t imm64 = 0;
    uint64_t cm = ( cmode >> 1 );
    switch ( cm )
    {
        case 0: { imm64 = replicate_bytes( imm8, 4 ); break; }
        case 1: { imm64 = replicate_bytes( imm8 << 8, 4 ); break; }
        case 2: { imm64 = replicate_bytes( imm8 << 16, 4 ); break; }
        case 3: { imm64 = replicate_bytes( imm8 << 24, 4 ); break; }
        case 4: { imm64 = replicate_bytes( imm8, 2 ); break; }
        case 5: { imm64 = replicate_bytes( imm8 << 8, 2 ); break; }
        case 6:
        {
            if ( 0 == ( cmode & 1 ) )
                imm64 = replicate_bytes( ( imm8 << 16 ) | 0xffff, 4 );
            else
                imm64 = replicate_bytes( ( imm8 << 8 ) | 0xff, 4 );
            break;
        }
        case 7:
        {
            if ( 0 == ( cmode & 1 ) )
            {
                if  ( 0 == operand )
                    imm64 = replicate_bytes( imm8, 1 );
                else if ( 1 == operand )
                {
                    uint64_t imm8a = ( imm8 & 0x80 ) ? 0xff : 0;
                    uint64_t imm8b = ( imm8 & 0x40 ) ? 0xff : 0;
                    uint64_t imm8c = ( imm8 & 0x20 ) ? 0xff : 0;
                    uint64_t imm8d = ( imm8 & 0x10 ) ? 0xff : 0;
                    uint64_t imm8e = ( imm8 & 0x08 ) ? 0xff : 0;
                    uint64_t imm8f = ( imm8 & 0x04 ) ? 0xff : 0;
                    uint64_t imm8g = ( imm8 & 0x02 ) ? 0xff : 0;
                    uint64_t imm8h = ( imm8 & 0x01 ) ? 0xff : 0;
                    imm64 = ( imm8a << 56 ) | ( imm8b << 48 ) | ( imm8c << 40 ) | ( imm8d << 32 ) | ( imm8e << 24 ) | ( imm8f << 16 ) | ( imm8g << 8 ) | imm8h;
                }
                else
                    unhandled();
            }
            else
            {
                if ( 0 == operand )
                {
                    // imm32 = imm8<7>:NOT(imm8<6>):Replicate(imm8<6>,5):imm8<5:0>:Zeros(19);
                    // imm64 = Replicate(imm32, 2);

                    uint64_t a = get_bit( imm8, 7 );
                    uint64_t b = !get_bit( imm8, 6 );
                    uint64_t c = replicate_bits( get_bit( imm8, 6 ), 5 );
                    uint64_t d = get_bits( imm8, 0, 6 );
                    uint32_t imm32 = (uint32_t) ( ( ( a << 12 ) | ( b << 11 ) | ( c << 6 ) | d ) << 19 );
                    imm64 = replicate_bytes( imm32, 4 );
                }
                else
                {
                    // imm64 = imm8<7>:NOT(imm8<6>):Replicate(imm8<6>,8):imm8<5:0>:Zeros(48);
                    imm64 = ( get_bits( imm8, 7, 1 ) << 63 ) |
                            ( ( get_bits( imm8, 6, 1 ) ? 0ull : 1ull ) << 62 ) |
                            ( replicate_bits( get_bits( imm8, 6, 1 ), 8 ) << ( 62 - 8 ) ) |
                            ( get_bits( imm8, 0, 6 ) << 48 );
                }
            }
            break;
        }
        default: { unhandled(); }
    }

    //tracer.Trace( "expand imm cmode %#llx, from %llx to %llx\n", cmode, imm8, imm64 );
    return imm64;
} //adv_simd_expand_imm

static inline uint64_t ror( uint64_t elt, uint64_t size )
{
    return ( ( elt & 1 ) << ( size - 1 ) ) | ( elt >> 1 );
} //ror

uint64_t count_leading_zeroes( uint64_t x, uint64_t bit_width )
{
    uint64_t count = 0;
    while ( x )
    {
        count++;
        x >>= 1;
    }

    return bit_width - count;
} //count_leading_zeroes

uint64_t decode_logical_immediate( uint64_t val, uint64_t bit_width )
{
    uint64_t N = get_bits( val, 12, 1 );
    uint64_t immr = get_bits( val, 6, 6 );
    uint64_t imms = get_bits( val, 0, 6 );

    uint64_t lzero_count = count_leading_zeroes( ( N << 6 ) | ( ~imms & 0x3f ), 32 );
    uint64_t len = 31 - lzero_count;
    uint64_t size = ( 1ull << len );
    uint64_t R = ( immr & ( size - 1 ) );
    uint64_t S = ( imms & ( size - 1 ) );
    uint64_t pattern = ( 1ull << ( S + 1 ) ) - 1;

    for ( uint64_t i = 0; i < R; i++ )
        pattern = ror( pattern, size );

    while ( size != bit_width )
    {
        pattern |= ( pattern << size );
        size *= 2;
    }
    return pattern;
} //decode_logical_immediate

static const char * conditions[16] = { "eq", "ne", "cs", "cc", "mi", "pl", "vs", "vc", "hi", "ls", "ge", "lt", "gt", "le", "al", "nv" };

const char * get_cond( uint64_t x )
{
    if ( x <= 15 )
        return conditions[ x ];

    return "UNKNOWN_CONDITION";
} //get_cond

char get_byte_len( uint64_t l )
{
    if ( 1 == l )
        return 'B';
    if ( 2 == l )
        return 'H';
    if ( 4 == l )
        return 'W';
    if ( 8 == l )
        return 'D';
    if ( 16 == l )
        return 'Q';

    return '?';
} //get_byte_len

uint64_t vfp_expand_imm( uint64_t imm8, uint64_t N )
{
    assert( 16 == N || 32 == N || 64 == N );
    uint64_t E = ( 16 == N ) ? 5 : ( 32 == N ) ? 8 : 11;
    uint64_t F = N - E - 1;
    uint64_t sign = ( 0 != ( imm8 & 0x80 ) );
    uint64_t exp_part_1 = ( get_bits( imm8, 6, 1 ) ? 0 : 1 );
    uint64_t exp_part_2 = replicate_bits( get_bits( imm8, 6, 1 ), E - 3 );
    uint64_t exp_part_3 = get_bits( imm8, 4, 2 );
    uint64_t exp = ( exp_part_1 << ( E - 3 + 2 ) ) | ( exp_part_2 << 2 ) | exp_part_3;
    uint64_t frac_shift = F - 4;
    uint64_t frac = ( imm8 & 0xf ) << frac_shift;
    uint64_t result = ( sign << ( N - 1 ) ) | ( exp << F ) | frac;
//    tracer.Trace( "result %#llx, imm8 %llu, N %llu, E %llu, F %llu, sign %llu, exp %#llx, exp1 %#llx, exp2 %#llx, exp3 %#llx, frac_shift %llu, frac %#llx\n",
//                  result, imm8, N, E, F, sign, exp, exp_part_1, exp_part_2, exp_part_3, frac_shift, frac );
    return result;
} //vfp_expand_imm

char get_fcvt_precision( uint64_t x )
{
    return ( 0 == x ) ? 's' : ( 1 == x ) ? 'd' : ( 3 == x ) ? 'h' : '?';
} //get_fcvt_precision

void Arm64::trace_state()
{
    static const char * previous_symbol = 0;
    uint64_t symbol_offset;
    const char * symbol_name = arm64_symbol_lookup( pc, symbol_offset );
    if ( symbol_name == previous_symbol )
        symbol_name = "";
    else
        previous_symbol = symbol_name;

    char symbol_offset_str[ 40 ];
    symbol_offset_str[ 0 ] = 0;

    if ( 0 != symbol_name[ 0 ] )
    {
        if ( 0 != symbol_offset )
            snprintf( symbol_offset_str, _countof( symbol_offset_str ), " + %llx", symbol_offset );
        strcat( symbol_offset_str, "\n            " );
    }

    //tracer.TraceBinaryData( getmem( 0x4946b0 ), 16, 4 );
    tracer.Trace( "pc %8llx %s%s op %08llx %s ==> ", pc, symbol_name, symbol_offset_str, op, render_flags() );

    uint8_t hi8 = (uint8_t) ( op >> 24 );
    switch ( hi8 )
    {
        case 0: // UDF
        {
            uint64_t bits23to16 = opbits( 16, 8 );
            uint64_t imm16 = opbits( 0, 16 );
            if ( 0 == bits23to16 )
                tracer.Trace( "udf %#llx\n", imm16 );
            else
                unhandled();
            break;
        }
        case 0x0d: case 0x4d: // LD1 { <Vt>.B }[<index>], [<Xn|SP>]    ;    LD1 { <Vt>.B }[<index>], [<Xn|SP>], #1
                              // LD1R { <Vt>.<T> }, [<Xn|SP>], <imm>   ;    LD1R { <Vt>.<T> }, [<Xn|SP>], <Xm>
                              // ST1 { <Vt>.B }[<index>], [<Xn|SP>]    ;    ST1 { <Vt>.B }[<index>], [<Xn|SP>], #1

        {
            uint64_t R = opbits( 21, 1 );
            if ( R )
                unhandled();
            uint64_t post_index = opbits( 23, 1 );
            uint64_t opcode = opbits( 13, 3 );
            uint64_t bit13 = opbits( 13, 1 );
            if ( bit13 )
                unhandled();
            uint64_t size = opbits( 10, 2 );
            uint64_t n = opbits( 5, 5 );
            uint64_t m = opbits( 16, 5 );
            uint64_t t = opbits( 0, 5 );
            uint64_t replicate = opbits( 14, 1 );
            uint64_t S = opbits( 12, 1 );
            uint64_t Q = opbits( 30, 1 );
            uint64_t L = opbits( 22, 1 );
            uint64_t index = 0;
            uint64_t scale = get_bits( opcode, 1, 2 );
            if ( 3 == scale )
                scale = size;
            else if ( 0 == scale )
                index = ( Q << 3 ) | ( S << 2 ) | size;
            else if ( 1 == scale )
                index = ( Q << 2 ) | ( S << 1 ) | get_bits( size, 1, 1 );
            else if ( 2 == scale )
            {
                if ( 0 == ( size & 1 ) )
                    index = ( Q << 1 ) | S;
                else
                {
                    index = Q;
                    scale = 3;
                }
            }

            const char * pOP = L ? "ld" : "st";
            char type = ( 0 == opcode ) ? 'b' : ( 2 == opcode ) ? 'h' : ( 4 == opcode && 0 == size ) ? 's' : 'd';
            if ( post_index )
            {
                if ( 31 == m ) // immediate offset
                {
                    uint64_t imm = 1ull << size;
                    tracer.Trace( "%s1%s {v%llu.%c}[%llu], [%s], #%llu\n", pOP, replicate ? "r" : "", t, type, index, reg_or_sp( n, true ), imm );
                }
                else // register offset
                    tracer.Trace( "%s1%s {v%llu.%c}[%llu], [%s], %s\n", pOP, replicate ? "r" : "", t, type, index, reg_or_sp( n, true ), reg_or_zr( m, true ) );
            }
            else // no offset
                tracer.Trace( "%s1%s {v%llu.%c}[%llu], [%s]\n", pOP, replicate ? "r" : "", t, type, index, reg_or_sp( n, true ) );
            break;
        }
        case 0x08: // LDAXRB <Wt>, [<Xn|SP>{, #0}]    ;    LDARB <Wt>, [<Xn|SP>{, #0}]    ;    STLXRB <Ws>, <Wt>, [<Xn|SP>{, #0}]    ;    STLRB <Wt>, [<Xn|SP>{, #0}]
                   // STXRB <Ws>, <Wt>, [<Xn|SP>{, #0}] ;  LDXRB <Wt>, [<Xn|SP>{, #0}]
        case 0x48: // LDAXRH <Wt>, [<Xn|SP>{, #0}]    ;    LDARH <Wt>, [<Xn|SP>{, #0}]    ;    STLXRH <Ws>, <Wt>, [<Xn|SP>{, #0}]    ;    STLRH <Wt>, [<Xn|SP>{, #0}]
                   // STXRH <Ws>, <Wt>, [<Xn|SP>{, #0}] ;  LDXRH <Wt>, [<Xn|SP>{, #0}]
        {
            uint64_t bit23 = opbits( 23, 1 );
            uint64_t L = opbits( 22, 1 );
            uint64_t bit21 = opbits( 21, 1 );
            uint64_t s = opbits( 16, 5 );
            uint64_t oO = opbits( 15, 1 );
            uint64_t t2 = opbits( 10, 5 );
            uint64_t n = opbits( 5, 5 );
            uint64_t t = opbits( 0, 5 );

            if ( 0 != bit21 || 0x1f != t2 )
                unhandled();

            char suffix = ( hi8 & 0x40 ) ? 'h' : 'b';

            if ( L )
            {
                if ( 0x1f != s )
                    unhandled();
                tracer.Trace( "%s%c, w%llu, [%s, #0]\n", bit23 ? "ldar" : oO ? "ldaxr" : "ldxr", suffix, t, reg_or_sp( n, true ) );
            }
            else
            {
                if ( bit23 )
                    tracer.Trace( "stlr%c w%llu, [%s, #0]\n", suffix, t, reg_or_sp( n, true ) );
                else
                    tracer.Trace( "%s%c w%llu, w%llu, [%s, #0]\n", oO ? "stlxr" : "stxr", suffix, s, t, reg_or_sp( n, true ) );
            }
            break;
        }
        case 0x1f: // fmadd, vnmadd, fmsub, fnmsub
        {
            uint64_t ftype = opbits( 22, 2 );
            uint64_t bit21 = opbits( 21, 1 );
            uint64_t bit15 = opbits( 15, 1 );
            uint64_t m = opbits( 16, 5 );
            uint64_t a = opbits( 10, 5 );
            uint64_t n = opbits( 5, 5 );
            uint64_t d = opbits( 0, 5 );

            bool isn = ( 0 != bit21 );
            char t = ( 0 == ftype ) ? 's' : ( 3 == ftype ) ? 'h' : ( 1 == ftype ) ? 'd' : '?';
            if ( !bit15 )
                tracer.Trace( "%s %c%llu, %c%llu, %c%llu, %c%llu\n", isn ? "fnmadd" : "fmadd", t, d, t, n, t, m, t, a );
            else
                tracer.Trace( "%s %c%llu, %c%llu, %c%llu, %c%llu\n", isn ? "fnmsub" : "fmsub", t, d, t, n, t, m, t, a );
            break;
        }
        case 0x3c: // LDR <Bt>, [<Xn|SP>], #<simm>    ;    LDR <Bt>, [<Xn|SP>, #<simm>]!    ;    LDR <Qt>, [<Xn|SP>], #<simm>    ;     LDR <Qt>, [<Xn|SP>, #<simm>]!
        case 0x3d: // LDR <Bt>, [<Xn|SP>{, #<pimm>}]  ;    LDR <Qt>, [<Xn|SP>{, #<pimm>}]
        case 0x7c: // LDR <Ht>, [<Xn|SP>], #<simm>    ;    LDR <Ht>, [<Xn|SP>, #<simm>]!    
        case 0x7d: // LDR <Ht>, [<Xn|SP>{, #<pimm>}]
        case 0xbc: // LDR <Wt>, [<Xn|SP>], #<simm>    ;    LDR <Wt>, [<Xn|SP>, #<simm>]!
        case 0xbd: // LDR <Wt>, [<Xn|SP>{, #<pimm>}]                                    
        case 0xfc: // LDR <Dt>, [<Xn|SP>], #<simm>    ;    LDR <Dt>, [<Xn|SP>, #<simm>]!    ;    STR <Dt>, [<Xn|SP>], #<simm>    ;    STR <Dt>, [<Xn|SP>, #<simm>]!
        case 0xfd: // LDR <Dt>, [<Xn|SP>{, #<pimm>}]  ;    STR <Dt>, [<Xn|SP>{, #<pimm>}]   ;    STR <Dt>, [<Xn|SP>, (<Wm>|<Xm>){, <extend> {<amount>}}]
        {
            uint64_t bits11_10 = opbits( 10, 2 );
            uint64_t bit21 = opbits( 21, 1 );
            bool unsignedOffset = ( 0xd == ( hi8 & 0xf ) );
            bool preIndex = ( ( 0xc == ( hi8 & 0xf ) ) && ( 3 == bits11_10 ) );
            bool postIndex = ( ( 0xc == ( hi8 & 0xf ) ) && ( 1 == bits11_10 ) );
            bool signedUnscaledOffset = ( ( 0xc == ( hi8 & 0xf ) ) && ( 0 == bits11_10 ) );
            bool shiftExtend = ( ( 0xc == ( hi8 & 0xf ) ) && ( bit21 ) && ( 2 == bits11_10 ) );
            uint64_t imm12 = opbits( 10, 12 );
            int64_t imm9 = sign_extend( opbits( 12, 9 ), 8 );
            uint64_t size = opbits( 30, 2 );
            uint64_t opc = opbits( 22, 2 );
            bool is_ldr = opbits( 22, 1 );
            uint64_t t = opbits( 0, 5 );
            uint64_t n = opbits( 5, 5 );
            uint64_t byte_len = 1ull << size;

            if ( is_ldr )
            {
                if ( 3 == opc )
                    byte_len = 16;

                if ( preIndex )
                    tracer.Trace( "ldr %c%llu, [%s, #%lld]! //pr\n", get_byte_len( byte_len ), t, reg_or_sp( n, true ), imm9 );
                else if ( postIndex )
                    tracer.Trace( "ldr %c%llu, [%s] #%lld //po\n", get_byte_len( byte_len ), t, reg_or_sp( n, true ), imm9 );
                else if ( unsignedOffset )
                    tracer.Trace( "ldr %c%llu, [%s, #%llu] //uo\n", get_byte_len( byte_len ), t, reg_or_sp( n, true ), imm12 * byte_len );
                else if ( signedUnscaledOffset )
                    tracer.Trace( "ldur %c%llu, [%s, #%lld] //so\n", get_byte_len( byte_len ), t, reg_or_sp( n, true ), imm9 );
                else if ( shiftExtend )
                {
                    uint64_t option = opbits( 13, 3 );
                    uint64_t m = opbits( 16, 5 );
                    uint64_t shift = 0;
                    uint64_t S = opbits( 12, 1 );
                    if ( 0 != S )
                    {
                        if ( 0 == size )
                        {
                           if ( 2 == opc )
                               shift = 4;
                           else if ( 1 != opc )
                               unhandled();
                        }
                        else if ( 1 == size && 1 == opc )
                            shift = 1;
                        else if ( 2 == size && 1 == opc )
                            shift = 2;
                        else if ( 3 == size && 1 == opc )
                            shift = 3;
                        else
                            unhandled();
                    }

                    tracer.Trace( "ldr %c%llu, [%s, %s, %s, #%lld] //se\n", get_byte_len( byte_len ), t, reg_or_sp( n, true ), reg_or_zr( m, true ),
                                   extend_type( option ), shift );
                }
                else
                    unhandled();
            }
            else // str
            {
                if ( 2 == opc )
                    byte_len = 16;

                if ( preIndex )
                    tracer.Trace( "str %c%llu, [%s, #%lld]! //pr\n", get_byte_len( byte_len ), t, reg_or_sp( n, true ), imm9 );
                else if ( postIndex )
                    tracer.Trace( "str %c%llu, [%s] #%lld //po\n", get_byte_len( byte_len ), t, reg_or_sp( n, true ), imm9 );
                else if ( unsignedOffset )
                    tracer.Trace( "str %c%llu, [%s, #%llu] //uo\n", get_byte_len( byte_len ), t, reg_or_sp( n, true ), imm12 * byte_len );
                else if ( signedUnscaledOffset )
                    tracer.Trace( "stur %c%llu, [%s, #%lld] //so\n", get_byte_len( byte_len ), t, reg_or_sp( n, true ), imm9 );
                else if ( shiftExtend )
                {
                    uint64_t option = opbits( 13, 3 );
                    uint64_t m = opbits( 16, 5 );
                    uint64_t shift = 0;
                    uint64_t S = opbits( 12, 1 );
                    if ( 0 != S )
                    {
                        if ( 0 == size )
                        {
                           if ( 2 == opc )
                               shift = 4;
                           else if ( 0 != opc )
                               unhandled();
                        }
                        else if ( 1 == size && 0 == opc )
                            shift = 1;
                        else if ( 2 == size && 0 == opc )
                            shift = 2;
                        else if ( 3 == size && 0 == opc )
                            shift = 3;
                        else
                            unhandled();
                    }

                    tracer.Trace( "str %c%llu, [%s, %s, %s, #%lld] //se\n", get_byte_len( byte_len ), t, reg_or_sp( n, true ), reg_or_zr( m, true ),
                                   extend_type( option ), shift );
                }
                else
                    unhandled();
            }
            break;
        }
        case 0x2c: // STP <St1>, <St2>, [<Xn|SP>], #<imm>     ;    LDP <St1>, <St2>, [<Xn|SP>], #<imm>
        case 0x6c: // STP <Dt1>, <Dt2>, [<Xn|SP>], #<imm>     ;    LDP <Dt1>, <Dt2>, [<Xn|SP>], #<imm>
        case 0xac: // STP <Qt1>, <Qt2>, [<Xn|SP>], #<imm>          LDP <Qt1>, <Qt2>, [<Xn|SP>], #<imm>
        case 0x2d: // STP <St1>, <St2>, [<Xn|SP>, #<imm>]!    ;    STP <St1>, <St2>, [<Xn|SP>{, #<imm>}]    ;    LDP <St1>, <St2>, [<Xn|SP>, #<imm>]!    ;    LDP <St1>, <St2>, [<Xn|SP>{, #<imm>}]
        case 0x6d: // STP <Dt1>, <Dt2>, [<Xn|SP>, #<imm>]!    ;    STP <Dt1>, <Dt2>, [<Xn|SP>{, #<imm>}]    ;    LDP <Dt1>, <Dt2>, [<Xn|SP>, #<imm>]!    ;    LDP <Dt1>, <Dt2>, [<Xn|SP>{, #<imm>}]
        case 0xad: // STP <Qt1>, <Qt2>, [<Xn|SP>, #<imm>]!    ;    STP <Qt1>, <Qt2>, [<Xn|SP>{, #<imm>}]    ;    LDP <Qt1>, <Qt2>, [<Xn|SP>, #<imm>]!    ;    LDP <Qt1>, <Qt2>, [<Xn|SP>{, #<imm>}]
        {
            uint64_t opc = opbits( 30, 2 );
            char vector_width = ( 0 == opc ) ? 's' : ( 1 == opc ) ? 'd' : 'q';
            uint64_t imm7 = opbits( 15, 7 );
            uint64_t t2 = opbits( 10, 5 );
            uint64_t n = opbits( 5, 5 );
            uint64_t t1 = opbits( 0, 5 );
            uint64_t L = opbits( 22, 1 );
            uint64_t bit23 = opbits( 23, 1 );

            bool preIndex = ( ( 0xd == ( hi8 & 0xf ) ) && bit23 );
            bool postIndex = ( ( 0xc == ( hi8 & 0xf ) ) && bit23 );
            bool signedOffset = ( ( 0xd == ( hi8 & 0xf ) ) && !bit23 );

            uint64_t scale = 2 + opc;
            int64_t offset = sign_extend( imm7, 6 ) << scale;

            if ( L )
            {
                if ( postIndex )
                   tracer.Trace( "ldp %c%llu, %c%llu, [%s], #%lld //po\n", vector_width, t1, vector_width, t2, reg_or_sp( n, true ), offset );
                else if ( preIndex )
                   tracer.Trace( "ldp %c%llu, %c%llu, [%s, #%lld]! //pr\n", vector_width, t1, vector_width, t2, reg_or_sp( n, true ), offset );
                else if ( signedOffset )
                   tracer.Trace( "ldp %c%llu, %c%llu, [%s, #%lld] //so\n", vector_width, t1, vector_width, t2, reg_or_sp( n, true ), offset );
                else
                    unhandled();
            }
            else
            {
                if ( postIndex )
                   tracer.Trace( "stp %c%llu, %c%llu, [%s], #%lld //po\n", vector_width, t1, vector_width, t2, reg_or_sp( n, true ), offset );
                else if ( preIndex )
                   tracer.Trace( "stp %c%llu, %c%llu, [%s, #%lld]! //pr\n", vector_width, t1, vector_width, t2, reg_or_sp( n, true ), offset );
                else if ( signedOffset )
                   tracer.Trace( "stp %c%llu, %c%llu, [%s, #%lld] //so\n", vector_width, t1, vector_width, t2, reg_or_sp( n, true ), offset );
                else
                    unhandled();
            }
            break;
        }
        case 0x0f: case 0x2f: case 0x4f: case 0x6f: case 0x7f:
            // BIC <Vd>.<T>, #<imm8>{, LSL #<amount>}    ;    MOVI <Vd>.<T>, #<imm8>{, LSL #0}    ;    MVNI <Vd>.<T>, #<imm8>, MSL #<amount>
            // USHR <Vd>.<T>, <Vn>.<T>, #<shift>         ;    FMUL <Vd>.<T>, <Vn>.<T>, <Vm>.<Ts>[<index>]
            // FMOV <Vd>.<T>, #<imm>                     ;    FMOV <Vd>.<T>, #<imm>               ;    FMOV <Vd>.2D, #<imm>
            // USHLL{2} <Vd>.<Ta>, <Vn>.<Tb>, #<shift>   ;    SHRN{2} <Vd>.<Tb>, <Vn>.<Ta>, #<shift>  ;   SSHLL{2} <Vd>.<Ta>, <Vn>.<Tb>, #<shift>
            // FMLA <Vd>.<T>, <Vn>.<T>, <Vm>.<Ts>[<index>] ; SSHR <Vd>.<T>, <Vn>.<T>, #<shift>
        {
            uint64_t cmode = opbits( 12, 4 );
            uint64_t abc = opbits( 16, 3 );
            uint64_t defgh = opbits( 5, 5 );
            uint64_t val = ( abc << 5 ) | defgh;
            uint64_t Q = opbits( 30, 1 );
            uint64_t bit29 = opbits( 29, 1 );
            uint64_t o2 = opbits( 11,1 );
            uint64_t bit10 = opbits( 10, 1 );
            uint64_t bit11 = opbits( 11, 1 );
            uint64_t bit12 = opbits( 12, 1 );
            uint64_t bit23 = opbits( 23, 1 );
            uint64_t d = opbits( 0, 5 );
            uint64_t bits23_19 = opbits( 19, 5 );
            uint64_t imm = adv_simd_expand_imm( bit29, cmode, val );
            //tracer.Trace( "bit12: %llu, cmode %llx, bit29 %llu, bit11 %llu, bit10 %llu\n", bit12, cmode, bit29, bit11, bit10 );

            if ( 0 == bits23_19 )
            {
                if ( ( 0x2f == hi8 || 0x6f == hi8 ) && !bit11 && bit10 &&
                     ( ( 8 == ( cmode & 0xd ) ) || ( 0 == ( cmode & 9 ) ) || ( 0xc == ( cmode & 0xf ) ) ) ) // mvni
                {
                    if ( 8 == ( cmode & 0xd ) ) // 16-bit shifted immediate
                    {
                        uint64_t amount = ( cmode & 2 ) ? 8 : 0;
                        const char * pT = Q ? "8H" : "4H";
                        tracer.Trace( "mvni v%llu.%s, #%#llx, lsl #%llu\n", d, pT, val, amount );
                    }
                    else if ( 0 == ( cmode & 9 ) ) // 32-bit shifted immediate
                    {
                        uint64_t amount = get_bits( cmode, 1, 2 ) * 8;
                        const char * pT = Q ? "4S" : "2S";
                        tracer.Trace( "mvni v%llu.%s, #%#llx, lsl #%llu\n", d, pT, val, amount );
                    }
                    else if ( 0xc == ( cmode & 0xf ) ) // 32-bit shifting ones
                    {
                        imm = adv_simd_expand_imm( 1, cmode, val );
                        uint64_t amount = get_bit( cmode, 0 ) ? 16 : 8;
                        const char * pT = Q ? "4S" : "2S";
                        tracer.Trace( "mvni v%llu.%s, #%#llx, msl #%llu\n", d, pT, val, amount );
                    }
                    else
                        unhandled();
                }
                else if ( 0 == bit12 || ( 0xc == ( cmode & 0xe ) ) ) // movi
                {
                    if ( !bit29) // movi
                    {
                        if ( 0xe == cmode )
                        {
                            const char * pT = Q ? "16B" : "8B";
                            tracer.Trace( "movi v%llu.%s, #%#llx // imm %llx\n", d, pT, val, imm );
                        }
                        else if ( 8 == ( cmode & 0xd ) )
                        {
                            const char * pT = Q ? "8H" : "4H";
                            uint64_t amount = ( cmode & 2 ) ? 8 : 0;
                            tracer.Trace( "movi v%llu.%s, #%#llx, lsl #%llu\n", d, pT, imm, amount );
                        }
                        else if ( 0 == ( cmode & 9 ) )
                        {
                            const char * pT = Q ? "4S" : "2S";
                            uint64_t amount = ( 8 * ( ( cmode >> 1 ) & 3 ) );
                            tracer.Trace( "movi v%llu.%s, #%#llx, lsl #%llu\n", d, pT, imm, amount );
                        }
                        else if ( 0xa == ( cmode & 0xe ) )
                        {
                            const char * pT = Q ? "4S" : "2S";
                            uint64_t amount = ( cmode & 1 ) ? 16 : 8;
                            tracer.Trace( "movi v%llu.%s, #%#llx, msl #%llu\n", d, pT, imm, amount );
                        }
                        else
                            unhandled();
                    }
                    else // movi
                    {
                        uint64_t a = opbits( 18, 1 );
                        uint64_t b = opbits( 17, 1 );
                        uint64_t c = opbits( 16, 1 );
                        uint64_t bitd = opbits( 9, 1 );
                        uint64_t e = opbits( 8, 1 );
                        uint64_t f = opbits( 7, 1 );
                        uint64_t g = opbits( 6, 1 );
                        uint64_t h = opbits( 5, 1 );
    
                        imm = a ? ( 0xffull << 56 ) : 0;
                        imm |= b ? ( 0xffull << 48 ) : 0;
                        imm |= c ? ( 0xffull << 40 ) : 0;
                        imm |= bitd ? ( 0xffull << 32 ) : 0;
                        imm |= e ? ( 0xffull << 24 ) : 0;
                        imm |= f ? ( 0xffull << 16 ) : 0;
                        imm |= g ? ( 0xffull << 8 ) : 0;
                        imm |= h ? 0xffull : 0;
    
                        //tracer.Trace( "movi bit29 must be 1, Q %llu, cmode %llu\n", Q, cmode );
    
                        if ( ( 0 == Q ) && ( cmode == 0xe ) )
                            tracer.Trace( "movi D%llu, #%#llx\n", d, imm );
                        else if ( ( 1 == Q ) && ( cmode == 0xe ) )
                            tracer.Trace( "movi V%llu.2D, #%#llx\n", d, imm );
                        else
                            unhandled();
                    }
                }
                else if ( ( 0x6f == hi8 || 0x4f == hi8 || 0x2f == hi8 || 0x0f == hi8 ) && 0xf == cmode && !bit11 && bit10 ) // fmov single and double precision immediate
                {
                    double dval = 0.0;
                    tracer.Trace( "imm6: %#llx\n", imm );
                    if ( bit29 )
                        memcpy( &dval, &imm, sizeof( dval ) );
                    else
                    {
                        float float_val;
                        memcpy( &float_val, &imm, sizeof( float_val ) );
                        dval = (double) float_val;
                    }
                    tracer.Trace( "fmov v%llu.%s, #%lf\n", d, bit29 ? "2D" : Q ? "4S" : "2S", dval );
                }
                else if ( !bit29 ) // BIC register
                {
                    unhandled();
                }
                else if ( bit29 && bit12 ) // BIC immediate
                {
                    if ( 0 != o2 || 1 != bit10 )
                        unhandled();
        
                    bool sixteen_bit_mode = ( cmode == 0x9 || cmode == 0xb );
                    const char * pT = "";
                    uint64_t amount = 0;
                    if ( sixteen_bit_mode )
                    {
                        pT = Q ? "8H" : "4H";
                        amount = ( cmode & 2 ) ? 8 : 0;
                    }
                    else
                    {
                        pT = Q ? "4S" : "2S";
                        amount = 8 * ( ( cmode >> 1 ) & 3 );
                    }
        
                    tracer.Trace( "bic v%llu.%s, #%#llx, lsl #%llu\n", d, pT, val, amount );
                    //tracer.Trace( "bic bonus: cmode %#llx, val: %#llx, abc %#llx, defgh %#llx, sixteen_bit_mode %d, imm %#llx\n", cmode, val, abc, defgh, sixteen_bit_mode, imm );
                }
            }
            else // USHR, USHLL, SHRN, SHRN2, etc
            {
                uint64_t opcode = opbits( 12, 4 );

                if ( ( 0x0f == hi8 || 0x4f == hi8 ) && !bit23 && 0 == opcode && !bit11 && bit10 ) // SSHR <Vd>.<T>, <Vn>.<T>, #<shift>
                {
                    uint64_t n = opbits( 5, 5 );
                    uint64_t immh = opbits( 19, 4 );
                    uint64_t immb = opbits( 16, 3 );
                    uint64_t esize = 8ull << highest_set_bit_nz( immh );
                    uint64_t shift = ( esize * 2 ) - ( ( immh << 3 ) | immb );
                    const char * pT = get_sshr_vector_T( immh, Q );
                    tracer.Trace( "sshr v%llu.%s, v%llu.%s, #%llu\n", d, pT, n, pT, shift );
                }
                else if ( ( 0x4f == hi8 || 0x0f == hi8 ) && bit23 && 1 == opcode && !bit10 ) // FMLA <Vd>.<T>, <Vn>.<T>, <Vm>.<Ts>[<index>]
                {
                    uint64_t n = opbits( 5, 5 );
                    uint64_t m = opbits( 16, 5 );
                    uint64_t sz = opbits( 22, 1 );
                    uint64_t L = opbits( 21, 1 );
                    uint64_t H = opbits( 11, 1 );
                    uint64_t szL = ( sz << 1 ) | L;
                    uint64_t index = ( 0 == sz ) ? ( ( H << 1 ) | L ) : ( 2 == szL ) ? H : 0;
                    uint64_t Qsz = ( Q << 1 ) | sz;
                    const char * pT = ( 0 == Qsz ) ? "2s" : ( 2 == Qsz ) ? "4s" : ( 3 == Qsz ) ? "2d" : "?";
                    tracer.Trace( "fmla v%llu.%s, v%llu.%s, v%llu.%c[%llu]\n", d, pT, n, pT, m, sz ? 'd' : 's', index );
                }
                else if ( ( 0x0f == hi8 || 0x4f == hi8 ) && !bit23 && 0 != bits23_19 && 0xa == opcode && !bit11 && bit10 ) // SSHLL{2} <Vd>.<Ta>, <Vn>.<Tb>, #<shift>
                {
                    uint64_t n = opbits( 5, 5 );
                    uint64_t immh = opbits( 19, 4 );
                    uint64_t immb = opbits( 16, 3 );
                    uint64_t esize = 8ull << highest_set_bit_nz( immh & 0x7 );
                    uint64_t shift = ( ( immh << 3 ) | immb ) - esize;
                    const char * pTA = ( 1 == immh ) ? "8H" : ( 2 == ( 0xe & immh ) ) ? "4S" : "2D";
                    uint64_t sizeb = immh >> 1;
                    if ( 4 & sizeb )
                        sizeb = 4;
                    else if ( 2 & sizeb )
                        sizeb = 2;
                    const char * pTB = get_ld1_vector_T( sizeb, Q );
                    tracer.Trace( "sshll%s v%llu.%s, v%llu.%s, #%llu\n", Q ? "2" : "", d, pTA, n, pTB, shift );
                }
                else if ( ( 0x0f == hi8 || 0x4f == hi8 ) && !bit23 && 0 != bits23_19 && 8 == opcode && !bit11 && bit10 ) // SHRN{2} <Vd>.<Tb>, <Vn>.<Ta>, #<shift>
                {
                    uint64_t n = opbits( 5, 5 );
                    uint64_t immh = opbits( 19, 4 );
                    uint64_t immb = opbits( 16, 3 );
                    uint64_t esize = 8ull << highest_set_bit_nz( immh & 0x7 );
                    uint64_t shift = ( 2 * esize ) - ( ( immh << 3 ) | immb );
                    const char * pTA = ( 1 == immh ) ? "8H" : ( 2 == ( 0xe & immh ) ) ? "4S" : "2D";
                    uint64_t sizeb = immh >> 1;
                    if ( 4 & sizeb )
                        sizeb = 4;
                    else if ( 2 & sizeb )
                        sizeb = 2;
                    const char * pTB = get_ld1_vector_T( sizeb, Q );
                    tracer.Trace( "shrn%s v%llu.%s, v%llu.%s, #%llu\n", Q ? "2" : "", d, pTB, n, pTA, shift );
                }
                else if ( ( 0x2f == hi8 || 0x6f == hi8 ) && !bit23 && 0 != bits23_19 && ( 0xa == opcode ) && !bit11 && bit10 ) // USHLL{2} <Vd>.<Ta>, <Vn>.<Tb>, #<shift>
                {
                    uint64_t n = opbits( 5, 5 );
                    uint64_t immh = opbits( 19, 4 );
                    uint64_t immb = opbits( 16, 3 );
                    uint64_t esize = 8ull << highest_set_bit_nz( immh & 0x7 );
                    if ( 0x7f == hi8 )
                        esize = 8 << 3;
                    uint64_t shift = ( ( immh << 3 ) | immb ) - esize;
                    const char * pTA = ( 1 == immh ) ? "8H" : ( 2 == ( 0xe & immh ) ) ? "4S" : "2D";
                    uint64_t sizeb = immh >> 1;
                    if ( 2 & sizeb )
                        sizeb = 2;
                    const char * pTB = get_ld1_vector_T( sizeb, Q );
                    tracer.Trace( "ushll%s v%llu.%s, v%llu.%s, #%llu\n", Q ? "2" : "", d, pTA, n, pTB, shift );
                }
                else if ( ( 0x2f == hi8 || 0x7f == hi8 || 0x6f == hi8 ) && !bit23 && 0 == opcode && !bit11 && bit10 ) // USHR
                {
                    uint64_t n = opbits( 5, 5 );
                    uint64_t immh = opbits( 19, 4 );
                    uint64_t immb = opbits( 16, 3 );
                    uint64_t esize = 8ull << highest_set_bit_nz( immh );
                    if ( 0x7f == hi8 )
                        esize = 8 << 3;
                    uint64_t shift = ( esize * 2 ) - ( ( immh << 3 ) | immb );
                    tracer.Trace( "immh %llx, Q %llx\n", immh, Q );
                    uint64_t p_type = 0;
                    if ( 8 & immh )
                        p_type = 3;
                    else if ( 4 & immh )
                        p_type = 2;
                    else if ( 2 & immh )
                        p_type = 1;
                    else if ( 1 & immh )
                        p_type = 0;
                    const char * pT = get_ld1_vector_T( p_type, Q );
                    if ( 0x7f == hi8 ) // USHR D<d>, D<n>, #<shift>
                        tracer.Trace( "ushr, d%llu, d%llu, #%llu\n", d, n, shift );
                    else // USHR <Vd>.<T>, <Vn>.<T>, #<shift>
                        tracer.Trace( "ushr, v%llu.%s, v%llu.%s, #%llu\n", d, pT, n, pT, shift );
                }
                else if ( bit23 && !bit10 && 9 == opcode ) // FMUL <Vd>.<T>, <Vn>.<T>, <Vm>.<Ts>[<index>]. Vector, single-precision and double-precision
                {
                    uint64_t n = opbits( 5, 5 );
                    uint64_t m = opbits( 16, 5 );
                    uint64_t sz = opbits( 22, 1 );
                    uint64_t L = opbits( 21, 1 );
                    uint64_t H = opbits( 11, 1 );
                    uint64_t index = ( !sz ) ? ( ( H << 1 ) | L ) : H;
                    const char * pT = ( Q && sz ) ? "2D" : ( !Q && !sz ) ? "2S" : ( Q && !sz ) ? "4S" : "?";
                    tracer.Trace( "fmul v%llu.%s, v%llu.%s, v%llu.%c[%llu]\n", d, pT, n, pT, m, sz ? 'D' : 'S', index );
                }
                else
                    unhandled();
            }
            break;
        }
        case 0x5a: // REV <Wd>, <Wn>    ;    CSINV <Wd>, <Wn>, <Wm>, <cond>    ;    RBIT <Wd>, <Wn>    ;    CLZ <Wd>, <Wn>    ;    CSNEG <Wd>, <Wn>, <Wm>, <cond>
        case 0xda: // REV <Xd>, <Xn>    ;    CSINV <Xd>, <Xn>, <Xm>, <cond>    ;    RBIT <Xd>, <Xn>    ;    CLZ <Xd>, <Xn>    ;    CSNEG <Xd>, <Xn>, <Xm>, <cond>
        {
            uint64_t xregs = ( 0 != ( 0x80 & hi8 ) );
            uint64_t bits23_21 = opbits( 21, 3 );
            uint64_t bits15_10 = opbits( 10, 6 );
            uint64_t bit11 = opbits( 11, 1 );
            uint64_t bit10 = opbits( 10, 1 );
            uint64_t n = opbits( 5, 5 );
            uint64_t d = opbits( 0, 5 );

            if ( 4 == bits23_21 ) // csinv + csneg
            {
                if ( bit11 )
                    unhandled();
                uint64_t m = opbits( 16, 5 );
                uint64_t cond = opbits( 12, 4 );
                tracer.Trace( "%s %s, %s, %s, %s\n", bit10 ? "csneg" : "csinv", reg_or_zr( d, xregs ), reg_or_zr2( n, xregs ),  reg_or_zr3( m, xregs ), get_cond( cond ) );
            }
            else if ( 6 == bits23_21 )
            {
                if ( 0 == bits15_10 ) // rbit
                    tracer.Trace( "rbit %s, %s\n", reg_or_zr( d, xregs ), reg_or_zr2( n, xregs ) );
                else if ( 2 == bits15_10 || 3 == bits15_10 ) // rev
                    tracer.Trace( "rev %s, %s\n", reg_or_zr( d, xregs ), reg_or_zr2( n, xregs ) );
                else if ( 4 == bits15_10 ) // clz
                    tracer.Trace( "clz %s, %s\n", reg_or_zr( d, xregs ), reg_or_zr2( n, xregs ) );
                else
                    unhandled();
            }
            else
                unhandled();
            break;
        }
        case 0x14: case 0x15: case 0x16: case 0x17: // b label
        {
            int64_t imm26 = opbits( 0, 26 );
            imm26 <<= 2;
            imm26 = sign_extend( imm26, 27 );
            tracer.Trace( "b %#llx\n", pc + imm26 );
            break;
        }
        case 0x54: // b.cond
        {
            uint64_t cond = opbits( 0, 4 );
            int64_t imm19 = opbits( 5, 19 );
            imm19 <<= 2;
            imm19 = sign_extend( imm19, 20 );
            tracer.Trace( "b.%s %#llx\n", get_cond( cond ), pc + imm19 );
            break;
        }
        case 0x18: // ldr wt, (literal)
        case 0x58: // ldr xt, (literal)
        {
            uint64_t imm19 = opbits( 5, 19 );
            uint64_t t = opbits( 0, 5 );
            bool xregs = ( 0 != opbits( 30, 1 ) );
            tracer.Trace( "ldr %s, =%#llx\n", reg_or_zr( t, xregs ), pc + ( imm19 << 2 ) );
            break;
        }
        case 0x3a: // CCMN <Wn>, #<imm>, #<nzcv>, <cond>  ;    CCMN <Wn>, <Wm>, #<nzcv>, <cond>       ;    ADCS <Wd>, <Wn>, <Wm>
        case 0xba: // CCMN <Wn>, <Wm>, #<nzcv>, <cond>    ;    CCMN <Xn>, <Xm>, #<nzcv>, <cond>       ;    ADCS <Xd>, <Xn>, <Xm>
        case 0x7a: // CCMP <Wn>, <Wm>, #<nzcv>, <cond>    ;    CCMP <Wn>, #<imm>, #<nzcv>, <cond>
        case 0xfa: // CCMP <Xn>, <Xm>, #<nzcv>, <cond>    ;    CCMP <Xn>, #<imm>, #<nzcv>, <cond>
        {
            uint64_t bits23_21 = opbits( 21, 3 );
            uint64_t n = opbits( 5, 5 );
            bool xregs = ( 0 != ( 0x80 & hi8 ) );

            if ( 2 == bits23_21 )
            {
                uint64_t o3 = opbits( 4, 1 );
                if ( 0 != o3 )
                    unhandled();

                bool is_ccmn = ( 0 == ( hi8 & 0x40 ) );
                uint64_t cond = opbits( 12, 4 );
                uint64_t nzcv = opbits( 0, 4 );
                char width = xregs ? 'w' : 'x';
                uint64_t o2 = opbits( 10, 2 );
                if ( 0 == o2 ) // register
                {
                    uint64_t m = opbits( 16, 5 );
                    tracer.Trace( "%s %c%llu, %c%llu, #%llu, %s\n", is_ccmn ? "ccmn" : "ccmp", width, n, width, m, nzcv, get_cond( cond ) );
                }
                else if ( 2 == o2 ) // immediate
                {
                    uint64_t imm5 = ( ( op >> 16 ) & 0x1f ); // unsigned
                    tracer.Trace( "%s %c%llu, #%llx, #%llu, %s\n", is_ccmn ? "ccmn" : "ccmp", width, n, imm5, nzcv, get_cond( cond ) );
                }
                else
                    unhandled();
            }
            else if ( ( 0x3a == hi8 || 0xba == hi8 ) && 0 == bits23_21 ) // ADCS
            {
                uint64_t d = opbits( 0, 5 );
                uint64_t m = opbits( 16, 5 );
                tracer.Trace( "adcs %s, %s, %s\n", reg_or_zr( d, xregs ), reg_or_zr2( n, xregs ), reg_or_zr3( m, xregs ) );
            }
            else
                unhandled();
            break;
        }
        case 0x31: // ADDS <Wd>, <Wn|WSP>, #<imm>{, <shift>}  ;    CMN <Wn|WSP>, #<imm>{, <shift>}
        case 0xb1: // ADDS <Xd>, <Xn|SP>, #<imm>{, <shift>}   ;    CMN <Xn|SP>, #<imm>{, <shift>}
        {
            uint64_t xregs = ( 0 != ( 0x80 & hi8 ) );
            bool shift12 = opbits( 22, 1 );
            uint64_t imm12 = opbits( 10, 12 );
            uint64_t n = opbits( 5, 5 );
            uint64_t d = opbits( 0, 5 );

            if ( 31 == d )
                tracer.Trace( "cmn %s, #%#llx, lsl #%#u\n", reg_or_sp( n, xregs ), imm12, ( shift12 ? 12 : 0 ) );
            else
                tracer.Trace( "adds %s, %s, #%#llx, lsl #%#u\n", reg_or_zr( d, xregs ), reg_or_sp( n, xregs ), imm12, ( shift12 ? 12 : 0 ) );
            break;
        }
        case 0x0b: // ADD <Wd|WSP>, <Wn|WSP>, <Wm>{, <extend> {#<amount>}}      ;    ADD <Wd>, <Wn>, <Wm>{, <shift> #<amount>}
        case 0x2b: // ADDS <Wd>, <Wn|WSP>, <Wm>{, <extend> {#<amount>}}         ;    ADDS <Wd>, <Wn>, <Wm>{, <shift> #<amount>}
        case 0x4b: // SUB <Wd|WSP>, <Wn|WSP>, <Wm>{, <extend> {#<amount>}}      ;    SUB <Wd>, <Wn>, <Wm>{, <shift> #<amount>} 
        case 0x6b: // SUBS <Wd>, <Wn|WSP>, <Wm>{, <extend> {#<amount>}}         ;    SUBS <Wd>, <Wn>, <Wm>{, <shift> #<amount>}
        case 0x8b: // ADD <Xd|SP>, <Xn|SP>, <R><m>{, <extend> {#<amount>}}      ;    ADD <Xd>, <Xn>, <Xm>{, <shift> #<amount>}
        case 0xab: // ADDS <Xd>, <Xn|SP>, <R><m>{, <extend> {#<amount>}}        ;    ADDS <Xd>, <Xn>, <Xm>{, <shift> #<amount>}
        case 0xcb: // SUB <Xd|SP>, <Xn|SP>, <R><m>{, <extend> {#<amount>}}      ;    SUB <Xd>, <Xn>, <Xm>{, <shift> #<amount>}
        case 0xeb: // SUBS <Xd>, <Xn|SP>, <R><m>{, <extend> {#<amount>}}        ;    SUBS <Xd>, <Xn>, <Xm>{, <shift> #<amount>}
        {
            uint64_t extended = opbits( 21, 1 );
            uint64_t issub = ( 0 != ( 0x40 & hi8 ) );
            const char * opname = issub ? "sub" : "add";
            uint64_t setflags = ( 0 != ( 0x20 & hi8 ) );
            uint64_t xregs = ( 0 != ( 0x80 & hi8 ) );
            uint64_t m = opbits( 16, 5 );
            uint64_t n = opbits( 5, 5 );
            uint64_t d = opbits( 0, 5 );

            if ( 1 == extended ) // ADD <Xd|SP>, <Xn|SP>, <R><m>{, <extend> {#<amount>}}
            {
                uint64_t option = opbits( 13, 3 );
                uint64_t imm3 = opbits( 10, 3 );
                tracer.Trace( "%s%s, %s, %s, %s, %s #%llu\n", opname, setflags ? "s" : "",
                              setflags ? reg_or_zr( d, xregs ) : reg_or_sp( d, xregs ),
                              reg_or_sp2( n, xregs ), reg_or_zr2( m, xregs ),
                              extend_type( option ), imm3 );
            }
            else // ADD <Xd>, <Xn>, <Xm>{, <shift> #<amount>}
            {
                uint64_t shift = opbits( 22, 2 );
                uint64_t imm6 = opbits( 10, 6 );
                if ( issub && ( 31 == d ) )
                    tracer.Trace( "cmp %s, %s { %s #%llu }\n",
                                  reg_or_zr( n, xregs ), reg_or_zr2( m, xregs ),
                                  shift_type( shift ), imm6 );
                else
                    tracer.Trace( "%s%s %s, %s, %s { %s #%llu }\n", opname, setflags ? "s" : "",
                                  reg_or_zr( d, xregs ), reg_or_zr2( n, xregs ), reg_or_zr3( m, xregs ),
                                  shift_type( shift ), imm6 );
            }
            break;
        }
        case 0x11: // add <wd|SP>, <wn|SP>, #imm [,<shift>]
        case 0x51: // sub <wd|SP>, <wn|SP>, #imm [,<shift>]
        case 0x91: // add <xd|SP>, <xn|SP>, #imm [,<shift>]
        case 0xd1: // sub <xd|SP>, <xn|SP>, #imm [,<shift>]
        {
            bool sf = ( 0 != opbits( 31, 1 ) );
            bool sh = ( 0 != opbits( 22, 1 ) );
            uint64_t imm12 = opbits( 10, 12 );
            uint64_t n = opbits( 5, 5 );
            uint64_t d = opbits( 0, 5 );
            tracer.Trace( "%s %s, %s, #%#llx, lsl #%llu\n", ( 0x91 == hi8 || 0x11 == hi8 ) ? "add" : "sub",
                          reg_or_sp( d, sf ), reg_or_sp2( n, sf ), imm12, (uint64_t) ( sh ? 12 : 0 ) );
            break;
        }
        case 0xd5: // MSR / MRS
        {
            uint64_t bits2322 = opbits( 22, 2 );
            if ( 0 != bits2322 )
                unhandled();

            if ( 0xd503201f == op ) // nop
            {
                tracer.Trace( "nop\n" );
                break;
            }

            uint64_t upper20 = opbits( 12, 20 );
            uint64_t lower8 = opbits( 0, 8 );
            if ( ( 0xd5033 == upper20 ) && ( 0xbf == lower8 ) ) // dmb -- no memory barries are needed due to just one thread and core
            {
                tracer.Trace( "dmb\n" );
                break;
            }
    
            uint64_t l = opbits( 21, 1 );
            uint64_t op0 = opbits( 19, 2 );
            uint64_t op1 = opbits( 16, 3 );
            uint64_t op2 = opbits( 5, 3 );
            uint64_t n = opbits( 12, 4 );
            uint64_t m = opbits( 8, 4 );
            uint64_t t = opbits( 0, 5 );

            if ( l ) // MRS <Xt>, (<systemreg>|S<op0>_<op1>_<Cn>_<Cm>_<op2>).   read system register
            {
                if ( ( 3 == op0 ) && ( 14 == n ) && ( 3 == op1 ) && ( 0 == m ) && ( 2 == op2 ) ) // cntvct_el0 counter-timer virtual count register
                    tracer.Trace( "mrs x%llu, cntvct_el0\n", t );
                else if ( ( 3 == op0 ) && ( 14 == n ) && ( 3 == op1 ) && ( 0 == m ) && ( 0 == op2 ) ) // cntfrq_el0 counter-timer frequency register
                    tracer.Trace( "mrs x%llu, cntfrq_el0\n", t );
                else if ( ( 3 == op0 ) && ( 0 == n ) && ( 3 == op1 ) && ( 0 == m ) && ( 7 == op2 ) )
                    tracer.Trace( "mrs x%llu, dczid_elo\n", t );
                else if ( ( 3 == op0 ) && ( 0 == n ) && ( 0 == op1 ) && ( 0 == m ) && ( 0 == op2 ) ) // mrs x, midr_el1
                    tracer.Trace( "mrs x%llu, midr_el1\n", t );
                else if ( ( 3 == op0 ) && ( 13 == n ) && ( 3 == op1 ) && ( 0 == m ) && ( 2 == op2 ) )
                    tracer.Trace( "mrs x%llu, tpidr_el0\n", t );
                else if ( ( 3 == op0 ) && ( 4 == n ) && ( 3 == op1 ) && ( 4 == m ) && ( 0 == op2 ) ) // mrs x, fpcr
                    tracer.Trace( "mrs x%llu, fpcr\n", t );
                else
                {
                    tracer.Trace( "MRS unhandled: t %llu op0 %llu n %llu op1 %llu m %llu op2 %llu\n", t, op0, n, op1, m, op2 );
                    unhandled();
                }
            }
            else // MSR.   write system register
            {
                if ( ( 3 == op0 ) && ( 13 == n ) && ( 3 == op1 ) && ( 0 == m ) && ( 2 == op2 ) )
                    tracer.Trace( "msr tpidr_el0, x%llu\n", t );
                else if ( ( 0 == op0 ) && ( 2 == n ) && ( 3 == op1 ) && ( 4 == m ) && ( 2 == op2 ) )
                    tracer.Trace( "bti\n" ); // branch target identification (ignore );
                else if ( ( 1 == op0 ) && ( 7 == n ) && ( 3 == op1 ) && ( 4 == m ) && ( 1 == op2 ) )
                    tracer.Trace( "dc zva, %s\n", reg_or_zr( t, true ) ); // data cache operation
                else if ( ( 0 == op0 ) && ( 2 == n ) && ( 3 == op1 ) && ( 0 == m ) && ( 7 == op2 ) ) // xpaclri
                    tracer.Trace( "xpaclri\n" );
                else if ( ( 3 == op0 ) && ( 4 == n ) && ( 3 == op1 ) && ( 4 == m ) && ( 0 == op2 ) ) // msr fpcr, xt
                    tracer.Trace( "msr fpcr, x%llu\n", t );
                else
                {
                    tracer.Trace( "MSR unhandled: t %llu op0 %llu n %llu op1 %llu m %llu op2 %llu\n", t, op0, n, op1, m, op2 );
                    unhandled();
                }
            }
            break;
        }
        case 0x1b: // MADD <Wd>, <Wn>, <Wm>, <Wa>    ;    MSUB <Wd>, <Wn>, <Wm>, <Wa>
        case 0x9b: // MADD <Xd>, <Xn>, <Xm>, <Xa>    ;    MSUB <Xd>, <Xn>, <Xm>, <Xa>    ;    UMULH <Xd>, <Xn>, <Xm>    ;    UMADDL <Xd>, <Wn>, <Wm>, <Xa>
                   // SMADDL <Xd>, <Wn>, <Wm>, <Xa>  ;    SMULH <Xd>, <Xn>, <Xm>
        {
            bool xregs = ( 0 != opbits( 31, 1 ) );
            uint64_t m = opbits( 16, 5 );
            uint64_t a = opbits( 10, 5 );
            uint64_t n = opbits( 5, 5 );
            uint64_t d = opbits( 0, 5 );
            uint64_t bits23_21 = opbits( 21, 3 );
            bool bit15 = ( 1 == opbits( 15, 1 ) );

            if ( 1 == bits23_21 && bit15 ) // smsubl
                tracer.Trace( "mmsubl %s, %s, %s, %s\n", reg_or_zr( d, xregs ), reg_or_zr2( n, xregs ), reg_or_zr3( m, xregs ), reg_or_zr4( a, xregs ) );
            else if ( 5 == bits23_21 && !bit15 )
                tracer.Trace( "umaddl %s, %s, %s\n", reg_or_zr( d, true ), reg_or_zr2( n, true ), reg_or_zr3( m, true ) );
            else if ( 1 == bits23_21 && !bit15 )
                tracer.Trace( "smaddl %s, %s, %s, %s\n", reg_or_zr( d, xregs ), reg_or_zr2( n, false ), reg_or_zr3( m, false ), reg_or_zr4( a, xregs ) );
            else if ( 0 == bits23_21 && !bit15 )
                tracer.Trace( "madd %s, %s, %s, %s\n", reg_or_zr( d, xregs ), reg_or_zr2( n, xregs ), reg_or_zr3( m, xregs ), reg_or_zr4( a, xregs ) );
            else if ( 0 == bits23_21 && bit15 )
                tracer.Trace( "msub %s, %s, %s, %s\n", reg_or_zr( d, xregs ), reg_or_zr2( n, xregs ), reg_or_zr3( m, xregs ), reg_or_zr4( a, xregs ) );
            else if ( 6 == bits23_21 && !bit15 && 31 == a )
                tracer.Trace( "umulh %s, %s, %s\n", reg_or_zr( d, true ), reg_or_zr2( n, true ), reg_or_zr2( m, true ) );
            else if ( 2 == bits23_21 && !bit15 && 31 == a )
                tracer.Trace( "smulh %s, %s, %s\n", reg_or_zr( d, true ), reg_or_zr2( n, true ), reg_or_zr2( m, true ) );
            else
                unhandled();
            break;
        }
        case 0x71: // SUBS <Wd>, <Wn|WSP>, #<imm>{, <shift>}   ;   CMP <Wn|WSP>, #<imm>{, <shift>}
        case 0xf1: // SUBS <Xd>, <Xn|SP>, #<imm>{, <shift>}    ;   cmp <xn|SP>, #imm [,<shift>]    ;     
        {
            bool sf = ( 0 != opbits( 31, 1 ) );
            bool sh = ( 0 != opbits( 22, 1 ) );
            uint64_t imm12 = opbits( 10, 12 );
            uint64_t n = opbits( 5, 5 );
            uint64_t d = opbits( 0, 5 );

            if ( 31 == d )
                tracer.Trace( "cmp %s, #%#llx, LSL #%u\n", reg_or_zr( n, sf ), imm12, sh ? 12 : 0 );
            else
                tracer.Trace( "subs %s, %s, #%#llx, LSL #%u\n", reg_or_zr( d, sf ), reg_or_sp( n, sf ), imm12, sh ? 12 : 0 );
            break;
        }
        case 0x94: case 0x95: case 0x96: case 0x97: // bl offset. The lower 2 bits of this are the high part of the offset
        {
            int64_t offset = ( opbits( 0, 26 ) << 2 );
            offset = sign_extend( offset, 27 );
            tracer.Trace( "bl %#llx\n", pc + offset );
            break;
        }
        case 0x28: // ldp/stp 32 post index                   STP <Wt1>, <Wt2>, [<Xn|SP>], #<imm>     ;    LDP <Wt1>, <Wt2>, [<Xn|SP>], #<imm>
        case 0xa8: // ldp/stp 64 post-index                   STP <Xt1>, <Xt2>, [<Xn|SP>], #<imm>     ;    LDP <Xt1>, <Xt2>, [<Xn|SP>], #<imm>
        case 0x29: // ldp/stp 32 pre-index and signed offset: STP <Wt1>, <Wt2>, [<Xn|SP>, #<imm>]!    ;    STP <Wt1>, <Wt2>, [<Xn|SP>{, #<imm>}]
                   //                                         LDP <Wt1>, <Wt2>, [<Xn|SP>, #<imm>]!    ;    LDP <Wt1>, <Wt2>, [<Xn|SP>{, #<imm>}]
        case 0xa9: // ldp/stp 64 pre-index and signed offset: STP <Xt1>, <Xt2>, [<Xn|SP>, #<imm>]!    ;    STP <Xt1>, <Xt2>, [<Xn|SP>{, #<imm>}]
                   //                                         LDP <Xt1>, <Xt2>, [<Xn|SP>, #<imm>]!    ;    LDP <Xt1>, <Xt2>, [<Xn|SP>{, #<imm>}]
        case 0x68: // ldp 32-bit sign extended                LDPSW <Xt1>, <Xt2>, [<Xn|SP>], #<imm>
        case 0x69: // ldp 32-bit sign extended                LDPSW <Xt1>, <Xt2>, [<Xn|SP>, #<imm>]!  ;    LDPSW <Xt1>, <Xt2>, [<Xn|SP>{, #<imm>}]
        {
            bool xregs = ( 0 != opbits( 31, 1 ) );
            uint64_t t1 = opbits( 0, 5 );
            uint64_t t2 = opbits( 10, 5 );
            uint64_t n = opbits( 5, 5 );
            int64_t imm7 = sign_extend( opbits( 15, 7 ), 6 ) << ( xregs ? 3 : 2 );
            uint64_t variant = opbits( 23, 2 );
            if ( 0 == variant )
                unhandled();

            bool postIndex = ( 1 == variant );
            bool preIndex = ( 3 == variant );
            bool signedOffset = ( 2 == variant );

            if ( 0 == opbits( 22, 1 ) ) // bit 22 is 0 for stp
            {
                if ( 0x68 == hi8 || 0x69 == hi8 ) // these are ldpsw variants
                    unhandled();

                if ( signedOffset )
                    tracer.Trace( "stp %s, %s, [%s, #%lld] //so\n", reg_or_zr( t1, xregs ), reg_or_zr2( t2, xregs ), reg_or_sp( n, true ), imm7 );
                else if ( preIndex )
                    tracer.Trace( "stp %s, %s, [%s, #%lld]! //pr\n", reg_or_zr( t1, xregs ), reg_or_zr2( t2, xregs ), reg_or_sp( n, true ), imm7 );
                else if ( postIndex )
                    tracer.Trace( "stp %s, %s, [%s] #%lld //po\n", reg_or_zr( t1, xregs ), reg_or_zr2( t2, xregs ), reg_or_sp( n, true ), imm7 );
                else
                    unhandled();
            }
            else // 1 means ldp
            {
                bool se = ( 0 != ( hi8 & 0x40 ) );
                if ( signedOffset )
                    tracer.Trace( "ldp%s %s, %s, [%s, #%lld] //so\n", se ? "sw" : "", reg_or_zr( t1, xregs ), reg_or_zr2( t2, xregs ), reg_or_sp( n, true ), imm7 );
                else if ( preIndex )
                    tracer.Trace( "ldp%s %s, %s, [%s, #%lld]! //pr\n", se ? "sw" : "", reg_or_zr( t1, xregs ), reg_or_zr2( t2, xregs ), reg_or_sp( n, true ), imm7 );
                else if ( postIndex )
                    tracer.Trace( "ldp%s %s, %s, [%s] #%lld //po\n", se ? "sw" : "", reg_or_zr( t1, xregs ), reg_or_zr2( t2, xregs ), reg_or_sp( n, true ), imm7 );
                else
                    unhandled();
            }
            break;
        }
        case 0x4a: // EOR <Wd>, <Wn>, <Wm>{, <shift> #<amount>}    ;    EON <Wd>, <Wn>, <Wm>{, <shift> #<amount>}
        case 0xca: // EOR <Xd>, <Xn>, <Xm>{, <shift> #<amount>}    ;    EON <Xd>, <Xn>, <Xm>{, <shift> #<amount>}
        case 0x2a: // ORR <Wd>, <Wn>, <Wm>{, <shift> #<amount>}    ;    ORN <Wd>, <Wn>, <Wm>{, <shift> #<amount>}
        case 0xaa: // ORR <Xd>, <Xn>, <Xm>{, <shift> #<amount>}    ;    ORN <Xd>, <Xn>, <Xm>{, <shift> #<amount>}
        {
            uint64_t shift = opbits( 22, 2 );
            uint64_t N = opbits( 21, 1 );
            uint64_t m = opbits( 16, 5 );
            uint64_t n = opbits( 5, 5 );
            uint64_t d = opbits( 0, 5 );
            uint64_t imm6 = opbits( 10, 6 );
            uint64_t xregs = ( 0 != ( 0x80 & hi8 ) );
            if ( !xregs && ( 0 != ( imm6 & 0x20 ) ) ) // can't shift with 6 bits for 32-bit values
                unhandled();
            bool eor = ( 2 == opbits( 29, 2 ) ); // or eon

            if ( ( 0 == imm6 ) && ( 31 == n ) && ( 0 == shift ) && ( 0 == N ) )
                tracer.Trace( "mov %s, %s\n", reg_or_zr( d, xregs ), reg_or_zr2( m, xregs ) );
            else if ( ( 0 == shift ) && ( 0 == imm6 ) )
                tracer.Trace( "%s %s, %s, %s\n", eor ? ( N ? "eon" : "eor" ) : ( !N ) ? "orr" : "orn", reg_or_zr( d, xregs ), reg_or_zr2( n, xregs ), reg_or_zr3( m, xregs ) );
            else
                tracer.Trace( "%s %s, %s, %s, %s #%llu\n", eor ? ( N ? "eon" : "eor" ) : ( !N ) ? "orr" : "orn", reg_or_zr( d, xregs ), reg_or_zr2( n, xregs ), reg_or_zr3( m, xregs ), shift_type( shift ), imm6 );
            break;
        }
        case 0x32: // ORR <Wd|WSP>, <Wn>, #<imm>
        case 0xb2: // ORR <Xd|SP>, <Xn>, #<imm>
        {
            uint64_t xregs = ( 0 != ( 0x80 & hi8 ) );
            uint64_t N_immr_imms = opbits( 10, 13 );
            uint64_t op2 = decode_logical_immediate( N_immr_imms, xregs ? 64 : 32 );
            uint64_t n = opbits( 5, 5 );
            uint64_t d = opbits( 0, 5 );
            tracer.Trace( "orr %s, %s, #%#llx\n", reg_or_sp( d, xregs ), reg_or_zr( n, xregs ), op2 );
            break;
        }
        case 0x33: // BFM <Wd>, <Wn>, #<immr>, #<imms>
        case 0xb3: // BFM <Xd>, <Xn>, #<immr>, #<imms>
        case 0x13: // SBFM <Wd>, <Wn>, #<immr>, #<imms>    ;    EXTR <Wd>, <Wn>, <Wm>, #<lsb>
        case 0x93: // SBFM <Xd>, <Xn>, #<immr>, #<imms>    ;    EXTR <Xd>, <Xn>, <Xm>, #<lsb>
        case 0x53: // UBFM <Wd>, <Wn>, #<immr>, #<imms>
        case 0xd3: // UBFM <Xd>, <Xn>, #<immr>, #<imms>
        {
            uint64_t xregs = ( 0 != ( 0x80 & hi8 ) );
            uint64_t imms = opbits( 10, 6 );
            uint64_t n = opbits( 5, 5 );
            uint64_t d = opbits( 0, 5 );
            uint64_t bit23 = opbits( 23, 1 );
            if ( bit23 && ( 0x13 == ( 0x7f & hi8 ) ) )
            {
                uint64_t m = opbits( 16, 5 );
                tracer.Trace( "extr %s, %s, %s, #%llu\n", reg_or_zr( d, xregs ), reg_or_zr2( n, xregs ), reg_or_zr3( m, xregs ), imms );
            }
            else
            {
                uint64_t immr = opbits( 16, 6 );
                const char * ins = ( 0x13 == hi8 || 0x93 == hi8 ) ? "sbfm" : ( 0x33 == hi8 || 0xb3 == hi8) ? "bfm" : "ubfm";
                tracer.Trace( "%s %s, %s, #%llu, #%llu\n", ins, reg_or_zr( d, xregs ), reg_or_zr2( n, xregs ), immr, imms );
            }
            break;
        }
        case 0x0a: // AND <Wd>, <Wn>, <Wm>{, <shift> #<amount>}     ;    BIC <Wd>, <Wn>, <Wm>{, <shift> #<amount>}
        case 0x6a: // ANDS <Wd>, <Wn>, <Wm>{, <shift> #<amount>}    ;    BICS <Wd>, <Wn>, <Wm>{, <shift> #<amount>}
        case 0x8a: // AND <Xd>, <Xn>, <Xm>{, <shift> #<amount>}     ;    BIC <Xd>, <Xn>, <Xm>{, <shift> #<amount>}
        case 0xea: // ANDS <Xd>, <Xn>, <Xm>{, <shift> #<amount>}    ;    BICS <Xd>, <Xn>, <Xm>{, <shift> #<amount>}
        {
            uint64_t shift = opbits( 22, 2 );
            uint64_t N = opbits( 21, 1 );
            uint64_t m = opbits( 16, 5 );
            uint64_t imm6 = opbits( 10, 6 );
            uint64_t n = opbits( 5, 5 );
            uint64_t d = opbits( 0, 5 );
            bool set_flags = ( 0x60 == ( hi8 & 0x60 ) );
            bool xregs = ( 0 != ( hi8 & 0x80 ) );
            tracer.Trace( "%s%s %s, %s, %s, %s, #%llu\n", N ? "bic" : "and", set_flags ? "s" : "",
                          reg_or_zr( d, xregs ), reg_or_zr2( n, xregs ), reg_or_zr3( m, xregs ), shift_type( shift ), imm6 );
            break;
        }
        case 0x10: case 0x30: case 0x50: case 0x70: // ADR <Xd>, <label>
        {
            uint64_t d = opbits( 0, 5 );
            uint64_t immhi = opbits( 5, 19 );
            uint64_t immlo = opbits( 29, 2 );
            int64_t offset = sign_extend( immhi << 2 | immlo, 20 );
            tracer.Trace( "adr x%llu, %#llx\n", d, pc + offset );
            break;
        }
        case 0x90: case 0xb0: case 0xd0: case 0xf0: // adrp rd, immediate
        {
            uint64_t d = ( op & 0x1f );
            int64_t imm = ( ( op >> 3 ) & 0x1ffffc );  // 19 bits with bottom two bits 0 at the bottom
            imm |= ( ( op >> 29 ) & 3 );               // two low bits
            imm = sign_extend( imm, 20 );
            imm <<= 12;
            imm += ( pc & ( ~0xfff ) );
            tracer.Trace( "adrp x%llu, %#llx\n", d, imm );
            break;
        }
        case 0x36: // TBZ <R><t>, #<imm>, <label>
        case 0x37: // TBNZ <R><t>, #<imm>, <label>
        case 0xb6: // TBZ <R><t>, #<imm>, <label> where high bit is prepended to b40 bit selector for 6 bits total
        case 0xb7: // TBNZ <R><t>, #<imm>, <label> where high bit is prepended to b40 bit selector for 6 bits total
        {
            uint64_t b40 = opbits( 19, 5 );
            if ( 0 != ( 0x80 & hi8 ) )
                b40 |= 0x20;
            int64_t imm14 = (int64_t) sign_extend( ( opbits( 5, 14 ) << 2 ), 15 ) + pc;
            uint64_t t = opbits( 0, 5 );
            tracer.Trace( "tb%sz x%llu, #%llu, %#llx\n", ( hi8 & 1 ) ? "n" : "", t, b40, imm14 );
            break;
        }
        case 0x12: // MOVN <Wd>, #<imm>{, LSL #<shift>}   ;    AND <Wd|WSP>, <Wn>, #<imm>
        case 0x92: // MOVN <Xd>, #<imm16>, LSL #<shift>   ;    AND <Xd|SP>, <Xn>, #<imm>    ;    MOV <Xd>, #<imm>
        {
            uint64_t bit23 = opbits( 23, 1 );
            bool xregs = ( 0 != ( hi8 & 0x80 ) );
            if ( bit23 ) // MOVN
            {
                uint64_t d = opbits( 0, 5 );
                uint64_t imm16 = opbits( 5, 16 );
                uint64_t hw = opbits( 21, 2 );
                hw *= 16;
                imm16 <<= hw;
                imm16 = ~imm16;
                char width = 'x';
    
                if ( 0x12 == hi8 )
                {
                    if ( hw > 16 )
                        unhandled();
                    imm16 &= 0xffffffff;
                    width = 'w';
                }
    
                tracer.Trace( "movn %c%llu, %lld\n", width, d, imm16 );
            }
            else // AND
            {
                uint64_t N_immr_imms = opbits( 10, 13 );
                uint64_t n = opbits( 5, 5 );
                uint64_t d = opbits( 0, 5 );
                uint64_t op2 = decode_logical_immediate( N_immr_imms, xregs ? 64 : 32 );
                tracer.Trace( "and %s, %s, #%#llx\n", reg_or_sp( d, xregs ), reg_or_zr( n, xregs ), op2 );
            }
            break;
        }
        case 0x1a: // CSEL <Wd>, <Wn>, <Wm>, <cond>    ;    SDIV <Wd>, <Wn>, <Wm>    ;    UDIV <Wd>, <Wn>, <Wm>    ;    CSINC <Wd>, <Wn>, <Wm>, <cond>
                   // LSRV <Wd>, <Wn>, <Wm>            ;    LSLV <Wd>, <Wn>, <Wm>    ;    ADC <Wd>, <Wn>, <Wm>     ;    ASRV <Wd>, <Wn>, <Wm>
                   // RORV <Wd>, <Wn>, <Wm>
        case 0x9a: // CSEL <Xd>, <Xn>, <Xm>, <cond>    ;    SDIV <Xd>, <Xn>, <Xm>    ;    UDIV <Xd>, <Xn>, <Xm>    ;    CSINC <Xd>, <Xn>, <Xm>, <cond>
                   // LSRV <Xd>, <Xn>, <Xm>            ;    LSLV <Xd>, <Xn>, <Xm>    ;    ADC <Xd>, <Xn>, <Xm>     ;    ASRV <Xd>, <Xn>, <Xm>
                   // RORV <Xd>, <Xn>, <Xm>
        {
            bool xregs = ( 0 != ( hi8 & 0x80 ) );
            uint64_t bits11_10 = opbits( 10, 2 );
            uint64_t d = opbits( 0, 5 );
            uint64_t n = opbits( 5, 5 );
            uint64_t m = opbits( 16, 5 );
            uint64_t bits15_12 = opbits( 12, 4 );
            uint64_t bits23_21 = opbits( 21, 3 );

            if ( 0 == bits11_10 && 4 == bits23_21 ) // CSEL
            {
                uint64_t cond = opbits( 12, 4 );
                tracer.Trace( "csel %s, %s, %s, %s\n", reg_or_zr( d, xregs ), reg_or_zr2( n, xregs ), reg_or_zr3( m, xregs ), get_cond( cond ) );
            }
            else if ( 1 == bits11_10 && 4 == bits23_21 ) // CSINC <Xd>, XZR, XZR, <cond>
            {
                uint64_t cond = opbits( 12, 4 );
                tracer.Trace( "csinc %s, %s, %s, %s\n", reg_or_zr( d, xregs ), reg_or_zr2( n, xregs ), reg_or_zr3( m, xregs ), get_cond( cond ) );
            }
            else if ( 2 == bits11_10 && 6 == bits23_21 && 2 == bits15_12 ) // ASRV <Xd>, <Xn>, <Xm>
                tracer.Trace( "asrv %s, %s, %s\n", reg_or_zr( d, xregs ), reg_or_zr2( n, xregs ), reg_or_zr3( m, xregs ) );
            else if ( 2 == bits11_10 && 6 == bits23_21 && 0 == bits15_12 ) // UDIV <Xd>, <Xn>, <Xm>
                tracer.Trace( "udiv %s, %s, %s\n", reg_or_zr( d, xregs ), reg_or_zr2( n, xregs ), reg_or_zr3( m, xregs ) );
            else if ( 3 == bits11_10 && 6 == bits23_21 && 0 == bits15_12 ) // SDIV <Xd>, <Xn>, <Xm>
                tracer.Trace( "sdiv %s, %s, %s\n", reg_or_zr( d, xregs ), reg_or_zr2( n, xregs ), reg_or_zr3( m, xregs ) );
            else if ( 1 == bits11_10 && 6 == bits23_21 && 2 == bits15_12 ) // lsrv
                tracer.Trace( "lsrv %s, %s, %s\n", reg_or_zr( d, xregs ), reg_or_zr2( n, xregs ), reg_or_zr3( m, xregs ) );
            else if ( 0 == bits11_10 && 6 == bits23_21 && 2 == bits15_12 ) // lslv
                tracer.Trace( "lslv %s, %s, %s\n", reg_or_zr( d, xregs ), reg_or_zr2( n, xregs ), reg_or_zr3( m, xregs ) );
            else if ( 0 == bits11_10 && 0 == bits23_21 && 0 == bits15_12 && 0 == bits11_10 ) // addc
                tracer.Trace( "addc %s, %s, %s\n", reg_or_zr( d, xregs ), reg_or_zr2( n, xregs ), reg_or_zr3( m, xregs ) );
            else if ( 3 == bits11_10 && 6 == bits23_21 && 2 == bits15_12 ) // RORV <Xd>, <Xn>, <Xm>
                tracer.Trace( "rorv %s, %s, %s\n", reg_or_zr( d, xregs ), reg_or_zr2( n, xregs ), reg_or_zr3( m, xregs ) );
            else
                unhandled();
            break;
        }
        case 0x52: // MOVZ <Wd>, #<imm>{, LSL #<shift>}    ;    EOR <Wd|WSP>, <Wn>, #<imm>
        case 0xd2: // MOVZ <Xd>, #<imm>{, LSL #<shift>}    ;    EOR <Xd|SP>, <Xn>, #<imm>    
        {
            bool xregs = ( 0 != ( hi8 & 0x80 ) );
            uint64_t bit23 = opbits( 23, 1 );

            if ( bit23 ) // movz xd, imm16
            {
                uint64_t d = opbits( 0, 5 );
                uint64_t imm16 = opbits( 5, 16 );
                uint64_t hw = opbits( 21, 2 );
                tracer.Trace( "movz %s, %#llx, LSL #%llu\n", reg_or_zr( d, xregs ), imm16, hw * 16 );
            }
            else // EOR
            {
                uint64_t N_immr_imms = opbits( 10, 13 );
                uint64_t op2 = decode_logical_immediate( N_immr_imms, xregs ? 64 : 32 );
                uint64_t n = ( ( op >> 5 ) & 0x1f );
                uint64_t d = ( op & 0x1f );
                tracer.Trace( "eor %s, %s, #%#llx\n", reg_or_sp( d, xregs ), reg_or_sp2( n, xregs ), op2 );
            }
            break;
        }
        case 0x34: case 0xb4: // CBZ <Xt>, <label>     ;    CBZ <WXt>, <label>  
        case 0x35: case 0xb5: // CBNZ <Xt>, <label>    ;    CBNZ <Wt>, <label>
        {
            bool xregs = ( 0 != ( hi8 & 0x80 ) );
            uint64_t t = opbits( 0, 5 );
            bool zero_check = ( 0 == ( hi8 & 1 ) );
            int64_t imm19 = ( ( op >> 3 ) & 0x1ffffc );
            imm19 = sign_extend( imm19, 20 );
            tracer.Trace( "cb%sz %s, %#llx\n", zero_check ? "" : "n", reg_or_zr( t, xregs ), pc + imm19 );
            break;
        }
        case 0xd4: // svc
        {
            uint8_t bit23 = ( op >> 23 ) & 1;
            uint8_t hw = ( op >> 21 ) & 3;

            if ( !bit23 && ( 0 == hw ) )
            {
                uint64_t imm16 = ( op >> 5 ) & 0xffff;
                uint8_t op2 = (uint8_t) ( ( op >> 2 ) & 7 );
                uint8_t ll = (uint8_t) ( op & 3 );
                if ( ( 0 == op2 ) && ( 1 == ll ) ) // svc imm16 supervisor call
                    tracer.Trace( "svc %#llx\n", imm16 );
                else
                    unhandled();
            }
            break;
        }
        case 0x2e: case 0x6e: // CMEQ <Vd>.<T>, <Vn>.<T>, <Vm>.<T>    ;    CMHS <Vd>.<T>, <Vn>.<T>, <Vm>.<T>    ;    UMAXP <Vd>.<T>, <Vn>.<T>, <Vm>.<T>
                              // BIT <Vd>.<T>, <Vn>.<T>, <Vm>.<T>     ;    UMINP <Vd>.<T>, <Vn>.<T>, <Vm>.<T>   ;    BIF <Vd>.<T>, <Vn>.<T>, <Vm>.<T>
                              // EOR <Vd>.<T>, <Vn>.<T>, <Vm>.<T>     ;    SUB <Vd>.<T>, <Vn>.<T>, <Vm>.<T>     ;    UMULL{2} <Vd>.<Ta>, <Vn>.<Tb>, <Vm>.<Tb>
                              // MLS <Vd>.<T>, <Vn>.<T>, <Vm>.<Ts>[<index>] ;  BSL <Vd>.<T>, <Vn>.<T>, <Vm>.<T> ;    FMUL <Vd>.<T>, <Vn>.<T>, <Vm>.<T>
                              // EXT <Vd>.<T>, <Vn>.<T>, <Vm>.<T>, #<index> ;  INS <Vd>.<Ts>[<index1>], <Vn>.<Ts>[<index2>]  ;    UADDLV <V><d>, <Vn>.<T>
                              // USHL <Vd>.<T>, <Vn>.<T>, <Vm>.<T>    ;    FADDP <Vd>.<T>, <Vn>.<T>, <Vm>.<T> 
        {
            uint64_t Q = opbits( 30, 1 );
            uint64_t m = opbits( 16, 5 );
            uint64_t n = opbits( 5, 5 );
            uint64_t d = opbits( 0, 5 );
            uint64_t size = opbits( 22, 2 );
            uint64_t bit23 = opbits( 23, 1 );
            uint64_t bit21 = opbits( 21, 1 );
            uint64_t bit15 = opbits( 15, 1 );
            uint64_t bit10 = opbits( 10, 1 );
            uint64_t bits23_21 = opbits( 21, 3 );
            const char * pT = get_ld1_vector_T( size, Q );
            uint64_t opcode = opbits( 10, 6 );
            uint64_t opcode7 = opbits( 10, 7 );
            uint64_t bits20_17 = opbits( 17, 4 );

            if ( !bit23 && bit21 && 0x35 == opcode ) // FADDP <Vd>.<T>, <Vn>.<T>, <Vm>.<T>
            {
                uint64_t sz = opbits( 22, 1 );
                uint64_t ty = ( sz << 1 ) | Q;
                pT = ( 0 == ty ) ? "2s" : ( 1 == ty ) ? "4s" : ( 3 == ty ) ? "2d" : "?";
                tracer.Trace( "faddp v%llu.%s, v%llu,%s, v%llu.%s\n", d, pT, n, pT, m, pT );
            }
            else if ( bit21 && 0x11 == opcode ) // USHL <Vd>.<T>, <Vn>.<T>, <Vm>.<T>
                tracer.Trace( "ushl, v%llu.%s, v%llu.%s, v%llu.%s\n", d, pT, n, pT, m, pT );
            else if ( bit21 && 8 == bits20_17 && 0xe == opcode7 ) // UADDLV <V><d>, <Vn>.<T>
                tracer.Trace( "uaddlv v%llu, v%llu.%s\n", d, n, pT );
            else if ( 0x6e == hi8 && 0 == bits23_21 && !bit15 && bit10 )
            {
                uint64_t imm5 = opbits( 16, 5 );
                uint64_t imm4 = opbits( 11, 5 );
                uint64_t index1 = 0;
                uint64_t index2 = 0;
                char T = '?';
                if ( 1 & imm5 )
                {
                    index1 = get_bits( imm5, 1, 4 );
                    index2 = imm4;
                    T = 'B';
                }
                else if ( 2 & imm5 )
                {
                    index1 = get_bits( imm5, 2, 3 );
                    index2 = get_bits( imm4, 1, 3 );
                    T = 'H';
                }
                else if ( 4 & imm5 )
                {
                    index1 = get_bits( imm5, 3, 2 );
                    index2 = get_bits( imm4, 2, 2 );
                    T = 'S';
                }
                else if ( 8 & imm5 )
                {
                    index1 = get_bits( imm5, 4, 1 );
                    index1 = get_bits( imm5, 3, 1 );
                    T = 'D';
                }

                tracer.Trace( "ins v%llu.%c[%llu], v%llu.%c[%llu]\n", d, T, index1, n, T, index2 );
            }
            else if ( bit21 && 0x23 == opcode )
                tracer.Trace( "cmeq v%llu.%s, v%llu.%s, v%llu.%s\n", d, pT, n, pT, m, pT );
            else if ( bit21 && 0x0f == opcode )
                tracer.Trace( "cmhs v%llu.%s, v%llu.%s, v%llu.%s\n", d, pT, n, pT, m, pT );
            else if ( bit21 && 0x29 == opcode )
                tracer.Trace( "umaxp v%llu.%s, v%llu.%s, v%llu.%s\n", d, pT, n, pT, m, pT );
            else if ( bit21 && 0x2b == opcode )
                tracer.Trace( "uminp v%llu.%s, v%llu.%s, v%llu.%s\n", d, pT, n, pT, m, pT );
            else if ( bit21 && 0x07 == opcode )
            {
                uint64_t opc2 = opbits( 22, 2 );
                pT = ( 0 == Q ) ? "8B" : "16B";
                tracer.Trace( "%s v%llu.%s, v%llu.%s, v%llu.%s\n", ( 1 == opc2 ) ? "bsl" : ( 2 == opc2) ? "bit" : ( 3 == opc2 ) ? "bif" : "eor", d, pT, n, pT, m, pT );
            }
            else if ( bit21 && 0x21 == opcode )
                tracer.Trace( "sub v%llu.%s, v%llu.%s, v%llu.%s\n", d, pT, n, pT, m, pT );
            else if ( bit21 && 0x30 == opcode )
            {
                const char * pTA = ( 0 == size ) ? "8H" : ( 1 == size ) ? "4S" : ( 2 == size ) ? "2D" : "?";
                const char * pTB = get_ld1_vector_T( size, Q );
                tracer.Trace( "umull%s v%llu.%s, v%llu.%s, v%llu.%s\n", Q ? "2" : "", d, pTA, n, pTB, m, pTB );
            }
            else if ( bit21 && 0x25 == opcode ) // MLS <Vd>.<T>, <Vn>.<T>, <Vm>.<T>
            {
                if ( 3 == size )
                    unhandled();
                tracer.Trace( "mls v%llu.%s, v%llu.%s, v%llu.%s\n", d, pT, n, pT, m, pT );
            }
            else if ( bit21 && 0x37 == opcode )
            {
                uint64_t sz = opbits( 22, 1 );
                pT = ( 0 == sz ) ? ( 0 == Q ? "2S" : "4S" ) : ( 0 == Q ? "?" : "2D" );
                tracer.Trace( "fmul v%llu.%s, v%llu.%s, v%llu.%s\n", d, pT, n, pT, m, pT );
            }
            else if ( !bit21 && 0 == size && !bit10 && !bit15 )
            {
                uint64_t imm4 = opbits( 11, 4 );
                pT = Q ? "8B" : "16B";
                tracer.Trace( "ext v%llu.%s, v%llu.%s, v%llu.%s, #%llu\n", d, pT, n, pT, m, pT, imm4 );
            }
            else
                unhandled();
            break;
        }
        case 0x5e: // SCVTF <V><d>, <V><n>    ;    ADDP D<d>, <Vn>.2D    ;    DUP <V><d>, <Vn>.<T>[<index>]
        {
            uint64_t bits23_10 = opbits( 10, 14 );
            uint64_t n = opbits( 5, 5 );
            uint64_t d = opbits( 0, 5 );

            if ( 0x0876 == ( bits23_10 & 0x2fff ) ) // SCVTF <V><d>, <V><n>
            {
                uint64_t sz = opbits( 22, 1 );
                char width = sz ? 'd' : 's';
                tracer.Trace( "scvtf %c%llu, %c%llu\n", width, d, width, n );
            }
            else if ( 0x3c6e == bits23_10 ) // DUP <V><d>, <Vn>.<T>[<index>]
                tracer.Trace( "addp D%llu, v%llu.2D\n", d, n );
            else if ( 1 == ( bits23_10 & 0x383f ) ) // DUP <V><d>, <Vn>.<T>[<index>]   -- scalar
            {
                uint64_t imm5 = opbits( 16, 5 );
                uint64_t size = lowest_set_bit_nz( imm5 & 0xf );
                uint64_t index = get_bits( imm5, size + 1, size + 2 ); // imm5:<4:size+1>
                const char * pT = ( imm5 & 1 ) ? "B" : ( imm5 & 2 ) ? "H" : ( imm5 & 4 ) ? "S" : "D";
                tracer.Trace( "dup %s%llu, v%llu.%s[%llu]\n", pT, d, n, pT, index );
            }
            else
                unhandled();
            break;
        }
        case 0x7e: // CMGE    ;    UCVTF <V><d>, <V><n>    ;    UCVTF <Hd>, <Hn>    ;    FADDP <V><d>, <Vn>.<T>
        {
            uint64_t bits23_10 = opbits( 10, 14 );
            uint64_t n = opbits( 5, 5 );
            uint64_t d = opbits( 0, 5 );

            if ( 0x0c36 == bits23_10 || 0x1c36 == bits23_10 ) // FADDP <V><d>, <Vn>.<T>
            {
                uint64_t sz = opbits( 22, 1 );
                char width = sz ? 'd' : 's';
                tracer.Trace( "faddp %c%llu, v%llu.2%c\n", width, d, n, width );
            }
            else if ( 0x3822 == bits23_10 )
                tracer.Trace( "cmge d%llu, d%llu, #0\n", d, n );
            else if ( 0x0876 == ( bits23_10 & 0x2fff ) )
            {
                uint64_t sz = opbits( 22, 1 );
                char width = sz ? 'd' : 's';
                tracer.Trace( "ucvtf %c%llu, %c%llu\n", width, d, width, n );
            }
            else
                unhandled();
            break;
        }
        case 0x0e: case 0x4e: // DUP <Vd>.<T>, <Vn>.<Ts>[<index>]    ;    DUP <Vd>.<T>, <R><n>    ;             CMEQ <Vd>.<T>, <Vn>.<T>, #0    ;    ADDP <Vd>.<T>, <V
                              // AND <Vd>.<T>, <Vn>.<T>, <Vm>.<T>    ;    UMOV <Wd>, <Vn>.<Ts>[<index>]    ;    UMOV <Xd>, <Vn>.D[<index>]     ;    CNT <Vd>.<T>, <Vn>.<T>
                              // AND <Vd>.<T>, <Vn>.<T>, <Vm>.<T>    ;    UMOV <Wd>, <Vn>.<Ts>[<index>]    ;    UMOV <Xd>, <Vn>.D[<index>]     ;    ADDV <V><d>, <Vn>.<T>
                              // XTN{2} <Vd>.<Tb>, <Vn>.<Ta>         ;    UZP1 <Vd>.<T>, <Vn>.<T>, <Vm>.<T> ;   UZP2 <Vd>.<T>, <Vn>.<T>, <Vm>.<T>
                              // SMOV <Wd>, <Vn>.<Ts>[<index>]       ;    SMOV <Xd>, <Vn>.<Ts>[<index>]    ;    INS <Vd>.<Ts>[<index>], <R><n> ;    CMGT <Vd>.<T>, <Vn>.<T>, <Vm>.<T>
                              // SCVTF <Vd>.<T>, <Vn>.<T>            ;    FMLA <Vd>.<T>, <Vn>.<T>, <Vm>.<T>;    FADD <Vd>.<T>, <Vn>.<T>, <Vm>.<T>
                              // TRN1 <Vd>.<T>, <Vn>.<T>, <Vm>.<T>   ;    TRN2 <Vd>.<T>, <Vn>.<T>, <Vm>.<T> ;   TBL <Vd>.<Ta>, { <Vn>.16B }, <Vm>.<Ta> ; TBL <Vd>.<Ta>, { <Vn>.16B, <Vn+1>.16B, <Vn+2>.16B, <Vn+3>.16B }, <Vm>.<Ta>
                              // ZIP1 <Vd>.<T>, <Vn>.<T>, <Vm>.<T>   ;    ZIP2 <Vd>.<T>, <Vn>.<T>, <Vm>.<T> ;   SMULL{2} <Vd>.<Ta>, <Vn>.<Tb>, <Vm>.<Tb>

        {
            uint64_t Q = opbits( 30, 1 );
            uint64_t imm5 = opbits( 16, 5 );
            uint64_t bit15 = opbits( 15, 1 );
            uint64_t bits14_11 = opbits( 11, 4 );
            uint64_t bit10 = opbits( 10, 1 );
            uint64_t bits12_10 = opbits( 10, 3 );
            uint64_t bit21 = opbits( 21, 1 );
            uint64_t bit23 = opbits( 23, 1 );
            uint64_t bits23_21 = opbits( 21, 3 );
            uint64_t n = opbits( 5, 5 );
            uint64_t d = opbits( 0, 5 );
            uint64_t bits20_16 = opbits( 16, 5 );
            uint64_t bits14_10 = opbits( 10, 5 );

            if ( bit21 && bit15 && 8 == bits14_11 && !bit10 ) // SMULL{2} <Vd>.<Ta>, <Vn>.<Tb>, <Vm>.<Tb>
            {
                uint64_t m = opbits( 16, 5 );
                uint64_t size = opbits( 22, 2 );
                uint64_t part = Q;
                const char * pTA = ( 0 == size ) ? "8H" : ( 1 == size ) ? "4s" : ( 2 == size ) ? "2d" : "unknown";
                const char * pTB = get_ld1_vector_T( size, Q );
                tracer.Trace( "smull%s v%llu.%s, v%llu.%s, v%llu.%s\n", part ? "2" : "", d, pTA, n, pTB, m, pTB );
            }
            else if ( !bit21 && !bit15 && ( 0x1e == bits14_10 || 0xe == bits14_10 ) ) // ZIP1/2 <Vd>.<T>, <Vn>.<T>, <Vm>.<T>
            {
                uint64_t m = opbits( 16, 5 );
                uint64_t size = opbits( 22, 2 );
                const char * pT = get_ld1_vector_T( size, Q );
                tracer.Trace( "zip%c v%llu.%s, v%llu.%s, v%llu.%s\n", ( 0x1e == bits14_10 ) ? '2' : '1', d, pT, n, pT, m, pT );
            }
            else if ( 0 == bits23_21 && !bit15 && 0 == bits12_10 ) // TBL <Vd>.<Ta>, { <Vn>.16B, <Vn+1>.16B, <Vn+2>.16B, <Vn+3>.16B }, <Vm>.<Ta>
            {
                uint64_t m = opbits( 16, 5 );
                const char * pT = Q ? "16b" : "8b";
                uint64_t len = opbits( 13, 2 );
                if ( 0 == len )
                    tracer.Trace( "tbl v%llu.%s, {v%llu.16b}, v%llu.%s\n", d, pT, n, m, pT );
                else if ( 1 == len )
                    tracer.Trace( "tbl v%llu.%s, {v%llu.16b, v%llu.16b}, v%llu.%s\n", d, pT, n, n + 1, m, pT );
                else if ( 2 == len )
                    tracer.Trace( "tbl v%llu.%s, {v%llu.16b, v%llu.16b, v%llu.16b }, v%llu.%s\n", d, pT, n, n + 1, n + 2, m, pT );
                else if ( 3 == len )
                    tracer.Trace( "tbl v%llu.%s, {v%llu.16b, v%llu.16b, v%llu.16b, v%llu.16b }, v%llu.%s\n", d, pT, n, n + 1, n + 2, n + 3, m, pT );
            }
            else if ( !bit21 && !bit15 && 0xd == bits14_11 && !bit10 ) // TRN2 <Vd>.<T>, <Vn>.<T>, <Vm>.<T>
            {
                uint64_t m = opbits( 16, 5 );
                uint64_t size = opbits( 22, 2 );
                const char * pT = get_ld1_vector_T( size, Q );
                tracer.Trace( "trn2 v%llu.%s, v%llu.%s, v%llu.%s\n", d, pT, n, pT, m, pT );
            }
            else if ( !bit21 && !bit15 && 5 == bits14_11 && !bit10 ) // TRN1 <Vd>.<T>, <Vn>.<T>, <Vm>.<T>
            {
                uint64_t m = opbits( 16, 5 );
                uint64_t size = opbits( 22, 2 );
                const char * pT = get_ld1_vector_T( size, Q );
                tracer.Trace( "trn1 v%llu.%s, v%llu.%s, v%llu.%s\n", d, pT, n, pT, m, pT );
            }
            else if ( !bit23 && bit21 && bit15 && 0xa == bits14_11 && bit10 ) // FADD <Vd>.<T>, <Vn>.<T>, <Vm>.<T>
            {
                uint64_t sz = opbits( 22, 1 );
                uint64_t ty = ( sz << 1 ) | Q;
                const char * pT = ( 0 == ty ) ? "2s" : ( 1 == ty ) ? "4s" : ( 3 == ty ) ? "2d" : "?";
                uint64_t m = opbits( 16, 5 );
                tracer.Trace( "fadd v%llu.%s, v%llu.%s, v%llu.%s\n", d, pT, n, pT, m, pT );
            }
            else if ( !bit23 && bit21 && bit15 && 9 == bits14_11 && bit10 ) // FMLA <Vd>.<T>, <Vn>.<T>, <Vm>.<T>
            {
                uint64_t sz = opbits( 22, 1 );
                uint64_t ty = ( sz << 1 ) | Q;
                const char * pT = ( 0 == ty ) ? "2s" : ( 1 == ty ) ? "4s" : ( 3 == ty ) ? "2d" : "?";
                uint64_t m = opbits( 16, 5 );
                tracer.Trace( "fmla v%llu.%s, v%llu.%s, v%llu.%s\n", d, pT, n, pT, m, pT );
            }
            else if ( !bit23 && bit21 && 1 == bits20_16 && bit15 && 0x16 == bits14_10 ) // SCVTF <Vd>.<T>, <Vn>.<T>
            {
                uint64_t sz = opbits( 22, 1 );
                uint64_t ty = ( sz << 1 ) | Q;
                const char * pT = ( 0 == ty ) ? "2s" : ( 1 == ty ) ? "4s" : ( 3 == ty ) ? "2d" : "?";
                tracer.Trace( "scvtf v%llu.%s, v%llu.%s\n", d, pT, n, pT );
            }
            else if ( 0x4e == hi8 && 0 == bits23_21 && !bit15 && 3 == bits14_11 && bit10 ) // INS <Vd>.<Ts>[<index>], <R><n>
            {
                char T = '?';
                uint64_t index = 0;
                if ( imm5 & 1 )
                {
                    T = 'B';
                    index = get_bits( imm5, 1, 4 );
                }
                else if ( imm5 & 2 )
                {
                    T = 'H';
                    index = get_bits( imm5, 2, 3 );
                }
                else if ( imm5 & 4 )
                {
                    T = 'S';
                    index = get_bits( imm5, 3, 2 );
                }
                else if ( imm5 & 8 )
                {
                    T = 'D';
                    index = get_bits( imm5, 4, 1 );
                }
                else
                    unhandled();
                tracer.Trace( "ins v%llu.%c[%llu], %s\n", d, T, index, reg_or_zr( n, ( 4 == ( imm5 & 0xf ) ) ) );
            }
            else if ( !bit21 && !bit15 && ( 7 == bits14_11 || 5 == bits14_11 ) && bit10 )
            {
                // UMOV <Wd>, <Vn>.<Ts>[<index>]    ;    UMOV <Xd>, <Vn>.D[<index>]    ;     SMOV <Wd>, <Vn>.<Ts>[<index>]    ;    SMOV <Xd>, <Vn>.<Ts>[<index>]
                uint64_t size = lowest_set_bit_nz( imm5 & ( ( 7 == bits14_11 ) ? 0xf : 7 ) );
                uint64_t bits_to_copy = 4 - size;
                uint64_t index = get_bits( imm5, 4 + 1 - bits_to_copy, bits_to_copy );

                const char * pT = "UNKNOWN";
                if ( imm5 & 1 )
                    pT = "B";
                else if ( imm5 & 2 )
                    pT = "H";
                else if ( imm5 & 4 )
                    pT = "S";
                else if ( imm5 & 8 )
                    pT = "D";
                else
                    unhandled();
                tracer.Trace( "%cmov %s, v%llu.%s[%llu]\n", ( 7 == bits14_11 ) ? 'u' : 's', reg_or_zr( d, Q ), n, pT, index );
            }
            else if ( !bit21 && !bit15 && ( 0x3 == bits14_11 || 0xb == bits14_11 ) && !bit10 ) // 
            {
                uint64_t size = opbits( 22, 2 );
                uint64_t part = opbits( 14, 1 );
                uint64_t m = imm5;
                const char * pT = get_ld1_vector_T( size, Q );
                tracer.Trace( "uzp%c v%llu.%s, v%llu.%s, v%llu.%s\n", ( 1 == part ) ? '2' : '1', d, pT, n, pT, m, pT );
            }
            else if ( 1 == bits23_21 && !bit15 && 3 == bits14_11 && bit10 ) // AND <Vd>.<T>, <Vn>.<T>, <Vm>.<T>
            {
                uint64_t m = imm5;
                const char * pT = ( 0 == Q ) ? "8B" : "16B";
                tracer.Trace( "and v%llu.%s, v%llu.%s v%llu.%s\n", d, pT, n, pT, m, pT );
            }
            else if ( 5 == bits23_21 && !bit15 && 3 == bits14_11 && bit10 ) // ORR <Vd>.<T>, <Vn>.<T>, <Vm>.<T>
            {
                uint64_t m = imm5;
                const char * pT = ( 0 == Q ) ? "8B" : "16B";
                tracer.Trace( "orr v%llu.%s, v%llu.%s v%llu.%s\n", d, pT, n, pT, m, pT );
            }
            else if ( bit21 && bit15 && 3 == bits14_11 && !bit10 && 0 == bits20_16 )  // CMEQ <Vd>.<T>, <Vn>.<T>, #0
            {
                uint64_t size = opbits( 22, 2 );
                tracer.Trace( "cmeq v%llu.%s, v%llu.%s, #0\n", d, get_ld1_vector_T( size, Q ), n, get_ld1_vector_T( size, Q ) );
            }
            else if ( bit21 && !bit15 && 6 == bits14_11 && bit10 ) // CMGT <Vd>.<T>, <Vn>.<T>, <Vm>.<T>
            {
                uint64_t m = opbits( 16, 5 );
                uint64_t size = opbits( 22, 2 );
                const char * pT = get_ld1_vector_T( size, Q );
                tracer.Trace( "cmgt v%llu.%s, v%llu.%s, v%llu.%s\n", d, pT, n, pT, m, pT );
            }
            else if ( bit21 && bit15 && 7 == bits14_11 && bit10 ) // ADDP <Vd>.<T>, <Vn>.<T>, <Vm>.<T>
            {
                uint64_t m = opbits( 16, 5 );
                uint64_t size = opbits( 22, 2 );
                const char * pT = get_ld1_vector_T( size, Q );
                tracer.Trace( "addp v%llu.%s, v%llu.%s, v%llu.%s\n", d, pT, n, pT, m, pT );
            }
            else if ( 0 == bits23_21 && !bit15 && 1 == bits14_11 && bit10 ) // DUP <Vd>.<T>, <R><n>
                tracer.Trace( "dup v%llu.%s, %s\n", d, get_vector_T( imm5, Q ), reg_or_zr( n, 0x1000 == ( imm5 & 0xffff ) ) );
            else if ( 0 == bits23_21 && !bit15 && 0 == bits14_11 && bit10 ) // DUP <Vd>.<T>, <Vn>.<Ts>[<index>]
            {
                uint64_t size = lowest_set_bit_nz( imm5 & 0xf );
                uint64_t index = get_bits( imm5, size + 1, 4 - ( size + 1 ) + 1 );
                uint64_t indsize = 64ull << get_bits( imm5, 4, 1 );
                uint64_t esize = 8ull << size;
                uint64_t datasize = 64ull << Q;
                uint64_t elements = datasize / esize;
                tracer.Trace( "size %llu, index %llu, indsize %llu, esize %llu, datasize %llu, elements %llu\n", size, index, indsize, esize, datasize, elements );
                char byte_len = ( imm5 & 1 ) ? 'B' : ( 2 == ( imm5 & 3 ) ) ? 'H' : ( 4 == ( imm5 & 7 ) ) ? 'S' : ( 8 == ( imm5 & 0xf ) ) ? 'D' : '?';
                tracer.Trace( "dup v%llu.%s, v%llu.%c[%llu]\n", d, get_vector_T( imm5, Q ), n, byte_len, index );
            }
            else if ( bit21 && bit15 && 0 == bits14_11 && bit10 ) // ADD <Vd>.<T>, <Vn>.<T>, <Vm>.<T>.   add vector
            {
                uint64_t size = opbits( 22, 2 );
                uint64_t m = opbits( 16, 5 );
                const char * pT = get_ld1_vector_T( size, Q );
                tracer.Trace( "add v%llu.%s, v%llu.%s, v%llu.%s\n", d, pT, n, pT, m, pT );
            }
            else if ( bit21 && 0xb == bits14_11 && 0 == bits20_16 && !bit15 ) // CNT
            {
                uint64_t size = opbits( 22, 2 );
                if ( 0 != size )
                    unhandled();

                const char * pT = get_ld1_vector_T( size, Q );
                tracer.Trace( "cnt v%llu.%s, v%llu.%s\n", d, pT, n, pT );
            }
            else if ( bit21 && 0x11 == bits20_16 && bit15 && 7 == bits14_11 ) // ADDV <V><d>, <Vn>.<T>
            {
                uint64_t size = opbits( 22, 2 );
                if ( 3 == size )
                    unhandled();
                const char * pT = get_ld1_vector_T( size, Q );
                char dstT = ( 0 == size ) ? 'B' : ( 1 == size ) ? 'H' : 'S';
                tracer.Trace( "addv %c%llu, v%llu.%s\n", dstT, d, n, pT );
            }
            else if ( bit21 && 1 == bits20_16 && !bit15 && 5 == bits14_11 && !bit10 ) // xtn, xtn2 XTN{2} <Vd>.<Tb>, <Vn>.<Ta>
            {
                uint64_t size = opbits( 22, 2 );
                if ( 3 == size )
                    unhandled();

                const char * pTb = get_ld1_vector_T( size, Q );
                const char * pTa = ( 0 == size ) ? "8h" : ( 1 == size ) ? "4s" : "2d";
                tracer.Trace( "xtn%s v%llu.%s, v%llu.%s\n", Q ? "2" : "", d, pTb, n, pTa );
            }
            else
            {
                tracer.Trace( "unknown opcode bits23_21 %lld, bit15 %llu, bits14_11 %llu, bit10 %llu\n", bits23_21, bit15, bits14_11, bit10 );
                unhandled();
            }
            break;
        }
        case 0x1e: // FMOV <Wd>, <Hn>    ;    FMUL                ;    FMOV <Wd>, imm       ;    FCVTZU <Wd>, <Dn>    ;    FRINTA <Dd>, <Dn>
        case 0x9e: // FMOV <Xd>, <Hn>    ;    UCVTF <Hd>, <Dn>    ;    FCVTZU <Xd>, <Dn>    ;    FCVTAS <Xd>, <Dn>
        {
            uint64_t sf = opbits( 31, 1 );
            uint64_t ftype = opbits( 22, 2 );
            uint64_t bit21 = opbits( 21, 1 );
            uint64_t bit11 = opbits( 11, 1 );
            uint64_t bit10 = opbits( 10, 1 );
            uint64_t bit4 = opbits( 4, 1 );
            uint64_t bits21_19 = opbits( 19, 3 );
            uint64_t bits18_16 = opbits( 16, 3 );
            uint64_t bits18_10 = opbits( 10, 9 );
            uint64_t n = opbits( 5, 5 );
            uint64_t d = opbits( 0, 5 );
            uint64_t rmode = opbits( 19, 2 );
            //tracer.Trace( "ftype %llu, bit21 %llu, rmode %llu, bits18_10 %#llx\n", ftype, bit21, rmode, bits18_10 );

            if ( 0x1e == hi8 && bit21 && !bit11 && bit10 && bit4 ) // FCCMPE <Sn>, <Sm>, #<nzcv>, <cond>    ;    FCCMPE <Dn>, <Dm>, #<nzcv>, <cond>
            {
                char t = ( 0 == ftype ) ? 's' : ( 3 == ftype ) ? 'h' : ( 1 == ftype ) ? 'd' : '?';
                uint64_t m = opbits( 16, 5 );
                uint64_t nzcv = opbits( 0, 4 );
                uint64_t cond = opbits( 12, 4 );
                tracer.Trace( "fccmpe %c%llu, %c%llu, #%#llx, %s\n", t, n, t, m, nzcv, get_cond( cond ) );
            }
            else if ( 3 == bits21_19 && 0 == bits18_16 ) // FCVTZS <Xd>, <Dn>, #<fbits>
            {
                char t = ( 0 == ftype ) ? 's' : ( 3 == ftype ) ? 'h' : ( 1 == ftype ) ? 'd' : '?';
                uint64_t scale = opbits( 10, 6 );
                uint64_t fbits = 64 - scale;
                tracer.Trace( "fcvtzs %s, %c%llu, #%llu\n", reg_or_zr( d, sf ), t, n, fbits );
            }
            else if ( 4 == bits21_19 && 0x100 == bits18_10 ) // FCVTAS <Xd>, <Dn>
            {
                char type = ( 0 == ftype ) ? 's' : ( 1 == ftype ) ? 'd' : ( 3 == ftype ) ? 'h' : '?';
                tracer.Trace( "fcvtas %s, %c%llu\n", reg_or_zr( d, sf ), type, n );
            }
            else if ( 0x1e == hi8 && 4 == bits21_19 && 0x190 == bits18_10 ) // FRINTA <Dd>, <Dn>
            {
                char type = ( 0 == ftype ) ? 's' : ( 1 == ftype ) ? 'd' : ( 3 == ftype ) ? 'h' : '?';
                tracer.Trace( "frinta %c%llu, %c%llu\n", type, d, type, n );
            }
            else if ( ( 0x180 == ( bits18_10 & 0x1bf ) ) && ( bit21 ) && ( 0 == ( rmode & 2 ) ) ) // fmov reg, vreg  OR mov vreg, reg
            {
                uint64_t opcode = opbits( 16, 3 );
                if ( 0 == sf )
                {
                    if ( 0 != rmode )
                        unhandled();

                    if ( 3 == ftype )
                    {
                        if (  6 == opcode )
                            tracer.Trace( "fmov w%llu, h%llu\n", d, n );
                        else if ( 7 == opcode )
                            tracer.Trace( "fmov h%llu, w%llu\n", d, n );
                    }
                    else if ( 0 == ftype )
                    {
                        if ( 7 == opcode )
                            tracer.Trace( "fmov s%llu, w%llu\n", d, n );
                        else if ( 6 == opcode )
                            tracer.Trace( "fmov w%llu, s%llu\n", d, n );
                    }
                    else
                        unhandled();
                }
                else
                {
                    if ( 0 == rmode )
                    {
                        if ( 3 == ftype && 6 == opcode )
                            tracer.Trace( "fmov x%llu, h%llu\n", d, n );
                        else if ( 3 == ftype && 7 == opcode )
                            tracer.Trace( "fmov h%llu, %s\n", d, reg_or_zr( n, false ) );
                        else if ( 1 == ftype && 7 == opcode )
                            tracer.Trace( "fmov d%llu, %s\n", d, reg_or_zr( n, true ) );
                        else if ( 1 == ftype && 6 == opcode )
                            tracer.Trace( "fmov x%llu, d%llu\n", d, n );
                        else
                            unhandled();
                    }
                    else
                    {
                        if ( 2 == ftype && 7 == opcode )
                            tracer.Trace( "fmov v%llu.D[1], x%llu\n", d, n );
                        else if ( 2 == ftype && 6 == opcode )
                            tracer.Trace( "fmov x%llu, v%llu.D[1]\n", d, n );
                    }
                }
            }
            else if ( 0x40 == bits18_10 && bit21 && 3 == rmode ) // FCVTZU <Wd>, <Dn>
            {
                char t = ( 0 == ftype ) ? 's' : ( 3 == ftype ) ? 'h' : ( 1 == ftype ) ? 'd' : '?';
                tracer.Trace( "fcvtzu %s, %c%llu\n", reg_or_zr( d, sf ), t, n );
            }
            else if ( 0x40 == ( bits18_10 & 0x1c0 ) && !bit21 && 3 == rmode ) // FCVTZU <Wd>, <Dn>, #<fbits>
            {
                char t = ( 0 == ftype ) ? 's' : ( 3 == ftype ) ? 'h' : ( 1 == ftype ) ? 'd' : '?';
                uint64_t scale = opbits( 10, 6 );
                uint64_t fbits = 64 - scale;
                tracer.Trace( "fcvtzu %s, %cllu, #%llu\n", reg_or_zr( d, sf ), t, n, fbits );
            }
            else if ( ( 0x1e == hi8 ) && ( 4 == ( bits18_10 & 7 ) ) && bit21 && 0 == opbits( 5, 5 ) ) // fmov scalar immediate
            {
                tracer.Trace( "ftype %llu, bit21 %llu, rmode %llu, bits18_10 %#llx\n", ftype, bit21, rmode, bits18_10 );
                uint64_t fltsize = ( 2 == ftype ) ? 64 : ( 8 << ( ftype ^ 2 ) );
                char width = ( 3 == ftype ) ? 'H' : ( 0 == ftype ) ? 'S' : ( 1 == ftype ) ? 'D' : '?';
                uint64_t imm8 = opbits( 13, 8 );
                tracer.Trace( "imm8: %llu == %#llx\n", imm8, imm8 );
                uint64_t val = vfp_expand_imm( imm8, fltsize );
                double dval = 0.0;
                if ( 1 == ftype )
                    memcpy( &dval, &val, sizeof( dval ) );
                else if ( 0 == ftype )
                {
                    float float_val;
                    memcpy( &float_val, &val, sizeof( float_val ) );
                    dval = (double) float_val;
                }
                tracer.Trace( "fmov %c%llu, #%lf // %#llx\n", width, d, dval, val );
            }
            else if ( ( 0x1e == hi8 ) && ( 2 == ( bits18_10 & 0x3f ) ) && ( bit21 ) ) // fmul vreg, vreg, vreg
            {
                uint64_t m = opbits( 16, 5 );
                if ( 0 == ftype ) // single-precision
                    tracer.Trace( "fmul s%llu, s%llu, s%llu\n", d, n, m );
                else if ( 1 == ftype ) // double-precision
                    tracer.Trace( "fmul d%llu, d%llu, d%llu\n", d, n, m );
                else
                    unhandled();
            }
            else if ( ( 0x1e == hi8 ) && ( 0x90 == ( bits18_10 & 0x19f ) ) && ( bit21 ) ) // fcvt vreg, vreg
            {
                uint64_t opc = opbits( 15, 2 );
                tracer.Trace( "fcvt %c%llu, %c%llu\n", get_fcvt_precision( opc ), d, get_fcvt_precision( ftype ), n ); 
            }
            else if ( ( 0x1e == hi8 ) && ( 0x10 == bits18_10 ) && ( 4 == bits21_19 ) ) // fmov vreg, vreg
            {
                tracer.Trace( "fmov %c%llu, %c%llu\n", get_fcvt_precision( ftype ), d, get_fcvt_precision( ftype ), n );
            }
            else if ( ( 0x1e == hi8 ) && ( 8 == ( bits18_10 & 0x3f ) ) && ( bit21 ) ) // fcmp vreg, vreg   OR    fcmp vreg, 0.0 and fcmpe variants
            {
                uint64_t m = opbits( 16, 5 );
                uint64_t opc = opbits( 3, 2 );
                bool is_fcmpe = ( ( 3 == ftype && 2 == opc ) || ( 3 == ftype && 3 == opc ) || ( 0 == ftype && 2 == opc ) || ( 0 == ftype && 0 == m && 3 == opc ) ||
                                  ( 1 == ftype && 2 == opc ) || ( 1 == ftype && 0 == m && 3 == opc ) );
                if ( 3 == opc && 0 == m )
                    tracer.Trace( "%s %c%llu, 0.0\n", is_fcmpe ? "fcmpe" : "fcmp", get_fcvt_precision( ftype ), n );
                else
                    tracer.Trace( "%s %c%llu, %c%llu\n", is_fcmpe ? "fcmpe" : "fcmp", get_fcvt_precision( ftype ), n, get_fcvt_precision( ftype ), m );
            }
            else if ( ( 0x1e == hi8 ) && ( 0x30 == bits18_10 ) && ( 4 == bits21_19 ) ) // fabs vreg, vreg
            {
                tracer.Trace( "fabs %c%llu, %c%llu\n", get_fcvt_precision( ftype ), d, get_fcvt_precision( ftype ), n );
            }
            else if ( 0x1e == hi8 && ( 6 == ( 0x3f & bits18_10 ) ) && bit21 ) // fdiv
            {
                uint64_t m = opbits( 16, 5 );
                if ( 0 == ftype ) // single-precision
                    tracer.Trace( "fdiv s%llu, s%llu, s%llu\n", d, n, m );
                else if ( 1 == ftype ) // double-precision
                    tracer.Trace( "fdiv d%llu, d%llu, d%llu\n", d, n, m );
                else
                    unhandled();
            }
            else if ( 0x1e == hi8 && ( 0xa == ( 0x3f & bits18_10 ) ) && bit21 ) // fadd
            {
                uint64_t m = opbits( 16, 5 );
                if ( 0 == ftype ) // single-precision
                    tracer.Trace( "fadd s%llu, s%llu, s%llu\n", d, n, m );
                else if ( 1 == ftype ) // double-precision
                    tracer.Trace( "fadd d%llu, d%llu, d%llu\n", d, n, m );
                else
                    unhandled();
            }
            else if ( 0x1e == hi8 && ( 0xe == ( 0x3f & bits18_10 ) ) && bit21 ) // fsub
            {
                uint64_t m = opbits( 16, 5 );
                if ( 0 == ftype ) // single-precision
                    tracer.Trace( "fsub s%llu, s%llu, s%llu\n", d, n, m );
                else if ( 1 == ftype ) // double-precision
                    tracer.Trace( "fsub d%llu, d%llu, d%llu\n", d, n, m );
                else
                    unhandled();
            }
            else if ( 0x80 == bits18_10 && bit21 && 0 == rmode ) // SCVTF (scalar, integer)
            {
                char t = ( 0 == ftype ) ? 's' : ( 3 == ftype ) ? 'h' : ( 1 == ftype ) ? 'd' : '?';
                tracer.Trace( "scvtf %c%llu, %s\n", t, d, reg_or_zr( n, sf ) );
            }
            else if ( 0x70 == bits18_10 && bit21 && 0 == rmode ) // fsqrt s#, s#
            {
                char t = ( 0 == ftype ) ? 's' : ( 3 == ftype ) ? 'h' : ( 1 == ftype ) ? 'd' : '?';
                tracer.Trace( "fsqrt %c%llu, %c%llu\n", t, d, t, n );
            }
            else if ( bit21 && ( 3 == ( 3 & bits18_10 ) ) ) // fcsel
            {
                char t = ( 0 == ftype ) ? 's' : ( 3 == ftype ) ? 'h' : ( 1 == ftype ) ? 'd' : '?';
                uint64_t m = opbits( 16, 5 );
                uint64_t cond = opbits( 12, 4 );
                tracer.Trace( "fcsel %c%llu, %c%llu, %c%llu, %s\n", t, d, t, n, t, m, get_cond( cond ) );
            }
            else if ( bit21 && ( 0x50 == bits18_10 ) ) // fneg (scalar)
            {
                char t = ( 0 == ftype ) ? 's' : ( 3 == ftype ) ? 'h' : ( 1 == ftype ) ? 'd' : '?';
                tracer.Trace( "fneg %c%llu, %c%llu\n", t, d, t, n );
            }
            else if ( bit21 && 0 == bits18_10 && 3 == rmode ) // fcvtzs
            {
                char t = ( 0 == ftype ) ? 's' : ( 3 == ftype ) ? 'h' : ( 1 == ftype ) ? 'd' : '?';
                tracer.Trace( "fcvtzs %s, %c%llu\n", reg_or_zr( d, sf ), t, n );
            }
            else if ( bit21 && ( 1 == ( bits18_10 & 3 ) ) && ( 0 == opbits( 4, 1 ) ) ) // fccmp
            {
                char t = ( 0 == ftype ) ? 's' : ( 3 == ftype ) ? 'h' : ( 1 == ftype ) ? 'd' : '?';
                uint64_t m = opbits( 16, 5 );
                uint64_t cond = opbits( 12, 4 );
                uint64_t nzcv = opbits( 0, 4 );
                tracer.Trace( "fccmp %c%llu, %c%llu, #%#llx, %s\n", t, n, t, m, nzcv, get_cond( cond ) );
            }
            else if ( bit21 && ( 0xc0 == ( 0x1c0 & bits18_10 ) ) && 0 == rmode ) // UCVTF <Hd>, <Wn>, #<fbits>
            {
                char t = ( 0 == ftype ) ? 's' : ( 3 == ftype ) ? 'h' : ( 1 == ftype ) ? 'd' : '?';
                uint64_t scale = opbits( 10, 6 );
                uint64_t fbits = 64 - scale;
                tracer.Trace( "ucvtf %c%llu, %s, #%#llx\n", t, d, reg_or_zr( n, sf ), fbits );
            }
            else
            {
                tracer.Trace( "ftype %llu, bit21 %llu, rmode %llu, bits18_10 %#llx\n", ftype, bit21, rmode, bits18_10 );
                unhandled();
            }
            break;
        }
        case 0x4c: // LD1 { <Vt>.<T> }, [<Xn|SP>]    ;    LD2 { <Vt>.<T>, <Vt2>.<T> }, [<Xn|SP>]
                   // ST2 { <Vt>.<T>, <Vt2>.<T> }, [<Xn|SP>]    ;    ST2 { <Vt>.<T>, <Vt2>.<T> }, [<Xn|SP>], <imm>    ;    ST2 { <Vt>.<T>, <Vt2>.<T> }, [<Xn|SP>], <Xm>
                   // LD3 { <Vt>.<T>, <Vt2>.<T>, <Vt3>.<T> }, [<Xn|SP>]
                   // LD3 { <Vt>.<T>, <Vt2>.<T>, <Vt3>.<T> }, [<Xn|SP>], <imm>
                   // LD3 { <Vt>.<T>, <Vt2>.<T>, <Vt3>.<T> }, [<Xn|SP>], <Xm>
                   // LD4 { <Vt>.<T>, <Vt2>.<T>, <Vt3>.<T>, <Vt4>.<T> }, [<Xn|SP>]
                   // LD4 { <Vt>.<T>, <Vt2>.<T>, <Vt3>.<T>, <Vt4>.<T> }, [<Xn|SP>], <imm>
                   // LD4 { <Vt>.<T>, <Vt2>.<T>, <Vt3>.<T>, <Vt4>.<T> }, [<Xn|SP>], <Xm>
        {
            uint64_t Q = opbits( 30, 1 );
            uint64_t L = opbits( 22, 1 ); // load vs. store
            uint64_t post_index = opbits( 23, 1 );
            uint64_t opcode = opbits( 12, 4 );
            uint64_t size = opbits( 10, 2 );
            uint64_t bits23_21 = opbits( 21, 3 );
            uint64_t m = opbits( 16, 5 );
            uint64_t n = opbits( 5, 5 );
            uint64_t t = opbits( 0, 5 );

            if ( 2 != bits23_21 && 6 != bits23_21 && 0 != bits23_21 )
                unhandled();

            const char * pname = L ? "ld" : "st";

            if ( ( 2 & opcode ) || 8 == opcode || 4 == opcode || 0 == opcode ) // LD1 / LD2 / LD3 / LD4 / ST1 / ST2 / ST3 / ST4
            {
                uint64_t t2 = ( t + 1 ) % 32;
                if ( post_index )
                {
                    if ( 31 == m )
                    {
                        const char * pT = get_ld1_vector_T( size, Q );
                        if ( 7 == opcode ) // LD1 { <Vt>.<T> }, [<Xn|SP>], <imm>
                            tracer.Trace( "%s1 {v%llu.%s}, [%s], #%llu\n", pname, t, pT, reg_or_sp( n, true ), Q ? 16 : 8 );
                        else if ( 8 == opcode ) // LD2 { <Vt>.<T>, <Vt2>.<T> }, [<Xn|SP>], <imm>
                            tracer.Trace( "%s2 {v%llu.%s, v%llu.%s}, [%s], #%llu\n", pname, t, pT, t2, pT, reg_or_sp( n, true ), Q ? 32llu : 16llu );
                        else if ( 3 == opcode ) // LD3 { <Vt>.<T>, <Vt2>.<T>, <Vt3>.<T>, <Vt4>.<T> }, [<Xn|SP>], <imm>
                            tracer.Trace( "%s3 {v%llu.%s-v%llu.%s}, [%s], #%llu\n", pname, t, pT, ( t + 2 ) % 32, pT, reg_or_sp( n, true ), Q ? 64llu : 32llu );
                        else if ( 0 == opcode ) // LD4 { <Vt>.<T>, <Vt2>.<T>, <Vt3>.<T>, <Vt4>.<T> }, [<Xn|SP>], <imm>
                            tracer.Trace( "%s4 {v%llu.%s-v%llu.%s}, [%s], #%llu\n", pname, t, pT, ( t + 3 ) % 32, pT, reg_or_sp( n, true ), Q ? 64llu : 32llu );
                        else
                            unhandled();
                    }
                    else
                    {
                        unhandled();
                    }
                }
                else // no offset
                {
                    if ( 0 == m )
                    {
                        const char * pT = get_ld1_vector_T( size, Q );
                        if ( 7 == opcode ) // LD1 { <Vt>.<T> }, [<Xn|SP>]
                            tracer.Trace( "%s1 {v%llu.%s}, [%s]\n", pname, t, pT, reg_or_sp( n, true ) );
                        else if ( 10 == opcode ) // LD1 { <Vt>.<T>, <Vt2>.<T> }, [<Xn|SP>]
                            tracer.Trace( "%s1 {v%llu.%s}, {v%llu.%s}, [%s]\n", pname, t, pT, t2, pT, reg_or_sp( n, true ) );
                        else if ( 8 == opcode ) // LD2 { <Vt>.<T>, <Vt2>.<T> }, [<Xn|SP>]
                            tracer.Trace( "%s2 { v%llu.%s, %llu.%s }, [%s]\n", pname, t, pT, t2, pT, reg_or_sp( n, true ) );
                        else if ( 4 == opcode ) // LD3 { <Vt>.<T>, <Vt2>.<T> }, [<Xn|SP>]
                            tracer.Trace( "%s3 { v%llu.%s-v%llu.%s }, [%s]\n", pname, t, pT, ( t + 2 ) % 32, pT, reg_or_sp( n, true ) );
                        else if ( 0 == opcode ) // LD4 { <Vt>.<T>-<Vtn>.<T> }, [<Xn|SP>]
                            tracer.Trace( "%s4 { v%llu.%s-v%llu.%s }, [%s]\n", pname, t, pT, ( t + 3 ) % 32, pT, reg_or_sp( n, true ) );
                        else
                            unhandled();
                    }
                    else
                        unhandled();
                }
            }
            else if ( 0 == opcode && 0 == opbits( 12, 9 ) ) // LD4 multiple structures
            {
                if ( 2 == bits23_21 ) // no offset LD4 { <Vt>.<T>, <Vt2>.<T>, <Vt3>.<T>, <Vt4>.<T> }, [<Xn|SP>]
                {
                    const char * pT = get_ld1_vector_T( size, Q );
                    tracer.Trace( "ld4 {v%llu.%s-v%llu.%s}, [%s]\n", t, pT, ( t + 3 ) % 32, pT, reg_or_sp( n , true ) );
                }
                else if ( 6 == bits23_21 ) // post-index
                    unhandled();
                else
                    unhandled();
            }
            else
                unhandled();
            break;
        }
        case 0x88: // LDAXR <Wt>, [<Xn|SP>{, #0}]    ;    LDXR <Wt>, [<Xn|SP>{, #0}]    ;    STXR <Ws>, <Wt>, [<Xn|SP>{, #0}]    ;    STLXR <Ws>, <Wt>, [<Xn|SP>{, #0}]
                   //                                                                        STLR <Wt>, [<Xn|SP>{, #0}]          ;    STLR <Wt>, [<Xn|SP>, #-4]!
        case 0xc8: // LDAXR <Xt>, [<Xn|SP>{, #0}]    ;    LDXR <Xt>, [<Xn|SP>{, #0}]    ;    STXR <Ws>, <Xt>, [<Xn|SP>{, #0}]    ;    STLXR <Ws>, <Xt>, [<Xn|SP>{, #0}]
                   //                                                                        STLR <Xt>, [<Xn|SP>{, #0}]          ;    STLR <Xt>, [<Xn|SP>, #-8]!
        {
            uint64_t t = opbits( 0, 5 );
            uint64_t n = opbits( 5, 5 );
            uint64_t t2 = opbits( 10, 5 );
            uint64_t s = opbits( 16, 5 );
            uint64_t L = opbits( 21, 2 );
            uint64_t oO = opbits( 15, 1 );
            uint64_t bit23 = opbits( 23, 1 );
            uint64_t bit30 = opbits( 30, 1 );

            if ( 0x1f != t2 )
                unhandled();

            if ( 0 == L ) // stxr, stlr
            {
                if ( bit23 )
                    tracer.Trace( "stlr %s, [%s]\n", reg_or_zr( t, bit30 ), reg_or_sp( n, bit30 ) );
                else
                    tracer.Trace( "%s %s, %s, [ %s ]\n", ( 1 == oO ) ? "stlxr" : "stxr", reg_or_zr( s, false ), reg_or_zr2( t, ( 0xc8 == hi8 ) ), reg_or_sp( n, true ) );
            }
            else if ( 2 == L ) // ldxr and ldaxr
            {
                if ( 0x1f != s )
                    unhandled();
                tracer.Trace( "%s %s, [ %s ]\n", ( 1 == oO ) ? "ldaxr" : "ldxr", reg_or_zr( t, ( 0xc8 == hi8 ) ), reg_or_sp( n, true ) );
            }
            break;
        }
        case 0xd6: // BLR <Xn>    ;   RET    ;    BR <Xn>
        {
            uint64_t n = opbits( 5, 5 );
            uint64_t theop = opbits( 21, 2 );
            uint64_t bit23 = opbits( 23, 1 );
            uint64_t op2 = opbits( 12, 9 );
            uint64_t A = opbits( 11, 1 );
            uint64_t M = opbits( 10, 1 );
            if ( 0 != bit23 )
                unhandled();
            if ( 0x1f0 != op2 )
                unhandled();
            if ( ( 0 != A ) || ( 0 != M ) )
                unhandled();

            if ( 0 == theop ) // br
                tracer.Trace( "br x%llu\n", n );
            else if ( 1 == theop ) // blr
                tracer.Trace( "blr x%llu\n", n );
            else if ( 2 == theop ) // ret
                tracer.Trace( "ret x%llu\n", n );
            else
                unhandled();
            break;
        }
        case 0x72: // MOVK <Wd>, #<imm>{, LSL #<shift>}       ;  ANDS <Wd>, <Wn>, #<imm>
        case 0xf2: // MOVK <Xd>, #<imm>{, LSL #<shift>}       ;  ANDS <Xd>, <Xn>, #<imm>
        {
            uint64_t xregs = ( 0 != ( 0x80 & hi8 ) );
            uint64_t bit23 = opbits( 23, 1 ); // 1 for MOVK, 0 for ANDS
            if ( bit23 ) // MOVK
            {
                uint64_t hw = ( ( op >> 21 ) & 3 );
                uint64_t pos = ( hw << 4 );
                uint64_t imm16 = ( ( op >> 5 ) & 0xffff );
                uint64_t d = ( op & 0x1f );
                tracer.Trace( "movk %s, #%#llx, LSL #%llu\n", reg_or_zr( d, xregs ), imm16, pos );
            }
            else // ANDS
            {
                uint64_t N_immr_imms = opbits( 10, 13 );
                uint64_t op2 = decode_logical_immediate( N_immr_imms, xregs ? 64 : 32 );
                uint64_t n = ( ( op >> 5 ) & 0x1f );
                uint64_t d = ( op & 0x1f );
                tracer.Trace( "ands %s, %s, #%#llx\n", reg_or_zr( d, xregs ), reg_or_zr2( n, xregs ), op2 );
            }
            break;
        }
        case 0x38: // B
        case 0x78: // H
        case 0xb8: // W
        case 0xf8: // X
        {
            // LDR <Xt>, [<Xn|SP>, (<Wm>|<Xm>){, <extend> {<amount>}}]         
            // LDR <Xt>, [<Xn|SP>], #<simm>
            // LDR <Xt>, [<Xn|SP>, #<simm>]!
            // STR <Xt>, [<Xn|SP>, (<Wm>|<Xm>){, <extend> {<amount>}}]
            // STR <Xt>, [<Xn|SP>], #<simm>
            // STR <Xt>, [<Xn|SP>, #<simm>]!
            // W, H and B variants use <Wt> as the first argument
            // H and B variants use LDRH, STRH, LDRB, STRB instructions. W and X variants use STR and LDR
            // LDR has sign-extend LDRSx and LDURSW variants

            uint64_t opc = opbits( 21, 3 );
            uint64_t n = opbits( 5, 5 );
            uint64_t t = opbits( 0, 5 );
            bool xregs = ( 0 != opbits( 30, 1 ) );

            const char * suffix = "";
            if ( 0x38 == hi8 )
                suffix = "b";
            else if ( 0x78 == hi8 )
                suffix = "h";

            char prefix = 'w';
            if ( xregs )
                prefix = 'x';

            if ( 0 == opc ) // str (immediate) post-index and pre-index
            {
                uint64_t unsigned_imm9 = opbits( 12, 9 );
                int64_t extended_imm9 = sign_extend( unsigned_imm9, 8 );
                uint64_t option = opbits( 10, 2 );
                if ( 0 == option) // // STUR <Xt>, [<Xn|SP>{, #<simm>}]
                    tracer.Trace( "stur%s %s, %s, #%lld // so\n", suffix, reg_or_zr( t, xregs ), reg_or_sp( n, xregs ), extended_imm9 );
                else if ( 1 == option) // post-index STR <Xt>, [<Xn|SP>], #<simm>
                    tracer.Trace( "str%s %c%llu, %s, #%lld // po\n", suffix, prefix, t, reg_or_sp( n, true ), extended_imm9 );
                else if ( 3 == option ) // pre-index STR <Xt>, [<Xn|SP>, #<simm>]!
                    tracer.Trace( "str%s %c%llu, [%s, #%lld]! //pr\n", suffix, prefix, t, reg_or_sp( n, true ), extended_imm9 );
                else
                    unhandled();
            }
            else if ( 1 == opc ) // STR <Xt>, [<Xn|SP>, (<Wm>|<Xm>){, <extend> {<amount>}}]
            {
                uint64_t m = opbits( 16, 5 );
                uint64_t shift = opbits( 12, 1 );
                uint64_t option = opbits( 13, 3 );
                tracer.Trace( "str%s %s, [ %s, x%llu, %s #%u]\n", suffix, reg_or_zr( t, xregs ), reg_or_sp( n, true ), m, extend_type( option ), ( 3 == option ) ? 0 : ( 0 == shift ) ? 0 : xregs ? 3 : 2 );
            }
            else if ( 2 == opc ) // ldr (immediate)
            {
                uint64_t unsigned_imm9 = opbits( 12, 9 );
                int64_t extended_imm9 = sign_extend( unsigned_imm9, 8 );
                uint64_t option = opbits( 10, 2 );
                if ( 0 == option) // LDUR <Xt>, [<Xn|SP>{, #<simm>}]
                    tracer.Trace( "ldur%s %c%llu, [%s, #%lld] //so\n", suffix, prefix, t, reg_or_sp( n, true ), extended_imm9 );
                else if ( 1 == option) // post-index LDR <Xt>, [<Xn|SP>], #<simm>
                    tracer.Trace( "ldr%s %c%llu, [%s], #%lld //po\n", suffix, prefix, t, reg_or_sp( n, true ), extended_imm9 );
                else if ( 3 == option ) // pre-index LDR <Xt>, [<Xn|SP>, #<simm>]!
                    tracer.Trace( "ldr%s %c%llu, [%s, #%lld]! //pr\n", suffix, prefix, t, reg_or_sp( n, true ), extended_imm9 );
                else
                    unhandled();
            }
            else if ( 3 == opc ) // LDR <Xt>, [<Xn|SP>, (<Wm>|<Xm>){, <extend> {<amount>}}]
            {
                uint64_t m = opbits( 16, 5 );
                uint64_t shift = opbits( 12, 1 );
                uint64_t option = opbits( 13, 3 );
                tracer.Trace( "ldr%s %s, [%s, %s, %s #%u]\n", suffix, reg_or_zr( t, xregs ), reg_or_sp( n, true ), reg_or_zr2( m, true ),
                              extend_type( option ), ( 3 == option ) ? 0 : ( 0 == shift ) ? 0 : xregs ? 3 : 2 );
            }
            else if ( 4 == opc || 6 == opc ) // LDRSW <Xt>, [<Xn|SP>], #<simm>    ;    LDRSW <Xt>, [<Xn|SP>, #<simm>]!
            {
                uint64_t bits11_10 = opbits( 10, 2 );
                if ( 0 == bits11_10 ) // LDURSB <Wt>, [<Xn|SP>{, #<simm>}]    ;    LDURSB <Xt>, [<Xn|SP>{, #<simm>}]
                {
                    bool isx = ( 0 != opbits( 22, 1 ) );
                    int64_t imm9 = sign_extend( opbits( 12, 9 ), 8 );
                    tracer.Trace( "ldurs%s %s, [%s, #%lld]\n", suffix, reg_or_zr( t, isx ), reg_or_sp( n, true ), imm9 );
                }
                else
                {
                    uint64_t preindex = opbits( 11, 1 ); // 1 for pre, 0 for post increment
                    int64_t imm9 = sign_extend( opbits( 12, 9 ), 8 );
                    xregs = ( 4 == opc );
                    if ( preindex )
                        tracer.Trace( "ldrs%s %s [%s, #%lld]! // pr\n", suffix, reg_or_zr( t, xregs ), reg_or_sp( n, true ), imm9 );
                    else
                        tracer.Trace( "ldrs%s %s [%s], #%lld // po\n", suffix, reg_or_zr( t, xregs ), reg_or_sp( n, true ), imm9 );
                }
            }
            else if ( 5 == opc  || 7 == opc ) // hi8 = 0x78
                                              //     (opc == 7)                  LDRSH <Wt>, [<Xn|SP>, (<Wm>|<Xm>){, <extend> {<amount>}}]
                                              //     (opc == 5)                  LDRSH <Xt>, [<Xn|SP>, (<Wm>|<Xm>){, <extend> {<amount>}}]
                                              // hi8 = 0x38
                                              //     (opc == 7 && option != 011) LDRSB <Wt>, [<Xn|SP>, (<Wm>|<Xm>), <extend> {<amount>}]
                                              //     (opc == 5 && option != 011) LDRSB <Xt>, [<Xn|SP>, (<Wm>|<Xm>), <extend> {<amount>}]
                                              //     (opc == 7 && option == 011) LDRSB <Wt>, [<Xn|SP>, <Xm>{, LSL <amount>}]
                                              //     (opc == 5 && option == 011) LDRSB <Xt>, [<Xn|SP>, <Xm>{, LSL <amount>}]
                                              // hi8 == 0xb8
                                              //     (opc == 5 && option = many) LDRSW <Xt>, [<Xn|SP>, (<Wm>|<Xm>){, <extend> {<amount>}}]
            {
                uint64_t m = opbits( 16, 5 );
                uint64_t shift = opbits( 12, 1 );
                uint64_t option = opbits( 13, 3 );
                bool mIsX = ( 1 == ( option & 1 ) );
                bool tIsX = ( 5 == opc );

                if ( 0xb8 == hi8 )
                    tracer.Trace( "ldrsw %s, [%s, %s, %s, %llu]\n", reg_or_zr( t, true ), reg_or_sp( n, true ), reg_or_zr2( m, option & 1 ), extend_type( option ), shift ? 2 : 0 );
                else if ( 0x38 == hi8 )
                {
                    if ( 3 == option )
                        tracer.Trace( "ldrsb %s, [%s, x%llu {, LSL %u}]\n", reg_or_zr( t, tIsX ), reg_or_sp( n, true ), m, shift );
                    else
                        tracer.Trace( "ldrsb %s, [%s, %s, %s {#%llu}]\n", reg_or_zr( t, tIsX ), reg_or_sp( n, true ), reg_or_zr2( m, mIsX ), extend_type( option ), shift );
                }
                else if ( 0x78 == hi8 )
                    tracer.Trace( "ldrsh %s, [%s, %s {, %s #%llu}]\n", reg_or_zr( t, tIsX ), reg_or_sp( n, true ),
                                  reg_or_zr2( m, mIsX ), extend_type( option ), shift );
                else
                    unhandled();
            }
            else
                unhandled();
            
            break;
        }
        case 0x39: // B
        case 0x79: // H                              ;    LDRSH <Wt>, [<Xn|SP>{, #<pimm>}]
        case 0xb9: // W
        case 0xf9: // X ldr + str unsigned offset    ;    LDRSW <Xt>, [<Xn|SP>{, #<pimm>}]
        {
            // LDR <Xt>, [<Xn|SP>{, #<pimm>}]
            // STR <Xt>, [<Xn|SP>{, #<pimm>}]

            uint64_t opc = opbits( 22, 2 );
            uint64_t imm12 = opbits( 10, 12 );
            uint64_t lsl = opbits( 30, 2 );
            imm12 <<= lsl;
            uint64_t t = opbits( 0, 5 );
            uint64_t n = opbits( 5, 5 );

            const char * suffix = "";
            if ( 0x39 == hi8 )
                suffix = "b";
            else if ( 0x79 == hi8 )
                suffix = "h";

            char prefix = 'w';
            if ( 0xf9 == hi8 )
                prefix = 'x';

            if ( 0 == opc )
                tracer.Trace( "str%s %s, [%s,#%llu] //uo\n", suffix, reg_or_zr( t, ( 0xf9 == hi8 ) ), reg_or_sp( n, true ), imm12 );
            else if ( 1 == opc )
                tracer.Trace( "ldr%s %c%llu, [%s,#%llu] //uo\n", suffix, prefix, t, reg_or_sp( n, true ), imm12 );
            else if ( 2 == opc || 3 == opc )
                tracer.Trace( "ldrs%s %c%llu, [%s,#%llu] //uo\n", suffix, prefix, t, reg_or_sp( n, true ), imm12 );
            else
                unhandled();

            break;
        }
        default:
            unhandled();
    }

    static char acregs[ 32 * 32 + 10 ]; // way too much.
    acregs[ 0 ] = 0;
    int len = 0;
    for ( int r = 0; r < 31; r++ )
        if ( 0 != regs[ r ] )
            len += snprintf( & acregs[ len ], 32, "%u:%llx ", r, regs[ r ] );
    len += snprintf( &acregs[ len ], 32, "sp:%llx", regs[ 31 ] );
    tracer.Trace( "               %s\n", acregs );
} //trace_state

// N (negative): Set if the result is negative
// Z (zero): Set if the result is zero
// C (carry): Set if the result cannot be represented as an unsigned integer
// V (overflow): Set if the result cannot be represented as a signed integer 

uint64_t Arm64::add_with_carry64( uint64_t x, uint64_t y, bool carry, bool setflags )
{
    uint64_t result = x + y + (uint64_t) carry;

    if ( setflags )
    {
        fN = ( (int64_t) result < 0 );
        fZ = ( 0 == result );

        // strangely literal fC computation; there must be a faster way

        uint64_t uy = y + (uint64_t) carry;
        uint64_t u_low = ( ( x & 0xffffffff ) + ( uy & 0xffffffff ) );
        uint64_t u_low_carry = ( u_low >> 32 );
        uint64_t carry_carry = ( 0xffffffffffffffff == y && carry ) ? 1 : 0;
        uint64_t u_hi = ( ( x >> 32 ) + ( uy >> 32 ) + u_low_carry + carry_carry );
        uint64_t u_sum = ( u_hi << 32 ) | ( 0xffffffff & u_low );
        fC = ( ( result != u_sum ) || ( 0 != ( u_hi >> 32 ) ) );

        int64_t ix = (int64_t) x;
        int64_t iy = (int64_t) y;
        int64_t iresult = (int64_t) result;
        fV = ( ( ( ix >= 0 && iy >= 0 ) && ( iresult < ix || iresult < iy ) ) ||
               ( ( ix < 0 && iy < 0 ) && ( iresult > ix || iresult > iy ) ) );
    }
    return result;
} //add_with_carry64

uint64_t Arm64::sub64( uint64_t x, uint64_t y, bool setflags )
{
    return add_with_carry64( x, ~y, true, setflags );
} //sub64

uint32_t Arm64::add_with_carry32( uint32_t x, uint32_t y, bool carry, bool setflags )
{
    uint64_t unsigned_sum = (uint64_t) x + (uint64_t) y + (uint64_t) carry;
    uint32_t result = (uint32_t) ( unsigned_sum & 0xffffffff );

    if ( setflags )
    {
        // this method of setting flags is as the Arm documentation suggests
        fN = ( (int32_t) result < 0 );
        fZ = ( 0 == result );
        fC = ( (uint64_t) result != unsigned_sum );
        int64_t signed_sum = (int64_t) (int32_t) x + (int64_t) (int32_t) y + (uint64_t) carry;
        fV = ( (int64_t) (int32_t) result != signed_sum );
    }
    return result;
} //add_with_carry32

uint32_t Arm64::sub32( uint32_t x, uint32_t y, bool setflags )
{
    return add_with_carry32( x, ~y, true, setflags );
} //sub32

uint64_t Arm64::shift_reg64( uint64_t reg, uint64_t shift_type, uint64_t amount )
{
    uint64_t val = ( 31 == reg ) ? 0 : regs[ reg ];
    amount &= 0x7f;
    if ( 0 == amount )
        return val;

    if ( 0 == shift_type ) // lsl
        val <<= amount;
    else if ( 1 == shift_type ) // lsr
        val >>= amount;
    else if ( 2 == shift_type ) // asr
        val = (uint64_t) ( ( (int64_t) val ) >> amount ); // modern C compilers do the right thing
    else if ( 3 == shift_type ) // ror.
        val = ( ( val >> amount ) | ( val << ( 64 - amount ) ) );
    else
        unhandled();

    return val;
} //shift_reg64

uint32_t Arm64::shift_reg32( uint64_t reg, uint64_t shift_type, uint64_t amount )
{
    uint32_t val = ( 31 == reg ) ? 0 : ( regs[ reg ] & 0xffffffff );
    amount &= 0x3f;
    if ( 0 == amount )
        return val;

    if ( 0 == shift_type ) // lsl
        val <<= amount;
    else if ( 1 == shift_type ) // lsr
        val >>= amount;
    else if ( 2 == shift_type ) // asr
        val = (uint32_t) ( (int32_t) val >> amount ); // modern C compilers do the right thing
    else if ( 3 == shift_type ) // ror.
        val = ( ( val >> amount ) | ( val << ( 32 - amount ) ) );
    else
        unhandled();

    return val;
} //shift_reg32

bool Arm64::check_conditional( uint64_t cond )
{
    bool met = false;
    uint64_t chk = ( ( cond >> 1 ) & 7 ); // switch on bits 4..1

    switch ( chk )
    {
        case 0: { met = fZ; break; }                      // EQ or NE    EQ = Zero / Equal.                  NE = Not Equal
        case 1: { met = fC; break; }                      // CS or CC    CS = Carry Set.                     CC = Carry Clear
        case 2: { met = fN; break; }                      // MI or PL    MI = Minus / Negative.              PL = Plus. Positive or Zero
        case 3: { met = fV; break; }                      // VS or VC    VS = Overflow Set.                  VC = Overflow Clear
        case 4: { met = ( fC && !fZ ); break; }           // HI or LS    HI = Unsigned Higher.               LS = Lower or Same
        case 5: { met = ( fN == fV ); break; }            // GE or LT    GE = Signed Greater Than or Equal.  LT = Signed Less Than
        case 6: { met = ( ( fN == fV ) && !fZ ); break; } // GT or LE    GT = Signed Greater Than.           LE = Signed Less Than or Equal
        default: { return true; }                         // AL, regardless of low bit. not used in practice
    }

    if ( 0 != ( 1 & cond ) ) // invert if the low bit is set.
        met = !met;
    return met;
} //check_conditional

void Arm64::set_flags_from_double( double result )
{
    if ( isnan( result ) )
    {
        fN = fZ = false;
        fC = fV = true;
    }
    else if ( 0.0 == result )
    {
        fN = fV = false;
        fZ = fC = true;
    }
    else if ( result < 0.0 )
    {
        fN = true;
        fZ = fC = fV = false;
    }
    else
    {
        fN = fZ = fV = false;
        fC = true;
    }
} //set_flags_from_double

void Arm64::trace_vregs()
{
    if ( ! ( g_State & stateTraceInstructions ) )
        return;

    vec16_t v = {0};
    for ( uint64_t i = 0; i < _countof( vregs ); i++ )
    {
        if ( memcmp( &v, & ( vregs[ i ].b16 ), sizeof( v ) ) )
        {
            tracer.Trace( "    vreg %2llu: ", i );
            tracer.TraceBinaryData( vregs[ i ].b16, 16, 4 );
        }
    }
} //trace_vregs

#ifdef _WIN32
__declspec(noinline)
#endif
void Arm64::unhandled()
{
    arm64_hard_termination( *this, "opcode not handled:", op );
} //unhandled

uint64_t Arm64::run( uint64_t max_cycles )
{
    uint64_t start_cycles = cycles_so_far;
    uint64_t target_cycles = cycles_so_far + max_cycles;

    do
    {
        #ifndef NDEBUG
            if ( regs[ 31 ] <= ( stack_top - stack_size ) )
                arm64_hard_termination( *this, "stack pointer is below stack memory:", regs[ 31 ] );

            if ( regs[ 31 ] > stack_top )
                arm64_hard_termination( *this, "stack pointer is above the top of its starting point:", regs[ 31 ] );

            if ( pc < base )
                arm64_hard_termination( *this, "pc is lower than memory:", pc );

            if ( pc >= ( base + mem_size - stack_size ) )
                arm64_hard_termination( *this, "pc is higher than it should be:", pc );

            if ( 0 != ( regs[ 31 ] & 0xf ) ) // by convention, arm64 stacks are 16-byte aligned
                arm64_hard_termination( *this, "the stack pointer isn't 16-byte aligned:", regs[ 31 ] );
        #endif

        op = getui32( pc );

        if ( 0 != g_State )
        {
            if ( g_State & stateEndEmulation )
            {
                g_State &= ~stateEndEmulation;
                break;
            }

            if ( g_State & stateTraceInstructions )
                trace_state();
        }

        uint8_t hi8 = (uint8_t) ( op >> 24 );
        switch ( hi8 )
        {
            case 0: // UDF
            {
                uint64_t bits23to16 = opbits( 16, 8 );
                if ( 0 == bits23to16 )
                {
                    uint64_t imm16 = op & 0xffff;
                    arm64_hard_termination( *this, "permanently undefined instruction encountered", imm16 );
                }
                else
                    unhandled();
                break;
            }
            case 0x0d: case 0x4d: // LD1 { <Vt>.B }[<index>], [<Xn|SP>]    ;    LD1 { <Vt>.B }[<index>], [<Xn|SP>], #1
                                  // LD1R { <Vt>.<T> }, [<Xn|SP>], <imm>   ;    LD1R { <Vt>.<T> }, [<Xn|SP>], <Xm>
                                  // ST1 { <Vt>.B }[<index>], [<Xn|SP>]    ;    ST1 { <Vt>.B }[<index>], [<Xn|SP>], #1
            {
                uint64_t R = opbits( 21, 1 );
                if ( R )
                    unhandled();
                uint64_t post_index = opbits( 23, 1 );
                uint64_t opcode = opbits( 13, 3 );
                uint64_t bit13 = opbits( 13, 1 );
                if ( bit13 )
                    unhandled();
                uint64_t size = opbits( 10, 2 );
                uint64_t n = opbits( 5, 5 );
                uint64_t m = opbits( 16, 5 );
                uint64_t t = opbits( 0, 5 );
                uint64_t S = opbits( 12, 1 );
                uint64_t Q = opbits( 30, 1 );
                uint64_t L = opbits( 22, 1 );
                uint64_t index = 0;
                uint64_t replicate = opbits( 14, 1 );
                uint64_t scale = get_bits( opcode, 1, 2 );
                if ( 3 == scale )
                    scale = size;
                else if ( 0 == scale )
                    index = ( Q << 3 ) | ( S << 2 ) | size;
                else if ( 1 == scale )
                    index = ( Q << 2 ) | ( S << 1 ) | get_bits( size, 1, 1 );
                else if ( 2 == scale )
                {
                    if ( 0 == ( size & 1 ) )
                        index = ( Q << 1 ) | S;
                    else
                    {
                        index = Q;
                        scale = 3;
                    }
                }
    
                uint64_t esize = 8ull << scale;
                uint64_t ebytes = esize / 8;
                uint64_t offs = 0;
                uint64_t selem = ( ( opcode & 1 ) << 1 ) + 1;
                uint64_t nval = regs[ n ];

                if ( replicate )
                {
                    if ( !L )
                        unhandled();

                    for ( uint64_t e = 0; e < selem; e++ )
                    {
                        uint64_t eaddr = nval + offs;
                        uint64_t element = 0;
                        memcpy( &element, getmem( eaddr ), ebytes );
                        element = replicate_bytes( element, ebytes );
                        vreg_setui64( t, 0, element );
                        vreg_setui64( t, 8, Q ? element : 0 );
                        offs += ebytes;
                        t = ( ( t + 1 ) % 32 );
                    }
                }
                else
                {
                    for ( uint64_t e = 0; e < selem; e++ )
                    {
                        uint64_t eaddr = nval + offs;
                        if ( L )
                            memcpy( vreg_ptr( t, index * ebytes ), getmem( eaddr ), ebytes );
                        else
                            memcpy( getmem( eaddr ), vreg_ptr( t, index * ebytes ), ebytes );
                        offs += ebytes;
                        t = ( ( t + 1 ) % 32 );
                    }

                }

                if ( 31 != m )
                    offs = regs[ m ];

                if ( post_index )
                    regs[ n ] += offs;

                trace_vregs();
                break;
            }
            case 0x08: // LDAXRB <Wt>, [<Xn|SP>{, #0}]    ;    LDARB <Wt>, [<Xn|SP>{, #0}]    ;    STLXRB <Ws>, <Wt>, [<Xn|SP>{, #0}]    ; 
                       // STXRB <Ws>, <Wt>, [<Xn|SP>{, #0}] ;  LDXRB <Wt>, [<Xn|SP>{, #0}]
            case 0x48: // LDAXRH <Wt>, [<Xn|SP>{, #0}]    ;    LDARH <Wt>, [<Xn|SP>{, #0}]    ;    STLXRH <Ws>, <Wt>, [<Xn|SP>{, #0}]    ;    STLRH <Wt>, [<Xn|SP>{, #0}]
                       // STXRH <Ws>, <Wt>, [<Xn|SP>{, #0}] ;  LDXRH <Wt>, [<Xn|SP>{, #0}]
            {
                uint64_t bit23 = opbits( 23, 1 );
                uint64_t L = opbits( 22, 1 );
                uint64_t bit21 = opbits( 21, 1 );
                uint64_t s = opbits( 16, 5 );
                uint64_t t2 = opbits( 10, 5 );
                uint64_t n = opbits( 5, 5 );
                uint64_t t = opbits( 0, 5 );
                bool is16 = opbits( 30, 1 );
    
                if ( 0 != bit21 || 0x1f != t2 )
                    unhandled();
    
                if ( L )
                {
                    if ( 31 == t )
                        break;

                    if ( 0x1f != s )
                        unhandled();

                    if ( is16 )
                        regs[ t ] = getui16( regs[ n ] );
                    else
                        regs[ t ] = getui8( regs[ n ] );
                }
                else
                {
                    if ( !bit23 && 31 != s )
                        regs[ s ] = 0; // indicate the store succeeded for stlxrW and stxrW

                    if ( is16 )
                        setui16( regs[ n ], 0xffff & val_reg_or_zr( t ) );
                    else
                        setui8( regs[ n ], 0xff & val_reg_or_zr( t ) );
                }
                break;
            }
            case 0x1f: // fmadd, fnmadd, fmsub, fnmsub
            {
                uint64_t ftype = opbits( 22, 2 );
                uint64_t m = opbits( 16, 5 );
                uint64_t a = opbits( 10, 5 );
                uint64_t n = opbits( 5, 5 );
                uint64_t d = opbits( 0, 5 );
                uint64_t subtract = opbits( 15, 1 );
                uint64_t negate = opbits( 21, 1 );

                if ( 0 == ftype ) // float
                {
                    float product = vregs[ n ].f * vregs[ m ].f;
                    if ( subtract )
                    {
                        if ( negate )
                            vregs[ d ].f = product - vregs[ a ].f;
                        else
                            vregs[ d ].f = vregs[ a ].f - product;
                    }
                    else
                    {
                        if ( negate )
                            vregs[ d ].f = -product - vregs[ a ].f;
                        else
                            vregs[ d ].f = product + vregs[ a ].f;
                    }
                    memset( vreg_ptr( d, 4 ), 0, 12 );
                }
                else if ( 1 == ftype ) // double
                {
                    double product = vregs[ n ].d * vregs[ m ].d;
                    if ( subtract )
                    {
                        if ( negate )
                            vregs[ d ].d = product - vregs[ a ].d;
                        else
                            vregs[ d ].d = vregs[ a ].d - product;
                    }
                    else
                    {
                        if ( negate )
                            vregs[ d ].d = -product - vregs[ a ].d;
                        else
                            vregs[ d ].d = product + vregs[ a ].d;
                    }
                    memset( vreg_ptr( d, 8 ), 0, 8 );
                }
                else
                    unhandled();
                trace_vregs();
                break;
            }
            case 0x3c: // LDR <Bt>, [<Xn|SP>], #<simm>    ;    LDR <Bt>, [<Xn|SP>, #<simm>]!    ;    LDR <Qt>, [<Xn|SP>], #<simm>    ;     LDR <Qt>, [<Xn|SP>, #<simm>]!    ;    STUR <Bt>, [<Xn|SP>{, #<simm>}]
            case 0x3d: // LDR <Bt>, [<Xn|SP>{, #<pimm>}]  ;    LDR <Qt>, [<Xn|SP>{, #<pimm>}]
            case 0x7c: // LDR <Ht>, [<Xn|SP>], #<simm>    ;    LDR <Ht>, [<Xn|SP>, #<simm>]!    
            case 0x7d: // LDR <Ht>, [<Xn|SP>{, #<pimm>}]
            case 0xbc: // LDR <Wt>, [<Xn|SP>], #<simm>    ;    LDR <Wt>, [<Xn|SP>, #<simm>]!
            case 0xbd: // LDR <Wt>, [<Xn|SP>{, #<pimm>}]                                    
            case 0xfc: // LDR <Dt>, [<Xn|SP>], #<simm>    ;    LDR <Dt>, [<Xn|SP>, #<simm>]!    ;    STR <Dt>, [<Xn|SP>], #<simm>    ;    STR <Dt>, [<Xn|SP>, #<simm>]!
            case 0xfd: // LDR <Dt>, [<Xn|SP>{, #<pimm>}]  ;    STR <Dt>, [<Xn|SP>{, #<pimm>}]
            {
                uint64_t bits11_10 = opbits( 10, 2 );
                uint64_t bit21 = opbits( 21, 1 );
                bool unsignedOffset = ( 0xd == ( hi8 & 0xf ) );
                bool preIndex = ( ( 0xc == ( hi8 & 0xf ) ) && ( 3 == bits11_10 ) );
                bool postIndex = ( ( 0xc == ( hi8 & 0xf ) ) && ( 1 == bits11_10 ) );
                bool signedUnscaledOffset = ( ( 0xc == ( hi8 & 0xf ) ) && ( 0 == bits11_10 ) );
                bool shiftExtend = ( ( 0xc == ( hi8 & 0xf ) ) && ( bit21 ) && ( 2 == bits11_10 ) );
                uint64_t imm12 = opbits( 10, 12 );
                int64_t imm9 = sign_extend( opbits( 12, 9 ), 8 );
                uint64_t size = opbits( 30, 2 );
                uint64_t opc = opbits( 22, 2 );
                bool is_ldr = opbits( 22, 1 );
                uint64_t t = opbits( 0, 5 );
                uint64_t n = opbits( 5, 5 );
                uint64_t address = regs[ n ];
                uint64_t byte_len = 1ull << size;

                if ( ( is_ldr && ( 3 == opc ) ) || ( !is_ldr && ( 2 == opc ) ) )
                    byte_len = 16;

                //tracer.Trace( "hi8 %#x, str/ldr %d, preindex %d, postindex %d, signedUnscaledOffset %d, shiftExtend %d, imm9 %lld, imm12 %llu, n %llu, t %llu, byte_len %llu\n",
                //              hi8, is_ldr, preIndex, postIndex, signedUnscaledOffset, shiftExtend, imm9, imm12, n, t, byte_len );
    
                if ( preIndex )
                {
                     regs[ n ] += imm9;
                     address = regs[ n ];
                }
                else if ( unsignedOffset )
                    address += ( imm12 * byte_len );
                else if ( signedUnscaledOffset )
                    address += imm9;
                else if ( shiftExtend )
                {
                    uint64_t option = opbits( 13, 3 );
                    uint64_t m = opbits( 16, 5 );
                    uint64_t shift = 0;
                    uint64_t S = opbits( 12, 1 );
                    if ( 0 != S )
                    {
                        if ( is_ldr )
                        {
                            if ( 0 == size )
                            {
                               if ( 2 == opc )
                                   shift = 4;
                               else if ( 1 != opc )
                                   unhandled();
                            }
                            else if ( 1 == size && 1 == opc )
                                shift = 1;
                            else if ( 2 == size && 1 == opc )
                                shift = 2;
                            else if ( 3 == size && 1 == opc )
                                shift = 3;
                            else
                                unhandled();
                        }
                        else
                        {
                            if ( 0 == size )
                            {
                               if ( 2 == opc )
                                   shift = 4;
                               else if ( 0 != opc )
                                   unhandled();
                            }
                            else if ( 1 == size && 0 == opc )
                                shift = 1;
                            else if ( 2 == size && 0 == opc )
                                shift = 2;
                            else if ( 3 == size && 0 == opc )
                                shift = 3;
                            else
                                unhandled();
                        }
                    }
                    int64_t offset = extend_reg( m, option, shift );
                    address += offset;
                }
                else if ( !postIndex )
                    unhandled();

                if ( is_ldr )
                {
                    memset( vreg_ptr( t, 0 ), 0, sizeof( vec16_t ) );
                    memcpy( vreg_ptr( t, 0 ), getmem( address ), byte_len );
                }
                else
                    memcpy( getmem( address ), vreg_ptr( t, 0 ), byte_len );
    
                if ( postIndex )
                     regs[ n ] += imm9;

                trace_vregs();
                break;
            }
            case 0x2c: // STP <St1>, <St2>, [<Xn|SP>], #<imm>     ;    LDP <St1>, <St2>, 
            case 0x6c: // STP <Dt1>, <Dt2>, [<Xn|SP>], #<imm>     ;    LDP <Dt1>, <Dt2>, [<Xn|SP>], #<imm>
            case 0xac: // STP <Qt1>, <Qt2>, [<Xn|SP>], #<imm>          LDP <Qt1>, <Qt2>, [<Xn|SP>], #<imm>
            case 0x2d: // STP <St1>, <St2>, [<Xn|SP>, #<imm>]!    ;    STP <St1>, <St2>, [<Xn|SP>{, #<imm>}]    ;    LDP <St1>, <St2>, [<Xn|SP>, #<imm>]!    ;    LDP <St1>, <St2>, [<Xn|SP>{, #<imm>}]
            case 0x6d: // STP <Dt1>, <Dt2>, [<Xn|SP>, #<imm>]!    ;    STP <Dt1>, <Dt2>, [<Xn|SP>{, #<imm>}]    ;    LDP <Dt1>, <Dt2>, [<Xn|SP>, #<imm>]!    ;    LDP <Dt1>, <Dt2>, [<Xn|SP>{, #<imm>}]
            case 0xad: // STP <Qt1>, <Qt2>, [<Xn|SP>, #<imm>]!    ;    STP <Qt1>, <Qt2>, [<Xn|SP>{, #<imm>}]    ;    LDP <Qt1>, <Qt2>, [<Xn|SP>, #<imm>]!    ;    LDP <Qt1>, <Qt2>, [<Xn|SP>{, #<imm>}]
            {
                uint64_t opc = opbits( 30, 2 );
                uint64_t imm7 = opbits( 15, 7 );
                uint64_t t2 = opbits( 10, 5 );
                uint64_t n = opbits( 5, 5 );
                uint64_t t1 = opbits( 0, 5 );
                uint64_t L = opbits( 22, 1 );
                uint64_t bit23 = opbits( 23, 1 );
    
                bool preIndex = ( ( 0xd == ( hi8 & 0xf ) ) && bit23 );
                bool postIndex = ( ( 0xc == ( hi8 & 0xf ) ) && bit23 );
                bool signedOffset = ( ( 0xd == ( hi8 & 0xf ) ) && !bit23 );
    
                uint64_t scale = 2 + opc;
                int64_t offset = sign_extend( imm7, 6 ) << scale;
                uint64_t address = regs[ n ];
                uint64_t byte_len = 4ull << opc;
    
                if ( preIndex || signedOffset )
                    address += offset;

                if ( 1 == L ) // ldp
                {
                    memset( vreg_ptr( t1, 0 ), 0, sizeof( vec16_t ) );
                    memset( vreg_ptr( t2, 0 ), 0, sizeof( vec16_t ) );
                    memcpy( vreg_ptr( t1, 0 ), getmem( address ), byte_len );
                    memcpy( vreg_ptr( t2, 0 ), getmem( address + byte_len ), byte_len );
                }
                else // stp
                {
                    memcpy( getmem( address ), vreg_ptr( t1, 0 ), byte_len );
                    memcpy( getmem( address + byte_len ), vreg_ptr( t2, 0 ), byte_len );
                }

                if ( postIndex )
                    address += offset;

                if ( !signedOffset )
                    regs[ n ] = address;

                trace_vregs();
                break;
            }
            case 0x0f: case 0x2f: case 0x4f: case 0x6f: case 0x7f:
                // BIC <Vd>.<T>, #<imm8>{, LSL #<amount>}    ;    MOVI <Vd>.<T>, #<imm8>{, LSL #0}    ;    MVNI <Vd>.<T>, #<imm8>, MSL #<amount>
                // USHR <Vd>.<T>, <Vn>.<T>, #<shift>         ;    FMUL <Vd>.<T>, <Vn>.<T>, <Vm>.<Ts>[<index>]
                // FMOV <Vd>.<T>, #<imm>                     ;    FMOV <Vd>.<T>, #<imm>               ;    FMOV <Vd>.2D, #<imm>
                // USHLL{2} <Vd>.<Ta>, <Vn>.<Tb>, #<shift>   ;    SHRN{2} <Vd>.<Tb>, <Vn>.<Ta>, #<shift>
            {
                uint64_t cmode = opbits( 12, 4 );
                uint64_t abc = opbits( 16, 3 );
                uint64_t defgh = opbits( 5, 5 );
                uint64_t val = ( abc << 5 ) | defgh;
                uint64_t Q = opbits( 30, 1 );
                uint64_t bit29 = opbits( 29, 1 );
                uint64_t bit10 = opbits( 10, 1 );
                uint64_t bit11 = opbits( 11, 1 );
                uint64_t bit12 = opbits( 12, 1 );
                uint64_t bit13 = opbits( 13, 1 );
                uint64_t bit14 = opbits( 14, 1 );
                uint64_t bit15 = opbits( 15, 1 );
                uint64_t bit23 = opbits( 23, 1 );
                uint64_t d = opbits( 0, 5 );
                uint64_t bits23_19 = opbits( 19, 5 );
                uint64_t imm = adv_simd_expand_imm( bit29, cmode, val );

                if ( 0 == bits23_19 )
                {
                    if ( ( 0x2f == hi8 || 0x6f == hi8 ) && !bit11 && bit10 && // MOVI <Vd>.<T>, #<imm8>{, LSL #0}    ;    MVNI <Vd>.<T>, #<imm8>, MSL #<amount>
                         ( ( 8 == ( cmode & 0xd ) ) || ( 0 == ( cmode & 9 ) ) || ( 0xc == ( cmode & 0xf ) ) ) ) // mvni
                    {
                        if ( 8 == ( cmode & 0xd ) ) // 16-bit shifted immediate
                        {
                            uint64_t amount = get_bits( cmode, 1, 1 ) * 8;
                            val <<= amount;
                            uint16_t invval = (uint16_t) ~val;
                            for ( uint64_t o = 0; o < ( Q ? 16 : 8 ); o+= 2 )
                                vreg_setui16( d, o, invval );
                        }
                        else if ( 0 == ( cmode & 9 ) ) // 32-bit shifted immediate
                        {
                            uint64_t amount = get_bits( cmode, 1, 2 ) * 8;
                            val <<= amount;
                            uint32_t invval = (uint32_t) ~val;
                            for ( uint64_t o = 0; o < ( Q ? 16 : 8 ); o+= 4 )
                                vreg_setui32( d, o, invval );
                        }
                        else if ( 0xc == ( cmode & 0xf ) ) // 32-bit shifting ones
                        {
                            uint64_t invimm = (uint64_t) ~imm;
                            vreg_setui64( d, 0, invimm );
                            if ( Q )
                                vreg_setui64( d, 8, invimm );
                        }
                        else
                            unhandled();
                    }
                    else if ( !bit12|| ( 0xc == ( cmode & 0xe ) ) ) // movi
                    {
                        if ( !bit29)
                        {
                            if ( 0xe == cmode ) // 64-bit 
                            {
                                zero_vreg( d );
                                vreg_setui64( d, 0, imm );
                                if ( Q )
                                    vreg_setui64( d, 8, imm );
                            }
                            else if ( 8 == ( cmode & 0xd ) ) // 16-bit shifted immediate
                            {
                                uint64_t amount = ( cmode & 2 ) ? 8 : 0;
                                val <<= amount;
                                zero_vreg( d );
                                for ( uint64_t o = 0; o < ( Q ? 16 : 8 ); o += 2 )
                                    * (uint16_t *) vreg_ptr( d, o ) = (uint16_t) val;
                            }
                            else if ( 0 == ( cmode & 9 ) ) // 32-bit shifted immediate
                            {
                                uint64_t amount = ( 8 * ( ( cmode >> 1 ) & 3 ) );
                                val <<= amount;
                                val = replicate_bytes( val, 4 );
                                zero_vreg( d );
                                vreg_setui64( d, 0, val );
                                if ( Q )
                                    vreg_setui64( d, 8, val );
                            }
                            else if ( 0xa == ( cmode & 0xe ) )
                            {
                                //uint64_t amount = ( cmode & 1 ) ? 16 : 8;
                                unhandled();
                            }
                            else
                                unhandled();
                        }
                        else
                        {
                            uint64_t a = opbits( 18, 1 );
                            uint64_t b = opbits( 17, 1 );
                            uint64_t c = opbits( 16, 1 );
                            uint64_t dbit = opbits( 9, 1 );
                            uint64_t e = opbits( 8, 1 );
                            uint64_t f = opbits( 7, 1 );
                            uint64_t g = opbits( 6, 1 );
                            uint64_t h = opbits( 5, 1 );
        
                            imm = a ? ( 0xffull << 56 ) : 0;
                            imm |= b ? ( 0xffull << 48 ) : 0;
                            imm |= c ? ( 0xffull << 40 ) : 0;
                            imm |= dbit ? ( 0xffull << 32 ) : 0;
                            imm |= e ? ( 0xffull << 24 ) : 0;
                            imm |= f ? ( 0xffull << 16 ) : 0;
                            imm |= g ? ( 0xffull << 8 ) : 0;
                            imm |= h ? 0xffull : 0;
        
                            if ( ( 0 == Q ) && ( cmode == 0xe ) )
                                vreg_setui64( d, 0, imm );
                            else if ( ( 1 == Q ) && ( cmode == 0xe ) )
                            {
                                vreg_setui64( d, 0, imm );
                                vreg_setui64( d, 8, imm );
                            }
                            else
                                unhandled();
                        }
                    }
                    else if ( ( 0x6f == hi8 || 0x4f == hi8 || 0x2f == hi8 || 0x0f == hi8 ) && 0xf == cmode && !bit11 && bit10 ) // fmov single and double precision immediate
                    {
                        zero_vreg( d );
                        if ( bit29 )
                        {
                            vreg_setui64( d, 0, imm );
                            if ( Q )
                                vreg_setui64( d, 8, imm );
                        }
                        else
                        {
                            vreg_setui32( d, 0, (uint32_t) imm );
                            vreg_setui32( d, 4, (uint32_t) imm );
                            if ( Q )
                            {
                                vreg_setui32( d, 8, (uint32_t) imm );
                                vreg_setui32( d, 12, (uint32_t) imm );
                            }
                        }
                    }
                    else if ( !bit29 ) // BIC register
                    {
                        unhandled();
                    }
                    else if ( bit29 && bit12 ) // BIC immediate
                    {
                        uint64_t notimm = ~imm;
    
                        if ( 9 == ( cmode & 0xd ) ) // 16-bit mode
                        {
                            uint64_t limit = ( 0 == Q ) ? 4 : 8;
                            for ( uint64_t i = 0; i < limit; i++ )
                            {
                                uint16_t * pval = (uint16_t *) vreg_ptr( d, i * 2 );
                                * pval = *pval & (uint16_t) notimm;
                            }
                        }
                        else if ( 1 == ( cmode & 1 ) ) // 32-bit mode
                        {
                            uint64_t limit = ( 0 == Q ) ? 2 : 4;
                            for ( uint64_t i = 0; i < limit; i++ )
                            {
                                uint32_t * pval = (uint32_t *) vreg_ptr( d, i * 4 );
                                * pval = *pval & (uint32_t) notimm;
                            }
                        }
                        else
                            unhandled();
                    }
                }
                else // USHR, USHLL, SHRN, SHRN2, etc
                {
                    uint64_t opcode = opbits( 12, 4 );

                    if ( ( 0x0f == hi8 || 0x4f == hi8 ) && !bit23 && 0 == opcode && !bit11 && bit10 ) // SSHR <Vd>.<T>, <Vn>.<T>, #<shift>
                    {
                        uint64_t n = opbits( 5, 5 );
                        uint64_t immh = opbits( 19, 4 );
                        uint64_t immb = opbits( 16, 3 );
                        uint64_t esize = 8ull << highest_set_bit_nz( immh );
                        uint64_t ebytes = esize / 8;
                        uint64_t datasize = 64 << Q;
                        uint64_t elements = datasize / esize;
                        uint64_t shift = ( esize * 2 ) - ( ( immh << 3 ) | immb );
                        vec16_t target = { 0 };
                        uint8_t * ptarget = (uint8_t *) &target;

                        for ( uint64_t e = 0; e < elements; e++ )
                        {
                            uint64_t elem = 0;
                            memcpy( &elem, vreg_ptr( n, e * ebytes ), ebytes );
                            elem >>= shift;
                            memcpy( ptarget + e * ebytes, &elem, ebytes );
                        }

                        memcpy( vreg_ptr( d, 0 ), ptarget, sizeof( target ) );
                    }
                    else if ( ( 0x4f == hi8 || 0x0f == hi8 ) && bit23 && 1 == opcode && !bit10 ) // FMLA <Vd>.<T>, <Vn>.<T>, <Vm>.<Ts>[<index>]
                    {
                        uint64_t n = opbits( 5, 5 );
                        uint64_t m = opbits( 16, 5 );
                        uint64_t sz = opbits( 22, 1 );
                        uint64_t L = opbits( 21, 1 );
                        uint64_t H = opbits( 11, 1 );
                        uint64_t szL = ( sz << 1 ) | L;
                        uint64_t index = ( 0 == sz ) ? ( ( H << 1 ) | L ) : ( 2 == szL ) ? H : 0;
                        uint64_t esize = 32ull << sz;
                        uint64_t ebytes = esize / 8;
                        uint64_t datasize = 64ull << Q;
                        uint64_t elements = datasize / esize;
                        vec16_t target = { 0 };
                        uint8_t * ptarget = (uint8_t *) &target;
                        // tracer.Trace( "elements %llu, esize %llu, idxsize %llu, datasize %llu, d %llu, n %llu, m %llu, index %llu\n", elements, esize, idxsize, datasize, d, n, m, index );

                        if ( 8 == ebytes )
                        {
                            double element2 = vreg_getdouble( m, 8 * index );
                            if ( 1 == elements )
                                memcpy( ptarget, vreg_ptr( d, 0 ), sizeof( target ) );
                            for ( uint64_t e = 0; e < elements; e++ )
                            {
                                double element1 = vreg_getdouble( n, e * 8 );
                                double cur = vreg_getdouble( d, e * 8 );
                                //tracer.Trace( "element1: %lf, element2 %lf, cur %lf\n", element1, element2, cur );
                                cur += ( element1 * element2 );
                                tracer.Trace( "  new value: %lf written to element %llu\n", cur, e );
                                memcpy( ptarget + ( e * 8 ), &cur, 8 );
                            }
                        }
                        else if ( 4 == ebytes )
                        {
                            float element2 = vreg_getfloat( m, 4 * index );
                            if ( 1 == elements )
                                memcpy( ptarget, vreg_ptr( d, 0 ), sizeof( target ) );
                            for ( uint64_t e = 0; e < elements; e++ )
                            {
                                float element1 = vreg_getfloat( n, e * 4 );
                                float cur = vreg_getfloat( d, e * 4 );
                                cur += ( element1 * element2 );
                                memcpy( ptarget + ( e * 4 ), &cur, 4 );
                            }
                        }
                        else
                            unhandled() ;

                        memcpy( vreg_ptr( d, 0 ), ptarget, sizeof( target ) );
                    }
                    else if ( ( 0x0f == hi8 || 0x4f == hi8 ) && !bit23 && 0 != bits23_19 && 0xa == opcode && !bit11 && bit10 ) // SSHLL{2} <Vd>.<Ta>, <Vn>.<Tb>, #<shift>
                    {
                        uint64_t n = opbits( 5, 5 );
                        uint64_t immh = opbits( 19, 4 );
                        uint64_t immb = opbits( 16, 3 );
                        uint64_t esize = 8ull << highest_set_bit_nz( immh & 0x7 );
                        uint64_t esize_bytes = esize / 8;
                        uint64_t shift = ( ( immh << 3 ) | immb ) - esize;
                        uint64_t datasize = 64;
                        uint64_t elements = datasize / esize;
                        vec16_t target = { 0 };
                        uint8_t * ptarget = (uint8_t *) &target;
                        //tracer.Trace( "sshl{2} shift %llu, esize_bytes %llu, elements %llu\n", shift, esize_bytes, elements );

                        for ( uint64_t e = 0; e < elements; e++ )
                        {
                            uint64_t v = 0;
                            memcpy( &v, vreg_ptr( n, ( Q ? 8 : 0 ) + e * esize_bytes ), esize_bytes );
                            v <<= shift;
                            //tracer.Trace( "e %llu, v after shift: %#llx\n", e, v );
                            assert( ( ( 1 + e ) * 2 * esize_bytes ) <= sizeof( target ) );
                            memcpy( ptarget + e * 2 * esize_bytes, &v, 2 * esize_bytes );
                        }

                        memcpy( vreg_ptr( d, 0 ), ptarget, sizeof( target ) );
                    }
                    else if ( ( 0x0f == hi8 || 0x4f == hi8 ) && !bit23 && 0 != bits23_19 && 8 == opcode && !bit11 && bit10 ) // SHRN{2} <Vd>.<Tb>, <Vn>.<Ta>, #<shift>
                    {
                        uint64_t n = opbits( 5, 5 );
                        uint64_t immh = opbits( 19, 4 );
                        uint64_t immb = opbits( 16, 3 );
                        uint64_t esize = 8ull << highest_set_bit_nz( immh & 0x7 );
                        uint64_t esize_bytes = esize / 8;
                        uint64_t datasize = 64;
                        uint64_t part = Q;
                        uint64_t elements = datasize / esize;
                        uint64_t shift = ( 2 * esize ) - ( ( immh << 3 ) | immb );
                        vec16_t target = { 0 };
                        uint8_t * ptarget = (uint8_t *) &target;

                        for ( uint64_t e = 0; e < elements; e++ )
                        {
                            uint64_t v = 0;
                            memcpy( &v, vreg_ptr( n, 2 * e * esize_bytes ), 2 * esize_bytes );
                            v >>= shift;
                            assert( ( ( 1 + e ) * esize_bytes ) <= sizeof( target ) );
                            memcpy( ptarget + e * esize_bytes, &v, esize_bytes );
                        }

                        if ( part )
                            memcpy( vreg_ptr( d, 8 ), ptarget, sizeof( target ) );
                        else
                        {
                            memcpy( vreg_ptr( d, 0 ), ptarget, sizeof( target ) );
                            vreg_setui64( d, 8, 0 );
                        }
                    }
                    else if ( ( 0x2f == hi8 || 0x6f == hi8 ) && !bit23 && 0 != bits23_19 && ( 0xa == opcode ) && !bit11 && bit10 ) // USHLL{2} <Vd>.<Ta>, <Vn>.<Tb>, #<shift>
                    {
                        uint64_t n = opbits( 5, 5 );
                        uint64_t immh = opbits( 19, 4 );
                        uint64_t immb = opbits( 16, 3 );
                        uint64_t esize = 8ull << highest_set_bit_nz( immh & 0x7 );
                        uint64_t esize_bytes = esize / 8;
                        uint64_t datasize = 64;
                        uint64_t part = Q;
                        uint64_t elements = datasize / esize;
                        uint64_t shift = ( ( immh << 3 ) | immb ) - esize;
                        vec16_t target = { 0 };
                        uint8_t * ptarget = (uint8_t *) &target;

                        for ( uint64_t e = 0; e < elements; e++ )
                        {
                            uint64_t v = 0;
                            memcpy( &v, vreg_ptr( n, ( part ? 8 : 0 ) + e * esize_bytes ), esize_bytes );
                            v <<= shift;
                            assert( ( ( 1 + e ) * 2 * esize_bytes ) <= sizeof( target ) );
                            memcpy( ptarget + 2 * e * esize_bytes, &v, esize_bytes * 2 );
                        }

                        memcpy( vreg_ptr( d, 0 ), ptarget, sizeof( target ) );
                    }
                    else if ( ( 0x2f == hi8 || 0x7f == hi8 || 0x6f == hi8 ) && !bit23 && !bit15 && !bit14 && !bit13 && !bit12 && !bit11 && bit10 ) // USHR
                    {
                        uint64_t n = opbits( 5, 5 );
                        uint64_t immh = opbits( 19, 4 );
                        uint64_t immb = opbits( 16, 3 );
                        uint64_t esize = 8ull << highest_set_bit_nz( immh );
                        if ( 0x7f == hi8 )
                            esize = 8 << 3;
                        uint64_t esize_bytes = esize / 8;
                        uint64_t datasize = 64ull << Q;
                        if ( 0x7f == hi8 )
                            datasize = esize;
                        uint64_t elements = datasize / esize;
                        if ( 0x7f == hi8 )
                            elements = 1;
                        uint64_t shift = ( esize * 2 ) - ( ( immh << 3 ) | immb );
                        vec16_t target = { 0 };
                        uint8_t * ptarget = (uint8_t *) &target;

                        for ( uint64_t e = 0; e < elements; e++ )
                        {
                            uint64_t v = 0;
                            memcpy( &v, vreg_ptr( n, e * esize_bytes ), esize_bytes );
                            v >>= shift;
                            assert( ( ( e * esize_bytes ) + esize_bytes ) <= sizeof( target ) );
                            memcpy( ptarget + e * esize_bytes, &v, esize_bytes );
                        }

                        memcpy( vreg_ptr( d, 0 ), ptarget, sizeof( target ) );
                    }
                    else if ( bit23 && !bit10 && 9 == opcode ) // FMUL <Vd>.<T>, <Vn>.<T>, <Vm>.<Ts>[<index>]. Vector, single-precision and double-precision
                    {
                        uint64_t n = opbits( 5, 5 );
                        uint64_t m = opbits( 16, 5 );
                        uint64_t sz = opbits( 22, 1 );
                        uint64_t L = opbits( 21, 1 );
                        uint64_t H = opbits( 11, 1 );
    
                        uint64_t index = ( !sz ) ? ( ( H << 1 ) | L ) : H;
                        uint64_t esize = 32ull << sz;
                        uint64_t esize_bytes = esize / 8;
                        uint64_t datasize = 64ull << Q;
                        uint64_t elements = datasize / esize;

                        //tracer.Trace( "index: %llu, esize %llu, esize_bytes %llu, datasize %llu, elements %llu\n", index, esize, esize_bytes, datasize, elements );
                        vec16_t target = { 0 };
                        uint8_t * ptarget = (uint8_t *) &target;

                        double mdouble = 0.0;
                        float mfloat = 0.0;
                        if ( 8 == esize_bytes )
                            mdouble = vreg_getdouble( m, 8 * index );
                        else if ( 4 == esize_bytes )
                            mfloat = vreg_getfloat( m, 4 * index );

                        for ( uint64_t e = 0; e < elements; e++ )
                        {
                            if ( 8 == esize_bytes )
                            {
                                double ndouble = vreg_getdouble( n, e * 8 );
                                double product = ndouble * mdouble;
                                assert( ( ( e * 8 ) + 8 ) <= sizeof( target ) );
                                memcpy( ptarget + e * 8, &product, 8 );
                            }
                            else
                            {
                                float nfloat = vreg_getfloat( n, e * 4 );
                                float product = nfloat * mfloat;
                                assert( ( ( e * 4 ) + 4 ) <= sizeof( target ) );
                                memcpy( ptarget + e * 4, &product, 4 );
                            }
                        }
                        memcpy( vreg_ptr( d, 0 ), ptarget, sizeof( target ) );
                    }
                    else
                        unhandled();
                }

                trace_vregs();
                break;
            }
            case 0x5a: // REV <Wd>, <Wn>    ;    CSINV <Wd>, <Wn>, <Wm>, <cond>    ;    RBIT <Wd>, <Wn>    ;    CLZ <Wd>, <Wn>    ;    CSNEG <Wd>, <Wn>, <Wm>, <cond>
            case 0xda: // REV <Xd>, <Xn>    ;    CSINV <Xd>, <Xn>, <Xm>, <cond>    ;    RBIT <Xd>, <Xn>    ;    CLZ <Xd>, <Xn>    ;    CSNEG <Xd>, <Xn>, <Xm>, <cond>
            {
                uint64_t xregs = ( 0 != ( 0x80 & hi8 ) );
                uint64_t opc = opbits( 10, 2 ); // 2 or 3 for container size
                uint64_t data_size = ( 32ull << opbits( 31, 1 ) );
                uint64_t container_size = ( 8ull << opc );
                uint64_t containers = data_size / container_size;
                uint64_t bits23_21 = opbits( 21, 3 );
                uint64_t bits15_10 = opbits( 10, 6 );
                uint64_t bit11 = opbits( 11, 1 );
                uint64_t bit10 = opbits( 10, 1 );
                uint64_t n = opbits( 5, 5 );
                uint64_t d = opbits( 0, 5 );
                uint64_t result = 0;
                uint64_t nval = val_reg_or_zr( n );

                if ( 4 == bits23_21 ) // csinv / csneg
                {
                    if ( bit11 )
                        unhandled();
                    uint64_t m = opbits( 16, 5 );
                    uint64_t mval = val_reg_or_zr( m );
                    uint64_t cond = opbits( 12, 4 );
                    if ( check_conditional( cond ) )
                        result = val_reg_or_zr( n );
                    else
                        result = bit10 ? ( (uint64_t) ( - (int64_t) mval ) ) : ~ ( mval );
                }
                else if ( 6 == bits23_21 )
                {
                    if ( 0 == bits15_10 ) // rbit
                    {
                        if ( xregs )
                        {
                            for ( uint64_t bit = 0; bit < 64; bit++ )
                            {
                                uint64_t thebit = ( nval & ( 1ull << bit ) );
                                thebit >>= bit;
                                result |= ( thebit << ( 63ull - bit ) );
                            }
                        }
                        else
                        {
                            for ( uint64_t bit = 0; bit < 32; bit++ )
                            {
                                uint64_t thebit = ( nval & ( 1ull << bit ) );
                                thebit >>= bit;
                                result |= ( thebit << ( 31ull - bit ) );
                            }
                        }
                    }
                    else if ( 2 == bits15_10 || 3 == bits15_10 ) // rev
                    {
                        for ( uint64_t c = 0; c < containers; c++ )
                        {
                            //tracer.Trace( "in rev top level loop, c: %llu, container_size %llu\n", c, container_size );
                            uint64_t container = get_elem_bits( nval, c, container_size );
                            result |= get_elem_bits( reverse_bytes( container, container_size ), c, container_size );
                        }
                    }
                    else if ( 4 == bits15_10 ) // clz
                    {
                        int64_t cur = ( xregs ? 63 : 31 );
                        while ( cur >= 0 )
                        {
                            if ( ! ( nval & ( 1ull << cur ) ) )
                            {
                                result++;
                                cur--;
                            }
                            else
                                break;
                        }
                    }
                    else
                        unhandled();
                }
                else
                    unhandled();

                if ( 31 != d )
                {
                    if ( !xregs )
                        result &= 0xffffffff;
                    regs[ d ] = result;
                }
                break;
            }
            case 0x14: case 0x15: case 0x16: case 0x17: // b label
            {
                int64_t imm26 = opbits( 0, 26 );
                imm26 <<= 2;
                imm26 = sign_extend( imm26, 27 );
                pc += imm26;
                continue;
            }
            case 0x1a: // CSEL <Wd>, <Wn>, <Wm>, <cond>    ;    SDIV <Wd>, <Wn>, <Wm>    ;    UDIV <Wd>, <Wn>, <Wm>    ;    CSINC <Wd>, <Wn>, <Wm>, <cond>
                       // LSRV <Wd>, <Wn>, <Wm>            ;    LSLV <Wd>, <Wn>, <Wm>    ;    ADC <Wd>, <Wn>, <Wm>     ;    ASRV <Wd>, <Wn>, <Wm>
                       // RORV <Wd>, <Wn>, <Wm>
            case 0x9a: // CSEL <Xd>, <Xn>, <Xm>, <cond>    ;    SDIV <Xd>, <Xn>, <Xm>    ;    UDIV <Xd>, <Xn>, <Xm>    ;    CSINC <Xd>, <Xn>, <Xm>, <cond>
                       // LSRV <Xd>, <Xn>, <Xm>            ;    LSLV <Xd>, <Xn>, <Xm>    ;    ADC <Xd>, <Xn>, <Xm>     ;    ASRV <Xd>, <Xn>, <Xm>
                       // RORV <Xd>, <Xn>, <Xm>
            {
                uint64_t xregs = ( 0 != ( 0x80 & hi8 ) );
                uint64_t bits11_10 = opbits( 10, 2 );
                uint64_t d = opbits( 0, 5 );
                uint64_t n = opbits( 5, 5 );
                uint64_t m = opbits( 16, 5 );
                uint64_t bits15_12 = opbits( 12, 4 );
                uint64_t bits23_21 = opbits( 21, 3 );
                if ( 31 == d )
                    break;

                uint64_t mval = val_reg_or_zr( m );
                uint64_t nval = val_reg_or_zr( n );

                if ( 0 == bits11_10 && 4 == bits23_21 ) // CSEL
                {
                    uint64_t cond = opbits( 12, 4 );
                    if ( check_conditional( cond ) )
                        regs[ d ] = nval;
                    else
                        regs[ d ] = mval;
                }
                else if ( 1 == bits11_10 && 4 == bits23_21 ) // CSINC <Xd>, XZR, XZR, <cond>
                {
                    uint64_t cond = opbits( 12, 4 );
                    if ( check_conditional( cond ) )
                        regs[ d ] = nval;
                    else
                        regs[ d ] = 1 + mval;
                }
                else if ( 2 == bits11_10 && 6 == bits23_21 && 2 == bits15_12 ) // ASRV <Xd>, <Xn>, <Xm>
                {
                    uint64_t shift = mval;
                    uint64_t result = 0;
                    if ( xregs )
                    {
                        shift = shift % 64;
                        result = ( ( (int64_t) nval ) >> shift );
                    }
                    else
                    {
                        shift = ( shift & 0xffffffff ) % 32;
                        result = (uint32_t) ( ( (int32_t) nval ) >> shift );
                    }

                    regs[ d ] = result;
                }
                else if ( 2 == bits11_10 && 6 == bits23_21 && 0 == bits15_12 ) // UDIV <Xd>, <Xn>, <Xm>
                {
                    if ( xregs )
                        regs[ d ] = ( 0 == mval ) ? 0 : ( nval / mval );
                    else
                        regs[ d ] = ( 0xffffffff & ( ( 0 == mval ) ? 0 : ( (uint32_t) nval / (uint32_t) mval ) ) );
                }
                else if ( 3 == bits11_10 && 6 == bits23_21 && 0 == bits15_12 ) // SDIV <Xd>, <Xn>, <Xm>
                {
                    if ( xregs )
                    {
                        if ( 0 != regs[ m ] )
                            regs[ d ] = ( 0 == mval ) ? 0 : ( (int64_t) nval / (int64_t) mval );
                    }
                    else
                    {
                        if ( 0 != ( 0xffffffff & ( mval ) ) )
                            regs[ d ] = ( ( 0 == mval ) ? 0 : ( (int32_t) ( 0xffffffff & nval ) / (int32_t) ( 0xffffffff & mval ) ) );
                    }
                }
                else if ( 1 == bits11_10 && 6 == bits23_21 && 2 == bits15_12 ) // lsrv
                {
                    uint64_t shift = mval;
                    if ( xregs )
                        shift = shift % 64;
                    else
                    {
                        nval &= 0xffffffff;
                        shift = ( shift & 0xffffffff ) % 32;
                    }
                    regs[ d ] = ( nval >> shift );
                }
                else if ( 0 == bits11_10 && 6 == bits23_21 && 2 == bits15_12 ) // lslv
                {
                    uint64_t shift = mval;
                    if ( xregs )
                    {
                        shift = shift % 64;
                        regs[ d ] = ( nval << shift );
                    }
                    else
                    {
                        shift = ( shift & 0xffffffff ) % 32;
                        regs[ d ] = 0xffffffff & ( nval << shift );
                    }
                }
                else if ( 0 == bits11_10 && 0 == bits23_21 && 0 == bits15_12 && 0 == bits11_10 ) // addc
                {
                    if ( xregs )
                        regs[ d ] = add_with_carry64( nval, mval, fC, false );
                    else
                        regs[ d ] = add_with_carry32( nval & 0xffffffff, mval & 0xffffffff, fC, false );
                }
                else if ( 3 == bits11_10 && 6 == bits23_21 && 2 == bits15_12 ) // RORV <Xd>, <Xn>, <Xm>
                {
                    if ( xregs )
                        regs[ d ] = shift_reg64( n, 3, mval );
                    else
                        regs[ d ] = shift_reg32( n, 3, mval );
                }
                else
                    unhandled();

                if ( !xregs )
                    regs[ d ] &= 0xffffffff;
                break;
            }
            case 0x54: // b.cond
            {
                uint64_t cond = opbits( 0, 4 );

                bool branch = check_conditional( cond );
                if ( branch )
                {
                    int64_t imm19 = opbits( 5, 19 );
                    imm19 <<= 2;
                    imm19 = sign_extend( imm19, 20 );
                    pc += imm19;
                    continue;
                }
                break;
            }
            case 0x18: // ldr wt, (literal)
            case 0x58: // ldr xt, (literal)
            {
                uint64_t imm19 = opbits( 5, 19 );
                uint64_t t = opbits( 0, 5 );
                bool xregs = ( 0 != opbits( 30, 1 ) );
                uint64_t address = pc + ( imm19 << 2 );
                if ( 31 != t )
                {
                    if ( xregs )
                        regs[ t ] = getui64( address );
                    else
                        regs[ t ] = getui32( address );
                }
                break;
            }
            case 0x3a: // CCMN <Wn>, #<imm>, #<nzcv>, <cond>  ;    CCMN <Wn>, <Wm>, #<nzcv>, <cond>       ;    ADCS <Wd>, <Wn>, <Wm>
            case 0xba: // CCMN <Wn>, <Wm>, #<nzcv>, <cond>    ;    CCMN <Xn>, <Xm>, #<nzcv>, <cond>       ;    ADCS <Xd>, <Xn>, <Xm>
            case 0x7a: // CCMP <Wn>, <Wm>, #<nzcv>, <cond>    ;    CCMP <Wn>, #<imm>, #<nzcv>, <cond>
            case 0xfa: // CCMP <Xn>, <Xm>, #<nzcv>, <cond>    ;    CCMP <Xn>, #<imm>, #<nzcv>, <cond>
            {
                uint64_t bits23_21 = opbits( 21, 3 );
                uint64_t n = opbits( 5, 5 );
                bool xregs = ( 0 != ( 0x80 & hi8 ) );
    
                if ( 2 == bits23_21 )
                {
                    uint64_t o3 = opbits( 4, 1 );
                    if ( 0 != o3 )
                        unhandled();
        
                    uint64_t cond = opbits( 12, 4 );
                    uint64_t nzcv = opbits( 0, 4 );
                    uint64_t o2 = opbits( 10, 2 );
                    if ( check_conditional( cond ) )
                    {
                        uint64_t op2 = 0;
                        if ( 0 == o2 ) // register
                        {
                            uint64_t m = opbits( 16, 5 );
                            op2 = val_reg_or_zr( m );
                        }
                        else if ( 2 == o2 ) // immediate
                            op2 = ( ( op >> 16 ) & 0x1f );
                        else
                            unhandled();
    
                        if ( 0 == ( hi8 & 0x40 ) ) // ccmn negative
                        {
                            if ( xregs )
                                op2 = - (int64_t) op2;
                            else
                                op2 = (uint32_t) ( - (int32_t) ( op2 & 0xffffffff ) );
                        }
    
                        uint64_t op1 = val_reg_or_zr( n );
                        if ( xregs )
                            sub64( op1, op2, true );
                        else
                            sub32( op1 & 0xffffffff, op2 & 0xffffffff, true );
                    }
                    else
                        set_flags_from_nzcv( nzcv );
                }
                else if ( ( 0x3a == hi8 || 0xba == hi8 ) && 0 == bits23_21 ) // ADCS
                {
                    uint64_t d = opbits( 0, 5 );
                    uint64_t m = opbits( 16, 5 );
                    uint64_t nval = val_reg_or_zr( n );
                    uint64_t mval = val_reg_or_zr( m );

                    uint64_t result = 0;
                    if ( xregs )
                        result = add_with_carry64( nval, mval, fC, true );
                    else
                        result = add_with_carry32( 0xffffffff & nval, 0xffffffff & mval, fC, true );
                    if ( 31 != d )
                        regs[ d ] = result;
                }
                break;
            }
            case 0x71: // SUBS <Wd>, <Wn|WSP>, #<imm>{, <shift>}   ;   CMP <Wn|WSP>, #<imm>{, <shift>}
            case 0xf1: // SUBS <Xd>, <Xn|SP>, #<imm>{, <shift>}    ;   cmp <xn|SP>, #imm [,<shift>]
            case 0x31: // ADDS <Wd>, <Wn|WSP>, #<imm>{, <shift>}  ;    CMN <Wn|WSP>, #<imm>{, <shift>}
            case 0xb1: // ADDS <Xd>, <Xn|SP>, #<imm>{, <shift>}   ;    CMN <Xn|SP>, #<imm>{, <shift>}
            {
                uint64_t xregs = ( 0 != ( 0x80 & hi8 ) );
                uint64_t imm12 = opbits( 10, 12 );
                uint64_t n = opbits( 5, 5 );
                uint64_t d = opbits( 0, 5 );
                bool is_sub = ( 0 != ( 0x40 & hi8 ) );
                bool shift12 = opbits( 22, 1 );
                if ( shift12 )
                    imm12 <<= 12;

                uint64_t result;

                if ( xregs )
                {
                    if ( is_sub )
                        result = sub64( regs[ n ], imm12, true );
                    else
                        result = add_with_carry64( regs[ n ], imm12, false, true );
                }
                else
                {
                    if ( is_sub )
                        result = sub32( 0xffffffff & regs[ n ], 0xffffffff & imm12, true );
                    else
                        result = add_with_carry32( regs[ n ] & 0xffffffff, imm12 & 0xffffffff, false, true );
                }

                if ( 31 != d )
                    regs[ d ] = result;
                break;
            }
            case 0x0b: // ADD <Wd|WSP>, <Wn|WSP>, <Wm>{, <extend> {#<amount>}}      ;    ADD <Wd>, <Wn>, <Wm>{, <shift> #<amount>}
            case 0x2b: // ADDS <Wd>, <Wn|WSP>, <Wm>{, <extend> {#<amount>}}         ;    ADDS <Wd>, <Wn>, <Wm>{, <shift> #<amount>}
            case 0x4b: // SUB <Wd|WSP>, <Wn|WSP>, <Wm>{, <extend> {#<amount>}}      ;    SUB <Wd>, <Wn>, <Wm>{, <shift> #<amount>} 
            case 0x6b: // SUBS <Wd>, <Wn|WSP>, <Wm>{, <extend> {#<amount>}}         ;    SUBS <Wd>, <Wn>, <Wm>{, <shift> #<amount>}
            case 0x8b: // ADD <Xd|SP>, <Xn|SP>, <R><m>{, <extend> {#<amount>}}      ;    ADD <Xd>, <Xn>, <Xm>{, <shift> #<amount>}
            case 0xab: // ADDS <Xd>, <Xn|SP>, <R><m>{, <extend> {#<amount>}}        ;    ADDS <Xd>, <Xn>, <Xm>{, <shift> #<amount>}
            case 0xcb: // SUB <Xd|SP>, <Xn|SP>, <R><m>{, <extend> {#<amount>}}      ;    SUB <Xd>, <Xn>, <Xm>{, <shift> #<amount>}
            case 0xeb: // SUBS <Xd>, <Xn|SP>, <R><m>{, <extend> {#<amount>}}        ;    SUBS <Xd>, <Xn>, <Xm>{, <shift> #<amount>}
            {
                uint64_t extended = opbits( 21, 1 );
                uint64_t issub = ( 0 != ( 0x40 & hi8 ) );
                uint64_t setflags = ( 0 != ( 0x20 & hi8 ) );
                uint64_t xregs = ( 0 != ( 0x80 & hi8 ) );
                uint64_t m = opbits( 16, 5 );
                uint64_t n = opbits( 5, 5 );
                uint64_t d = opbits( 0, 5 );
                uint64_t offset = 0;
                uint64_t nvalue = regs[ n ];

                if ( 1 == extended ) // ADD <Xd|SP>, <Xn|SP>, <R><m>{, <extend> {#<amount>}}
                {
                    uint64_t option = opbits( 13, 3 );
                    uint64_t imm3 = opbits( 10, 3 );
                    offset = extend_reg( m, option, imm3 );
                }
                else // ADD <Xd>, <Xn>, <Xm>{, <shift> #<amount>}
                {
                    uint64_t shift = opbits( 22, 2 );
                    uint64_t imm6 = opbits( 10, 6 );
                    if ( xregs )
                        offset = shift_reg64( m, shift, imm6 );
                    else
                        offset = shift_reg32( m, shift, imm6 );
                    if ( 31 == n )
                        nvalue = 0;

                    //tracer.Trace( "shifted m %llx with resulting value %llx\n", val_reg_or_zr( m ), offset );
                }

                uint64_t result = 0;
                if ( issub )
                {
                    if ( xregs )
                        result = sub64( nvalue, offset, setflags );
                    else
                        result = sub32( nvalue & 0xffffffff, offset & 0xffffffff, setflags );
                }
                else
                {
                    if ( xregs )
                        result = add_with_carry64( nvalue, offset, false, setflags );
                    else
                        result = add_with_carry32( nvalue & 0xffffffff, offset & 0xffffffff, false, setflags );
                }

                if ( ( !setflags ) || ( 31 != d ) )
                    regs[ d ] = result;
                break;
            }
            case 0x94: case 0x95: case 0x96: case 0x97: // bl offset. The lower 2 bits of this are the high part of the offset
            {
                int64_t offset = ( opbits( 0, 26 ) << 2 );
                offset = sign_extend( offset, 27 );
                regs[ 30 ] = pc + 4;
                pc += offset;
                trace_vregs();
                continue;
            }
            case 0x11: // add <wd|SP>, <wn|SP>, #imm [,<shift>]
            case 0x51: // sub <wd|SP>, <wn|SP>, #imm [,<shift>]
            case 0x91: // add <xd|SP>, <xn|SP>, #imm [,<shift>]
            case 0xd1: // sub <xd|SP>, <xn|SP>, #imm [,<shift>]
            {
                bool sf = ( 0 != opbits( 31, 1 ) );
                bool sh = ( 0 != opbits( 22, 1 ) );
                uint64_t imm12 = opbits( 10, 12 );
                uint64_t n = opbits( 5, 5 );
                uint64_t d = opbits( 0, 5 );
                uint64_t op1 = regs[ n ];
                uint64_t op2 = ( imm12 << ( sh ? 12 : 0 ) );
                bool isadd = ( 0x91 == hi8 || 0x11 == hi8 );
                uint64_t result;
                if ( isadd )
                {
                    if ( sf )
                        result = add_with_carry64( op1, op2, false, false );
                    else
                        result = add_with_carry32( op1 & 0xffffffff, op2 & 0xffffffff, false, false );
                }
                else
                {
                    if ( sf )
                        result = sub64( op1, op2, false );
                    else
                        result = sub32( op1 & 0xffffffff, op2 & 0xffffffff, false );
                }
                regs[ d ] = result;
                break;
            }
            case 0x28: // ldp/stp 32 post index                   STP <Wt1>, <Wt2>, [<Xn|SP>], #<imm>     ;    LDP <Wt1>, <Wt2>, [<Xn|SP>], #<imm>
            case 0xa8: // ldp/stp 64 post-index                   STP <Xt1>, <Xt2>, [<Xn|SP>], #<imm>     ;    LDP <Xt1>, <Xt2>, [<Xn|SP>], #<imm>
            case 0x29: // ldp/stp 32 pre-index and signed offset: STP <Wt1>, <Wt2>, [<Xn|SP>, #<imm>]!    ;    STP <Wt1>, <Wt2>, [<Xn|SP>{, #<imm>}]
                       //                                         LDP <Wt1>, <Wt2>, [<Xn|SP>, #<imm>]!    ;    LDP <Wt1>, <Wt2>, [<Xn|SP>{, #<imm>}]
            case 0xa9: // ldp/stp 64 pre-index and signed offset: STP <Xt1>, <Xt2>, [<Xn|SP>, #<imm>]!    ;    STP <Xt1>, <Xt2>, [<Xn|SP>{, #<imm>}]
                       //                                         LDP <Xt1>, <Xt2>, [<Xn|SP>, #<imm>]!    ;    LDP <Xt1>, <Xt2>, [<Xn|SP>{, #<imm>}]
            case 0x68: // ldp 32-bit sign extended                LDPSW <Xt1>, <Xt2>, [<Xn|SP>], #<imm>
            case 0x69: // ldp 32-bit sign extended                LDPSW <Xt1>, <Xt2>, [<Xn|SP>, #<imm>]!  ;    LDPSW <Xt1>, <Xt2>, [<Xn|SP>{, #<imm>}]
            {
                bool xregs = ( 0 != opbits( 31, 1 ) );
                uint64_t t1 = opbits( 0, 5 );
                uint64_t t2 = opbits( 10, 5 );
                uint64_t n = opbits( 5, 5 );
                int64_t imm7 = sign_extend( opbits( 15, 7 ), 6 ) << ( xregs ? 3 : 2 );
                uint64_t variant = opbits( 23, 2 );
                if ( 0 == variant )
                    unhandled();
    
                bool postIndex = ( 1 == variant );
                bool preIndex = ( 3 == variant );
                bool signedOffset = ( 2 == variant );
                uint64_t address = regs[ n ];

                if ( 0 == opbits( 22, 1 ) ) // bit 22 is 0 for stp
                {
                    if ( preIndex )
                        address += imm7;

                    uint64_t t1val = val_reg_or_zr( t1 );
                    uint64_t t2val = val_reg_or_zr( t2 );

                    if ( xregs )
                    {
                        setui64( address + ( signedOffset ? imm7 : 0 ), t1val );
                        setui64( address + 8 + ( signedOffset ? imm7 : 0 ), t2val );
                    }
                    else
                    {
                        setui32( address + ( signedOffset ? imm7 : 0 ), 0xffffffff & t1val );
                        setui32( address + 4 + ( signedOffset ? imm7 : 0 ), 0xffffffff & t2val );
                    }

                    if ( postIndex )
                        address += imm7;

                    if ( preIndex || postIndex )
                        regs[ n ] = address;
                }
                else // 1 means ldp
                {
                    // LDP <Xt1>, <Xt2>, [<Xn|SP>], #<imm>
                    // LDP <Xt1>, <Xt2>, [<Xn|SP>, #<imm>]!
                    // LDP <Xt1>, <Xt2>, [<Xn|SP>{, #<imm>}]

                    if ( preIndex )
                        address += imm7;

                    if ( xregs )
                    {
                        if ( 31 != t1 )
                            regs[ t1 ] = getui64( address + ( signedOffset ? imm7 : 0 ) );
                        if ( 31 != t2 )
                            regs[ t2 ] = getui64( address + 8 + ( signedOffset ? imm7 : 0 ) );
                    }
                    else
                    {
                        bool se = ( 0 != ( hi8 & 0x40 ) );

                        if ( 31 != t1 )
                        {
                            regs[ t1 ] = getui32( address + ( signedOffset ? imm7 : 0 ) );
                            if ( se )
                                regs[ t1 ] = sign_extend( regs[ t1 ], 31 );
                        }
                        if ( 31 != t2 )
                        {
                            regs[ t2 ] = getui32( address + 4 + ( signedOffset ? imm7 : 0 ) );
                            if ( se )
                                regs[ t2 ] = sign_extend( regs[ t2 ], 31 );
                        }
                    }

                    if ( postIndex )
                        address += imm7;

                    if ( preIndex || postIndex )
                        regs[ n ] = address;
                }
                break;
            }
            case 0x32: // ORR <Wd|WSP>, <Wn>, #<imm>
            case 0xb2: // ORR <Xd|SP>, <Xn>, #<imm>
            {
                uint64_t xregs = ( 0 != ( 0x80 & hi8 ) );
                uint64_t N_immr_imms = opbits( 10, 13 );
                uint64_t op2 = decode_logical_immediate( N_immr_imms, xregs ? 64 : 32 );
                uint64_t n = opbits( 5, 5 );
                uint64_t d = opbits( 0, 5 );
                uint64_t nvalue = val_reg_or_zr( n );

                regs[ d ] = nvalue | op2;
                if ( !xregs )
                    regs[ d ] &= 0xffffffff;
                break;
            }
            case 0x4a: // EOR <Wd>, <Wn>, <Wm>{, <shift> #<amount>}    ;    EON <Wd>, <Wn>, <Wm>{, <shift> #<amount>}
            case 0xca: // EOR <Xd>, <Xn>, <Xm>{, <shift> #<amount>}    ;    EON <Xd>, <Xn>, <Xm>{, <shift> #<amount>}
            case 0x2a: // ORR <Wd>, <Wn>, <Wm>{, <shift> #<amount>}    ;    ORN <Wd>, <Wn>, <Wm>{, <shift> #<amount>}
            case 0xaa: // ORR <Xd>, <Xn>, <Xm>{, <shift> #<amount>}    ;    ORN <Xd>, <Xn>, <Xm>{, <shift> #<amount>}
            {
                uint64_t shift = opbits( 22, 2 );
                uint64_t N = opbits( 21, 1 );
                uint64_t m = opbits( 16, 5 );
                uint64_t n = opbits( 5, 5 );
                uint64_t d = opbits( 0, 5 );
                uint64_t imm6 = opbits( 10, 6 );
                uint64_t xregs = ( 0 != ( 0x80 & hi8 ) );
                if ( !xregs && ( 0 != ( imm6 & 0x20 ) ) ) // can't shift with 6 bits for 32-bit values
                    unhandled();
                bool eor = ( 2 == opbits( 29, 2 ) );

                if ( 31 == d )
                    break;

                uint64_t nval = val_reg_or_zr( n );
                if ( ( 0 == imm6 ) && ( 31 == n ) && ( 0 == shift ) && ( 0 == N ) )
                    regs[ d ] = val_reg_or_zr( m );
                else if ( ( 0 == shift ) && ( 0 == imm6 ) )
                {
                    uint64_t mval = val_reg_or_zr( m );
                    if ( eor )
                        regs[ d ] = nval ^ ( ( 0 == N ) ? mval : ~mval );
                    else
                        regs[ d ] = nval | ( ( 0 == N ) ? mval : ~mval );
                }
                else
                {
                    uint64_t mval = xregs ? shift_reg64( m, shift, imm6 ) : shift_reg32( m, shift, imm6 );
                    if ( eor )
                        regs[ d ] = nval ^ ( ( 0 == N ) ? mval : ~mval );
                    else
                        regs[ d ] = nval | ( ( 0 == N ) ? mval : ~mval );
                }

                if ( !xregs )
                    regs[ d ] &= 0xffffffff;
                break;
            }
            case 0x33: // BFM <Wd>, <Wn>, #<immr>, #<imms>       // original bits intact
            case 0xb3: // BFM <Xd>, <Xn>, #<immr>, #<imms>
            case 0x13: // SBFM <Wd>, <Wn>, #<immr>, #<imms>    ;    EXTR <Wd>, <Wn>, <Wm>, #<lsb>
            case 0x93: // SBFM <Xd>, <Xn>, #<immr>, #<imms>    ;    EXTR <Xd>, <Xn>, <Xm>, #<lsb>
            case 0x53: // UBFM <Wd>, <Wn>, #<immr>, #<imms>      // unmodified bits set to 0
            case 0xd3: // UBFM <Xd>, <Xn>, #<immr>, #<imms>
            {
                uint64_t N = ( ( op >> 22 ) & 1 );
                if ( ( 0x33 == hi8 || 0x53 == hi8 || 0x13 == hi8 ) && ( 0 != N ) )
                    unhandled();
                if ( ( 0xb3 == hi8 || 0xd3 == hi8 || 0x93 == hi8 ) && ( 1 != N ) )
                    unhandled();

                uint64_t imms = opbits( 10, 6 );
                uint64_t n = opbits( 5, 5 );
                uint64_t d = opbits( 0, 5 );
                uint64_t bit23 = opbits( 23, 1 );
                bool xregs = ( 0 != opbits( 31, 1 ) );

                if ( 31 == d )
                    break;

                if ( bit23 && ( 0x13 == ( 0x7f & hi8 ) ) ) // EXTR. rotate right preserving bits shifted out in the high bits
                {
                    uint64_t m = opbits( 16, 5 );
                    uint64_t result = 0;

                    if ( xregs )
                    {
                        uint64_t nval = val_reg_or_zr( n );
                        uint64_t mval = val_reg_or_zr( m );
                        result = ( mval >> imms ) | ( nval << ( 64 - imms ) );
                    }
                    else
                    {
                        uint32_t nval = 0xffffffff & val_reg_or_zr( n );
                        uint32_t mval = 0xffffffff & val_reg_or_zr( m );
                        result = ( mval >> imms ) | ( nval << ( 32 - imms ) );
                    }

                    regs[ d ] = result;
                }
                else // others
                {
                    uint64_t immr = opbits( 16, 6 );
                    uint64_t regsize = ( 0 != ( 0x80 & hi8 ) ) ? 64 : 32;
                    uint64_t s = val_reg_or_zr( n );
                    uint64_t dval = regs[ d ];
                    uint64_t result = 0;
                    if ( 0x33 == hi8 || 0xb3 == hi8 ) // restore original bits for BFM
                        result = dval; // not s
                    uint64_t dpos = 0;
    
                    if ( imms >= immr )
                    {
                        uint64_t len = imms - immr + 1;
                        for ( uint64_t x = immr; x < ( immr + len ); x++ )
                        {
                            uint64_t bit_val = get_bit( s, x );
                            result = plaster_bit( result, dpos, bit_val );
                            dpos++;
                        }
                    }
                    else
                    {
                        uint64_t len = imms + 1;
                        dpos = regsize - immr;
                        for ( uint64_t x = 0; x < len; x++ )
                        {
                            uint64_t bit_val = get_bit( s, x );
                            result = plaster_bit( result, dpos, bit_val );
                            dpos++;
                        }
                    }
    
                    if ( ( dpos > 0 ) && ( 1 == get_bit( result, dpos - 1 ) ) && ( 0x13 == hi8 || 0x93 == hi8 ) ) // SBFM
                    {
                        //tracer.Trace( "  dpos %llu, most significant bit set, sbfm, extending %llx\n", dpos, result );
                        result = sign_extend( result, dpos - 1 );
                    }
    
                    if ( 0 == ( hi8 & 0x80 ) )
                        result &= 0xffffffff;
                    regs[ d ] = result;
                    //tracer.Trace( "  source %#llx changed to result %#llx\n", s, result );
                }
                break;
            }
            case 0x0a: // AND <Wd>, <Wn>, <Wm>{, <shift> #<amount>}     ;    BIC <Wd>, <Wn>, <Wm>{, <shift> #<amount>}
            case 0x6a: // ANDS <Wd>, <Wn>, <Wm>{, <shift> #<amount>}    ;    BICS <Wd>, <Wn>, <Wm>{, <shift> #<amount>}
            case 0x8a: // AND <Xd>, <Xn>, <Xm>{, <shift> #<amount>}     ;    BIC <Xd>, <Xn>, <Xm>{, <shift> #<amount>}
            case 0xea: // ANDS <Xd>, <Xn>, <Xm>{, <shift> #<amount>}    ;    BICS <Xd>, <Xn>, <Xm>{, <shift> #<amount>}
            {
                uint64_t shift = opbits( 22, 2 );
                uint64_t N = opbits( 21, 1 ); // BICS -- complement
                uint64_t m = opbits( 16, 5 );
                uint64_t imm6 = opbits( 10, 6 );
                uint64_t n = opbits( 5, 5 );
                uint64_t d = opbits( 0, 5 );
                bool set_flags = ( 0x60 == ( hi8 & 0x60 ) );
                bool xregs = ( 0 != ( hi8 & 0x80 ) );

                uint64_t op2;
                if ( xregs )
                {
                    op2 = shift_reg64( m, shift, imm6 );
                    if ( N )
                        op2 = ~op2;
                }
                else
                {
                    op2 = shift_reg32( m, shift, imm6 );
                    if ( N )
                    {
                        uint32_t val = op2 & 0xffffffff;
                        val = ~val;
                        op2 = val;
                    }
                }

                uint64_t result = ( regs[ n ] & op2 );

                if ( set_flags )
                {
                    fZ = ( 0 == result );
                    fV = fC = 0;
                    fN = xregs ? get_bits( result, 63, 1 ) : get_bits( result, 31, 1 );
                }

                if ( 31 != d )
                    regs[ d ] = result;
                break;
            }
            case 0x10: case 0x30: case 0x50: case 0x70: // ADR <Xd>, <label>
            {
                uint64_t d = opbits( 0, 5 );
                uint64_t immhi = opbits( 5, 19 );
                uint64_t immlo = opbits( 29, 2 );
                int64_t offset = sign_extend( immhi << 2 | immlo, 20 );
                if ( 31 != d )
                    regs[ d ] = pc + offset;
                break;
            }
            case 0x90: case 0xb0: case 0xd0: case 0xf0: // ADRP <Xd>, <label>
            {
                uint64_t d = ( op & 0x1f );
                int64_t imm = ( ( op >> 3 ) & 0x1ffffc );  // 19 bits with bottom two bits 0
                imm |= ( ( op >> 29 ) & 3 );               // two low bits
                imm = sign_extend( imm, 20 );
                imm <<= 12;
                imm += ( pc & ( ~0xfff ) );
                regs[ d ] = imm;
                break;
            }
            case 0x52: // MOVZ <Wd>, #<imm>{, LSL #<shift>}    ;    EOR <Wd|WSP>, <Wn>, #<imm>
            case 0xd2: // MOVZ <Xd>, #<imm>{, LSL #<shift>}    ;    EOR <Xd|SP>, <Xn>, #<imm>    
            {
                bool xregs = ( 0 != ( hi8 & 0x80 ) );
                uint8_t bit23 = ( op >> 23 ) & 1;

                if ( bit23 ) // movz xd, imm16
                {
                    uint64_t d = opbits( 0, 5 );
                    uint64_t imm16 = opbits( 5, 16 );
                    uint64_t hw = opbits( 21, 2 );
                    if ( 31 != d )
                        regs[ d ] = ( imm16 << ( hw * 16 ) );
                }
                else // EOR
                {
                    uint64_t N_immr_imms = opbits( 10, 13 );
                    uint64_t op2 = decode_logical_immediate( N_immr_imms, xregs ? 64 : 32 );
                    uint64_t n = ( ( op >> 5 ) & 0x1f );
                    uint64_t d = ( op & 0x1f );
                    uint64_t nvalue = val_reg_or_zr( n );
                    regs[ d ] = nvalue ^ op2;
                    if ( !xregs )
                        regs[ d ] &= 0xffffffff;
                }
                break;
            }
            case 0x36: // TBZ <R><t>, #<imm>, <label>
            case 0x37: // TBNZ <R><t>, #<imm>, <label>
            case 0xb6: // TBZ <R><t>, #<imm>, <label> where high bit is prepended to b40 bit selector for 6 bits total
            case 0xb7: // TBNZ <R><t>, #<imm>, <label> where high bit is prepended to b40 bit selector for 6 bits total
            {
                uint64_t b40 = opbits( 19, 5 );
                if ( 0 != ( 0x80 & hi8 ) )
                    b40 |= 0x20;
                uint64_t t = opbits( 0, 5 );
                uint64_t mask = ( 1ull << b40 );
                bool isset = ( 0 != ( regs[ t ] & mask ) );
                bool zerocheck = ( 0 == ( hi8 & 1 ) );
                if ( isset != zerocheck )
                {
                    int64_t imm14 = (int64_t) sign_extend( ( opbits( 5, 14 ) << 2 ), 15 );
                    pc += imm14;
                    continue;
                }
                break;
            }
            case 0x12: // MOVN <Wd>, #<imm>{, LSL #<shift>}   ;    AND <Wd|WSP>, <Wn>, #<imm>
            case 0x92: // MOVN <Xd>, #<imm16>, LSL #<shift>   ;    AND <Xd|SP>, <Xn>, #<imm>    ;    MOV <Xd>, #<imm>
            {
                uint64_t bit23 = opbits( 23, 1 );
                bool xregs = ( 0 != ( hi8 & 0x80 ) );
                if ( bit23 ) // MOVN
                {
                    uint64_t d = opbits( 0, 5 );
                    uint64_t imm16 = opbits( 5, 16 );
                    uint64_t hw = opbits( 21, 2 );
                    hw *= 16;
                    imm16 <<= hw;
                    imm16 = ~imm16;
    
                    if ( 0x12 == hi8 )
                    {
                        if ( hw > 16 )
                            unhandled();
                        imm16 &= 0xffffffff;
                    }
        
                    if ( 31 != d )
                        regs[ d ] = imm16;
                }
                else // AND
                {
                    uint64_t N_immr_imms = opbits( 10, 13 );
                    uint64_t op2 = decode_logical_immediate( N_immr_imms, xregs ? 64 : 32 );
                    uint64_t n = opbits( 5, 5 );
                    uint64_t nval = val_reg_or_zr( n );
                    uint64_t d = opbits( 0, 5 );
                    regs[ d ] = ( nval & op2 );
                }
                break;
            }
            case 0x34: // CBZ <Wt>, <label>
            case 0x35: // CBNZ <Wt>, <label>
            case 0xb4: // CBZ <Xt>, <label>
            case 0xb5: // CBNZ <Xt>, <label>
            {
                uint64_t t = opbits( 0, 5 );
                uint64_t val = val_reg_or_zr( t );
                bool zero_check = ( 0 == ( hi8 & 1 ) );
                if ( 0 == ( 0x80 & hi8 ) )
                    val &= 0xffffffff;

                if ( zero_check == ( 0 == val ) )
                {
                    int64_t imm19 = ( ( op >> 3 ) & 0x1ffffc );
                    imm19 = sign_extend( imm19, 20 );
                    pc += imm19;
                    continue;
                }
                break;
            }
            case 0xd4: // SVC
            {
                uint64_t bit23 = opbits( 23, 1 );
                uint64_t hw = opbits( 21, 2 );

                if ( !bit23 && ( 0 == hw ) )
                {
                    uint8_t op2 = (uint8_t) ( ( op >> 2 ) & 7 );
                    uint8_t ll = (uint8_t) ( op & 3 );
                    if ( ( 0 == op2 ) && ( 1 == ll ) ) // svc imm16 supervisor call
                        arm64_invoke_svc( *this );
                    else
                        unhandled();
                }
                else
                    unhandled();
                break;
            }
            case 0xd5: // MSR / MRS
            {
                uint64_t bits2322 = opbits( 22, 2 );
                if ( 0 != bits2322 )
                    unhandled();

                if ( 0xd503201f == op ) // nop
                    break;
    
                uint64_t upper20 = opbits( 12, 20 );
                uint64_t lower8 = opbits( 0, 8 );
                if ( ( 0xd5033 == upper20 ) && ( 0xbf == lower8 ) ) // dmb -- no memory barries are needed due to just one thread and core
                    break;
    
                uint64_t l = opbits( 21, 1 );
                uint64_t op0 = opbits( 19, 2 );
                uint64_t op1 = opbits( 16, 3 );
                uint64_t op2 = opbits( 5, 3 );
                uint64_t n = opbits( 12, 4 );
                uint64_t m = opbits( 8, 4 );
                uint64_t t = opbits( 0, 5 );
    
                if ( l ) // MRS <Xt>, (<systemreg>|S<op0>_<op1>_<Cn>_<Cm>_<op2>).   read system register
                {
                    if ( ( 3 == op0 ) && ( 14 == n ) && ( 3 == op1 ) && ( 0 == m ) && ( 2 == op2 ) ) // cntvct_el0 counter-timer virtual count register
                    {
                        system_clock::duration d = system_clock::now().time_since_epoch();
                        regs[ t ] = duration_cast<nanoseconds>( d ).count();
                    }
                    else if ( ( 3 == op0 ) && ( 14 == n ) && ( 3 == op1 ) && ( 0 == m ) && ( 0 == op2 ) ) // cntfrq_el0 counter-timer frequency register
                        regs[ t ] = 1000000000; // nanoseconds = billions of a second 
                    else if ( ( 3 == op0 ) && ( 0 == n ) && ( 3 == op1 ) && ( 0 == m ) && ( 7 == op2 ) ) // DCZID_EL0. data cache block size for dc zva instruction
                        regs[ t ] = 4; // doesn't matter becasuse there is no caching in the emulator
                    else if ( ( 3 == op0 ) && ( 0 == n ) && ( 0 == op1 ) && ( 0 == m ) && ( 0 == op2 ) ) // mrs x, midr_el1
                        regs[ t ] = 0x595a5449; // ITZY // my dev machine: 0x410fd4c0;
                    else if ( ( 3 == op0 ) && ( 13 == n ) && ( 3 == op1 ) && ( 0 == m ) && ( 2 == op2 ) ) // software thread id
                        regs[ t ] = tpidr_el0;
                    else if ( ( 3 == op0 ) && ( 4 == n ) && ( 3 == op1 ) && ( 4 == m ) && ( 0 == op2 ) ) // mrs x, fpcr
                        regs[ t ] = 0;
                    else
                        unhandled();
                }
                else // MSR.   write system register
                {
                    if ( ( 3 == op0 ) && ( 13 == n ) && ( 3 == op1 ) && ( 0 == m ) && ( 2 == op2 ) ) // software thread id
                        tpidr_el0 = regs[ t ];
                    else if ( ( 0 == op0 ) && ( 2 == n ) && ( 3 == op1 ) && ( 4 == m ) && ( 2 == op2 ) )
                    {
                        // branch target identification (ignore)
                    }
                    else if ( ( 1 == op0 ) && ( 7 == n ) && ( 3 == op1 ) && ( 4 == m ) && ( 1 == op2 ) )
                    {
                        // dc zva <Xt>
                        memset( getmem( regs[ t ] ), 0, 4 * 32 );
                    }
                    else if ( ( 0 == op0 ) && ( 2 == n ) && ( 3 == op1 ) && ( 0 == m ) && ( 7 == op2 ) ) // xpaclri
                    {
                        // do nothing
                    }
                    else if ( ( 3 == op0 ) && ( 4 == n ) && ( 3 == op1 ) && ( 4 == m ) && ( 0 == op2 ) ) // msr fpcr, xt
                    {
                        // do nothing
                    }
                    else
                        unhandled();
                }
    
                break;
            }
            case 0x2e: case 0x6e: // CMEQ <Vd>.<T>, <Vn>.<T>, <Vm>.<T>    ;    CMHS <Vd>.<T>, <Vn>.<T>, <Vm>.<T>    ;    UMAXP <Vd>.<T>, <Vn>.<T>, <Vm>.<T>
                                  // BIT <Vd>.<T>, <Vn>.<T>, <Vm>.<T>     ;    UMINP <Vd>.<T>, <Vn>.<T>, <Vm>.<T>   ;    BIF <Vd>.<T>, <Vn>.<T>, <Vm>.<T>
                                  // EOR <Vd>.<T>, <Vn>.<T>, <Vm>.<T>     ;    SUB <Vd>.<T>, <Vn>.<T>, <Vm>.<T>     ;    UMULL{2} <Vd>.<Ta>, <Vn>.<Tb>, <Vm>.<Tb>
                                  // MLS <Vd>.<T>, <Vn>.<T>, <Vm>.<Ts>[<index>] ;  BSL <Vd>.<T>, <Vn>.<T>, <Vm>.<T> ;    FMUL <Vd>.<T>, <Vn>.<T>, <Vm>.<T>
                                  // EXT <Vd>.<T>, <Vn>.<T>, <Vm>.<T>, #<index> ;  INS <Vd>.<Ts>[<index1>], <Vn>.<Ts>[<index2>]  ;    UADDLV <V><d>, <Vn>.<T>
                                  // USHL <Vd>.<T>, <Vn>.<T>, <Vm>.<T>
            {
                uint64_t Q = opbits( 30, 1 );
                uint64_t m = opbits( 16, 5 );
                uint64_t n = opbits( 5, 5 );
                uint64_t d = opbits( 0, 5 );
                uint64_t size = opbits( 22, 2 );
                uint64_t opc2 = opbits( 22, 2 );
                uint64_t bit23 = opbits( 23, 1 );
                uint64_t bit21 = opbits( 21, 1 );
                uint64_t bit15 = opbits( 15, 1 );
                uint64_t bit10 = opbits( 10, 1 );
                uint64_t opcode = opbits( 10, 6 );
                uint64_t esize = 8ull << size;
                uint64_t ebytes = esize / 8;
                uint64_t datasize = 64ull << Q;
                uint64_t elements = datasize / esize;
                uint64_t bits23_21 = opbits( 21, 3 );
                uint64_t opcode7 = opbits( 10, 7 );
                uint64_t bits20_17 = opbits( 17, 4 );
                //tracer.Trace( "elements: %llu, size %llu, esize %llu, datasize %llu, ebytes %llu, opcode %llu\n", elements, size, esize, datasize, ebytes, opcode );

                if ( !bit23 && bit21 && 0x35 == opcode ) // FADDP <Vd>.<T>, <Vn>.<T>, <Vm>.<T>
                {
                    uint64_t sz = opbits( 22, 1 );
                    esize = sz ? 64 : 32;
                    ebytes = esize / 8;
                    elements = datasize / esize;
                    vec16_t target = { 0 };
                    uint8_t * ptarget = (uint8_t *) &target;
                    // tracer.Trace( "faddp, ebytes %llu, elements %llu\n", ebytes, elements );
                    for ( uint64_t e = 0; e < elements / 2; e++ )
                    {
                        if ( 8 == ebytes )
                        {
                            double d1 = vreg_getdouble( m, 8 * 2 * e );
                            double d2 = vreg_getdouble( m, 8 * ( 1 + 2 * e ) );
                            double result = d1 + d2;
                            memcpy( ptarget + e * 8, &result, 8 );
                        }
                        else if ( 4 == ebytes )
                        {
                            float f1 = vreg_getfloat( m, 4 * 2 * e );
                            float f2 = vreg_getfloat( m, 4 * ( 1 + 2 * e ) );
                            float result = f1 + f2;
                            memcpy( ptarget + e * 4, &result, 4 );
                        }
                        else
                            unhandled();
                    }
                    for ( uint64_t e = 0; e < elements / 2; e++ )
                    {
                        if ( 8 == ebytes )
                        {
                            double d1 = vreg_getdouble( n, 8 * 2 * e );
                            double d2 = vreg_getdouble( n, 8 * ( 2 * e + 1 ) );
                            double result = d1 + d2;
                            memcpy( ptarget + ( ( elements / 2 ) + e ) * 8, &result, 8 );
                        }
                        else if ( 4 == ebytes )
                        {
                            float f1 = vreg_getfloat( n, 4 * 2 * e );
                            float f2 = vreg_getfloat( n, 4 * ( 2 * e + 1 ) );
                            float result = f1 + f2;
                            memcpy( ptarget + ( ( elements / 2 ) + e ) * 4, &result, 4 );
                        }
                        else
                            unhandled();
                    }
                    memcpy( vreg_ptr( d, 0 ), ptarget, sizeof( target ) );
                }
                else if ( bit21 && 0x11 == opcode ) // USHL <Vd>.<T>, <Vn>.<T>, <Vm>.<T>
                {
                    uint64_t esize_bytes = esize / 8;
                    vec16_t target = { 0 };
                    uint8_t * ptarget = (uint8_t *) &target;
                    uint8_t * pn = vreg_ptr( n, 0 );
                    for ( uint64_t e = 0; e < elements; e++ )
                    {
                        uint64_t a = 0;
                        memcpy( &a, pn + ( e * esize_bytes ), esize_bytes );
                        int8_t shift = vreg_getui8( m, e * esize_bytes );
                        if ( shift < 0 )
                            a >>= -shift;
                        else
                            a <<= shift;
                        memcpy( ptarget + ( e * esize_bytes ), &a, esize_bytes );
                    }
                    memcpy( vreg_ptr( d, 0 ), ptarget, sizeof( target ) );
                }
                else if ( bit21 && 8 == bits20_17 && 0xe == opcode7 ) // UADDLV <V><d>, <Vn>.<T>
                {
                    uint64_t esize_bytes = esize / 8;
                    uint64_t sum = 0;

                    for ( uint64_t e = 0; e < elements; e++ )
                    {
                        uint64_t a = 0;
                        memcpy( &a, vreg_ptr( n, e * esize_bytes), esize_bytes );
                        sum += a;
                    }

                    zero_vreg( d );
                    vreg_setui64( d, 0, sum );
                }
                else if ( 0x6e == hi8 && 0 == bits23_21 && !bit15 && bit10 )
                {
                    uint64_t imm5 = opbits( 16, 5 );
                    uint64_t imm4 = opbits( 11, 5 );
                    uint64_t byte_width = 0;
                    uint64_t index1 = 0;
                    uint64_t index2 = 0;
                    if ( 1 & imm5 )
                    {
                        index1 = get_bits( imm5, 1, 4 );
                        index2 = imm4;
                        byte_width = 1;
                    }
                    else if ( 2 & imm5 )
                    {
                        index1 = get_bits( imm5, 2, 3 );
                        index2 = get_bits( imm4, 1, 3 );
                        byte_width = 2;
                    }
                    else if ( 4 & imm5 )
                    {
                        index1 = get_bits( imm5, 3, 2 );
                        index2 = get_bits( imm4, 2, 2 );
                        byte_width = 4;
                    }
                    else if ( 8 & imm5 )
                    {
                        index1 = get_bits( imm5, 4, 1 );
                        index1 = get_bits( imm5, 3, 1 );
                        byte_width = 8;
                    }
    
                    memcpy( vreg_ptr( d, index1 * byte_width ), vreg_ptr( n, index2 * byte_width ), byte_width );
                }
                else if ( bit21 && 0x29 == opcode || 0x2b == opcode ) // UMAXP / UMINP
                {
                    vec16_t target = { 0 };
                    uint8_t * ptarget = (uint8_t *) &target;
                    vec16_t ncopy, mcopy;
                    memcpy( &ncopy, vreg_ptr( n, 0 ), sizeof( ncopy ) );
                    memcpy( &mcopy, vreg_ptr( m, 0 ), sizeof( mcopy ) );
                    for ( uint64_t e = 0; e < elements; e += 2 )
                    {
                        uint8_t * pn = & ncopy[ 0 ];
                        uint64_t a = 0;
                        uint64_t b = 0;;
                        memcpy( &a, pn + ( e * ebytes ), ebytes );
                        memcpy( &b, pn + ( ( e + 1 ) * ebytes ), ebytes );
                        uint64_t c = ( 0x2b == opcode ) ? get_min( a, b ) : get_max( a, b );
                        //tracer.Trace( "    writing %#llx + %#llx = %#llx to offset %llu\n", a, b, c, ( e / 2 * ebytes ) );
                        assert( ( ( e / 2 * ebytes ) + ebytes ) <= sizeof( target ) );
                        memcpy( ptarget + ( e / 2 * ebytes ), &c, ebytes );
                    }
                    for ( uint64_t e = 0; e < elements; e += 2 )
                    {
                        uint8_t * pm = & mcopy[ 0 ];
                        uint64_t a = 0;
                        uint64_t b = 0;;
                        memcpy( &a, pm + ( e * ebytes ), ebytes );
                        memcpy( &b, pm + ( ( e + 1 ) * ebytes ), ebytes );
                        uint64_t c = ( 0x2b == opcode ) ? get_min( a, b ) : get_max( a, b );
                        //tracer.Trace( "    writing %#llx + %#llx = %#llx to offset %llu\n", a, b, c, ( ( ( elements + e ) / 2 ) * ebytes ) );
                        assert( ( ( ( ( elements + e ) / 2 ) * ebytes ) + ebytes ) <= sizeof( target ) );
                        memcpy( ptarget + ( ( ( elements + e ) / 2 ) * ebytes ), &c, ebytes );
                    }
                    memcpy( vreg_ptr( d, 0 ), ptarget, sizeof( target ) );
                }
                else if ( bit21 && 7 == opcode && 1 == opc2 ) // BSL
                {
                    elements = Q ? 2 : 1;
                    for ( uint64_t x = 0; x < elements; x++ )
                    {
                        uint64_t dval = vreg_getui64( d, 8 * x );
                        uint64_t nval = vreg_getui64( n, 8 * x );
                        uint64_t mval = vreg_getui64( m, 8 * x );
                        //tracer.Trace( "x: %llu, dval %#llx, nval %#llx, mval %#llx\n", x, dval, nval, mval );
                        uint64_t result = 0;
                        for ( uint64_t b = 0; b < 64; b++ )
                        {
                            uint64_t bbit = ( get_bits( dval, b, 1 ) ) ? get_bits( nval, b, 1 ) : get_bits( mval, b, 1 );
                            result = plaster_bit( result, b, bbit );
                        }
                        //tracer.Trace( "  bsl writing %#llx at offset %llu\n", result, 8 * x );
                        vreg_setui64( d, 8 * x, result );
                    }
                }
                else if ( bit21 && 0x37 == opcode ) // FMUL <Vd>.<T>, <Vn>.<T>, <Vm>.<T>
                {
                    uint64_t sz = opbits( 22, 1 );
                    esize = 32ull << sz;
                    uint64_t esize_bytes = esize / 8;
                    elements = datasize / esize;
                    // tracer.Trace( "fmul sz %llu esize %llu, esize_bytes %llu, elements %llu\n", sz, esize, esize_bytes, elements );
                    vec16_t target = { 0 };
                    uint8_t * ptarget = (uint8_t *) &target;

                    for ( uint64_t e = 0; e < elements; e++ )
                    {
                        if ( 4 == esize_bytes )
                        {
                            float fn = vreg_getfloat( n, e * esize_bytes );
                            float fm = vreg_getfloat( m, e * esize_bytes );
                            float fd = fn * fm;
                            assert( ( ( e * esize_bytes ) + esize_bytes ) <= sizeof( target ) );
                            memcpy( ptarget + e * esize_bytes, &fd, esize_bytes );
                        }
                        else if ( 8 == esize_bytes )
                        {
                            double dn = vreg_getdouble( n, e * esize_bytes );
                            double dm = vreg_getdouble( m, e * esize_bytes );
                            double dd = dn * dm;
                            assert( ( ( e * esize_bytes ) + esize_bytes ) <= sizeof( target ) );
                            memcpy( ptarget + e * esize_bytes, &dd, esize_bytes );
                        }
                        else
                            unhandled();
                    }
                    memcpy( vreg_ptr( d, 0 ), ptarget, sizeof( target ) );
                }
                else if ( !bit21 && 0 == size && !bit10 && !bit15 ) // EXT <Vd>.<T>, <Vn>.<T>, <Vm>.<T>, #<index>
                {
                    uint64_t imm4 = opbits( 11, 4 );
                    uint64_t position = 8 * imm4;

                    if ( Q )
                    {
                        if ( 64 != position ) // not implemented
                            unhandled();

                        uint64_t nval = vreg_getui64( n, 8 );
                        uint64_t mval = vreg_getui64( m, 0 );
                        vreg_setui64( d, 0, nval );
                        vreg_setui64( d, 8, mval );
                    }
                    else
                       unhandled();
                }
                else if ( bit21 )
                {
                    if ( 0x30 == opcode && Q ) // umull{2}
                        elements /= 2;

                    //tracer.Trace( "elements: %llu, ebytes %llu\n", elements, ebytes );
                    vec16_t target = { 0 };
                    uint8_t * ptarget = (uint8_t *) &target;
                    for ( uint64_t e = 0; e < elements; e++ )
                    {
                        uint64_t offset = ( e * ebytes );
                        ElementComparisonResult res = ecr_eq;
                        if ( 0x21 != opcode && 0x07 != opcode && 0x30 != opcode && 0x25 != opcode )
                            res = compare_vector_elements( vreg_ptr( n, offset ), vreg_ptr( m, offset ), ebytes, true );
    
                        if ( 0x23 == opcode ) // CMEQ
                        {
                            assert( ( offset + ebytes ) <= sizeof( target ) );
                            if ( ecr_eq == res )
                                memcpy( ptarget + offset, vec_ones, ebytes );
                            else
                                memcpy( ptarget + offset, vec_zeroes, ebytes );
                        }
                        else if ( 0x0f == opcode ) // CMHS
                        {
                            assert( ( offset + ebytes ) <= sizeof( target ) );
                            if ( ecr_gt == res || ecr_eq == res )
                                memcpy( ptarget + offset, vec_ones, ebytes );
                            else
                                memcpy( ptarget + offset, vec_zeroes, ebytes );
                        }
                        else if ( 0x21 == opcode ) // SUB
                        {
                            if ( ebytes <= 8 )
                            {
                                uint64_t a = 0;
                                uint64_t b = 0;
                                memcpy( &a, vreg_ptr( n, offset ), ebytes );
                                memcpy( &b, vreg_ptr( m, offset ), ebytes );
                                uint64_t result = a - b;
                                assert( ( offset + ebytes ) <= sizeof( target ) );
                                memcpy( ptarget + offset, &result, ebytes );
                            }
                            else
                                unhandled();
                        }
                        else if ( 0x07 == opcode ) // EOR, BIT, BSL, and BIF
                        {
                            if ( ebytes <= 8 )
                            {
                                uint64_t a = 0;
                                uint64_t b = 0;
                                memcpy( &a, vreg_ptr( n, offset ), ebytes );
                                memcpy( &b, vreg_ptr( m, offset ), ebytes );
                                uint64_t result = 0;
                                if ( 0 == opc2 ) // EOR
                                    result = ( a ^ b );
                                else
                                {
                                    if ( 3 == opc2 ) // BIF
                                        b = ~b;
                                    memcpy( &result, vreg_ptr( d, offset ), ebytes );
                                    result = ( result ^ ( ( result ^ a ) & b ) );
                                }
                                assert( ( offset + ebytes ) <= sizeof( target ) );
                                memcpy( ptarget + offset, &result, ebytes );
                            }
                            else
                                unhandled();
                        }
                        else if ( 0x30 == opcode ) // UMULL{2} <Vd>.<Ta>, <Vn>.<Tb>, <Vm>.<Tb>
                        {
                            uint64_t a = 0;
                            uint64_t b = 0;
                            if ( Q )
                                offset += 8; // {2}
                            assert( ebytes <= sizeof( a ) );
                            memcpy( &a, vreg_ptr( n, offset ), ebytes );
                            memcpy( &b, vreg_ptr( m, offset ), ebytes );
                            uint64_t result = a * b;
                            uint64_t output_offset = e * ebytes * 2;
                            assert( ( output_offset + ebytes * 2 ) <= sizeof( target ) );
                            //tracer.Trace( "  umullX read a %#llx, b %#llx, from offset %#llx, ebytes %#llx, output offset: %#llx\n", a, b, offset, ebytes, output_offset );
                            memcpy( ptarget + output_offset, &result, ebytes * 2 );
                        }
                        else if ( 0x25 == opcode ) // MLS <Vd>.<T>, <Vn>.<T>, <Vm>.<T>
                        {
                            uint64_t a = 0;
                            uint64_t b = 0;
                            uint64_t c = 0;
                            memcpy( &a, vreg_ptr( n, offset ), ebytes );
                            memcpy( &b, vreg_ptr( m, offset ), ebytes );
                            memcpy( &c, vreg_ptr( d, offset ), ebytes );

                            if ( 2 == size ) // S datatype not yet implemented
                                unhandled();

                            uint64_t result = c - ( a * b );
                            assert( ( offset + ebytes ) <= sizeof( target ) );
                            memcpy( ptarget + offset, &result, ebytes );
                        }
                        else
                            unhandled();
                    }

                    memcpy( vreg_ptr( d, 0 ), ptarget, sizeof( target ) );
                }
                trace_vregs();
                break;
            }
            case 0x5e: // SCVTF <V><d>, <V><n>    ;    ADDP D<d>, <Vn>.2D    ;    DUP <V><d>, <Vn>.<T>[<index>]
            {
                uint64_t bits23_10 = opbits( 10, 14 );
                uint64_t n = opbits( 5, 5 );
                uint64_t d = opbits( 0, 5 );
    
                if ( 0x0876 == ( bits23_10 & 0x2fff ) )
                {
                    uint64_t sz = opbits( 22, 1 );
                    if ( sz )
                        vregs[ d ].d = (double) (int64_t) vreg_getui64( n, 0 );
                    else
                        vregs[ d ].f = (float) (int32_t) vreg_getui32( n, 0 );
                }
                else if ( 0x3c6e == bits23_10 ) // ADDP D<d>, <Vn>.2D
                {
                    // addp is an integer addition, not floating point addition.
                    uint64_t result = vreg_getui64( n, 0 ) + vreg_getui64( n, 8 );
                    vreg_setui64( d, 8, 0ull );
                    vreg_setui64( d, 0, result );
                }
                else if ( 1 == ( bits23_10 & 0x383f ) ) // DUP <V><d>, <Vn>.<T>[<index>]   -- scalar
                {
                    uint64_t imm5 = opbits( 16, 5 );
                    uint64_t size = lowest_set_bit_nz( imm5 & 0xf );
                    uint64_t index = get_bits( imm5, size + 1, size + 2 ); // imm5:<4:size+1>
                    uint64_t esize = 8ull << size;
                    uint64_t esize_bytes = esize / 8;
                    uint64_t val = 0;
                    memcpy( &val, vreg_ptr( n, index * esize_bytes ), esize_bytes );
                    zero_vreg( d );
                    vreg_setui64( d, 0, val );
                    trace_vregs();
                }
                else
                    unhandled();
                break;
            }
            case 0x7e: // CMGE    ;    UCVTF <V><d>, <V><n>    ;    UCVTF <Hd>, <Hn>    ;    FADDP <V><d>, <Vn>.<T>
            {
                uint64_t bits23_10 = opbits( 10, 14 );
                uint64_t n = opbits( 5, 5 );
                uint64_t d = opbits( 0, 5 );
    
                if ( 0x0c36 == bits23_10 || 0x1c36 == bits23_10 ) // FADDP <V><d>, <Vn>.<T>
                {
                    uint64_t sz = opbits( 22, 1 );
                    if ( sz )
                    {
                        double result = vreg_getdouble( n, 0 ) + vreg_getdouble( n, 8 );
                        //tracer.Trace( "adding %lf + %lf = %lf\n", vreg_getdouble( n, 0 ), vreg_getdouble( n, 8 ), result );
                        zero_vreg( d );
                        vreg_setdouble( d, 0, result );
                    }
                    else
                    {
                        float result = vreg_getfloat( n, 0 ) + vreg_getfloat( n, 4 );
                        //tracer.Trace( "adding %f + %f = %f\n", vreg_getfloat( n, 0 ), vreg_getfloat( n, 4 ), result );
                        zero_vreg( d );
                        vreg_setfloat( d, 0, result );
                    }
                    trace_vregs();
                }
                else if ( 0x3822 == bits23_10 )
                {
                    if ( vregs[ n ].d >= 0 )
                        memset( vreg_ptr( d, 0 ), 0xff, 8 );
                    else
                        memset( vreg_ptr( d, 0 ), 0, 8 );
                }
                else if ( 0x0876 == ( bits23_10 & 0x2fff ) )
                {
                    uint64_t sz = opbits( 22, 1 );
                    if ( sz )
                        vregs[ d ].d = (double) vreg_getui64( n, 0 );
                    else
                        vregs[ d ].f = (float) vreg_getui32( n, 0 );
                }
                else
                    unhandled();
                break;
            }
            case 0x0e: case 0x4e: // DUP <Vd>.<T>, <Vn>.<Ts>[<index>]    ;    DUP <Vd>.<T>, <R><n>    ;             CMEQ <Vd>.<T>, <Vn>.<T>, #0    ;    ADDP <Vd>.<T>, <V
                                  // AND <Vd>.<T>, <Vn>.<T>, <Vm>.<T>    ;    UMOV <Wd>, <Vn>.<Ts>[<index>]    ;    UMOV <Xd>, <Vn>.D[<index>]     ;    CNT <Vd>.<T>, <Vn>.<T>
                                  // AND <Vd>.<T>, <Vn>.<T>, <Vm>.<T>    ;    UMOV <Wd>, <Vn>.<Ts>[<index>]    ;    UMOV <Xd>, <Vn>.D[<index>]     ;    ADDV <V><d>, <Vn>.<T>
                                  // XTN{2} <Vd>.<Tb>, <Vn>.<Ta>         ;    UZP1 <Vd>.<T>, <Vn>.<T>, <Vm>.<T> ;   UZP2 <Vd>.<T>, <Vn>.<T>, <Vm>.<T>
                                  // SMOV <Wd>, <Vn>.<Ts>[<index>]       ;    SMOV <Xd>, <Vn>.<Ts>[<index>]    ;    INS <Vd>.<Ts>[<index>], <R><n> ;    CMGT <Vd>.<T>, <Vn>.<T>, <Vm>.<T>
                                  // SCVTF <Vd>.<T>, <Vn>.<T>            ;    FMLA <Vd>.<T>, <Vn>.<T>, <Vm>.<T>;    FADD <Vd>.<T>, <Vn>.<T>, <Vm>.<T>
                                  // TRN1 <Vd>.<T>, <Vn>.<T>, <Vm>.<T>   ;    TRN2 <Vd>.<T>, <Vn>.<T>, <Vm>.<T> ;   TBL <Vd>.<Ta>, { <Vn>.16B }, <Vm>.<Ta> ; TBL <Vd>.<Ta>, { <Vn>.16B, <Vn+1>.16B, <Vn+2>.16B, <Vn+3>.16B }, <Vm>.<Ta>
                                  // ZIP1 <Vd>.<T>, <Vn>.<T>, <Vm>.<T>   ;    ZIP2 <Vd>.<T>, <Vn>.<T>, <Vm>.<T> ;   SMULL{2} <Vd>.<Ta>, <Vn>.<Tb>, <Vm>.<Tb>
            {
                uint64_t Q = opbits( 30, 1 );
                uint64_t imm5 = opbits( 16, 5 );
                uint64_t bit15 = opbits( 15, 1 );
                uint64_t bits14_11 = opbits( 11, 4 );
                uint64_t bit10 = opbits( 10, 1 );
                uint64_t bit21 = opbits( 21, 1 );
                uint64_t bit23 = opbits( 23, 1 );
                uint64_t bits23_21 = opbits( 21, 3 );
                uint64_t n = opbits( 5, 5 );
                uint64_t d = opbits( 0, 5 );
                uint64_t datasize = 64ull << Q;
                uint64_t bits20_16 = opbits( 16, 5 );
                uint64_t bits14_10 = opbits( 10, 5 );
                uint64_t bits12_10 = opbits( 10, 3 );

                if ( bit21 && bit15 && 8 == bits14_11 && !bit10 ) // SMULL{2} <Vd>.<Ta>, <Vn>.<Tb>, <Vm>.<Tb>
                {
                    uint64_t m = opbits( 16, 5 );
                    uint64_t size = opbits( 22, 2 );
                    uint64_t esize = 8 << size;
                    uint64_t ebytes = esize / 8;
                    datasize = 64;
                    uint64_t part = Q;
                    uint64_t elements = datasize / esize;
                    vec16_t target = { 0 };
                    uint8_t * ptarget = (uint8_t *) &target;

                    for ( uint64_t e = 0; e < elements; e++ )
                    {
                        int64_t nval = 0;
                        memcpy( &nval, vreg_ptr( n, ( part ? 8 : 0 ) + e * ebytes ), ebytes );
                        nval = sign_extend( nval, esize - 1 );
                        int64_t mval = 0;
                        memcpy( &mval, vreg_ptr( m, (part ? 8 : 0 ) + e * ebytes ), ebytes );
                        mval = sign_extend( mval, esize - 1 );
                        int64_t result = nval * mval;
                        memcpy( ptarget + e * ebytes * 2, &result, ebytes * 2 );
                    }
                    memcpy( vreg_ptr( d, 0 ), ptarget, sizeof( target ) );
                }
                else if ( !bit21 && !bit15 && ( 0x1e == bits14_10 || 0xe == bits14_10 ) ) // ZIP1/2 <Vd>.<T>, <Vn>.<T>, <Vm>.<T>
                {
                    uint64_t m = opbits( 16, 5 );
                    uint64_t size = opbits( 22, 2 );
                    uint64_t esize = 8 << size;
                    uint64_t ebytes = esize / 8;
                    uint64_t elements = datasize / esize;
                    uint64_t part = opbits( 14, 1 );
                    uint64_t pairs = elements / 2;
                    uint64_t base_amount = part * pairs;
                    vec16_t target = { 0 };
                    uint8_t * ptarget = (uint8_t *) &target;

                    for ( uint64_t p = 0; p < pairs; p++ )
                    {
                        memcpy( ptarget + 2 * p * ebytes, vreg_ptr( n, ( base_amount + p ) * ebytes ), ebytes );
                        memcpy( ptarget + ( 2 * p + 1 ) * ebytes, vreg_ptr( m, ( base_amount + p ) * ebytes ), ebytes );
                    }
                    memcpy( vreg_ptr( d, 0 ), ptarget, sizeof( target ) );
                }
                else if ( 0 == bits23_21 && !bit15 && 0 == bits12_10 ) // TBL <Vd>.<Ta>, { <Vn>.16B, <Vn+1>.16B, <Vn+2>.16B, <Vn+3>.16B }, 
                {
                    uint64_t m = opbits( 16, 5 );
                    uint64_t len = opbits( 13, 2 );
                    uint64_t elements = datasize / 8;
                    uint64_t reg_count = len + 1;
                    vec16_t src[ 4 ] = {0};
                    for ( uint64_t i = 0; i < reg_count; i++ )
                        memcpy( & src[ i ], vreg_ptr( ( n + i ) % 32, 0 ), sizeof( vec16_t ) );

                    vec16_t target = { 0 };
                    uint8_t * ptarget = (uint8_t *) &target;

                    for ( uint64_t i = 0; i < elements; i++ )
                    {
                        uint64_t index = vreg_getui8( m, i );
                        if ( index < ( 16 * reg_count ) )
                            target[ i ] = vreg_getui8( ( n + ( i / 16 ) ) % 32, index );
                    }

                    memcpy( vreg_ptr( d, 0 ), ptarget, sizeof( target ) );
                }
                else if ( !bit21 && !bit15 && ( 0xd == bits14_11 || 5 == bits14_11 ) && !bit10 ) // TRN1/2 <Vd>.<T>, <Vn>.<T>, <Vm>.<T>
                {
                    uint64_t m = opbits( 16, 5 );
                    uint64_t size = opbits( 22, 2 );
                    uint64_t esize = 8ull << size;
                    uint64_t ebytes = esize / 8;
                    uint64_t elements = datasize / esize;
                    uint64_t pairs = elements / 2;
                    uint64_t part = opbits( 14, 1 ); // TRN1 vs TRN2
                    vec16_t target = { 0 };
                    uint8_t * ptarget = (uint8_t *) &target;

                    for ( uint64_t p = 0; p < pairs; p++ )
                    {
                        memcpy( ptarget + ( ( 2 * p ) * ebytes ),    vreg_ptr( n, ( 2 * p + part ) * ebytes ), ebytes );
                        memcpy( ptarget + ( ( 2 * p + 1 ) * ebytes ), vreg_ptr( m, ( 2 * p + part ) * ebytes ), ebytes );
                    }

                    memcpy( vreg_ptr( d, 0 ), ptarget, sizeof( target ) );
                }
                else if ( !bit23 && bit21 && bit15 && 0xa == bits14_11 && bit10 ) // FADD <Vd>.<T>, <Vn>.<T>, <Vm>.<T>
                {
                    uint64_t sz = opbits( 22, 1 );
                    uint64_t m = opbits( 16, 5 );
                    uint64_t esize = 32ull << sz;
                    uint64_t ebytes = esize / 8;
                    uint64_t elements = datasize / esize;
                    vec16_t target = { 0 };
                    uint8_t * ptarget = (uint8_t *) &target;

                    for ( uint64_t e = 0; e < elements; e++ )
                    {
                        if ( 8 == ebytes )
                        {
                            double dn = vreg_getdouble( n, e * 8 );
                            double dm = vreg_getdouble( m, e * 8 );
                            double result = dn + dm;
                            assert( ( ( 1 + e ) * 8 ) <= sizeof( target ) );
                            memcpy( ptarget + ( e * 8 ), &result, 8 );
                        }
                        else if ( 4 == ebytes )
                        {
                            float fn = vreg_getfloat( n, e * 4 );
                            float fm = vreg_getfloat( m, e * 4 );
                            float result = fn + fm;
                            assert( ( ( 1 + e ) * 4 ) <= sizeof( target ) );
                            memcpy( ptarget + ( e * 4 ), &result, 4 );
                        }
                        else
                            unhandled();
                    }

                    memcpy( vreg_ptr( d, 0 ), ptarget, sizeof( target ) );
                }
                else if ( !bit23 && bit21 && bit15 && 9 == bits14_11 && bit10 ) // FMLA <Vd>.<T>, <Vn>.<T>, <Vm>.<T>
                {
                    uint64_t sz = opbits( 22, 1 );
                    uint64_t m = opbits( 16, 5 );
                    uint64_t esize = 32ull << sz;
                    uint64_t ebytes = esize / 8;
                    uint64_t elements = datasize / esize;
                    vec16_t target = { 0 };
                    uint8_t * ptarget = (uint8_t *) &target;

                    for ( uint64_t e = 0; e < elements; e++ )
                    {
                        if ( 8 == ebytes )
                        {
                            double dn = vreg_getdouble( n, e * 8 );
                            double dm = vreg_getdouble( m, e * 8 );
                            double dd = vreg_getdouble( d, e * 8 );
                            double result = ( dn * dm ) + dd;
                            assert( ( ( 1 + e ) * 8 ) <= sizeof( target ) );
                            memcpy( ptarget + ( e * 8 ), &result, 8 );
                        }
                        else if ( 4 == ebytes )
                        {
                            float fn = vreg_getfloat( n, e * 4 );
                            float fm = vreg_getfloat( m, e * 4 );
                            float fd = vreg_getfloat( d, e * 4 );
                            float result = ( fn * fm ) + fd;
                            assert( ( ( 1 + e ) * 4 ) <= sizeof( target ) );
                            memcpy( ptarget + ( e * 4 ), &result, 4 );
                        }
                        else
                            unhandled();
                    }

                    memcpy( vreg_ptr( d, 0 ), ptarget, sizeof( target ) );
                }
                else if ( !bit23 && bit21 && 1 == bits20_16 && bit15 && 0x16 == bits14_10 ) // SCVTF <Vd>.<T>, <Vn>.<T>
                {
                    uint64_t sz = opbits( 22, 1 );
                    uint64_t esize = 32 << sz;
                    uint64_t esize_bytes = esize / 8;
                    uint64_t elements = datasize / esize;
                    for ( uint64_t e = 0; e < elements; e++ )
                    {
                        if ( 4 == esize_bytes )
                            vreg_setfloat( d, e * esize_bytes, (float) (int32_t) vreg_getui32( n, e * esize_bytes ) );
                        else if ( 8 == esize_bytes )
                            vreg_setdouble( d, e * esize_bytes, (double) (int64_t) vreg_getui64( n, e * esize_bytes ) );
                    }
                }
                else if ( 0x4e == hi8 && 0 == bits23_21 && !bit15 && 3 == bits14_11 && bit10 ) // INS <Vd>.<Ts>[<index>], <R><n>
                {
                    uint64_t index = 0;
                    uint64_t target_bytes = 0;
                    if ( imm5 & 1 )
                    {
                        target_bytes = 1;
                        index = get_bits( imm5, 1, 4 );
                    }
                    else if ( imm5 & 2 )
                    {
                        target_bytes = 2;
                        index = get_bits( imm5, 2, 3 );
                    }
                    else if ( imm5 & 4 )
                    {
                        target_bytes = 4;
                        index = get_bits( imm5, 3, 2 );
                    }
                    else if ( imm5 & 8 )
                    {
                        target_bytes = 8;
                        index = get_bits( imm5, 4, 1 );
                    }
                    else
                        unhandled();

                    uint64_t src = regs[ n ];
                    if ( 4 != ( imm5 & 0xf ) )
                        src &= 0xffffffff;
                    memcpy( vreg_ptr( d, index * target_bytes ), &src, target_bytes );
                }
                else if ( !bit21 && !bit15 && ( 7 == bits14_11 || 5 == bits14_11 ) && bit10 )
                {
                    // UMOV <Wd>, <Vn>.<Ts>[<index>]    ;    UMOV <Xd>, <Vn>.D[<index>]    ;     SMOV <Wd>, <Vn>.<Ts>[<index>]    ;   
                    uint64_t size = lowest_set_bit_nz( imm5 & ( ( 7 == bits14_11 ) ? 0xf : 7 ) );
                    uint64_t esize = 8ull << size;
                    uint64_t esize_bytes = esize / 8;
                    datasize = 32ull << Q;
                    uint64_t bits_to_copy = 4 - size;
                    uint64_t index = get_bits( imm5, 4 + 1 - bits_to_copy, bits_to_copy );
                    // tracer.Trace( "mov, size %llu, esize %llu, esize_bytes %llu, datasize %llu, index %llu\n", size, esize, esize_bytes, datasize, index );

                    uint64_t val = 0;
                    memcpy( &val, vreg_ptr( n, esize_bytes * index ), esize_bytes );
                    if ( 5 == bits14_11 )
                        val = sign_extend( val, esize - 1 );
                    if ( 31 != d )
                        regs[ d ] = Q ? val : ( val & 0xffffffff );
                }
                else if ( 1 == bits23_21 && !bit15 && 3 == bits14_11 && bit10 ) // AND <Vd>.<T>, 
                {
                    uint64_t m = imm5;
                    uint64_t lo = vreg_getui64( n, 0 ) & vreg_getui64( m, 0 );
                    uint64_t hi = 0;
                    if ( Q )
                        hi = vreg_getui64( n, 8 ) & vreg_getui64( m, 8 );
                    vreg_setui64( d, 0, lo );
                    vreg_setui64( d, 8, hi );
                }
                else if ( !bit21 && !bit15 && ( 0x3 == bits14_11 || 0xb == bits14_11 ) && !bit10 ) // UZP2 <Vd>.<T>, <Vn>.<T>, <Vm>.<T>    ;    UZP1 <Vd>.<T>, <Vn>.<T>, <Vm>.<T>
                {
                    uint64_t size = opbits( 22, 2 );
                    uint64_t m = imm5;
                    uint64_t esize = 8ull << size;
                    uint64_t esize_bytes = esize / 8;
                    uint64_t elements = datasize / esize;
                    uint64_t part = opbits( 14, 1 ); // UZP2 is 1, UZP1 is 0
                    vec16_t target = { 0 };
                    uint8_t * ptarget = (uint8_t *) &target;
                    uint64_t second_offset = elements / 2 * esize_bytes;
                    //tracer.Trace( "elements %llu, esize_bytes %llu, second_offset %llu\n", elements, esize_bytes, second_offset );
                    for ( uint64_t e = 0; e < elements / 2; e++ )
                    {
                        assert( ( ( e * esize_bytes ) + esize_bytes ) <= sizeof( target ) );
                        memcpy( ptarget + e * esize_bytes, vreg_ptr( n, ( e * 2 + part ) * esize_bytes ), esize_bytes ); // odd or even from n into lower half of d
                        assert( ( second_offset + ( e * esize_bytes ) + esize_bytes ) <= sizeof( target ) );
                        memcpy( ptarget + second_offset + e * esize_bytes, vreg_ptr( m, ( e * 2 + part ) * esize_bytes ), esize_bytes ); // odd or even from m into upper half of d
                    }
                    memcpy( vreg_ptr( d, 0 ), ptarget, sizeof( target ) );
                }
                else if ( 5 == bits23_21 && !bit15 && 3 == bits14_11 && bit10 ) // ORR <Vd>.<T>, <Vn>.<T>, <Vm>.<T>
                {
                    uint64_t m = imm5;
                    uint64_t lo = vreg_getui64( n, 0 ) | vreg_getui64( m, 0 );
                    uint64_t hi = 0;
                    if ( Q )
                        hi = vreg_getui64( n, 8 ) | vreg_getui64( m, 8 );
                    vreg_setui64( d, 0, lo );
                    vreg_setui64( d, 8, hi );
                }
                else if ( 0 == bits23_21 && !bit15 && 1 == bits14_11 && bit10 ) // DUP <Vd>.<T>, <R><n>
                {
                    uint64_t size = lowest_set_bit_nz( imm5 & 0xf );
                    uint64_t esize = 8ull << size;
                    uint64_t elements = datasize / esize;
                    uint64_t val = val_reg_or_zr( n );
                    uint8_t * pmem = vregs[ d ].b16;
                    uint64_t bytesize = esize / 8;
                    memset( pmem, 0, sizeof( vregs[ d ].b16 ) );
                    for ( uint64_t e = 0; e < elements; e++ )
                        memcpy( pmem + ( e * bytesize ), &val, bytesize );
                    //tracer.TraceBinaryData( vregs[ d ].b16, sizeof( vregs[ d ].b16 ), 4 );
                }
                else if ( 0 == bits23_21 && !bit15 && 0 == bits14_11 && bit10 ) // DUP <Vd>.<T>, <Vn>.<Ts>[<index>]
                {
                    uint64_t size = lowest_set_bit_nz( imm5 & 0xf );
                    uint64_t index = get_bits( imm5, size + 1, 4 - ( size + 1 ) + 1 );
                    uint64_t esize = 8ull << size;
                    uint64_t ebytes = esize / 8;
                    uint64_t elements = datasize / esize;
                    uint64_t element = 0;
                    //tracer.Trace( "index %llu, indbytes %llu, ebytes: %llu, elements %llu\n", index, indbytes, ebytes, elements );
                    memcpy( &element, vreg_ptr( n, index * ebytes ), ebytes );
                    for ( uint64_t e = 0; e < elements; e++ )
                        memcpy( vreg_ptr( d, e * ebytes ), &element, ebytes );
                }
                else if ( bit21 && bit15 && 3 == bits14_11 && !bit10 && 0 == bits20_16 )  // CMEQ <Vd>.<T>, <Vn>.<T>, #
                {
                    uint64_t size = opbits( 22, 2 );
                    uint64_t esize = 8ull << size;
                    uint64_t bytesize = esize / 8;
                    uint64_t elements = datasize / esize;

                    uint8_t * pn = vregs[ n ].b16;
                    uint8_t * pd = vregs[ d ].b16;
                    for ( uint64_t e = 0; e < elements; e++ )
                    {
                        if ( 0 == memcmp( pn + ( e * bytesize ), vec_zeroes, bytesize ) )
                            memcpy( pd + ( e * bytesize ), vec_ones, bytesize );
                        else
                            memcpy( pd + ( e * bytesize ), vec_zeroes, bytesize );
                    }
                }
                else if ( bit21 && !bit15 && 6 == bits14_11 && bit10 ) // CMGT <Vd>.<T>, <Vn>.<T>, <Vm>.<T>
                {
                    uint64_t m = opbits( 16, 5 );
                    uint64_t size = opbits( 22, 2 );
                    uint64_t esize = 8ull << size;
                    uint64_t bytesize = esize / 8;
                    uint64_t elements = datasize / esize;
                    uint64_t zeroes = 0;
                    uint64_t ones = ~0ull;

                    for ( uint64_t e = 0; e < elements; e += 2 )
                    {
                        uint8_t * pn = vregs[ n ].b16;
                        int64_t a = 0;
                        int64_t b = 0;;
                        memcpy( &a, pn + ( e * bytesize ), bytesize );
                        a = sign_extend( a, esize );
                        uint8_t * pm = vregs[ m ].b16;
                        memcpy( &b, pm + ( e * bytesize ), bytesize );
                        b = sign_extend( b, esize );
                        assert( ( ( e + 1 ) * bytesize ) <= sizeof( vec16_t ) );
                        memcpy( vreg_ptr( d, e * bytesize ), ( a > b ) ? &ones : &zeroes, bytesize );
                    }
                }
                else if ( bit21 && bit15 && 7 == bits14_11 && bit10 ) // ADDP <Vd>.<T>, <Vn>.<T>, <Vm>.<T>
                {
                    uint64_t size = opbits( 22, 2 );
                    uint64_t esize = 8ull << size;
                    uint64_t m = opbits( 16, 5 );
                    uint64_t bytesize = esize / 8;
                    uint64_t elements = datasize / esize;
                    //tracer.Trace( "elements: %llu, bytesize %llu\n", elements, bytesize );

                    vec16_t target = { 0 };
                    uint8_t * ptarget = (uint8_t *) &target;
                    for ( uint64_t e = 0; e < elements; e += 2 )
                    {
                        uint8_t * pn = vregs[ n ].b16;
                        uint64_t a = 0;
                        uint64_t b = 0;;
                        memcpy( &a, pn + ( e * bytesize ), bytesize );
                        memcpy( &b, pn + ( ( e + 1 ) * bytesize ), bytesize );
                        uint64_t c = a + b;
                        //tracer.Trace( "    writing %#llx + %#llx = %#llx to offset %llu\n", a, b, c, ( e / 2 * bytesize ) );
                        assert( ( ( 1 + ( e / 2 ) ) * bytesize ) <= sizeof( target ) );
                        memcpy( ptarget + ( e / 2 * bytesize ), &c, bytesize );
                    }
                    for ( uint64_t e = 0; e < elements; e += 2 )
                    {
                        uint8_t * pm = vregs[ m ].b16;
                        uint64_t a = 0;
                        uint64_t b = 0;;
                        memcpy( &a, pm + ( e * bytesize ), bytesize );
                        memcpy( &b, pm + ( ( e + 1 ) * bytesize ), bytesize );
                        uint64_t c = a + b;
                        //tracer.Trace( "    writing %#llx + %#llx = %#llx to offset %llu\n", a, b, c, ( ( ( elements + e ) / 2 ) * bytesize ) );
                        assert( ( ( 1 + ( ( elements + e ) / 2 ) ) * bytesize ) <= sizeof( target ) );
                        memcpy( ptarget + ( ( ( elements + e ) / 2 ) * bytesize ), &c, bytesize );
                    }
                    memcpy( vreg_ptr( d, 0 ), &target, sizeof( target ) );
                }
                else if ( ( 0x4e == hi8 || 0x0e == hi8 ) && bit21 && bit15 && 0 == bits14_11 && bit10 ) // ADD <Vd>.<T>, <Vn>.<T>, <Vm>.<T>.   add vector
                {
                    uint64_t size = opbits( 22, 2 );
                    uint64_t esize = 8ull << size;
                    uint64_t m = opbits( 16, 5 );
                    uint64_t bytesize = esize / 8;
                    uint64_t elements = datasize / esize;
                    vec16_t target = { 0 };
                    uint8_t * ptarget = (uint8_t *) &target;
                    //tracer.Trace( "elements: %llu, bytesize %llu, size %llu, esize %llu\n", elements, bytesize, size, esize );

                    uint8_t * pn = vregs[ n ].b16;
                    uint8_t * pm = vregs[ m ].b16;
                    for ( uint64_t e = 0; e < elements; e++ )
                    {
                        // even though the arm64 doc says the types can be S and D, always do integer addition
                        uint64_t a = 0;
                        uint64_t b = 0;
                        memcpy( &a, pn + ( e * bytesize ), bytesize );
                        memcpy( &b, pm + ( e * bytesize ), bytesize );
                        uint64_t c = a + b;
                        assert( ( ( 1 + e ) * bytesize ) <= sizeof( target ) );
                        memcpy( ptarget + ( e * bytesize ), &c, bytesize );
                    }

                    memcpy( vreg_ptr( d, 0 ), &target, sizeof( target ) );
                }
                else if ( bit21 && 0xb == bits14_11 && 0 == bits20_16 && !bit15 ) // CNT <Vd>.<T>, <Vn>.<T>
                {
                    uint64_t size = opbits( 22, 2 );
                    if ( 0 != size )
                        unhandled();

                    uint64_t bytes = ( 0 == Q ) ? 8 : 16;
                    uint64_t bitcount = 0;
                    for ( uint64_t x = 0; x < bytes; x += 8 )
                        bitcount += count_bits( vreg_getui64( n, x ) );
                    zero_vreg( d );
                    vreg_setui64( d, 0, bitcount );
                }
                else if ( ( 0x4e == hi8 || 0x0e == hi8 ) && bit21 && 0x11 == bits20_16 && bit15 && 7 == bits14_11 ) // ADDV <V><d>, <Vn>.<T>
                {
                    uint64_t size = opbits( 22, 2 );
                    if ( 3 == size )
                        unhandled();

                    // even though arm64 doc says types can include S, always use integer math
                    uint64_t esize = 8ull << size;
                    uint64_t esize_bytes = esize / 8;
                    uint64_t elements = ( Q ? 16 : 8 ) / esize_bytes;
                    uint64_t total = 0;
                    for ( uint64_t x = 0; x < elements; x++ )
                    {
                        uint64_t v = 0;
                        assert( ( ( x + 1 ) * esize_bytes ) <= 16 );
                        memcpy( &v, vreg_ptr( n, x * esize_bytes ), esize_bytes );
                        total += v;
                    }
                    zero_vreg( d );
                    memcpy( vreg_ptr( d, 0 ), &total, esize_bytes );
                }
                else if ( bit21 && 1 == bits20_16 && !bit15 && 5 == bits14_11 && !bit10 ) // xtn, xtn2 XTN{2} <Vd>.<Tb>, <Vn>.<Ta>
                {
                    uint64_t size = opbits( 22, 2 );
                    if ( 3 == size )
                        unhandled();

                    uint64_t target_esize = 8ull << size;
                    uint64_t source_esize_bytes = target_esize * 2 / 8;
                    uint64_t target_esize_bytes = target_esize / 8;
                    uint64_t elements = 64 / target_esize;
                    uint64_t result = 0;
                    uint8_t * psrc = vreg_ptr( n, 0 );
                    //tracer.Trace( "  xtn. Q %llu, elements %llu, target_esize %llu, size %llu\n", Q, elements, target_esize, size );
                    assert( target_esize_bytes <= sizeof( result ) );

                    for ( uint64_t x = 0; x < elements; x++ )
                    {
                        assert( ( ( x * target_esize_bytes ) + target_esize_bytes ) <= sizeof( result ) );
                        memcpy( ( (uint8_t *) &result ) + x * target_esize_bytes, psrc + x * source_esize_bytes, target_esize_bytes );
                    }

                    if ( Q )
                        memcpy( vreg_ptr( d, 8 ), &result, 8 ); // don't modifiy the lower half
                    else
                    {
                        zero_vreg( d ); // zero the upper half
                        memcpy( vreg_ptr( d, 0 ), &result, 8 );
                    }
                }
                else
                    unhandled();

                trace_vregs();
                break;
            }
            case 0x1e: // FMOV <Wd>, <Hn>    ;    FMUL                ;    FMOV <Wd>, imm       ;    FCVTZU <Wd>, <Dn>    ;    FRINTA <Dd>, <Dn>
            case 0x9e: // FMOV <Xd>, <Hn>    ;    UCVTF <Hd>, <Dn>    ;    FCVTZU <Xd>, <Dn>    ;    FCVTAS <Xd>, <Dn>
            {
                uint64_t sf = opbits( 31, 1 );
                uint64_t ftype = opbits( 22, 2 );
                uint64_t bit21 = opbits( 21, 1 );
                uint64_t bit11 = opbits( 11, 1 );
                uint64_t bit10 = opbits( 10, 1 );
                uint64_t bit4 = opbits( 4, 1 );
                uint64_t bits21_19 = opbits( 19, 3 );
                uint64_t rmode = opbits( 19, 2 );
                uint64_t bits18_16 = opbits( 16, 3 );
                uint64_t bits18_10 = opbits( 10, 9 );
                uint64_t n = opbits( 5, 5 );
                uint64_t d = opbits( 0, 5 );

                if ( 0x1e == hi8 && bit21 && !bit11 && bit10 && bit4 ) // FCCMPE <Sn>, <Sm>, #<nzcv>, <cond>    ;    FCCMPE <Dn>, <Dm>, #<nzcv>, <cond>
                {
                    uint64_t m = opbits( 16, 5 );
                    uint64_t cond = opbits( 12, 4 );

                    if ( check_conditional( cond ) )
                    {
                        //tracer.Trace( "condition holds, so doing compare\n" );
                        double result = 0.0;
                        if ( 0 == ftype )
                            result = (double) ( vregs[ n ].f - vregs[ m ].f );
                        else if ( 1 == ftype )
                            result = vregs[ n ].d - vregs[ m ].d;
                        else
                            unhandled();
    
                        set_flags_from_double( result );
                    }
                }
                else if ( 3 == bits21_19 && 0 == bits18_16 ) // FCVTZS <Xd>, <Dn>, #<fbits>
                {
                    if ( 31 == d )
                        break;
                    uint64_t scale = opbits( 10, 6 );
                    uint64_t fracbits = 64 - scale;
                    double src = 0.0;
                    if ( 0 == ftype )
                        src = vreg_getfloat( n, 0 );
                    else if ( 1 == ftype )
                        src = vreg_getdouble( n, 0 );
                    else
                        unhandled();

                    uint64_t result = 0;

                    if ( sf )
                        result = double_to_fixed_int64( src, fracbits, rmode );
                    else
                        result = (uint64_t) (uint32_t) double_to_fixed_int64( src, fracbits, rmode );

                    regs[ d ] = result;
                }
                else if ( 4 == bits21_19 && 0x100 == bits18_10 ) // FCVTAS <Xd>, <Dn>
                {
                    if ( !sf && 0 == ftype )
                        regs[ d ] = (uint32_t) (int32_t) round( vreg_getfloat( n, 0 ) );
                    else if ( sf && 0 == ftype )
                        regs[ d ] = (uint64_t) (int64_t) (int32_t) round( vreg_getfloat( n, 0 ) );
                    else if ( !sf && 1 == ftype )
                        regs[ d ] = (uint32_t) (int32_t) round( vreg_getdouble( n, 0 ) );
                    else if ( sf && 1 == ftype )
                        regs[ d ] = (uint64_t) (int64_t) (int32_t) round( vreg_getdouble( n, 0 ) );
                    else
                        unhandled();
                }
                else if ( 0x1e == hi8 && 4 == bits21_19 && 0x190 == bits18_10 ) // FRINTA <Dd>, <Dn>
                {
                    if ( 0 == ftype )
                        vreg_setfloat( d, 0, (float) round( vreg_getfloat( n, 0 ) ) );
                    else if ( 1 == ftype )
                        vreg_setdouble( d, 0, (double) round( vreg_getdouble( n, 0 ) ) );
                    else
                        unhandled();
                    trace_vregs();
                }
                else if ( ( 0x180 == ( bits18_10 & 0x1bf ) ) && ( bit21 ) && ( 0 == ( rmode & 2 ) ) ) // fmov reg, vreg  OR mov vreg, reg
                {
                    uint64_t opcode = opbits( 16, 3 );
                    uint64_t nval = val_reg_or_zr( n );
                    if ( 0 == sf )
                    {
                        if ( 0 != rmode )
                            unhandled();
    
                        if ( 3 == ftype )
                        {
                            if (  6 == opcode )
                                regs[ d ] = vregs[ n ].h;
                            else if ( 7 == opcode )
                            {
                                zero_vreg( d );
                                vregs[ d ].h = (uint16_t) nval;
                            }
                            else
                                unhandled();
                        }
                        else if ( 0 == ftype )
                        {
                            if ( 7 == opcode )
                            {
                                zero_vreg( d );
                                memcpy( vreg_ptr( d, 0 ), & nval, 4 );
                            }
                            else if ( 6 == opcode )
                            {
                                regs[ d ] = 0;
                                memcpy( & regs[ d ], vreg_ptr( n, 0 ), 4 );
                            }
                            else
                                unhandled();
                        }
                        else
                            unhandled();
                    }
                    else
                    {
                        if ( 0 == rmode )
                        {
                            if ( 3 == ftype && 6 == opcode )
                                regs[ d ] = vregs[ n ].h;
                            else if ( 3 == ftype && 7 == opcode )
                            {
                                zero_vreg( d );
                                memcpy( vreg_ptr( d, 0 ), & nval, 2 );
                            }
                            else if ( 1 == ftype && 7 == opcode )
                            {
                                zero_vreg( d );
                                memcpy( vreg_ptr( d, 0 ), & nval, 8 );
                            }
                            else if ( 1 == ftype && 6 == opcode )
                                memcpy( & regs[ d ], vreg_ptr( n, 0 ), 8 );
                            else
                                unhandled();
                        }
                        else
                        {
                            if ( 2 == ftype && 7 == opcode )
                                memcpy( vreg_ptr( d, 8 ), & nval, 8 );
                            else if ( 2 == ftype && 6 == opcode )
                                memcpy( & regs[ d ], vreg_ptr( n, 8 ), 8 );
                            else
                                unhandled();
                        }
                    }
                }
                else if ( 0x40 == bits18_10 && bit21 && 3 == rmode ) // FCVTZU <Wd>, <Dn>
                {
                    if ( 31 == d )
                        break;

                    double src = 0.0;
                    if ( 0 == ftype )
                        src = vreg_getfloat( n, 0 );
                    else if ( 1 == ftype )
                        src = vreg_getdouble( n, 0 );
                    else
                        unhandled();

                    uint64_t result = 0;
                    if ( src > 0.0 )
                    {
                        if ( sf )
                        {
                            if ( src > (double) UINT64_MAX )
                                result = UINT64_MAX;
                            else
                                result = (uint64_t) src;
                        }
                        else
                        {
                            if ( src > (double) UINT32_MAX )
                                result = UINT32_MAX;
                            else
                                result = (uint32_t) src;
                        }
                    }
                    regs[ d ] = result;
                }
                else if ( ( 0x40 == ( bits18_10 & 0x1c0 ) ) && !bit21 && 3 == rmode ) // FCVTZU <Wd>, <Dn>, #<fbits>
                {
                    double src = 0.0;
                    if ( 31 == d )
                        break;

                    if ( 0 == ftype )
                        src = vregs[ n ].f;
                    else if ( 1 == ftype )
                        src = vregs[ n ].d;
                    else
                        unhandled();

                    uint64_t result = 0;

                    if ( src > 0.0 )
                    {
                        uint64_t scale = opbits( 10, 6 );
                        uint64_t fracbits = 64 - scale;

                        if ( sf )
                        {
                            if ( src > (double) UINT64_MAX )
                                result = UINT64_MAX;
                            else
                                result = double_to_fixed_uint64( src, fracbits, rmode );
                        }
                        else
                        {
                            if ( src > (double) UINT32_MAX )
                                result = UINT32_MAX;
                            else
                                result = double_to_fixed_uint32( src, fracbits, rmode );
                        }
                    }

                    regs[ d ] = result;
                }
                else if ( ( 0x1e == hi8 ) && ( 4 == ( bits18_10 & 7 ) ) && ( bit21 ) ) // fmov scalar immediate
                {
                    uint64_t fltsize = ( 2 == ftype ) ? 64 : ( 8 << ( ftype ^ 2 ) );
                    uint64_t imm8 = opbits( 13, 8 );
                    uint64_t val = vfp_expand_imm( imm8, fltsize );
                    memset( & vregs[ d ], 0, sizeof( vregs[ d ] ) );
                    memcpy( & vregs[ d ], & val, fltsize / 8 );
                }
                else if ( ( 0x1e == hi8 ) && ( 2 == ( bits18_10 & 0x3f ) ) && ( bit21 ) ) // fmul (scalar)
                {
                    uint64_t m = opbits( 16, 5 );
                    if ( 0 == ftype ) // single-precision
                    {
                        vregs[ d ].f = vregs[ n ].f * vregs[ m ].f;
                        memset( vreg_ptr( d, 4 ), 0, 12 );
                    }
                    else if ( 1 == ftype ) // double-precision
                    {
                        vregs[ d ].d = vregs[ n ].d * vregs[ m ].d;
                        memset( vreg_ptr( d, 8 ), 0, 8 );
                    }
                    else
                        unhandled();
                    trace_vregs();
                }
                else if ( ( 0x1e == hi8 ) && ( 0x90 == ( bits18_10 & 0x19f ) ) && ( bit21 ) ) // fcvt
                {
                    uint64_t opc = opbits( 15, 2 );
                    if ( 0 == ftype )
                    {
                        if ( 1 == opc ) // single to double
                        {
                            vregs[ d ].d = (double) vregs[ n ].f;
                            memset( vreg_ptr( d, 8 ), 0, 8 );
                        }
                        else
                            unhandled();
                    }
                    else if ( 1 == ftype )
                    {
                        if ( 0 == opc ) // double to single
                        {
                            vregs[ d ].f = (float) vregs[ n ].d;
                            memset( vreg_ptr( d, 4 ), 0, 12 );
                        }
                        else
                            unhandled();
                    }
                    else
                        unhandled();

                    trace_vregs();
                }
                else if ( ( 0x1e == hi8 ) && ( 0x10 == bits18_10 ) && ( 4 == bits21_19 ) ) // fmov
                {
                    memcpy( vreg_ptr( d, 0 ), vreg_ptr( n, 0 ), sizeof( vec16_t ) );
                }
                else if ( ( 0x1e == hi8 ) && ( 8 == ( bits18_10 & 0x3f ) ) && ( bit21 ) ) // fcmp and fcmpe (no signaling yet)
                {
                    uint64_t m = opbits( 16, 5 );
                    uint64_t opc = opbits( 3, 2 );
                    double result = 0.0;

                    if ( 3 == ftype && ( ( 0 == opc ) || ( 2 == opc ) ) )
                        unhandled(); // Hn, Hm
                    else if ( 3 == ftype && ( 0 == m && ( ( 1 == opc ) || 3 == opc ) ) )
                        unhandled(); // Hn, 0.0
                    else if ( 0 == ftype && ( ( 0 == opc ) || ( 2 == opc ) ) )
                        result = vregs[ n ].f - vregs[ m ].f;
                    else if ( 0 == ftype && 0 == m && ( ( 1 == opc ) || ( 3 == opc ) ) )
                        result = vregs[ n ].f - 0.0f;
                    else if ( 1 == ftype && ( ( 0 == opc ) || ( 2 == opc ) ) )
                        result = vregs[ n ].d - vregs[ m ].d;
                    else if ( 1 == ftype && 0 == m && ( ( 1 == opc ) || ( 3 == opc ) ) )
                        result = vregs[ n ].d - 0.0;
                    else
                        unhandled();

                    set_flags_from_double( result );
                }
                else if ( ( 0x1e == hi8 ) && ( 0x30 == bits18_10 ) && ( 4 == bits21_19 ) ) // fabs (scalar)
                {
                    if ( 3 == ftype )
                        unhandled();
                    else if ( 0 == ftype )
                    {
                        vregs[ d ].f = (float) fabs( vregs[ n ].f );
                        memset( vreg_ptr( d, 4 ), 0, 12 );
                    }
                    else if ( 1 == ftype )
                    {
                        vregs[ d ].d = fabs( vregs[ n ].d );
                        memset( vreg_ptr( d, 8 ), 0, 8 );
                    }
                    else
                        unhandled();
                }
                else if ( 0x1e == hi8 && ( 6 == ( 0x3f & bits18_10 ) ) && bit21 ) // fdiv v, v, v
                {
                    uint64_t m = opbits( 16, 5 );
                    if ( 0 == ftype ) // single-precision
                        vregs[ d ].f = vregs[ n ].f / vregs[ m ].f;
                    else if ( 1 == ftype ) // double-precision
                        vregs[ d ].d = vregs[ n ].d / vregs[ m ].d;
                    else
                        unhandled();
                    trace_vregs();
                }
                else if ( 0x1e == hi8 && ( 0xa == ( 0x3f & bits18_10 ) ) && bit21 ) // fadd v, v, v
                {
                    uint64_t m = opbits( 16, 5 );
                    if ( 0 == ftype ) // single-precision
                        vregs[ d ].f = vregs[ n ].f + vregs[ m ].f;
                    else if ( 1 == ftype ) // double-precision
                        vregs[ d ].d = vregs[ n ].d + vregs[ m ].d;
                    else
                        unhandled();
                }
                else if ( 0x1e == hi8 && ( 0xe == ( 0x3f & bits18_10 ) ) && bit21 ) // fsub v, v, v
                {
                    uint64_t m = opbits( 16, 5 );
                    if ( 0 == ftype ) // single-precision
                        vregs[ d ].f = vregs[ n ].f - vregs[ m ].f;
                    else if ( 1 == ftype ) // double-precision
                        vregs[ d ].d = vregs[ n ].d - vregs[ m ].d;
                    else
                        unhandled();
                }
                else if ( 0x80 == bits18_10 && bit21 && 0 == rmode ) // SCVTF (scalar, integer)
                {
                    uint64_t nval = val_reg_or_zr( n );
                    if ( !sf )
                        nval &= 0xffffffff;

                    zero_vreg( d );
                    if ( 0 == ftype )
                    {
                        float f = (float) (int32_t) nval;
                        memcpy( vreg_ptr( d, 0 ), &f, sizeof( f ) );
                    }
                    else if ( 1 == ftype )
                    {
                        double doubleval = (double) (int64_t) nval;
                        memcpy( vreg_ptr( d, 0 ), &doubleval, sizeof( doubleval ) );
                    }
                    else
                        unhandled();
                }
                else if ( 0x70 == bits18_10 && bit21 && 0 == rmode ) // fsqrt s#, s#
                {
                    if ( 0 == ftype )
                        vregs[ d ].f = sqrtf( vregs[ n ].f );
                    else if ( 1 == ftype )
                        vregs[ d ].d = sqrt( vregs[ n ].d );
                    else
                        unhandled();
                }
                else if ( bit21 && ( 3 == ( 3 & bits18_10 ) ) ) // fcsel
                {
                    uint64_t m = opbits( 16, 5 );
                    uint64_t cond = opbits( 12, 4 );
                    bool met = check_conditional( cond );

                    if ( 0 == ftype )
                        vregs[ d ].f = met ? vregs[ n ].f : vregs[ m ].f;
                    else if ( 1 == ftype )
                        vregs[ d ].d = met ? vregs[ n ].d : vregs[ m ].d;
                    else
                        unhandled();
                }
                else if ( bit21 && ( 0x50 == bits18_10 ) ) // fneg (scalar)
                {
                    if ( 0 == ftype )
                        vregs[ d ].f = - vregs[ n ].f;
                    else if ( 1 == ftype )
                        vregs[ d ].d = - vregs[ n ].d;
                    else
                        unhandled();
                }
                else if ( bit21 && 0 == bits18_10 && 3 == rmode ) // fcvtzs
                {
                    if ( 0 == ftype )
                    {
                        if ( sf )
                            regs[ d ] = (uint64_t) floor( vregs[ n ].f );
                        else
                            regs[ d ] = (uint32_t) floor( vregs[ n ].f );
                    }
                    else if ( 1 == ftype )
                    {
                        if ( sf )
                            regs[ d ] = (uint64_t) floor( vregs[ n ].d );
                        else
                            regs[ d ] = (uint32_t) floor( vregs[ n ].d );
                    }
                    else
                        unhandled();
                }
                else if ( bit21 && ( 1 == ( bits18_10 & 3 ) ) && ( 0 == opbits( 4, 1 ) ) ) // fccmp
                {
                    uint64_t m = opbits( 16, 5 );
                    uint64_t cond = opbits( 12, 4 );
                    double result = 0.0;

                    if ( 0 == ftype )
                        result = vregs[ n ].f - vregs[ m ].f;
                    else if ( 1 == ftype )
                        result = vregs[ n ].d - vregs[ m ].d;
                    else
                        unhandled();

                    set_flags_from_double( result );

                    if ( ! check_conditional( cond ) )
                    {
                        uint64_t nzcv = opbits( 0, 4 );
                        set_flags_from_nzcv( nzcv );
                    }
                }
                else if ( bit21 && ( 0xc0 == ( 0x1c0 & bits18_10 ) ) && 0 == rmode ) // UCVTF <Hd>, <Wn>, #<fbits>
                {
                    uint64_t val = val_reg_or_zr( n );
                    if ( 0 == sf )
                        val &= 0xffffffff;

                    zero_vreg( d );

                    if ( 0 == ftype )
                        vregs[ d ].f = (float) val;
                    else if ( 1 == ftype )
                        vregs[ d ].d = (double) val;
                    else
                        unhandled();
                }
                else
                    unhandled();
                break;
            }
            case 0x4c: // LD1 { <Vt>.<T> }, [<Xn|SP>]    ;    LD2 { <Vt>.<T>, <Vt2>.<T> }, [<Xn|SP>]
                       // ST2 { <Vt>.<T>, <Vt2>.<T> }, [<Xn|SP>]    ;    ST2 { <Vt>.<T>, <Vt2>.<T> }, [<Xn|SP>], <imm>    ;    ST2 { <Vt>.<T>, <Vt2>.<T> }, [<Xn|SP>], <Xm>
                       // LD3 { <Vt>.<T>, <Vt2>.<T>, <Vt3>.<T> }, [<Xn|SP>]
                       // LD3 { <Vt>.<T>, <Vt2>.<T>, <Vt3>.<T> }, [<Xn|SP>], <imm>
                       // LD3 { <Vt>.<T>, <Vt2>.<T>, <Vt3>.<T> }, [<Xn|SP>], <Xm>
                       // LD4 { <Vt>.<T>, <Vt2>.<T>, <Vt3>.<T>, <Vt4>.<T> }, [<Xn|SP>]
                       // LD4 { <Vt>.<T>, <Vt2>.<T>, <Vt3>.<T>, <Vt4>.<T> }, [<Xn|SP>], <imm>
                       // LD4 { <Vt>.<T>, <Vt2>.<T>, <Vt3>.<T>, <Vt4>.<T> }, [<Xn|SP>], <Xm>
            {
                uint64_t Q = opbits( 30, 1 );
                uint64_t L = opbits( 22, 1 ); // load vs. store
                uint64_t post_index = opbits( 23, 1 );
                uint64_t opcode = opbits( 12, 4 );
                uint64_t size = opbits( 10, 2 );
                uint64_t bits23_21 = opbits( 21, 3 );
                uint64_t m = opbits( 16, 5 );
                uint64_t n = opbits( 5, 5 );
                uint64_t t = opbits( 0, 5 );
    
                if ( 2 != bits23_21 && 6 != bits23_21 && 0 != bits23_21 )
                    unhandled();

                if ( ( 2 & opcode ) || 8 == opcode || 4 == opcode || 0 == opcode ) // LD1 / LD2 / LD3 / LD4 / ST1 / ST2 / ST3 / ST4
                {
                    uint64_t datasize = 64ull << Q;
                    uint64_t esize = 8ull << size;
                    uint64_t elements = datasize / esize;
                    uint64_t selem = 1;
                    uint64_t ebytes = esize / 8;
                    uint64_t address = regs[ n ];
                    uint64_t rpt = 0;

                    if ( 2 == opcode )
                        rpt = 4;
                    else if ( 6 == opcode )
                        rpt = 3;
                    else if ( 10 == opcode )
                        rpt = 2;
                    else if ( 7 == opcode )
                        rpt = 1;
                    else if ( 8 == opcode )
                    {
                        selem = 2;
                        rpt = 1;
                    }
                    else if ( 4 == opcode )
                    {
                        selem = 3;
                        rpt = 1;
                    }
                    else if ( 0 == opcode )
                    {
                        selem = 4;
                        rpt = 1;
                    }
                    else
                        unhandled();

                    //tracer.Trace( "rpt %llu, elements %llu selem %llu, datasize %llu, esize %llu, ebytes %llu\n", rpt, elements, selem, datasize, esize, ebytes );
                    //tracer.Trace( "source data at pc %#llx:\n", pc );
                    //tracer.TraceBinaryData( getmem( address ), (uint32_t) ( rpt * elements * ebytes * selem ), 8 );
                    uint64_t offs = 0;

                    for ( uint64_t r = 0; r < rpt; r++ ) // can't combine in one big memcpy because for rpt > 1 the registers may wrap back to 0. plus, de-interleaving
                    {
                        for ( uint64_t e = 0; e < elements; e++ )
                        {
                            uint64_t tt = ( t + r ) % 32;
                            for ( uint64_t s = 0; s < selem; s++ )
                            {
                                uint64_t eaddr = address + offs;
                                if ( L ) // LD
                                    memcpy( vreg_ptr( tt, e * ebytes ), getmem( eaddr ), ebytes );
                                else // ST
                                    memcpy( getmem( eaddr ), vreg_ptr( tt, e * ebytes ), ebytes );
                                offs += ebytes;
                                tt = ( tt + 1 ) % 32;
                            }
                        }
                    }

                    if ( L )
                        trace_vregs();

                    if ( post_index )
                    {
                        if ( 31 == m )
                        {
                            if ( 7 == opcode )
                                offs = Q ? 16 : 8;
                            else if ( 8 == opcode )
                                offs = Q ? 32 : 16;
                            else if ( 4 == opcode )
                                offs = Q ? 48 : 24;
                            else if ( 0 == opcode )
                                offs = Q ? 64 : 32;
                            else
                                unhandled();
                        }
                        else
                            offs = regs[ m ];
                        address += offs;
                        regs[ n ] = address;
                    }
                }
                else
                    unhandled();
                break;
            }
            case 0x88: // LDAXR <Wt>, [<Xn|SP>{, #0}]    ;    LDXR <Wt>, [<Xn|SP>{, #0}]    ;    STXR <Ws>, <Wt>, [<Xn|SP>{, #0}]    ;    STLXR <Ws>, <Wt>, [<Xn|SP>{, #0}]
                       //                                                                        STLR <Wt>, [<Xn|SP>{, #0}]          ;    STLR <Wt>, [<Xn|SP>, #-4]!
            case 0xc8: // LDAXR <Xt>, [<Xn|SP>{, #0}]    ;    LDXR <Xt>, [<Xn|SP>{, #0}]    ;    STXR <Ws>, <Xt>, [<Xn|SP>{, #0}]    ;    STLXR <Ws>, <Xt>, [<Xn|SP>{, #0}]
                       //                                                                        STLR <Xt>, [<Xn|SP>{, #0}]          ;    STLR <Xt>, [<Xn|SP>, #-8]!
            {
                uint64_t t = opbits( 0, 5 );
                uint64_t n = opbits( 5, 5 );
                uint64_t t2 = opbits( 10, 5 );
                uint64_t s = opbits( 16, 5 );
                uint64_t L = opbits( 21, 2 );
                uint64_t bit23 = opbits( 23, 1 );
    
                if ( 0x1f != t2 )
                    unhandled();

                if ( 0 == L ) // stxr, stlr
                {
                    uint64_t bit30 = opbits( 30, 1 );
                    uint64_t tval = val_reg_or_zr( t );
                    if ( bit30 )
                        setui64( regs[ n ], tval );
                    else
                        setui32( regs[ n ], tval & 0xffffffff );

                    if ( !bit23 && 31 != s ) // stxr
                        regs[ s ] = 0; // success
                }
                else if ( 2 == L ) // ldaxr or ldxr
                {
                    if ( 0x1f != s )
                        unhandled();

                    if ( 31 != t )
                    {
                        if ( 0xc8 == hi8 )
                            regs[ t ] = getui64( regs[ n ] );
                        else
                            regs[ t ] = getui32( regs[ n ] );
                    }
                }
                break;
            }
            case 0xd6: // BLR <Xn>
            {
                uint64_t n = opbits( 5, 5 );
                uint64_t theop = opbits( 21, 2 );
                uint64_t bit23 = opbits( 23, 1 );
                uint64_t op2 = opbits( 12, 9 );
                uint64_t A = opbits( 11, 1 );
                uint64_t M = opbits( 10, 1 );
                if ( 0 != bit23 )
                    unhandled();
                if ( 0x1f0 != op2 )
                    unhandled();
                if ( A || M )
                    unhandled();

                if ( 0 == theop ) // br
                    pc = regs[ n ];
                else if ( 1 == theop ) // blr
                {
                    uint64_t location = pc + 4;
                    pc = regs[ n ];
                    regs[ 30 ] = location; // hard-coded to register 30
                }
                else if ( 2 == theop ) // ret
                    pc = regs[ n ];
                else
                    unhandled();

                continue;
            }
            case 0x1b: // MADD <Wd>, <Wn>, <Wm>, <Wa>    ;    MSUB <Wd>, <Wn>, <Wm>, <Wa>
            case 0x9b: // MADD <Xd>, <Xn>, <Xm>, <Xa>    ;    MSUB <Xd>, <Xn>, <Xm>, <Xa>    ;    UMULH <Xd>, <Xn>, <Xm>    ;    UMADDL <Xd>, <Wn>, <Wm>, <Xa>
                       // SMADDL <Xd>, <Wn>, <Wm>, <Xa>  ;    SMULH <Xd>, <Xn>, <Xm>
            {
                uint64_t d = opbits( 0, 5 );
                if ( 31 == d )
                    break;
                bool xregs = ( 0 != opbits( 31, 1 ) );
                uint64_t m = opbits( 16, 5 );
                uint64_t a = opbits( 10, 5 );
                uint64_t n = opbits( 5, 5 );
                uint64_t bits23_21 = opbits( 21, 3 );
                bool bit15 = ( 1 == opbits( 15, 1 ) );
                uint64_t aval = val_reg_or_zr( a );
                uint64_t nval = val_reg_or_zr( n );
                uint64_t mval = val_reg_or_zr( m );

                if ( xregs )
                {
                    //tracer.Trace( "bits23_21 %llx, bit15 %d\n", bits23_21, bit15 );
                    if ( 1 == bits23_21 && bit15 ) // smsubl
                        regs[ d ] = aval - ( ( 0xffffffff & nval ) * ( 0xffffffff & mval ) );
                    else if ( 0 == bits23_21 && bit15 ) // msub
                        regs[ d ] = aval - ( nval * mval );
                    else if ( 6 == bits23_21 && 31 == a && !bit15 ) // umulh
                    {
                        uint64_t hi, lo;
                        CMultiply128:: mul_u64_u64( &hi, &lo, nval, mval );
                        regs[ d ] = hi;
                    }
                    else if ( 2 == bits23_21 && !bit15 && 31 == a ) // smulh
                    {
                        int64_t hi, lo;
                        CMultiply128:: mul_s64_s64( &hi, &lo, nval, mval );
                        regs[ d ] = hi;
                    }
                    else if ( 5 == bits23_21 && !bit15 ) // umaddl
                        regs[ d ] = aval + ( ( 0xffffffff & nval ) * ( 0xffffffff & mval ) );
                    else if ( 1 == bits23_21 && !bit15 ) // smaddl
                        regs[ d ] = aval + ( (int64_t) (int32_t) ( 0xffffffff & nval ) * (int64_t) (int32_t) ( 0xffffffff & mval ) );
                    else if ( 0 == bits23_21 && !bit15) // madd
                        regs[ d ] = aval + ( nval * mval );
                    else
                        unhandled();
                }
                else
                {
                    if ( 0 == bits23_21 && bit15 ) // msub
                        regs[ d ] = (uint32_t) aval - (uint32_t) ( 0xffffffff & ( (uint32_t) nval * (uint32_t) mval ) );
                    else if ( 0 == bits23_21 && !bit15) // madd
                        regs[ d ] = (uint32_t) aval + (uint32_t) ( 0xffffffff & ( (uint32_t) nval * mval ) );
                    else
                        unhandled();
                }
                break;
            }
            case 0x72: // MOVK <Wd>, #<imm>{, LSL #<shift>}       ;  ANDS <Wd>, <Wn>, #<imm> 
            case 0xf2: // MOVK <Xd>, #<imm>{, LSL #<shift>}       ;  ANDS <Xd>, <Xn>, #<imm> 
            {
                uint64_t d = opbits( 0, 5 );
                uint64_t xregs = ( 0 != ( 0x80 & hi8 ) );
                uint64_t bit23 = opbits( 23, 1 ); // 1 for MOVK, 0 for ANDS
                if ( bit23 )  // MOVK <Xd>, #<imm>{, LSL #<shift>}
                {
                    uint64_t hw = opbits( 21, 2 );
                    uint64_t pos = ( hw << 4 );
                    uint64_t imm16 = opbits( 5, 16 );
                    if ( 31 != d )
                        regs[ d ] = plaster_bits( regs[ d ], imm16, 16, pos );
                }
                else // ANDS <Xd>, <Xn>, #<imm>
                {
                    uint64_t N_immr_imms = opbits( 10, 13 );
                    uint64_t op2 = decode_logical_immediate( N_immr_imms, xregs ? 64 : 32 );
                    uint64_t n = opbits( 5, 5 );
                    uint64_t nvalue = val_reg_or_zr( n );
                    uint64_t result = ( nvalue & op2 );
                    if ( xregs )
                        fN = get_bits( result, 63, 1 );
                    else
                    {
                        result &= 0xffffffff;
                        fN = get_bits( result, 31, 1 );
                    }

                    fZ = ( 0 == result );
                    fC = fV = 0;
                    if ( 31 != d )
                        regs[ d ] = result;
                }
                break;
            }
            case 0x38: // B LDRB STRB
            case 0x78: // H LDRH STRH
            case 0xb8: // W
            case 0xf8: // X
            {
                // LDR <Xt>, [<Xn|SP>, (<Wm>|<Xm>){, <extend> {<amount>}}]
                // LDR <Xt>, [<Xn|SP>], #<simm>
                // LDR <Xt>, [<Xn|SP>, #<simm>]!
                // STR <Xt>, [<Xn|SP>, (<Wm>|<Xm>){, <extend> {<amount>}}]
                // STR <Xt>, [<Xn|SP>], #<simm>
                // STR <Xt>, [<Xn|SP>, #<simm>]!
    
                uint64_t opc = opbits( 21, 3 );
                uint64_t n = opbits( 5, 5 );
                uint64_t t = opbits( 0, 5 );
    
                if ( 0 == opc ) // str (immediate) post-index and pre-index
                {
                    uint64_t unsigned_imm9 = opbits( 12, 9 );
                    int64_t extended_imm9 = sign_extend( unsigned_imm9, 8 );
                    uint64_t option = opbits( 10, 2 );
                    uint64_t address = 0;

                    if ( 0 == option )
                        address = regs[ n ] + extended_imm9;
                    else if ( 1 == option )
                        address = regs[ n ];
                    else if ( 3 == option )
                    {
                        regs[ n ] += extended_imm9;
                        address = regs[ n ];
                    }    
                    else
                        unhandled();

                    uint64_t val = ( 31 == t ) ? 0 : regs[ t ];

                    if ( 0x38 == hi8 )
                        setui8( address, val & 0xff );
                    else if ( 0x78 == hi8 )
                        setui16( address, val & 0xffff );
                    else if ( 0xb8 == hi8 )
                        setui32( address, val & 0xffffffff );
                    else
                        setui64( address, val );

                    if ( 1 == option ) // post index
                        regs[ n ] += extended_imm9;
                }
                else if ( 1 == opc ) // str (register) STR <Xt>, [<Xn|SP>, (<Wm>|<Xm>){, <extend> {<amount>}}]
                {
                    uint64_t m = opbits( 16, 5 );
                    uint64_t shift = opbits( 12, 1 );
                    if ( 1 == shift )
                        shift = ( hi8 >> 6 );
                    uint64_t option = opbits( 13, 3 );
                    uint64_t address = regs[ n ];
                    int64_t offset = extend_reg( m, option, shift );
                    address += offset;
                    uint64_t val = ( 31 == t ) ? 0 : regs[ t ];

                    if ( 0x38 == hi8 )
                        setui8( address, val & 0xff );
                    else if ( 0x78 == hi8 )
                        setui16( address, val & 0xffff );
                    else if ( 0xb8 == hi8 )
                        setui32( address, val & 0xffffffff );
                    else
                        setui64( address, val );
                }
                else if ( 2 == opc ) // ldr (immediate)
                {
                    uint64_t unsigned_imm9 = opbits( 12, 9 );
                    int64_t extended_imm9 = sign_extend( unsigned_imm9, 8 );
                    uint64_t option = opbits( 10, 2 );
                    uint64_t address = 0;

                    if ( 0 == option )
                        address = regs[ n ] + extended_imm9;
                    else if ( 1 == option )
                        address = regs[ n ];
                    else if ( 3 == option )
                    {
                        regs[ n ] += extended_imm9;
                        address = regs[ n ];
                    }    
                    else
                        unhandled();

                    if ( 0x38 == hi8 )
                        regs[ t ] = getui8( address );
                    else if ( 0x78 == hi8 )
                        regs[ t ] = getui16( address );
                    else if ( 0xb8 == hi8 )
                        regs[ t ] = getui32( address );
                    else
                        regs[ t ] = getui64( address );

                    if ( 1 == option ) // post index
                        regs[ n ] += extended_imm9;
                }
                else if ( 3 == opc ) // ldr (register) LDR <Xt>, [<Xn|SP>, (<Wm>|<Xm>){, <extend> {<amount>}}]
                {
                    uint64_t m = opbits( 16, 5 );
                    uint64_t shift = opbits( 12, 1 );
                    if ( 1 == shift )
                        shift = ( hi8 >> 6 );

                    uint64_t option = opbits( 13, 3 );
                    uint64_t address = regs[ n ];
                    int64_t offset = extend_reg( m, option, shift );
                    address += offset;

                    if ( 0x38 == hi8 )
                        regs[ t ] = getui8( address );
                    else if ( 0x78 == hi8 )
                        regs[ t ] = getui16( address );
                    else if ( 0xb8 == hi8 )
                        regs[ t ] = getui32( address );
                    else
                        regs[ t ] = getui64( address );
                }
                else if ( 4 == opc || 6 == opc ) // LDRSW <Xt>, [<Xn|SP>], #<simm>    ;    LDRSW <Xt>, [<Xn|SP>, #<simm>]!
                {
                    uint64_t bits11_10 = opbits( 10, 2 );
                    if ( 0 == bits11_10 ) // LDURSB <Wt>, [<Xn|SP>{, #<simm>}]    ;    LDURSB <Xt>, [<Xn|SP>{, #<simm>}]
                    {
                        int64_t imm9 = sign_extend( opbits( 12, 9 ), 8 );
                        if ( 31 != t )
                        {
                            if ( 0x38 == hi8 ) // ldursb
                                regs[ t ] = sign_extend( getui8( regs[ n ] + imm9 ), 7 );
                            else if ( 0x78 == hi8 ) // ldursh
                                    regs[ t ] = sign_extend( getui16( regs[ n ] + imm9 ), 15 );
                            else if ( 0xb8 == hi8 ) // ldursw
                                regs[ t ] = sign_extend( getui32( regs[ n ] + imm9 ), 31 );
                            else
                                unhandled();
                            bool isx = ( 0 != opbits( 22, 1 ) );
                            if ( !isx )
                                regs[ t ] &= 0xffffffff;
                        }
                    }
                    else
                    {
                        int64_t imm9 = sign_extend( opbits( 12, 9 ), 8 );
                        uint64_t option = opbits( 10, 2 );
                        uint64_t address = 0;
    
                        if ( 1 == option )
                            address = regs[ n ];
                        else if ( 3 == option )
                        {
                            regs[ n ] += imm9;
                            address = regs[ n ];
                        }
                        else
                            unhandled();
    
                        if ( 0x38 == hi8 )
                            regs[ t ] = sign_extend( getui8( address ), 7 );
                        else if ( 0x78 == hi8 )
                            regs[ t ] = sign_extend( getui16( address ), 15 );
                        else if ( 0xb8 == hi8 )
                            regs[ t ] = sign_extend( getui32( address ), 31 );
                        else
                            unhandled();
    
                        if ( 1 == option ) // post index
                            regs[ n ] += imm9;
                    }
                }
                else if ( 5 == opc  || 7 == opc ) // hi8 = 0x78
                                                  //     (opc == 7)                  LDRSH <Wt>, [<Xn|SP>, (<Wm>|<Xm>){, <extend> {<amount>}}]
                                                  //     (opc == 5)                  LDRSH <Xt>, [<Xn|SP>, (<Wm>|<Xm>){, <extend> {<amount>}}]
                                                  // hi8 = 0x38
                                                  //     (opc == 7 && option != 011) LDRSB <Wt>, [<Xn|SP>, (<Wm>|<Xm>), <extend> {<amount>}]
                                                  //     (opc == 5 && option != 011) LDRSB <Xt>, [<Xn|SP>, (<Wm>|<Xm>), <extend> {<amount>}]
                                                  //     (opc == 7 && option == 011) LDRSB <Wt>, [<Xn|SP>, <Xm>{, LSL <amount>}]
                                                  //     (opc == 5 && option == 011) LDRSB <Xt>, [<Xn|SP>, <Xm>{, LSL <amount>}]
                                                  // hi8 == 0xb8
                                                  //     (opc == 5 && option = many) LDRSW <Xt>, [<Xn|SP>, (<Wm>|<Xm>){, <extend> {<amount>}}]
                {
                    uint64_t m = opbits( 16, 5 );
                    uint64_t shift = opbits( 12, 1 );
                    if ( 1 == shift )
                        shift = ( hi8 >> 6 );
                    uint64_t option = opbits( 13, 3 );
                    bool mIsX = ( 1 == ( option & 1 ) );
                    uint64_t address = regs[ n ];
                    if ( 31 == t )
                        break;

                    if ( 0xb8 == hi8 )
                    {
                        uint64_t offset = extend_reg( m, option, opbits( 12, 1 ) ? 2 : 0 );
                        regs[ t ] = sign_extend( getui32( address + offset ), 31 );
                    }
                    else if ( 0x38 == hi8 )
                    {
                        int64_t offset = 0;
                        if ( 3 == option )
                        {
                            uint64_t mval = regs[ m ];
                            offset = ( ( mIsX ? mval : ( mval & 0xffffffff ) ) << shift );
                        }
                        else
                            offset = extend_reg( m, option, shift );
                        address += offset;
                        regs[ t ] = sign_extend( getui8( address ), 7 );
                    }
                    else if ( 0x78 == hi8 )
                    {
                        int64_t offset = extend_reg( m, option, shift );
                        address += offset;
                        regs[ t ] = sign_extend( getui16( address ), 15 );
                    }
                    else
                        unhandled();
                }
                break;
            }
            case 0x39: // B
            case 0x79: // H                              ;    LDRSH <Wt>, [<Xn|SP>{, #<pimm>}]
            case 0xb9: // W
            case 0xf9: // X ldr + str unsigned offset    ;    LDRSW <Xt>, [<Xn|SP>{, #<pimm>}]
            {
                // STR <Xt>, [<Xn|SP>{, #<pimm>}]
                // LDR <Xt>, [<Xn|SP>{, #<pimm>}]
    
                uint64_t opc = opbits( 22, 2 );
                uint64_t imm12 = opbits( 10, 12 );
                uint64_t lsl = opbits( 30, 2 );
                imm12 <<= lsl;
                uint64_t t = opbits( 0, 5 );
                uint64_t n = opbits( 5, 5 );
                uint64_t address = regs[ n ] + imm12;

                if ( 0 == opc ) // str
                {
                    uint64_t val = val_reg_or_zr( t );

                    if ( 0x39 == hi8 )
                        setui8( address, val & 0xff );
                    else if ( 0x79 == hi8 )
                        setui16( address, val & 0xffff );
                    else if ( 0xb9 == hi8 )
                        setui32( address, val & 0xffffffff );
                    else
                        setui64( address, val );
                }
                else if ( 1 == opc ) // 0-extend ldr
                {
                    if ( 31 == t )
                        break;

                    if ( 0x39 == hi8 )
                        regs[ t ] = getui8( address );
                    else if ( 0x79 == hi8 )
                        regs[ t ] = getui16( address );
                    else if ( 0xb9 == hi8 )
                        regs[ t ] = getui32( address );
                    else
                        regs[ t ] = getui64( address );
                }
                else if ( 2 == opc ) // sign-extend to 64 bits ldr
                {
                    if ( 31 == t )
                        break;

                    if ( 0x39 == hi8 )
                        regs[ t ] = sign_extend( getui8( address ), 7 );
                    else if ( 0x79 == hi8 )
                        regs[ t ] = sign_extend( getui16( address ), 15 );
                    else if ( 0xb9 == hi8 )
                        regs[ t ] = sign_extend( getui32( address ), 31 );
                    else
                        unhandled();
                }
                else if ( 3 == opc ) // sign-extend to 32 bits ldr
                {
                    if ( 31 == t )
                        break;

                    if ( 0x39 == hi8 )
                        regs[ t ] = sign_extend32( getui8( address ), 7 );
                    else if ( 0x79 == hi8 )
                        regs[ t ] = sign_extend32( getui16( address ), 15 );
                    else if ( 0xb9 == hi8 )
                        regs[ t ] = sign_extend32( getui32( address ), 31 );
                    else
                        unhandled();
                }
                else
                    unhandled();
    
                break;
            }
            default:
                unhandled();
        }

        pc += 4;
        cycles_so_far++;
    } while ( cycles_so_far < target_cycles );

    return cycles_so_far - start_cycles;
} //run

