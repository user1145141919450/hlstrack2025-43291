#ifndef _XF_SECURITY_SHA224_256_HPP_
#define _XF_SECURITY_SHA224_256_HPP_

#include <ap_int.h>
#include <hls_stream.h>

#include "xf_security/types.hpp"
#include "xf_security/utils.hpp"

namespace xf {
namespace security {
namespace internal {

// 基础位运算（函数化，便于工具优化）
static inline ap_uint<32> rotr32(ap_uint<32> x, int n) {
#pragma HLS INLINE
    return (x >> n) | (x << (32 - n));
}

static inline ap_uint<32> Sigma0(ap_uint<32> x) {
#pragma HLS INLINE
    return rotr32(x, 2) ^ rotr32(x, 13) ^ rotr32(x, 22);
}

static inline ap_uint<32> Sigma1(ap_uint<32> x) {
#pragma HLS INLINE
    return rotr32(x, 6) ^ rotr32(x, 11) ^ rotr32(x, 25);
}

static inline ap_uint<32> sigma0(ap_uint<32> x) {
#pragma HLS INLINE
    return rotr32(x, 7) ^ rotr32(x, 18) ^ (x >> 3);
}

static inline ap_uint<32> sigma1(ap_uint<32> x) {
#pragma HLS INLINE
    return rotr32(x, 17) ^ rotr32(x, 19) ^ (x >> 10);
}

static inline ap_uint<32> Ch(ap_uint<32> e, ap_uint<32> f, ap_uint<32> g) {
#pragma HLS INLINE
    return (e & f) ^ (~e & g);
}

static inline ap_uint<32> Maj(ap_uint<32> a, ap_uint<32> b, ap_uint<32> c) {
#pragma HLS INLINE
    return (a & b) ^ (a & c) ^ (b & c);
}

// 处理块结构
struct SHA256Block {
    ap_uint<32> M[16];
};

// 预处理函数（32位版本）- 简化版本避免综合错误
inline void preProcessing(hls::stream<ap_uint<32> >& msg_strm,
                         hls::stream<ap_uint<64> >& len_strm,
                         hls::stream<bool>& end_len_strm,
                         hls::stream<SHA256Block>& blk_strm,
                         hls::stream<uint64_t>& nblk_strm,
                         hls::stream<bool>& end_nblk_strm) {
    
    bool end_flag = end_len_strm.read();
    
LOOP_SHA256_GENENERATE_MAIN_32:
    while (!end_flag) {
        #pragma HLS LOOP_TRIPCOUNT min=1 max=10
        
        uint64_t len = len_strm.read();
        uint64_t L = 8 * len;
        uint64_t blk_num = (len >> 6) + 1 + ((len & 0x3f) > 55);
        nblk_strm.write(blk_num);
        end_nblk_strm.write(false);

        char left = (char)(len & 0x3fULL);
        uint64_t full_blocks = len >> 6;

    LOOP_SHA256_GEN_FULL_BLKS_32:
        for (uint64_t j = 0; j < full_blocks; ++j) {
            #pragma HLS PIPELINE II=1
            SHA256Block b0;
            #pragma HLS ARRAY_PARTITION variable=b0.M complete dim=1

        LOOP_SHA256_GEN_ONE_FULL_BLK_32:
            for (int i = 0; i < 16; ++i) {
                #pragma HLS UNROLL
                ap_uint<32> l = msg_strm.read();
                ap_uint<32> be = ((l & 0x000000ffU) << 24) | 
                                ((l & 0x0000ff00U) << 8) | 
                                ((l & 0x00ff0000U) >> 8) |
                                ((l & 0xff000000U) >> 24);
                b0.M[i] = be;
            }
            blk_strm.write(b0);
        }

        if (left == 0) {
            SHA256Block b;
            #pragma HLS ARRAY_PARTITION variable=b.M complete dim=1
            b.M[0] = 0x80000000U;
            for (int i = 1; i < 14; ++i) {
                #pragma HLS UNROLL
                b.M[i] = 0;
            }
            b.M[14] = (ap_uint<32>)((L >> 32) & 0xffffffffULL);
            b.M[15] = (ap_uint<32>)(L & 0xffffffffULL);
            blk_strm.write(b);
        } 
        else if (left < 56) {
            SHA256Block b;
            #pragma HLS ARRAY_PARTITION variable=b.M complete dim=1
            char left_shift_2 = left >> 2;
            char left_mod_4 = left & 3;

        LOOP_SHA256_GEN_COPY_TAIL_AND_ONE_32:
            for (int i = 0; i < 14; ++i) {
                #pragma HLS PIPELINE II=1
                if (i < left_shift_2) {
                    ap_uint<32> l = msg_strm.read();
                    ap_uint<32> be = ((l & 0x000000ffU) << 24) | 
                                    ((l & 0x0000ff00U) << 8) | 
                                    ((l & 0x00ff0000U) >> 8) |
                                    ((l & 0xff000000U) >> 24);
                    b.M[i] = be;
                } 
                else if (i > left_shift_2) {
                    b.M[i] = 0U;
                } 
                else {
                    if (left_mod_4 == 0) {
                        b.M[i] = 0x80000000U;
                    } else if (left_mod_4 == 1) {
                        ap_uint<32> l = msg_strm.read();
                        ap_uint<32> be = ((l & 0x000000ffU) << 24);
                        b.M[i] = be | 0x00800000U;
                    } else if (left_mod_4 == 2) {
                        ap_uint<32> l = msg_strm.read();
                        ap_uint<32> be = ((l & 0x000000ffU) << 24) | ((l & 0x0000ff00U) << 8);
                        b.M[i] = be | 0x00008000U;
                    } else {
                        ap_uint<32> l = msg_strm.read();
                        ap_uint<32> be = ((l & 0x000000ffU) << 24) | ((l & 0x0000ff00U) << 8) | ((l & 0x00ff0000U) >> 8);
                        b.M[i] = be | 0x00000080U;
                    }
                }
            }
            b.M[14] = (ap_uint<32>)((L >> 32) & 0xffffffffULL);
            b.M[15] = (ap_uint<32>)(L & 0xffffffffULL);
            blk_strm.write(b);
        } 
        else {
            SHA256Block b;
            #pragma HLS ARRAY_PARTITION variable=b.M complete dim=1
            char left_shift_2 = left >> 2;
            char left_mod_4 = left & 3;

        LOOP_SHA256_GEN_COPY_TAIL_ONLY_32:
            for (int i = 0; i < 16; ++i) {
                #pragma HLS UNROLL
                if (i < left_shift_2) {
                    ap_uint<32> l = msg_strm.read();
                    ap_uint<32> be = ((l & 0x000000ffU) << 24) | 
                                    ((l & 0x0000ff00U) << 8) | 
                                    ((l & 0x00ff0000U) >> 8) |
                                    ((l & 0xff000000U) >> 24);
                    b.M[i] = be;
                } 
                else if (i > left_shift_2) {
                    b.M[i] = 0U;
                } 
                else {
                    if (left_mod_4 == 0) {
                        b.M[i] = 0x80000000U;
                    } else if (left_mod_4 == 1) {
                        ap_uint<32> l = msg_strm.read();
                        ap_uint<32> be = ((l & 0x000000ffU) << 24);
                        b.M[i] = be | 0x00800000U;
                    } else if (left_mod_4 == 2) {
                        ap_uint<32> l = msg_strm.read();
                        ap_uint<32> be = ((l & 0x000000ffU) << 24) | ((l & 0x0000ff00U) << 8);
                        b.M[i] = be | 0x00008000U;
                    } else {
                        ap_uint<32> l = msg_strm.read();
                        ap_uint<32> be = ((l & 0x000000ffU) << 24) | ((l & 0x0000ff00U) << 8) | ((l & 0x00ff0000U) >> 8);
                        b.M[i] = be | 0x00000080U;
                    }
                }
            }
            blk_strm.write(b);

            SHA256Block b1;
            #pragma HLS ARRAY_PARTITION variable=b1.M complete dim=1
            for (int i = 0; i < 14; ++i) {
                #pragma HLS UNROLL
                b1.M[i] = 0U;
            }
            b1.M[14] = (ap_uint<32>)((L >> 32) & 0xffffffffULL);
            b1.M[15] = (ap_uint<32>)(L & 0xffffffffULL);
            blk_strm.write(b1);
        }
        
        end_flag = end_len_strm.read();
    }
    end_nblk_strm.write(true);
}

// 预处理函数（64位版本）- 简化版本
inline void preProcessing(hls::stream<ap_uint<64> >& msg_strm,
                         hls::stream<ap_uint<64> >& len_strm,
                         hls::stream<bool>& end_len_strm,
                         hls::stream<SHA256Block>& blk_strm,
                         hls::stream<uint64_t>& nblk_strm,
                         hls::stream<bool>& end_nblk_strm) {
    
    bool end_flag = end_len_strm.read();
    
LOOP_SHA256_GENENERATE_MAIN_64:
    while (!end_flag) {
        #pragma HLS LOOP_TRIPCOUNT min=1 max=10
        
        uint64_t len = len_strm.read();
        uint64_t L = 8 * len;
        uint64_t blk_num = (len >> 6) + 1 + ((len & 0x3f) > 55);
        nblk_strm.write(blk_num);
        end_nblk_strm.write(false);

        char left = (char)(len & 0x3fULL);
        uint64_t full_blocks = len >> 6;

    LOOP_SHA256_GEN_FULL_BLKS_64:
        for (uint64_t j = 0; j < full_blocks; ++j) {
            #pragma HLS PIPELINE II=1
            SHA256Block b0;
            #pragma HLS ARRAY_PARTITION variable=b0.M complete dim=1

        LOOP_SHA256_GEN_ONE_FULL_BLK_64:
            for (int i = 0; i < 16; i += 2) {
                #pragma HLS UNROLL
                ap_uint<64> ll = msg_strm.read();
                ap_uint<32> l0 = (ap_uint<32>)(ll & 0xffffffffULL);
                ap_uint<32> be0 = ((l0 & 0x000000ffU) << 24) | 
                                 ((l0 & 0x0000ff00U) << 8) | 
                                 ((l0 & 0x00ff0000U) >> 8) |
                                 ((l0 & 0xff000000U) >> 24);
                ap_uint<32> l1 = (ap_uint<32>)((ll >> 32) & 0xffffffffULL);
                ap_uint<32> be1 = ((l1 & 0x000000ffU) << 24) | 
                                 ((l1 & 0x0000ff00U) << 8) | 
                                 ((l1 & 0x00ff0000U) >> 8) |
                                 ((l1 & 0xff000000U) >> 24);
                b0.M[i] = be0;
                b0.M[i + 1] = be1;
            }
            blk_strm.write(b0);
        }

        if (left == 0) {
            SHA256Block b;
            #pragma HLS ARRAY_PARTITION variable=b.M complete dim=1
            b.M[0] = 0x80000000U;
            for (int i = 1; i < 14; ++i) {
                #pragma HLS UNROLL
                b.M[i] = 0;
            }
            b.M[14] = (ap_uint<32>)((L >> 32) & 0xffffffffULL);
            b.M[15] = (ap_uint<32>)(L & 0xffffffffULL);
            blk_strm.write(b);
        } 
        else {
            SHA256Block b;
            #pragma HLS ARRAY_PARTITION variable=b.M complete dim=1
            char left_shift_3 = left >> 3;
            bool need_second_block = (left >= 56);
            int loop_bound = need_second_block ? 8 : 7;
            
        LOOP_SHA256_GEN_COPY_TAIL_PAD_64:
            for (int i = 0; i < loop_bound; ++i) {
                #pragma HLS PIPELINE II=1
                if (i < left_shift_3) {
                    ap_uint<64> ll = msg_strm.read();
                    ap_uint<32> l0 = (ap_uint<32>)(ll & 0xffffffffULL);
                    ap_uint<32> be0 = ((l0 & 0x000000ffU) << 24) | 
                                     ((l0 & 0x0000ff00U) << 8) | 
                                     ((l0 & 0x00ff0000U) >> 8) |
                                     ((l0 & 0xff000000U) >> 24);
                    ap_uint<32> l1 = (ap_uint<32>)((ll >> 32) & 0xffffffffULL);
                    ap_uint<32> be1 = ((l1 & 0x000000ffU) << 24) | 
                                     ((l1 & 0x0000ff00U) << 8) | 
                                     ((l1 & 0x00ff0000U) >> 8) |
                                     ((l1 & 0xff000000U) >> 24);
                    b.M[i * 2] = be0;
                    b.M[i * 2 + 1] = be1;
                } 
                else if (i > left_shift_3) {
                    b.M[i * 2] = 0U;
                    b.M[i * 2 + 1] = 0U;
                } 
                else {
                    if ((left & 4) == 0) {
                        ap_uint<32> e = left & 3;
                        if (e == 0) {
                            b.M[i * 2] = 0x80000000U;
                        } else if (e == 1) {
                            ap_uint<32> l = (ap_uint<32>)(msg_strm.read().to_uint64() & 0xffffffffULL);
                            ap_uint<32> be = ((l & 0x000000ffU) << 24);
                            b.M[i * 2] = be | 0x00800000U;
                        } else if (e == 2) {
                            ap_uint<32> l = (ap_uint<32>)(msg_strm.read().to_uint64() & 0xffffffffULL);
                            ap_uint<32> be = ((l & 0x000000ffU) << 24) | ((l & 0x0000ff00U) << 8);
                            b.M[i * 2] = be | 0x00008000U;
                        } else {
                            ap_uint<32> l = (ap_uint<32>)(msg_strm.read().to_uint64() & 0xffffffffULL);
                            ap_uint<32> be = ((l & 0x000000ffU) << 24) | ((l & 0x0000ff00U) << 8) | ((l & 0x00ff0000U) >> 8);
                            b.M[i * 2] = be | 0x00000080U;
                        }
                        b.M[i * 2 + 1] = 0U;
                    } 
                    else {
                        ap_uint<64> ll = msg_strm.read();
                        ap_uint<32> l0 = (ap_uint<32>)(ll & 0xffffffffULL);
                        ap_uint<32> be0 = ((l0 & 0x000000ffU) << 24) | 
                                         ((l0 & 0x0000ff00U) << 8) | 
                                         ((l0 & 0x00ff0000U) >> 8) |
                                         ((l0 & 0xff000000U) >> 24);
                        b.M[i * 2] = be0;

                        ap_uint<32> l1 = (ap_uint<32>)((ll >> 32) & 0xffffffffULL);
                        ap_uint<32> e = left & 3;
                        ap_uint<32> be1;
                        if (e == 0) {
                            be1 = 0x80000000U;
                        } else if (e == 1) {
                            be1 = ((l1 & 0x000000ffU) << 24) | 0x00800000U;
                        } else if (e == 2) {
                            be1 = ((l1 & 0x000000ffU) << 24) | ((l1 & 0x0000ff00U) << 8) | 0x00008000U;
                        } else {
                            be1 = ((l1 & 0x000000ffU) << 24) | ((l1 & 0x0000ff00U) << 8) | ((l1 & 0x00ff0000U) >> 8) | 0x00000080U;
                        }
                        b.M[i * 2 + 1] = be1;
                    }
                }
            }

            if (!need_second_block) {
                b.M[14] = (ap_uint<32>)((L >> 32) & 0xffffffffULL);
                b.M[15] = (ap_uint<32>)(L & 0xffffffffULL);
                blk_strm.write(b);
            } 
            else {
                blk_strm.write(b);
                SHA256Block b1;
                #pragma HLS ARRAY_PARTITION variable=b1.M complete dim=1
                for (int i = 0; i < 14; ++i) {
                    #pragma HLS UNROLL
                    b1.M[i] = 0U;
                }
                b1.M[14] = (ap_uint<32>)((L >> 32) & 0xffffffffULL);
                b1.M[15] = (ap_uint<32>)(L & 0xffffffffULL);
                blk_strm.write(b1);
            }
        }
        
        end_flag = end_len_strm.read();
    }
    end_nblk_strm.write(true);
}

// 压缩函数 - 简化版本
static inline void compress_one(const ap_uint<32> M_in[16], ap_uint<32> H[8]) {
#pragma HLS INLINE off
    
    ap_uint<32> wbuf[16];
#pragma HLS ARRAY_PARTITION variable=wbuf complete dim=1

    // K常数数组
    const ap_uint<32> K[64] = {
        0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
        0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
        0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
        0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
        0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
        0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
        0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
        0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U
    };

    // 预装载消息字
LOAD_W0_15:
    for (int i = 0; i < 16; i++) {
        #pragma HLS UNROLL
        wbuf[i] = M_in[i];
    }

    ap_uint<32> a = H[0], b = H[1], c = H[2], d = H[3];
    ap_uint<32> e = H[4], f = H[5], g = H[6], h = H[7];

RoundLoop:
    for (int i = 0; i < 64; i++) {
        #pragma HLS PIPELINE II=1
        
        ap_uint<32> Wi;
        if (i < 16) {
            Wi = wbuf[i];
        } else {
            ap_uint<4> i_m2 = (i - 2) & 15;
            ap_uint<4> i_m7 = (i - 7) & 15;
            ap_uint<4> i_m15 = (i - 15) & 15;
            ap_uint<4> i_m16 = (i - 16) & 15;

            ap_uint<32> s1 = sigma1(wbuf[i_m2]);
            ap_uint<32> s0 = sigma0(wbuf[i_m15]);
            ap_uint<32> t1 = s1 + wbuf[i_m7];
            ap_uint<32> t2 = s0 + wbuf[i_m16];
            Wi = t1 + t2;

            wbuf[i & 15] = Wi;
        }

        ap_uint<32> S1_e = Sigma1(e);
        ap_uint<32> ch_efg = Ch(e, f, g);
        ap_uint<32> S0_a = Sigma0(a);
        ap_uint<32> maj_abc = Maj(a, b, c);

        ap_uint<32> T1 = h + S1_e + ch_efg + K[i] + Wi;
        ap_uint<32> T2 = S0_a + maj_abc;

        h = g;
        g = f;
        f = e;
        e = d + T1;
        d = c;
        c = b;
        b = a;
        a = T1 + T2;
    }

    H[0] = H[0] + a;
    H[1] = H[1] + b;
    H[2] = H[2] + c;
    H[3] = H[3] + d;
    H[4] = H[4] + e;
    H[5] = H[5] + f;
    H[6] = H[6] + g;
    H[7] = H[7] + h;
}

// 融合摘要计算
template <int h_width>
void fusedDigest(hls::stream<SHA256Block>& blk_strm,
                hls::stream<uint64_t>& nblk_strm,
                hls::stream<bool>& end_nblk_strm,
                hls::stream<ap_uint<h_width> >& hash_strm,
                hls::stream<bool>& end_hash_strm) {
    XF_SECURITY_STATIC_ASSERT((h_width == 256) || (h_width == 224), "Unsupported hash width, must be 224 or 256");

    bool end_flag = end_nblk_strm.read();
    
MAIN_MSG_LOOP:
    while (!end_flag) {
        #pragma HLS LOOP_TRIPCOUNT min=1 max=10
        
        uint64_t blk_num = nblk_strm.read();

        ap_uint<32> H[8];
        #pragma HLS ARRAY_PARTITION variable=H complete dim=1
        
        // 初始化哈希值
        if (h_width == 224) {
            H[0] = 0xc1059ed8U;
            H[1] = 0x367cd507U;
            H[2] = 0x3070dd17U;
            H[3] = 0xf70e5939U;
            H[4] = 0xffc00b31U;
            H[5] = 0x68581511U;
            H[6] = 0x64f98fa7U;
            H[7] = 0xbefa4fa4U;
        } else {
            H[0] = 0x6a09e667U;
            H[1] = 0xbb67ae85U;
            H[2] = 0x3c6ef372U;
            H[3] = 0xa54ff53aU;
            H[4] = 0x510e527fU;
            H[5] = 0x9b05688cU;
            H[6] = 0x1f83d9abU;
            H[7] = 0x5be0cd19U;
        }

    BLOCK_LOOP:
        for (uint64_t n = 0; n < blk_num; ++n) {
            #pragma HLS PIPELINE II=1
            #pragma HLS LOOP_TRIPCOUNT min=1 max=2
            
            SHA256Block blk = blk_strm.read();
            #pragma HLS ARRAY_PARTITION variable=blk.M complete
            compress_one(blk.M, H);
        }

        // 输出哈希值
        if (h_width == 224) {
            ap_uint<224> out_hash = 0;
            for (int i = 0; i < 7; ++i) {
                #pragma HLS UNROLL
                ap_uint<32> word = H[i];
                ap_uint<32> little_endian = ((word & 0x000000ffU) << 24) |
                                          ((word & 0x0000ff00U) << 8) |
                                          ((word & 0x00ff0000U) >> 8) |
                                          ((word & 0xff000000U) >> 24);
                out_hash.range(32*i+31, 32*i) = little_endian;
            }
            hash_strm.write(out_hash);
        } else {
            ap_uint<256> out_hash = 0;
            for (int i = 0; i < 8; ++i) {
                #pragma HLS UNROLL
                ap_uint<32> word = H[i];
                ap_uint<32> little_endian = ((word & 0x000000ffU) << 24) |
                                          ((word & 0x0000ff00U) << 8) |
                                          ((word & 0x00ff0000U) >> 8) |
                                          ((word & 0xff000000U) >> 24);
                out_hash.range(32*i+31, 32*i) = little_endian;
            }
            hash_strm.write(out_hash);
        }
        end_hash_strm.write(false);
        
        end_flag = end_nblk_strm.read();
    }
    end_hash_strm.write(true);
}

// 顶层数据流
template <int m_width, int h_width>
inline void sha256_top(hls::stream<ap_uint<m_width> >& msg_strm,
                      hls::stream<ap_uint<64> >& len_strm,
                      hls::stream<bool>& end_len_strm,
                      hls::stream<ap_uint<h_width> >& hash_strm,
                      hls::stream<bool>& end_hash_strm) {
    #pragma HLS DATAFLOW
    
    XF_SECURITY_STATIC_ASSERT((m_width == 32) || (m_width == 64), "m_width must be 32 or 64");
    XF_SECURITY_STATIC_ASSERT((h_width == 224) || (h_width == 256), "h_width must be 224 or 256");

    // 流配置
    hls::stream<SHA256Block> blk_strm("blk_strm");
    #pragma HLS STREAM variable=blk_strm depth=32

    hls::stream<uint64_t> nblk_strm("nblk_strm");
    #pragma HLS STREAM variable=nblk_strm depth=8

    hls::stream<bool> end_nblk_strm("end_nblk_strm");
    #pragma HLS STREAM variable=end_nblk_strm depth=8

    // 选择预处理函数
    if (m_width == 32) {
        preProcessing((hls::stream<ap_uint<32> >&)msg_strm, len_strm, 
                     end_len_strm, blk_strm, nblk_strm, end_nblk_strm);
    } else {
        preProcessing((hls::stream<ap_uint<64> >&)msg_strm, len_strm, 
                     end_len_strm, blk_strm, nblk_strm, end_nblk_strm);
    }

    fusedDigest<h_width>(blk_strm, nblk_strm, end_nblk_strm, hash_strm, end_hash_strm);
}

} // namespace internal

// 顶层接口
template <int m_width>
void sha224(hls::stream<ap_uint<m_width> >& msg_strm,
           hls::stream<ap_uint<64> >& len_strm,
           hls::stream<bool>& end_len_strm,
           hls::stream<ap_uint<224> >& hash_strm,
           hls::stream<bool>& end_hash_strm) {
    #pragma HLS INLINE off
    internal::sha256_top<m_width, 224>(msg_strm, len_strm, end_len_strm, hash_strm, end_hash_strm);
}

template <int m_width>
void sha256(hls::stream<ap_uint<m_width> >& msg_strm,
           hls::stream<ap_uint<64> >& len_strm,
           hls::stream<bool>& end_len_strm,
           hls::stream<ap_uint<256> >& hash_strm,
           hls::stream<bool>& end_hash_strm) {
    #pragma HLS INLINE off
    internal::sha256_top<m_width, 256>(msg_strm, len_strm, end_len_strm, hash_strm, end_hash_strm);
}

} // namespace security
} // namespace xf

#endif // _XF_SECURITY_SHA224_256_HPP_