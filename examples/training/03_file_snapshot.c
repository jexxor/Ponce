#include <stdio.h>

// Break here after fread: RDI points to four bytes in the stack buffer.
__attribute__((noinline)) void training_pause(const unsigned char *input)
{
    __asm__ __volatile__("" : : "r"(input) : "memory");
}

__attribute__((noinline)) int check_input(const unsigned char *input)
{
    if (input[0] != 'S') return 0;
    if (input[1] != 'E') return 0;
    if (input[2] != 'E') return 0;
    if (input[3] != 'D') return 0;
    return 1;
}

int main(int argc, char **argv)
{
    unsigned char input[4];
    if (argc != 2) {
        puts("Usage: 03_file_snapshot FILE");
        return 2;
    }

    FILE *file = fopen(argv[1], "rb");
    if (file == NULL) {
        perror("fopen");
        return 2;
    }
    size_t count = fread(input, 1, sizeof input, file);
    fclose(file);
    if (count != sizeof input) {
        puts("File must contain at least four bytes");
        return 2;
    }

    training_pause(input);
    puts(check_input(input) ? "Win" : "Try again");
    return 0;
}
