#include <stdio.h>

int main()
{
    int x = 0; // unused variable to provoke compiler output
    fprintf(stderr, "...world!\n");
    printf("Hello...\n");
}
