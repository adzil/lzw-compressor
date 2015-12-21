#include "lzwgc.h"
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>

#define mpi_root 0

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
void compress(FILE* in, FILE* out, int bits, unsigned long start, unsigned long offset) {
    lzwgc_compress st;
    uint32_t const dict_size = (1 << bits) - 1; // reserve top token for client

    int read_buff;

    fseek(in, start, SEEK_SET);

    lzwgc_compress_init(&st, dict_size);

    while (true) {
        if (feof(in)) break;
        if (offset) if ((unsigned long) ftell(in) >= start + offset) break;

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
    pos[0] = 0;
    sum = 1;

    while (fread(&buf, 2, 1, in)) {
        if (buf == 0xffff) {
            pos[sum++] = ftell(in);
        }
    }

    sum--;

    fseek(in, lastpos, SEEK_SET);

    return sum;
}

int fsize(FILE *f) {
    unsigned long pos, sz;

    pos = ftell(f);
    fseek(f, 0, SEEK_END);
    sz = ftell(f);
    fseek(f, pos, SEEK_SET);

    return sz;
}

/** Some required arguments to run LZW compressor:
 *  lzw.exe c|d file.in file.out
 */
int main(int argc, char *argv[]) {
    FILE *in, *out;
    int pNum, pId, i, buf;
    char *fname;
    unsigned long start, offset, startsize, endsize, ratio;
    double starttime, deltatime;
    bool isDecompress;

    // Initialize MPI
    MPI_Init(NULL, NULL);
    MPI_Comm_rank(MPI_COMM_WORLD, &pId);
    MPI_Comm_size(MPI_COMM_WORLD, &pNum);

    fname = malloc(sizeof(char) * 256);
    
    // Checks for legitimate arguments
    if (pId == mpi_root) {
        if (argc != 4) {
            printf("ERROR: Argument required.");
            return -1;
        }

        if (argv[1][0] == 'c') {
            isDecompress = false;
        } else if (argv[1][0] == 'd') {
            isDecompress = true;
        } else {
            printf("ERROR: Job argument (c|d) required.");
            return -1;
        }

        if (fopen_s(&in, argv[2], "rb")) {
            printf("ERROR: Cannot open file %s.", argv[2]);
            return -1;
        }
        startsize = fsize(in);
        offset = startsize / pNum;
        fclose(in);

        starttime = MPI_Wtime();
    }
    
    // Create job requirement
    MPI_Bcast(&isDecompress, 1, MPI_BYTE, mpi_root, MPI_COMM_WORLD);
    MPI_Bcast(&offset, 1, MPI_UNSIGNED_LONG, mpi_root, MPI_COMM_WORLD);
    start = offset * pId;
    sprintf_s(fname, 256, "%s.%d", argv[3], pId);
    
    // Start compress job
    fopen_s(&in, argv[2], "rb");
    fopen_s(&out, fname, "wb");

    compress(in, out, 16, start, offset);

    fclose(in);
    fclose(out);

    MPI_Barrier(MPI_COMM_WORLD);

    // Gather written data
    if (pId == mpi_root) {

        // Write final file
        fopen_s(&out, argv[3], "wb");

        for (i = 0; i < pNum; i++) {
            // Create temp filename
            sprintf_s(fname, 256, "%s.%d", argv[3], i);
            fopen_s(&in, fname, "rb");
            while (true) {
                buf = fgetc(in);
                if (buf == EOF) break;
                fputc(buf, out);
            }
            fclose(in);
            _unlink(fname);
        }
        endsize = ftell(out);
        fclose(out);

        deltatime = MPI_Wtime() - starttime;
        ratio = (endsize * 100) / startsize;
        printf("The compression process took %.8fsec with %d threads --> %ldb/%ldb (%d%%).\n",
               deltatime, pNum, startsize, endsize, ratio);
    }
    
    MPI_Finalize();

    return 0;
}