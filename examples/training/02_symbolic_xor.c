#include <stdio.h>
#include <string.h>

// Break here, then symbolize four bytes at the address in RDI.
__attribute__((noinline)) void training_pause(const unsigned char *input)
{
    __asm__ __volatile__("" : : "r"(input) : "memory");
}

__attribute__((noinline)) int check_input(const unsigned char *input)
{
    const unsigned char encoded[4] = { 0x16, 0x14, 0x03, 0x10 };
    for (int i = 0; i < 4; ++i) {
        if ((unsigned char)(input[i] ^ 0x55) != encoded[i])
            return 0;
    }
    return 1;
}

int main(int argc, char **argv)
{
    if (argc != 2 || strlen(argv[1]) != 4) {
        puts("Usage: 02_symbolic_xor FOUR_CHARS");
        return 2;
    }

    training_pause((const unsigned char *)argv[1]);
    puts(check_input((const unsigned char *)argv[1]) ? "Win" : "Try again");
    return 0;
}
