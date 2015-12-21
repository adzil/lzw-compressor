/*
* This is the header just for LZW-GC, a variation on LZW that will
* incrementally garbage collect the dictionary with intention to
* better support large files or streams.
*
* Unlike standard LZW, LZW-GC uses the same algorithm to update the
* dictionary at both compression and decompression. This avoids the
* special LZW corner case.
*
* These APIs use simple assertions (assert.h) for error checking.
*/

#ifndef LZWGC_H

#include <stdbool.h>
#include <stdint.h>

typedef uint32_t token_t; // documentation some integers as tokens

typedef struct
{
    uint32_t        size;        // size of dictionary
    uint32_t      * match_count; // slice of match counts
    token_t       * prev_token;  // previously matched
    unsigned char * added_char;  // character matched
    token_t         hist_token;  // last token in update stream 
    uint32_t        alloc_idx;   // last index allocated

                                 // hashtable is constructed on first lzwgc_dict_lookup
    token_t *       ht_data;     // tokens to search 
    uint32_t        ht_size;     // space for hashtable
    uint32_t        ht_sat;      // number of filled locations
} lzwgc_dict;

typedef struct
{
    // internal state
    lzwgc_dict      dict;
    token_t         matched_token;

    // output after each operation (at most one token)
    bool            have_output;
    token_t         token_output;
} lzwgc_compress;


typedef struct
{
    // internal state
    lzwgc_dict      dict;
    unsigned char * srbuff;

    // output after each operation (a number of characters)
    uint32_t        output_count;
    unsigned char * output_chars;
} lzwgc_decompress;

void lzwgc_dict_init(lzwgc_dict*, uint32_t size); // size from 2^8 to 2^24
void lzwgc_dict_update(lzwgc_dict*, token_t); // update from token stream
bool lzwgc_dict_lookup(lzwgc_dict*, token_t s, unsigned char c, token_t* out);
void lzwgc_dict_fini(lzwgc_dict*); // clear memory from dictionary

                                   // fetch at most count elements (reversed)
uint32_t lzwgc_dict_readrev(lzwgc_dict*, token_t, unsigned char*, uint32_t count);

// Incremental API; element at a time
// compress will receive bytes and output zero or one tokens
// finalization will release memory and usually emits a final output.
void lzwgc_compress_init(lzwgc_compress*, uint32_t size);
void lzwgc_compress_recv(lzwgc_compress*, unsigned char);
void lzwgc_compress_fini(lzwgc_compress*);

// decompress will receive a token and output at least one byte
// finalization will release memory, and in this case never has output.
void lzwgc_decompress_init(lzwgc_decompress*, uint32_t size);
void lzwgc_decompress_recv(lzwgc_decompress*, token_t  tok);
void lzwgc_decompress_fini(lzwgc_decompress*);

#define LZWGC_H
#endif


