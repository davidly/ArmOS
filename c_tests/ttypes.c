#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <cmath>
#include <typeinfo>

typedef unsigned __int128 uint128_t;
typedef __int128 int128_t;
typedef long double ldouble_t;

#define _countof( X ) ( sizeof( X ) / sizeof( X[0] ) )

template <class T> T do_abs( T x )
{
    return ( x < 0 ) ? -x : x;
}

template <class T> T do_sum( T array[], size_t size )
{
    T sum = 0;
    for ( int i = 0; i < size; i++ )
    {
        /*printf( "in do_sum, element %d %.12g\n", i, (double) array[ i ] );*/
        sum += array[ i ];
    }
    /*printf( "sum in do_sym: %.12g\n", (double) sum );*/
    return sum;
}

template <class T, class U, size_t size> T tst( T t, U u )
{
    T a[ size ] = { 0 };
    U b[ _countof( a ) ] = { 0 };
    T c[ _countof( a ) ] = { 0 };
    T x = t;

    srand( 0 );

    for ( int i = 0; i < _countof( a ); i++ )
    {
        x += ( rand() % ( i + 1000 ) ) / 2;
        x = -x;
        x = (int128_t) x & 0x33303330333033;
        x = do_abs( x );
        x = (T) sqrt( (double) x );
        x += (T) 1.02;
        x = (T) ( (double) x * 3.2 );
        u += ( rand() % ( i + 2000 ) ) / 3;
        a[ i ] = ( x * (T) u ) + ( x + (T) u );
    }

    //syscall( 0x2002, 1 );        
    for ( int i = 0; i < _countof( a ); i++ )
    {
        b[ i ] = a[ i ] * (T) 2.2;
        c[ i ] = a[ i ] * (T) 4.4;
        /*printf( "c[%d] = %.12g, a = %.12g\n", i, (double) c[i], (double) a[i] );*/
    }

#if 0
    for ( int i = 0; i < _countof( a ); i++ )
    {
        printf( "a[%d] = %.2lf\n", i, a[i] );
        printf( "c[%d] = %.15g\n", i, c[i] );
    }
#endif     

    T sumA = do_sum( a, _countof( a ) );
    T sumB = do_sum( b, _countof( b ) );
    T sumC = do_sum( c, _countof( c ) );
    
    x = sumA / 128;

    // beyond 12 digits of precision, results will vary across compilers, compiler optimization flags, hardware, and emulators since
    // doubles only have 12 digits of precision and the loop above will cause more to be used.
    printf( "types %s + %s, size %d, sumA %.12g, sumB %.12g, sumC %.12g\n", typeid(T).name(), typeid(U).name(), size, (double) sumA, (double) sumB, (double) sumC );
    return x;
}

#define run_tests( ftype, dim ) \
  tst<ftype,int8_t,dim>( 0, 0 ); \
  tst<ftype,uint8_t,dim>( 0, 0 ); \
  tst<ftype,int16_t,dim>( 0, 0 ); \
  tst<ftype,uint16_t,dim>( 0, 0 ); \
  tst<ftype,int32_t,dim>( 0, 0 ); \
  tst<ftype,uint32_t,dim>( 0, 0 ); \
  tst<ftype,int64_t,dim>( 0, 0 ); \
  tst<ftype,uint64_t,dim>( 0, 0 ); \
  tst<ftype,int128_t,dim>( 0, 0 ); \
  tst<ftype,uint128_t,dim>( 0, 0 ); \
  tst<ftype,float,dim>( 0, 0 ); \
  tst<ftype,double,dim>( 0, 0 ); \
  tst<ftype,ldouble_t,dim>( 0, 0 ); 

#define run_tests_one( ftype, dim ) \
  tst<ftype,int8_t,dim>( 0, 0 );

#define run_dimension( dim ) \
  run_tests( int8_t, dim ); \
  run_tests( uint8_t, dim ); \
  run_tests( int16_t, dim ); \
  run_tests( uint16_t, dim ); \
  run_tests( int32_t, dim ); \
  run_tests( uint32_t, dim ); \
  run_tests( int64_t, dim ); \
  run_tests( uint64_t, dim ); \
  run_tests( int128_t, dim ); \
  run_tests( uint128_t, dim ); \
  run_tests( float, dim ); \
  run_tests( double, dim ); \
  run_tests( ldouble_t, dim );

#define run_dimension_one( dim ) \
  run_tests_one( float, dim );

int main( int argc, char * argv[], char * env[] )
{
    printf( "types: i8 %s, ui8 %s, i16 %s, ui16 %s, i32 %s, ui32 %s, i64 %s, ui64 %s, i128 %s, ui128 %s, f %s, d %s, ld %s\n",
            typeid(int8_t).name(), typeid(uint8_t).name(), typeid(int16_t).name(), typeid(uint16_t).name(),
            typeid(int32_t).name(), typeid(uint32_t).name(), typeid(int64_t).name(), typeid(uint64_t).name(),
            typeid(int128_t).name(), typeid(uint128_t).name(), 
            typeid(float).name(), typeid(double).name(), typeid(ldouble_t).name() );

#if 1
    run_dimension( 2 );    
    run_dimension( 3 );    
    run_dimension( 4 );    
    run_dimension( 5 );    
    run_dimension( 6 );    
    run_dimension( 15 );    
    run_dimension( 16 );    
    run_dimension( 17 );    
    run_dimension( 31 );    
    run_dimension( 32 );    
    run_dimension( 33 );    
    run_dimension( 128 );
#else    
    run_dimension_one( 128 );
#endif

    printf( "test types completed with great success\n" );
    return 0;
}