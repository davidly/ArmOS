/* BYTE magazine October 1982. Jerry Pournelle. */
/* ported to C and double test cases by David Lee */
/* various bugs not found because dimensions are square fixed by David Lee */
/* expected result: 4.65880E+05 */
/* normal version runs in 13 seconds on the original PC and 8.9 seconds with the "fast" versions */

#define LINT_ARGS

#include <stdio.h>

#define l 20 /* rows in A and resulting matrix C */
#define m 20 /* columns in A and rows in B (must be identical) */
#define n 20 /* columns in B and resulting matrix C */

float Summ;
float A[ l ] [ m ];
float B[ m ] [ n ];
float C[ l ] [ n ];

double d_Summ;
double d_A[ l ] [ m ];
double d_B[ m ] [ n ];
double d_C[ l ] [ n ];

void filla()
{
    int i, j;
    for ( i = 0; i < l; i++ )
        for ( j = 0; j < m; j++ )
            A[ i ][ j ] = (float) ( i + j + 2 );
}

void d_filla()
{
    int i, j;
    for ( i = 0; i < l; i++ )
        for ( j = 0; j < m; j++ )
            d_A[ i ][ j ] = (double) ( i + j + 2 );
}

void d_printa()
{
    int i, j;
    for ( i = 0; i < l; i++ )
        for ( j = 0; j < m; j++ )
            printf( "d_A[%d][%d] = %lf\n", i, j, d_A[ i ][ j ] );
}

void fillb()
{
    int i, j;
    for ( i = 0; i < m; i++ )
        for ( j = 0; j < n; j++ )
            B[ i ][ j ] = (float) ( ( i + j + 2 ) / ( j + 1 ) );
}

void d_fillb()
{
    int i, j;
    for ( i = 0; i < m; i++ )
        for ( j = 0; j < n; j++ )
            d_B[ i ][ j ] = (double) ( ( i + j + 2 ) / ( j + 1 ) );
}

void d_printb()
{
    int i, j;
    for ( i = 0; i < m; i++ )
        for ( j = 0; j < n; j++ )
            printf( "d_B[%d][%d] = %lf\n", i, j, d_B[i][j] );
}

void fillc()
{
    int i, j;
    for ( i = 0; i < l; i++ )
        for ( j = 0; j < n; j++ )
            C[ i ][ j ] = (float) 0;
}

void d_fillc()
{
    int i, j;
    for ( i = 0; i < l; i++ )
        for ( j = 0; j < n; j++ )
            d_C[ i ][ j ] = (double) 0;
}

void fast_fillc()
{
    float * p = (float *) C;
    float * pend = ( (float *) C ) + ( l * n );

    while ( p < pend )
        *p++ = 0;
}

void matmult()
{
    int i, j, k;
    for ( i = 0; i < l; i++ )
        for ( j = 0; j < n; j++ )
            for ( k = 0; k < m; k++ )
            {
                C[ i ][ j ] += A[ i ][ k ] * B[ k ][ j ];
//                printf( "i %d j %d k %d, newc: %lf\n", i, j, k, C[ i ][ j ] );
            }
}

void d_matmult()
{
    int i, j, k;
    for ( i = 0; i < l; i++ )
        for ( j = 0; j < n; j++ )
            for ( k = 0; k < m; k++ )
            {
                d_C[ i ][ j ] += d_A[ i ][ k ] * d_B[ k ][ j ];
//                printf( "i %d j %d k %d, A %lf, B %lf, newc: %lf\n", i, j, k, d_A[i][k], d_B[k][j], d_C[ i ][ j ] );
            }
}

void fast_matmult()
{
    static int i, j, k;
    static float * pC, * pA, * pAI, * pBJ, *pCI;

    for ( i = 0; i < l; i++ )
    {
        pAI = (float *) & ( A[ i ][ 0 ] );
        pCI = (float *) & ( C[ i ][ 0 ] );

        for ( j = 0; j < n; j++ )
        {
            pC = (float *) & pCI[ j ];
            pA = pAI;
            pBJ = & ( B[ 0 ][ j ] );

            for ( k = 0; k < m; k++ )
            {
                *pC += pA[ k ] * ( *pBJ );
                pBJ += m;
            }
        }
    }
}

void summit()
{
    int i, j;
    for ( i = 0; i < l; i++ )
        for ( j = 0; j < n; j++ )
            Summ += C[ i ][ j ];
}

void d_summit()
{
    int i, j;
    for ( i = 0; i < l; i++ )
        for ( j = 0; j < n; j++ )
            d_Summ += d_C[ i ][ j ];
}

void d_printC()
{
    int i, j;
    for ( i = 0; i < l; i++ )
        for ( j = 0; j < n; j++ )
            printf( "i %d j %d val %lf\n", i, j, d_C[ i ][ j ] );
}

void fast_summit()
{
    float * p = (float *) C;
    float * pend = ( (float *) C ) + ( l * n );

    while ( p < pend )
        Summ += *p++;
}

void matrix_test_3()
{
    const int size = 3;
    static float f_sum_3 = 0.0;
    static float f_A_3[ 3 ] [ 3 ];
    static float f_B_3[ 3 ] [ 3 ];
    static float f_C_3[ 3 ] [ 3 ];

    for ( int i = 0; i < 3; i++ )
        for ( int j = 0; j < 3; j++ )
            f_A_3[ i ][ j ] = (float) ( i + j + 2 );
printf( "after init A\n" );        

    for ( int i = 0; i < 3; i++ )
        for ( int j = 0; j < 3; j++ )
            f_B_3[ i ][ j ] = (float) ( ( i + j + 2 ) / ( j + 1 ) );

printf( "after init b\n" );
    for ( int i = 0; i < 3; i++ )
        for ( int j = 0; j < 3; j++ )
            f_C_3[ i ][ j ] = (float) 0;

printf( "after init c\n" );
    for ( int i = 0; i < 3; i++ )
        for ( int j = 0; j < 3; j++ )
            for ( int k = 0; k < 3; k++ )
                f_C_3[ i ][ j ] += f_A_3[ i ][ k ] * f_B_3[ k ][ j ];

printf( "after mult\n" );            

    for ( int i = 0; i < 3; i++ )
        for ( int j = 0; j < 3; j++ )
            f_sum_3 += f_C_3[ i ][ j ];

printf( "after summ\n" );        

    printf( "f_sum_3:   %f\n", f_sum_3 );

    static double d_sum_3;
    static double d_A_3[ 3 ] [ 3 ];
    static double d_B_3[ 3 ] [ 3 ];
    static double d_C_3[ 3 ] [ 3 ];

}

int main( int argc, char * argv[] )
{
    filla();
    fillb();

    Summ = 0;
    fillc();
    matmult();
    summit();
    printf( "slow summ is:   %f\n", Summ );

    Summ = 0;
    fast_fillc();
    fast_matmult();
    fast_summit();
    printf( "fast summ is:   %f\n", Summ );

    d_filla();
    d_fillb();
    d_Summ = 0;
    d_fillc();
    d_matmult();
    d_summit();
    printf( "double summ is: %lf\n", d_Summ );

    //matrix_test_3();

    return 0;
}

