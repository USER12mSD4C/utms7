#include "../include/string.h"
#include "../kernel/memory.h"

#define ZLIB_MAX_BITS 15
#define ZLIB_MAX_LIT 288
#define ZLIB_MAX_DIST 32

typedef struct {
    u8 *in;
    u32 in_pos;
    u32 in_len;
    u8 *out;
    u32 out_pos;
    u32 out_len;
    u32 bit_buf;
    int bit_count;
} zlib_stream_t;

static int zlib_get_bit(zlib_stream_t *z) {
    if (z->bit_count == 0) {
        if (z->in_pos >= z->in_len) return -1;
        z->bit_buf = z->in[z->in_pos++];
        z->bit_count = 8;
    }
    int bit = z->bit_buf & 1;
    z->bit_buf >>= 1;
    z->bit_count--;
    return bit;
}

static int zlib_get_bits(zlib_stream_t *z, int n) {
    int val = 0;
    for (int i = 0; i < n; i++) {
        int bit = zlib_get_bit(z);
        if (bit < 0) return -1;
        val |= (bit << i);
    }
    return val;
}

static int zlib_build_huffman(int *counts, int n, int *codes, int *first, int *pos) {
    int code = 0;
    for (int bits = 1; bits <= ZLIB_MAX_BITS; bits++) {
        code = (code + counts[bits-1]) << 1;
        first[bits] = code;
    }
    
    for (int i = 0; i < n; i++) {
        int len = counts[i + 256];
        if (len > 0) {
            codes[i] = first[len];
            first[len]++;
            pos[i] = first[len] - 1;
        }
    }
    
    return 0;
}

