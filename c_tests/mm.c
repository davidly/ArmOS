/* BYTE magazine October 1982. Jerry Pournelle. */
/* ported to C and added test cases by David Lee */
/* various bugs not found because dimensions are square fixed by David Lee */
/* expected result for 20/20/20: 4.65880E+05 */
/* 20/20/20 float version runs in 13 seconds on the original PC */

/* why so many matrix size variants? g++ produces different code to optimize for each ase */

#define LINT_ARGS

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

#define matrix_test( ftype, dim ) \
  ftype A_##ftype##dim[ dim ][ dim ]; \
  ftype B_##ftype##dim[ dim ][ dim ]; \
  ftype C_##ftype##dim[ dim ][ dim ]; \
  void fillA_##ftype##dim() \
  { \
    for ( int i = 0; i < dim; i++ ) \
        for ( int j = 0; j < dim; j++ ) \
            A_##ftype##dim[ i ][ j ] = (ftype) ( i + j + 2 ); \
  } \
  void fillB_##ftype##dim() \
  { \
    for ( int i = 0; i < dim; i++ ) \
        for ( int j = 0; j < dim; j++ ) \
            B_##ftype##dim[ i ][ j ] = (ftype) ( ( i + j + 2 ) / ( j + 1 ) ); \
  } \
  void fillC_##ftype##dim() \
  { \
    for ( int i = 0; i < dim; i++ ) \
        for ( int j = 0; j < dim; j++ ) \
            C_##ftype##dim[ i ][ j ] = (ftype) 0.0; \
  } \
  __attribute__((noinline)) void print_array_##ftype##dim( ftype a[dim][dim] ) \
  { \
    printf( "array: \n" ); \
    for ( int i = 0; i < dim; i++ ) \
    { \
        for ( int j = 0; j < dim; j++ ) \
            printf( " %lf", a[ i ][ j ] ); \
        printf( "\n" ); \
    } \
  } \
  void matmult_##ftype##dim() \
  { \
      /* this debugging line causes the compiler to optimize code differently (tbl/zip)! syscall( 0x2002, 1 ); */ \
      for ( int i = 0; i < dim; i++ ) \
          for ( int j = 0; j < dim; j++ ) \
              for ( int k = 0; k < dim; k++ ) \
                  C_##ftype##dim[ i ][ j ] += A_##ftype##dim[ i ][ k ] * B_##ftype##dim[ k ][ j ]; \
  } \
  ftype sum_##ftype##dim() \
  { \
    ftype result = 0.0; \
    for ( int i = 0; i < dim; i++ ) \
        for ( int j = 0; j < dim; j++ ) \
            result += C_##ftype##dim[ i ][ j ]; \
    return result; \
  } \
  ftype run_##ftype##dim() \
  { \
      fillA_##ftype##dim(); \
      fillB_##ftype##dim(); \
      fillC_##ftype##dim(); \
      matmult_##ftype##dim(); \
      /*print_array_##ftype##dim( A_##ftype##dim );*/ \
      /*print_array_##ftype##dim( B_##ftype##dim );*/ \
      /*print_array_##ftype##dim( C_##ftype##dim );*/ \
      return sum_##ftype##dim(); \
  }

matrix_test( float, 1 );  
matrix_test( float, 2 );  
matrix_test( float, 3 );  
matrix_test( float, 4 );  
matrix_test( float, 5 );  
matrix_test( float, 6 );  
matrix_test( float, 7 );  
matrix_test( float, 8 );
matrix_test( float, 9 );
matrix_test( float, 10 );
matrix_test( float, 11 );
matrix_test( float, 12 );
matrix_test( float, 13 );
matrix_test( float, 14 );
matrix_test( float, 15 );
matrix_test( float, 16 );
matrix_test( float, 17 );
matrix_test( float, 18 );
matrix_test( float, 19 );
matrix_test( float, 20 );  

matrix_test( double, 1 );  
matrix_test( double, 2 );  
matrix_test( double, 3 );  
matrix_test( double, 4 );  
matrix_test( double, 5 );  
matrix_test( double, 6 );  
matrix_test( double, 7 );  
matrix_test( double, 8 );  
matrix_test( double, 9 );  
matrix_test( double, 10 );  
matrix_test( double, 11 );  
matrix_test( double, 12 );  
matrix_test( double, 13 );  
matrix_test( double, 14 );  
matrix_test( double, 15 );  
matrix_test( double, 16 );  
matrix_test( double, 17 );  
matrix_test( double, 18 );  
matrix_test( double, 19 );  
matrix_test( double, 20 );  

int main( int argc, char * argv[] )
{
    printf( "matrix float 1: %f\n", run_float1() );
    printf( "matrix float 2: %f\n", run_float2() );
    printf( "matrix float 3: %f\n", run_float3() );
    printf( "matrix float 4: %f\n", run_float4() );
    printf( "matrix float 5: %f\n", run_float5() );
    printf( "matrix float 6: %f\n", run_float6() );
    printf( "matrix float 7: %f\n", run_float7() );
    printf( "matrix float 8: %f\n", run_float8() );
    printf( "matrix float 9: %f\n", run_float9() );
    printf( "matrix float 10: %f\n", run_float10() );
    printf( "matrix float 11: %f\n", run_float11() );
    printf( "matrix float 12: %f\n", run_float12() );
    printf( "matrix float 13: %f\n", run_float13() );
    printf( "matrix float 14: %f\n", run_float14() );
    printf( "matrix float 15: %f\n", run_float15() );
    printf( "matrix float 16: %f\n", run_float16() );
    printf( "matrix float 17: %f\n", run_float17() );
    printf( "matrix float 18: %f\n", run_float18() );
    printf( "matrix float 19: %f\n", run_float19() );
    printf( "matrix float 20: %f\n", run_float20() );

    printf( "matrix double 1: %lf\n", run_double1() );
    printf( "matrix double 2: %lf\n", run_double2() );
    printf( "matrix double 3: %lf\n", run_double3() );
    printf( "matrix double 4: %lf\n", run_double4() );
    printf( "matrix double 5: %lf\n", run_double5() );
    printf( "matrix double 6: %lf\n", run_double6() );
    printf( "matrix double 7: %lf\n", run_double7() );
    printf( "matrix double 8: %lf\n", run_double8() );
    printf( "matrix double 9: %lf\n", run_double9() );
    printf( "matrix double 10: %lf\n", run_double10() );
    printf( "matrix double 11: %lf\n", run_double11() );
    printf( "matrix double 12: %lf\n", run_double12() );
    printf( "matrix double 13: %lf\n", run_double13() );
    printf( "matrix double 14: %lf\n", run_double14() );
    printf( "matrix double 15: %lf\n", run_double15() );
    printf( "matrix double 16: %lf\n", run_double16() );
    printf( "matrix double 17: %lf\n", run_double17() );
    printf( "matrix double 18: %lf\n", run_double18() );
    printf( "matrix double 19: %lf\n", run_double19() );
    printf( "matrix double 20: %lf\n", run_double20() );
    printf( "matrix multiply test completed with great success\n" );
    return 0;
}

