#include <stdio.h>
#include <stdint.h>

//#pragma GCC optimize("O0")

template <class T> void cmp( T a, T b )
{
    bool gt = ( a > b );
    bool lt = ( a < b );
    bool eq = ( a == b );
    bool le = ( a <= b );
    bool ge = ( a >= b );
    printf( "  gt %d lt %d eq %d le %d ge %d\n", gt, lt, eq, le, ge );
} //cmp

int main( int argc, char * argv[] )
{
    printf( "uint8_t:\n" );
    cmp( (uint8_t) 1, (uint8_t) 3 );
    cmp( (uint8_t) 1, (uint8_t) -3 );
    cmp( (uint8_t) -1, (uint8_t) 3 );
    cmp( (uint8_t) -1, (uint8_t) -3 );
    cmp( (uint8_t) -1, (uint8_t) -1 );
    cmp( (uint8_t) 247, (uint8_t) 3 );
    cmp( (uint8_t) 247, (uint8_t) -3 );
    cmp( (uint8_t) -247, (uint8_t) 3 );
    cmp( (uint8_t) -247, (uint8_t) -247 );
    cmp( (uint8_t) 0xf1, (uint8_t) 0xf2 );
    cmp( (uint8_t) 0xf3, (uint8_t) 0xf2 );
    cmp( (uint8_t) 0xe1, (uint8_t) 0xe2 );
    cmp( (uint8_t) 0xe3, (uint8_t) 0xe2 );
    cmp( (uint8_t) 0x81, (uint8_t) 0x78 );
    cmp( (uint8_t) 0, (uint8_t) 0x80 );
    cmp( (uint8_t) 0x7f, (uint8_t) 0x80 );

    printf( "int8_t:\n" );
    cmp( (int8_t) 1, (int8_t) 3 );
    cmp( (int8_t) 1, (int8_t) -3 );
    cmp( (int8_t) -1, (int8_t) 3 );
    cmp( (int8_t) -1, (int8_t) -3 );
    cmp( (int8_t) -1, (int8_t) -1 );
    cmp( (int8_t) 247, (int8_t) 3 );
    cmp( (int8_t) 247, (int8_t) -3 );
    cmp( (int8_t) -247, (int8_t) 3 );
    cmp( (int8_t) -247, (int8_t) -247 );
    cmp( (int8_t) 0xf1, (int8_t) 0xf2 );
    cmp( (int8_t) 0xf3, (int8_t) 0xf2 );
    cmp( (int8_t) 0xe1, (int8_t) 0xe2 );
    cmp( (int8_t) 0xe3, (int8_t) 0xe2 );
    cmp( (int8_t) 0x81, (int8_t) 0x78 );
    cmp( (int8_t) 0, (int8_t) 0x80 );
    cmp( (int8_t) 0x7f, (int8_t) 0x80 );

    printf( "uint16_t:\n" );
    cmp( (uint16_t) 1, (uint16_t) 3 );
    cmp( (uint16_t) 1, (uint16_t) -3 );
    cmp( (uint16_t) -1, (uint16_t) 3 );
    cmp( (uint16_t) -1, (uint16_t) -3 );
    cmp( (uint16_t) -1, (uint16_t) -1 );
    cmp( (uint16_t) 247, (uint16_t) 3 );
    cmp( (uint16_t) 247, (uint16_t) -3 );
    cmp( (uint16_t) -247, (uint16_t) 3 );
    cmp( (uint16_t) -247, (uint16_t) -247 );
    cmp( (uint16_t) 0xff11, (uint16_t) 0xff22 );
    cmp( (uint16_t) 0xff33, (uint16_t) 0xff22 );
    cmp( (uint16_t) 0xef11, (uint16_t) 0xef22 );
    cmp( (uint16_t) 0xef33, (uint16_t) 0xef22 );
    cmp( (uint16_t) 0x8001, (uint16_t) 0x7ff8 );
    cmp( (uint16_t) 0, (uint16_t) 0x8000 );
    cmp( (uint16_t) 0x7fff, (uint16_t) 0x8000 );

    printf( "int16_t:\n" );
    cmp( (int16_t) 1, (int16_t) 3 );
    cmp( (int16_t) 1, (int16_t) -3 );
    cmp( (int16_t) -1, (int16_t) 3 );
    cmp( (int16_t) -1, (int16_t) -3 );
    cmp( (int16_t) -1, (int16_t) -1 );
    cmp( (int16_t) 247, (int16_t) 3 );
    cmp( (int16_t) 247, (int16_t) -3 );
    cmp( (int16_t) -247, (int16_t) 3 );
    cmp( (int16_t) -247, (int16_t) -247 );
    cmp( (int16_t) 0xff11, (int16_t) 0xff22 );
    cmp( (int16_t) 0xff33, (int16_t) 0xff22 );
    cmp( (int16_t) 0xef11, (int16_t) 0xef22 );
    cmp( (int16_t) 0xef33, (int16_t) 0xef22 );
    cmp( (int16_t) 0x8001, (int16_t) 0x7ff8 );
    cmp( (int16_t) 0, (int16_t) 0x8000 );
    cmp( (int16_t) 0x7fff, (int16_t) 0x8000 );

    printf( "uint32_t:\n" );
    cmp( (uint32_t) 1, (uint32_t) 3 );
    cmp( (uint32_t) 1, (uint32_t) -3 );
    cmp( (uint32_t) -1, (uint32_t) 3 );
    cmp( (uint32_t) -1, (uint32_t) -3 );
    cmp( (uint32_t) -1, (uint32_t) -1 );
    cmp( (uint32_t) 247, (uint32_t) 3 );
    cmp( (uint32_t) 247, (uint32_t) -3 );
    cmp( (uint32_t) -247, (uint32_t) 3 );
    cmp( (uint32_t) -247, (uint32_t) -247 );
    cmp( (uint32_t) 0xffff1111, (uint32_t) 0xffff2222 );
    cmp( (uint32_t) 0xffff3333, (uint32_t) 0xffff2222 );
    cmp( (uint32_t) 0xefff1111, (uint32_t) 0xefff2222 );
    cmp( (uint32_t) 0xefff3333, (uint32_t) 0xefff2222 );
    cmp( (uint32_t) 0x80000001, (uint32_t) 0x7ffffff8 );
    cmp( (uint32_t) 0, (uint32_t) 0x80000000 );
    cmp( (uint32_t) 0x7fffffff, (uint32_t) 0x80000000 );

    printf( "int32_t:\n" );
    cmp( (int32_t) 1, (int32_t) 3 );
    cmp( (int32_t) 1, (int32_t) -3 );
    cmp( (int32_t) -1, (int32_t) 3 );
    cmp( (int32_t) -1, (int32_t) -3 );
    cmp( (int32_t) -1, (int32_t) -1 );
    cmp( (int32_t) 247, (int32_t) 3 );
    cmp( (int32_t) 247, (int32_t) -3 );
    cmp( (int32_t) -247, (int32_t) 3 );
    cmp( (int32_t) -247, (int32_t) -247 );
    cmp( (int32_t) 0xffff1111, (int32_t) 0xffff2222 );
    cmp( (int32_t) 0xffff3333, (int32_t) 0xffff2222 );
    cmp( (int32_t) 0xefff1111, (int32_t) 0xefff2222 );
    cmp( (int32_t) 0xefff3333, (int32_t) 0xefff2222 );
    cmp( (int32_t) 0x80000001, (int32_t) 0x7ffffff8 );
    cmp( (int32_t) 0, (int32_t) 0x80000000 );
    cmp( (int32_t) 0x7fffffff, (int32_t) 0x80000000 );

    printf( "uint64_t:\n" );
    cmp( (uint64_t) 1, (uint64_t) 3 );
    cmp( (uint64_t) 1, (uint64_t) -3 );
    cmp( (uint64_t) -1, (uint64_t) 3 );
    cmp( (uint64_t) -1, (uint64_t) -3 );
    cmp( (uint64_t) -1, (uint64_t) -1 );
    cmp( (uint64_t) 247, (uint64_t) 3 );
    cmp( (uint64_t) 247, (uint64_t) -3 );
    cmp( (uint64_t) -247, (uint64_t) 3 );
    cmp( (uint64_t) -247, (uint64_t) -247 );
    cmp( (uint64_t) 0xffff111111111111, (uint64_t) 0xffff222222222222 );
    cmp( (uint64_t) 0xffff333333333333, (uint64_t) 0xffff222222222222 );
    cmp( (uint64_t) 0xefff111111111111, (uint64_t) 0xefff222222222222 );
    cmp( (uint64_t) 0xefff333333333333, (uint64_t) 0xefff222222222222 );
    cmp( (uint64_t) 0x8000000000000001, (uint64_t) 0x7ffffffffffffff8 );
    cmp( (uint64_t) 0, (uint64_t) 0x8000000000000000 );
    cmp( (uint64_t) 0x7fffffffffffffff, (uint64_t) 0x8000000000000000 );

    printf( "int64_t:\n" );
    cmp( (int64_t) 1, (int64_t) 3 );
    cmp( (int64_t) 1, (int64_t) -3 );
    cmp( (int64_t) -1, (int64_t) 3 );
    cmp( (int64_t) -1, (int64_t) -3 );
    cmp( (int64_t) -1, (int64_t) -1 );
    cmp( (int64_t) 247, (int64_t) 3 );
    cmp( (int64_t) 247, (int64_t) -3 );
    cmp( (int64_t) -247, (int64_t) 3 );
    cmp( (int64_t) -247, (int64_t) -247 );
    cmp( (int64_t) 0xffff111111111111, (int64_t) 0xffff222222222222 );
    cmp( (int64_t) 0xffff333333333333, (int64_t) 0xffff222222222222 );
    cmp( (int64_t) 0xefff111111111111, (int64_t) 0xefff222222222222 );
    cmp( (int64_t) 0xefff333333333333, (int64_t) 0xefff222222222222 );
    cmp( (int64_t) 0x8000000000000001, (int64_t) 0x7ffffffffffffff8 );
    cmp( (int64_t) 0, (int64_t) 0x8000000000000000 );
    cmp( (int64_t) 0x7fffffffffffffff, (int64_t) 0x8000000000000000 );
} //main
