
#include "lzwgc.h"
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#define buff_size 1

// for now, write tokens unpacked bigendian, 2-3 octets
void write_token(FILE* out, token_t tok, int bits) {
    int const t16 = (tok >> 16) & 0xff;
    int const t8 = (tok >> 8) & 0xff;
    int const t0 = tok & 0xff;
    if (bits > 16) fputc(t16, out);
    fputc(t8, out);
    fputc(t0, out);
}
bool read_token(FILE* in, token_t* tok, int bits) {
    int t16 = 0;
    int t8 = 0;
    int t0 = 0;
    if (bits > 16) t16 = fgetc(in);
    t8 = fgetc(in);
    t0 = fgetc(in);
    (*tok) = ((t16 & 0xff) << 16) + ((t8 & 0xff) << 8) + (t0 & 0xff);
    return (t16 != EOF) && (t8 != EOF) && (t0 != EOF);
}


// compress will process input to output.
// this uses the knowledge that N inputs can produce at most N outputs. 
/*void compress(FILE* in, FILE* out, int bits, unsigned long start, unsigned long offset) {
    lzwgc_compress st;
    uint32_t const dict_size = (1 << bits) - 1; // reserve top token for client

    int read_buff;
    //unsigned long len;
    bool isLoop;

    fseek(in, start, SEEK_SET);

    lzwgc_compress_init(&st, dict_size);
    
    while (true) {
        if (feof(in)) break;
        if (offset) if (ftell(in) >= start + offset) break;

        read_buff = fgetc(in);
        if (read_buff == EOF) break;
        lzwgc_compress_recv(&st, read_buff);

        if (st.have_output) {
            write_token(out, st.token_output, bits);
        }
    }

    lzwgc_compress_fini(&st);
    if (st.have_output) {
        write_token(out, st.token_output, bits);
    }

    write_token(out, 0xffff, bits);
}

void decompress(FILE* in, FILE* out, int bits, unsigned long start) {
    lzwgc_decompress st;
    uint32_t const dict_size = (1 << bits) - 1; // reserve top token for client
    lzwgc_decompress_init(&st, dict_size);
    token_t tok;

    fseek(in, start, SEEK_SET);

    while (read_token(in, &tok, bits)) {
        if (tok == 0xffff) break;
        lzwgc_decompress_recv(&st, tok);
        fwrite(st.output_chars, 1, st.output_count, out);
    }
    lzwgc_decompress_fini(&st);
}

int marker(unsigned long *pos, FILE *in) {
    int sum;
    uint16_t buf;
    unsigned long lastpos;

    lastpos = ftell(in);
    fseek(in, 0, SEEK_SET);
    sum = 0;

    while (fread(&buf, 2, 1, in)) {
        if (buf == 0xffff) {
            printf("Mark on %d\n", ftell(in));
            pos[sum++] = ftell(in);
        }
    }

    fseek(in, lastpos, SEEK_SET);

    return sum;
}

int main(int argc, char const * args[]) {
    FILE *fin, *fout1, *fout2;
    unsigned long fsize, fhalf, rem;
    unsigned long mark[16];
    int marklen;

    fopen_s(&fin, "image.tif", "rb");
    fopen_s(&fout1, "comp0.bin", "wb");
    fopen_s(&fout2, "comp1.bin", "wb");

    fseek(fin, 0, SEEK_END);
    fsize = ftell(fin);
    fseek(fin, 0, SEEK_SET);

    printf("The file size is %ld\n", fsize);
    fhalf = fsize / 2;
    
    compress(fin, fout1, 16, 0, fhalf);
    compress(fin, fout2, 16, fhalf, 0);

    fsize = ftell(fout1);
    printf("Output 1 file size is %ld\n", fsize);
    fsize = ftell(fout2);
    printf("Output 2 file size is %ld\n", fsize);

    fclose(fin);
    fclose(fout1);
    fclose(fout2);


    fopen_s(&fin, "comp.bin", "rb");
    fopen_s(&fout1, "decomp.tif", "wb");
    //fopen_s(&fout2, "decomp2.tif", "wb");

    marklen = marker(mark, fin);

    decompress(fin, fout1, 16, 0);
    decompress(fin, fout1, 16, mark[0]);

    fsize = ftell(fout1);
    printf("Output file size is %ld\n", fsize);

    fclose(fin);
    fclose(fout1);
    //fclose(fout2);

    return 0;
}*/



 