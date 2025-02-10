// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <stdio.h>
#include <stdlib.h>

int main(int argc, const char* argv[])
{
    FILE* is;
    int c;
    size_t offset = 0;

    if (argc != 3)
    {
        fprintf(stderr, "Usage: %s <filename> <varname>\n", argv[0]);
        exit(1);
    }

    if (!(is = fopen(argv[1], "rb")))
    {
        fprintf(stderr, "%s: failed to open %s\n", argv[0], argv[1]);
        exit(1);
    }

    printf("unsigned char %s[] =\n", argv[2]);
    printf("{");

    /* encode each character of the input file */
    while ((c = fgetc(is)) >= 0)
    {
        if ((offset % 8) == 0)
            printf("\n    ");

        printf("0x%02x, ", c);
        offset++;
    }

    /* add a null terminator */
    {
        if ((offset % 8) == 0)
            printf("\n    ");

        printf("0x00, ");
    }

    printf("\n};\n");

    fclose(is);

    return 0;
}
