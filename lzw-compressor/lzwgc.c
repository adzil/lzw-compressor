
#include "lzwgc.h"
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>

token_t token(uint32_t ix) { return 256 + ix; }
uint32_t index(token_t tok) { return tok - 256; }
bool valid_token(lzwgc_dict* dict, token_t tok) {
    return (tok < dict->size) &&
        ((tok < 256) ||
         (tok != dict->prev_token[index(tok)]));
}
uint32_t hash_sc(token_t t, unsigned char c) {
    return ((c << 23) + (t << 11) + (c << 7) + t) * 16180319;
}
void lzwgc_dict_hashtable_add(lzwgc_dict* dict, token_t const s, unsigned char const c, token_t const loc);
void lzwgc_dict_hashtable_rem(lzwgc_dict* dict, token_t const s, unsigned char const c, token_t const loc);
void lzwgc_dict_hashtable_clean_if_saturated(lzwgc_dict* dict);
void lzwgc_dict_hashtable_rebuild(lzwgc_dict* dict);

void lzwgc_dict_init(lzwgc_dict* dict, uint32_t size) {
    assert(((1 << 8) <= size) && (size <= (1 << 24)));
    uint32_t const dyn_size = index(size);
    dict->match_count = malloc(dyn_size * sizeof(uint32_t));
    dict->prev_token = malloc(dyn_size * sizeof(token_t));
    dict->added_char = malloc(dyn_size * sizeof(unsigned char));
    dict->alloc_idx = (dyn_size - 1);  // begin allocating at 0,
    dict->hist_token = size; // special case: no history yet
    dict->size = size;
    assert(0 != dict->match_count);
    assert(0 != dict->prev_token);
    assert(0 != dict->added_char);
    for (uint32_t ii = 0; ii < dyn_size; ++ii) {
        dict->match_count[ii] = 0;
        dict->prev_token[ii] = token(ii); // init to invalid token
        dict->added_char[ii] = 0;
    }

    // hashtable is lazily constructed on first lookup
    dict->ht_size = 0;
    dict->ht_sat = 0;
    dict->ht_data = 0;
}

bool lzwgc_dict_lookup(lzwgc_dict* dict, token_t s, unsigned char c, token_t* out) {

    // build dictionary on first lookup
    if (0 == dict->ht_data) {
        dict->ht_size = 2 * dict->size;
        dict->ht_sat = 0;
        dict->ht_data = malloc(dict->ht_size * sizeof(token_t));
        assert(0 != dict->ht_data);
        lzwgc_dict_hashtable_rebuild(dict);
    }

    // lookup entries from hashtable until we hit a match or a 0
    uint32_t const ht_size = dict->ht_size;
    uint32_t ix = hash_sc(s, c) % ht_size;
    while (0 != dict->ht_data[ix]) {
        token_t const tok = dict->ht_data[ix];
        ix = (ix + 1) % ht_size;
        if (tok < 256)
            continue; // entry was deleted
        uint32_t const tx = index(tok);
        if ((dict->added_char[tx] == c) && (dict->prev_token[tx] == s)) {
            (*out) = tok;
            return true;
        }
    }
    (*out) = dict->size;
    return false;
}


void lzwgc_dict_update(lzwgc_dict* dict, token_t tok) {
    token_t const tok_received = tok;

    // special case: this is first token; invalid history
    bool const this_is_first_token = (dict->hist_token == dict->size);
    if (this_is_first_token) {
        dict->hist_token = tok_received;
        return;
    }

    // increment match counts
    while (tok >= 256) {
        uint32_t const ix = index(tok);
        dict->match_count[ix] += 1;
        tok = dict->prev_token[ix];
    }
    unsigned char const first_char_of_tok_received = tok;

    // collect an entry for allocation
    uint32_t const ii_max = index(dict->size);
    uint32_t ii = dict->alloc_idx;
    while (1) {
        ii = (ii + 1) % ii_max;
        if (0 == dict->match_count[ii])
            break;
        dict->match_count[ii] /= 2;
    }
    dict->alloc_idx = ii;

    lzwgc_dict_hashtable_rem(dict, dict->prev_token[ii], dict->added_char[ii], token(ii));

    dict->prev_token[ii] = dict->hist_token;
    dict->added_char[ii] = first_char_of_tok_received;
    dict->hist_token = tok_received;

    lzwgc_dict_hashtable_add(dict, dict->prev_token[ii], dict->added_char[ii], token(ii));
    lzwgc_dict_hashtable_clean_if_saturated(dict);
}

uint32_t lzwgc_dict_readrev(lzwgc_dict* dict, token_t tok, unsigned char* sr, uint32_t size) {
    if (tok > dict->size) return 0;

    uint32_t ct = 0;
    while ((tok >= 256) && (ct < size)) {
        uint32_t const ix = index(tok);
        sr[ct++] = dict->added_char[ix];
        tok = dict->prev_token[ix];
    }
    if (ct < size)
        sr[ct++] = (unsigned char)tok;
    return ct;
}

