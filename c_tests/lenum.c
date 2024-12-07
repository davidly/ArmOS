#include <stdio.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/param.h>
#include <cerrno>
#include <cstring>

int main( int argc, char * argv[] )
{
    DIR * dir = opendir( "." );
    if ( 0 == dir )
    {
        printf( "can't open current folder, error %d\n", errno );
        return -1;
    }

    int count = 0;
    struct dirent * entry;
    while ( entry = readdir( dir ) )
    {
        if ( 'm' == entry->d_name[ 0 ] )
        {
            count++;
            printf( "file %d: %s\n", count, entry->d_name );
        }
    }
    closedir( dir );
} // main