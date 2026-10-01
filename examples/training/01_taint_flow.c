#include <stdio.h>
#include <string.h>

// Break here: on Linux x86_64, RDI points to the input bytes.
__attribute__((noinline)) void training_pause(const unsigned char *input)
{
    __asm__ __volatile__("" : : "r"(input) : "memory");
}

__attribute__((noinline)) int check_input(const unsigned char *input)
{
    unsigned char first = input[0];
    unsigned char second = input[1];
    if (first != 'R')
        return 0;
    if (second != 'U')
        return 0;
    if (input[2] != 'N' || input[3] != 'E')
        return 0;
    return 1;
}

int main(int argc, char **argv)
{
    if (argc != 2 || strlen(argv[1]) != 4) {
        puts("Usage: 01_taint_flow FOUR_CHARS");
        return 2;
    }

    training_pause((const unsigned char *)argv[1]);
    puts(check_input((const unsigned char *)argv[1]) ? "Win" : "Try again");
    return 0;
}