void lzwgc_dict_fini(lzwgc_dict* dict) {
    free(dict->match_count); dict->match_count = 0;
    free(dict->prev_token);  dict->prev_token = 0;
    free(dict->added_char);  dict->added_char = 0;
    free(dict->ht_data);     dict->ht_data = 0;
}

void lzwgc_compress_init(lzwgc_compress* st, uint32_t size) {
    lzwgc_dict_init(&(st->dict), size);
    st->matched_token = size; // invalid token to start

                              // no output at start
    st->have_output = false;
    st->token_output = size;
}

void lzwgc_compress_recv(lzwgc_compress* st, unsigned char c) {
    token_t const s = st->matched_token;
    token_t token_found;

    // lookup s+c in dictionary
    if (lzwgc_dict_lookup(&(st->dict), s, c, &token_found)) {
        st->matched_token = token_found; // compress s+c as token_found
        st->have_output = false;         // no output this step
        return;
    }

    // otherwise emit output and begin next match
    st->have_output = (s < st->dict.size);
    st->token_output = s;
    st->matched_token = (token_t)c;

    // update the dictionary after each output
    if (st->have_output) {
        lzwgc_dict_update(&(st->dict), st->token_output);
    }
}

// unless input was empty, we should have a final output.
void lzwgc_compress_fini(lzwgc_compress* st) {
    st->have_output = (st->matched_token < st->dict.size);
    st->token_output = st->matched_token;
    lzwgc_dict_fini(&(st->dict));
}


void lzwgc_decompress_init(lzwgc_decompress* st, uint32_t size) {
    lzwgc_dict_init(&(st->dict), size);

    size_t const szBuff = size * sizeof(unsigned char);
    st->srbuff = malloc(szBuff);
    assert(0 != st->srbuff);

    st->output_count = 0;
    st->output_chars = malloc(szBuff);
    assert(0 != st->output_chars);
}

// decompress will validate the input token for security reasons.
void lzwgc_decompress_recv(lzwgc_decompress* st, token_t tok) {
    // validate input for safety
    bool const valid_input_token = valid_token(&(st->dict), tok);
    assert(valid_input_token);
    if (!valid_input_token) return;

    // decode the token and compute output
    unsigned char * const sr = st->srbuff; // reversed string
    uint32_t ct = lzwgc_dict_readrev(&(st->dict), tok, sr, st->dict.size);
    uint32_t ix = 0;
    while (ct > 0) { st->output_chars[ix++] = sr[--ct]; }
    st->output_count = ix;

    // add token to dictionary
    lzwgc_dict_update(&(st->dict), tok);
}

void lzwgc_decompress_fini(lzwgc_decompress* st) {
    lzwgc_dict_fini(&(st->dict));
    st->output_count = 0;
    free(st->srbuff); st->srbuff = 0;
    free(st->output_chars); st->output_chars = 0;
}




// declare string s+c to be stored as token `loc`
void lzwgc_dict_hashtable_add(lzwgc_dict* dict, token_t const s, unsigned char const c, token_t const loc) {
    assert(loc >= 256);
    if (0 == dict->ht_data)
        return;

    uint32_t const ht_size = dict->ht_size;
    uint32_t ix = hash_sc(s, c) % ht_size;
    while (1) {
        token_t const tok = dict->ht_data[ix];
        if (0 == tok) {
            dict->ht_data[ix] = loc;
            dict->ht_sat += 1; //
            break;
        } else if (1 == tok) {
            dict->ht_data[ix] = loc;
            // no extra saturation (replacing deleted entry)
            break;
        }
        ix = (ix + 1) % ht_size; // collision
    }
}

void lzwgc_dict_hashtable_clean_if_saturated(lzwgc_dict* dict) {
    // a reasonable limit for this table is 80% saturation
    bool const saturated = ((5 * dict->ht_sat) > (4 * dict->ht_size));
    if (saturated) {
        lzwgc_dict_hashtable_rebuild(dict);
    }
}

void lzwgc_dict_hashtable_rebuild(lzwgc_dict* dict) {
    if (0 == dict->ht_data)
        return;

    // clear the existing table
    uint32_t const ht_size = dict->ht_size;
    for (uint32_t ii = 0; ii < ht_size; ++ii) {
        dict->ht_data[ii] = 0;
    }
    dict->ht_sat = 0;

    // rebuild the table from scratch
    uint32_t const dyn_size = index(dict->size);
    for (uint32_t ii = 0; ii < dyn_size; ++ii) {
        token_t const s = dict->prev_token[ii];
        unsigned char const c = dict->added_char[ii];
        token_t const loc = token(ii);
        lzwgc_dict_hashtable_add(dict, s, c, loc);
    }
}

void lzwgc_dict_hashtable_rem(lzwgc_dict* dict, token_t const s, unsigned char const c, token_t const loc) {
    assert(loc >= 256);
    if (0 == dict->ht_data)
        return;

    uint32_t const ht_size = dict->ht_size;
    uint32_t ix = hash_sc(s, c) % ht_size;

    while (0 != dict->ht_data[ix]) {
        if (loc == dict->ht_data[ix]) {
            dict->ht_data[ix] = 1; // entry deleted, but allows collisions
            break;
        }
        ix = (ix + 1) % ht_size;
    }
}
