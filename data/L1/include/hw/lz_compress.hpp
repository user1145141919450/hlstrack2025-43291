#ifndef _XFCOMPRESSION_LZ_COMPRESS_HPP_
#define _XFCOMPRESSION_LZ_COMPRESS_HPP_

#include "compress_utils.hpp" // for IntVectorStream_dt
#include "hls_stream.h"
#include <ap_int.h>
#include <stdint.h>

namespace xf {
    namespace compression {

        // ap_uint<8> → ap_uint<32>（LZ4路径）
        template <int MATCH_LEN,
            int MIN_MATCH,
            int LZ_MAX_OFFSET_LIMIT,
            int MATCH_LEVEL = 6,
            int MIN_OFFSET = 1,
            int LZ_DICT_SIZE = 1 << 12,
            int LEFT_BYTES = 64>
        void lzCompress(hls::stream<ap_uint<8> >& inStream,
            hls::stream<ap_uint<32> >& outStream,
            uint32_t input_size) {
#pragma HLS INLINE off
            const uint16_t c_idxW = 24;
            const int c_eleW = (MATCH_LEN * 8 + c_idxW);
            typedef ap_uint<MATCH_LEVEL* c_eleW> dictV_t;
            typedef ap_uint<c_eleW>               dict_t;

            if (input_size == 0) return;

#ifndef AVOID_STATIC_MODE
            static bool epoch = 0, needFlip = true;
            static uint32_t relBlocks = 0;
#else
            bool epoch = 0, needFlip = true; uint32_t relBlocks = 0;
#endif

            static dictV_t dict[LZ_DICT_SIZE];
            static ap_uint<MATCH_LEVEL> epv[LZ_DICT_SIZE];
#pragma HLS BIND_STORAGE variable = dict type = ram_2p impl = bram
#pragma HLS BIND_STORAGE variable = epv  type = ram_2p impl = bram

            if (needFlip) { epoch = !epoch; needFlip = false; relBlocks = 0; }
            else { relBlocks++; }

            uint8_t win[MATCH_LEN];
#pragma HLS ARRAY_PARTITION variable = win complete

            // preload
            for (uint8_t i = 1; i < MATCH_LEN; ++i) {
#pragma HLS PIPELINE II = 1
                win[i] = inStream.read();
            }

        main_loop:
            for (uint32_t i = MATCH_LEN - 1; i < input_size - LEFT_BYTES; ++i) {
#pragma HLS PIPELINE II = 1
#pragma HLS DEPENDENCE variable = dict inter false
#pragma HLS DEPENDENCE variable = epv  inter false
                uint32_t currIdx = (i - MATCH_LEN + 1) + relBlocks * (64 * 1024);

                for (int m = 0; m < MATCH_LEN - 1; ++m) {
#pragma HLS UNROLL
                    win[m] = win[m + 1];
                }
                win[MATCH_LEN - 1] = inStream.read();

                uint32_t h = 0;
                if (MIN_MATCH == 3)
                    h = (win[0] << 4) ^ (win[1] << 3) ^ (win[2] << 2) ^ (win[0] << 1) ^ (win[1]);
                else
                    h = (win[0] << 4) ^ (win[1] << 3) ^ (win[2] << 2) ^ (win[3]);

                dictV_t rd = dict[h];
                ap_uint<MATCH_LEVEL> ep = epv[h];

                dictV_t wr = (rd << c_eleW);
                for (int m = 0; m < MATCH_LEN; ++m) {
#pragma HLS UNROLL
                    wr.range((m + 1) * 8 - 1, m * 8) = win[m];
                }
                wr.range(c_eleW - 1, MATCH_LEN * 8) = (ap_uint<c_idxW>)currIdx;
                dict[h] = wr;

                ap_uint<MATCH_LEVEL> ep_w = (ap_uint<MATCH_LEVEL>)((ep << 1) | (epoch ? 1 : 0));
                epv[h] = ep_w;

                uint8_t  best_len = 0;
                uint32_t best_off = 0;

                for (int l = 0; l < MATCH_LEVEL; ++l) {
#pragma HLS UNROLL
                    bool valid_ep = (((ep >> l) & 0x1) == epoch);

                    dict_t cmp = rd.range((l + 1) * c_eleW - 1, l * c_eleW);
                    ap_uint<c_idxW> cmpIdx = cmp.range(c_eleW - 1, MATCH_LEN * 8);

                    uint8_t c0 = cmp.range(8 * 1 - 1, 8 * 0);
                    uint8_t c1 = cmp.range(8 * 2 - 1, 8 * 1);
                    uint8_t c2 = cmp.range(8 * 3 - 1, 8 * 2);
                    uint8_t c3 = cmp.range(8 * 4 - 1, 8 * 3);
                    uint8_t c4 = cmp.range(8 * 5 - 1, 8 * 4);
                    uint8_t c5 = cmp.range(8 * 6 - 1, 8 * 5);

                    bool e0 = (win[0] == c0);
                    bool e1 = (win[1] == c1);
                    bool e2 = (win[2] == c2);
                    bool e3 = (win[3] == c3);
                    bool e4 = (win[4] == c4);
                    bool e5 = (win[5] == c5);

                    uint8_t len = e0 ? (e1 ? (e2 ? (e3 ? (e4 ? (e5 ? 6 : 5) : 4) : 3) : 2) : 1) : 0;

                    bool ok = valid_ep &&
                        (len >= MIN_MATCH) &&
                        ((ap_uint<32>)currIdx > (ap_uint<32>)cmpIdx) &&
                        (((ap_uint<32>)currIdx - (ap_uint<32>)cmpIdx) < (ap_uint<32>)LZ_MAX_OFFSET_LIMIT) &&
                        ((((ap_uint<32>)currIdx - (ap_uint<32>)cmpIdx - 1) >= (ap_uint<32>)MIN_OFFSET)) &&
                        ((ap_uint<32>)cmpIdx >= (ap_uint<32>)(relBlocks * (64 * 1024)));

                    if (ok) {
                        if ((len == 3) && (((ap_uint<32>)currIdx - (ap_uint<32>)cmpIdx - 1) > 4096)) len = 0;
                    }
                    else {
                        len = 0;
                    }
                    if (len > best_len) {
                        best_len = len;
                        best_off = (ap_uint<32>)currIdx - (ap_uint<32>)cmpIdx - 1;
                    }
                }

                ap_uint<32> outv = 0;
                outv.range(7, 0) = win[0];
                outv.range(15, 8) = best_len;
                outv.range(31, 16) = (ap_uint<16>)best_off;
                outStream << outv;
            }

            // leftover
            for (int m = 1; m < MATCH_LEN; ++m) {
#pragma HLS PIPELINE II = 1
                ap_uint<32> v = 0; v.range(7, 0) = win[m]; outStream << v;
            }
            for (int l = 0; l < LEFT_BYTES; ++l) {
#pragma HLS PIPELINE II = 1
                ap_uint<32> v = 0; v.range(7, 0) = inStream.read(); outStream << v;
            }

            if ((relBlocks * (64 * 1024)) >= (1u << (c_idxW - 1))) needFlip = true;
        }

        // 向量流占位（LZ4路径不使用）
        template <int MAX_INPUT_SIZE = 64 * 1024, class SIZE_DT = uint32_t,
            int MATCH_LEN, int MIN_MATCH, int LZ_MAX_OFFSET_LIMIT, int CORE_ID = 0,
            int MATCH_LEVEL = 6, int MIN_OFFSET = 1, int LZ_DICT_SIZE = 1 << 12, int LEFT_BYTES = 64>
        void lzCompress(hls::stream<IntVectorStream_dt<8, 1> >& inStream,
            hls::stream<IntVectorStream_dt<32, 1> >& outStream) {
#pragma HLS INLINE off
            while (true) {
                auto v = inStream.read();
                IntVectorStream_dt<32, 1> o;
                if (v.strobe == 0) { o.strobe = 0; outStream << o; break; }
                o.strobe = 1; o.data[0] = v.data[0]; outStream << o;
            }
        }

    } // namespace compression
} // namespace xf
#endif // _XFCOMPRESSION_LZ_COMPRESS_HPP_