static int zlib_decode(zlib_stream_t *z, int *lit_counts, int *dist_counts) {
    int lit_codes[ZLIB_MAX_LIT];
    int lit_first[ZLIB_MAX_BITS+1];
    int lit_pos[ZLIB_MAX_LIT];
    
    int dist_codes[ZLIB_MAX_DIST];
    int dist_first[ZLIB_MAX_BITS+1];
    int dist_pos[ZLIB_MAX_DIST];
    
    memset(lit_first, 0, sizeof(lit_first));
    memset(dist_first, 0, sizeof(dist_first));
    
    zlib_build_huffman(lit_counts, ZLIB_MAX_LIT, lit_codes, lit_first, lit_pos);
    zlib_build_huffman(dist_counts, ZLIB_MAX_DIST, dist_codes, dist_first, dist_pos);
    
    while (1) {
        int bits = 0;
        int code = 0;
        
        for (int i = 1; i <= ZLIB_MAX_BITS; i++) {
            int bit = zlib_get_bit(z);
            if (bit < 0) return -1;
            code |= (bit << (i-1));
            
            if (code < lit_first[i+1]) {
                bits = i;
                break;
            }
        }
        
        if (bits == 0) return -1;
        
        int lit = -1;
        for (int i = 0; i < ZLIB_MAX_LIT; i++) {
            if (lit_counts[i+256] == bits && lit_codes[i] == code) {
                lit = i;
                break;
            }
        }
        
        if (lit < 0) return -1;
        
        if (lit < 256) {
            if (z->out_pos >= z->out_len) return -1;
            z->out[z->out_pos++] = lit;
        } else if (lit == 256) {
            break;
        } else {
            int len_extra[] = {0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
            int len_base[] = {3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
            
            int sym = lit - 257;
            int len = len_base[sym] + zlib_get_bits(z, len_extra[sym]);
            
            bits = 0;
            code = 0;
            for (int i = 1; i <= ZLIB_MAX_BITS; i++) {
                int bit = zlib_get_bit(z);
                if (bit < 0) return -1;
                code |= (bit << (i-1));
                
                if (code < dist_first[i+1]) {
                    bits = i;
                    break;
                }
            }
            
            if (bits == 0) return -1;
            
            int dist_sym = -1;
            for (int i = 0; i < ZLIB_MAX_DIST; i++) {
                if (dist_counts[i] == bits && dist_codes[i] == code) {
                    dist_sym = i;
                    break;
                }
            }
            
            if (dist_sym < 0) return -1;
            
            int dist_extra[] = {0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};
            int dist_base[] = {1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
            
            int dist = dist_base[dist_sym] + zlib_get_bits(z, dist_extra[dist_sym]);
            
            if (z->out_pos + len > z->out_len) return -1;
            for (int i = 0; i < len; i++) {
                z->out[z->out_pos] = z->out[z->out_pos - dist];
                z->out_pos++;
            }
        }
    }
    
    return z->out_pos;
}

int zlib_inflate(u8 *in, u32 in_len, u8 **out, u32 *out_len) {
    zlib_stream_t z;
    z.in = in;
    z.in_pos = 0;
    z.in_len = in_len;
    z.out = kmalloc(in_len * 4);
    z.out_pos = 0;
    z.out_len = in_len * 4;
    z.bit_buf = 0;
    z.bit_count = 0;
    
    int cmf = z.in[z.in_pos++];
    int flags = z.in[z.in_pos++];
    
    if ((cmf & 0x0F) != 8) return -1;
    if (((cmf * 256 + flags) % 31) != 0) return -1;
    if (flags & 0x20) {
        z.in_pos += 4;
    }
    
    int final = 0;
    while (!final) {
        final = zlib_get_bit(&z);
        int type = zlib_get_bits(&z, 2);
        
        if (type == 0) {
            z.bit_count = 0;
            z.in_pos = (z.in_pos + 3) & ~3;
            int len = (z.in[z.in_pos] | (z.in[z.in_pos+1] << 8));
            int nlen = (z.in[z.in_pos+2] | (z.in[z.in_pos+3] << 8));
            z.in_pos += 4;
            
            if ((len & 0xFFFF) != (~nlen & 0xFFFF)) return -1;
            
            if (z.out_pos + len > z.out_len) return -1;
            memcpy(z.out + z.out_pos, z.in + z.in_pos, len);
            z.out_pos += len;
            z.in_pos += len;
        } else if (type == 1 || type == 2) {
            int lit_counts[ZLIB_MAX_LIT + 256] = {0};
            int dist_counts[ZLIB_MAX_DIST] = {0};
            
            if (type == 1) {
                for (int i = 0; i < 144; i++) lit_counts[i+256] = 8;
                for (int i = 144; i < 256; i++) lit_counts[i+256] = 9;
                for (int i = 256; i < 280; i++) lit_counts[i+256] = 7;
                for (int i = 280; i < 288; i++) lit_counts[i+256] = 8;
                for (int i = 0; i < 32; i++) dist_counts[i] = 5;
            } else {
                int hlit = zlib_get_bits(&z, 5) + 257;
                int hdist = zlib_get_bits(&z, 5) + 1;
                int hclen = zlib_get_bits(&z, 4) + 4;
                
                int order[] = {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
                int len_counts[19] = {0};
                
                for (int i = 0; i < hclen; i++) {
                    len_counts[order[i]] = zlib_get_bits(&z, 3);
                }
                
                int lit_len[ZLIB_MAX_LIT + ZLIB_MAX_DIST];
                int pos = 0;
                while (pos < hlit + hdist) {
                    int sym = -1;
                    for (int i = 0; i < 19; i++) {
                        if (len_counts[i] > 0) {
                            sym = i;
                            break;
                        }
                    }
                    
                    if (sym < 0) return -1;
                    
                    if (sym < 16) {
                        lit_len[pos++] = sym;
                    } else if (sym == 16) {
                        int rep = zlib_get_bits(&z, 2) + 3;
                        int val = lit_len[pos-1];
                        for (int j = 0; j < rep; j++) lit_len[pos++] = val;
                    } else if (sym == 17) {
                        int rep = zlib_get_bits(&z, 3) + 3;
                        pos += rep;
                    } else if (sym == 18) {
                        int rep = zlib_get_bits(&z, 7) + 11;
                        pos += rep;
                    }
                }
                
                for (int i = 0; i < hlit; i++) lit_counts[i+256] = lit_len[i];
                for (int i = 0; i < hdist; i++) dist_counts[i] = lit_len[hlit + i];
            }
            
            if (zlib_decode(&z, lit_counts, dist_counts) < 0) return -1;
        } else {
            return -1;
        }
    }
    
    *out = z.out;
    *out_len = z.out_pos;
    return 0;
}
