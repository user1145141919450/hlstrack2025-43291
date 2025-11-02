#ifndef _XFCOMPRESSION_LZ_OPTIONAL_HPP_
#define _XFCOMPRESSION_LZ_OPTIONAL_HPP_

#include "hls_stream.h"
#include <ap_int.h>
#include <stdint.h>

namespace xf {
    namespace compression {

        typedef ap_uint<32> compressd_dt;

        // 选优过滤：完全展开，II=1
        template <int MATCH_LEN, int OFFSET_WINDOW>
        void lzBestMatchFilter(hls::stream<compressd_dt>& inStream,
            hls::stream<compressd_dt>& outStream,
            uint32_t input_size) {
#pragma HLS INLINE off
            if (input_size == 0) return;

            compressd_dt win[MATCH_LEN];
#pragma HLS ARRAY_PARTITION variable = win complete

            for (uint32_t i = 0; i < (uint32_t)MATCH_LEN; ++i) {
#pragma HLS PIPELINE II = 1
                win[i] = inStream.read();
            }

        main_loop:
            for (uint32_t i = MATCH_LEN; i < input_size; ++i) {
#pragma HLS PIPELINE II = 1
                compressd_dt outv = win[0];
                for (int j = 0; j < MATCH_LEN - 1; ++j) {
#pragma HLS UNROLL
                    win[j] = win[j + 1];
                }
                win[MATCH_LEN - 1] = inStream.read();

                ap_uint<8> cur = outv.range(15, 8);
                bool better = false;
                for (int j = 0; j < MATCH_LEN; ++j) {
#pragma HLS UNROLL
                    ap_uint<8> cmp = win[j].range(15, 8);
                    better |= (cmp > (ap_uint<8>)(cur + (ap_uint<8>)j));
                }
                if (better) { outv.range(15, 8) = 0; outv.range(31, 16) = 0; }
                outStream << outv;
            }

        tail:
            for (uint32_t i = 0; i < (uint32_t)MATCH_LEN; ++i) {
#pragma HLS PIPELINE II = 1
                outStream << win[i];
            }
        }

        // Booster：BRAM-2P + II=1
        template <int MAX_MATCH_LEN, int BOOSTER_OFFSET_WINDOW = 16 * 1024, int LEFT_BYTES = 64>
        void lzBooster(hls::stream<compressd_dt>& inStream,
            hls::stream<compressd_dt>& outStream,
            uint32_t input_size) {
#pragma HLS INLINE off
            if (input_size == 0) return;

            ap_uint<8> his[BOOSTER_OFFSET_WINDOW];
#pragma HLS BIND_STORAGE variable = his type = ram_2p impl = bram

            uint32_t match_loc = 0, match_len = 0;
            compressd_dt outv = 0, outq = 0;
            bool matchFlag = false, outFlag = false, boostFlag = false;
            ap_uint<16> skip = 0;
            ap_uint<8> nextCh = his[match_loc % BOOSTER_OFFSET_WINDOW];

        main_loop:
            for (uint32_t i = 0; i < (input_size - LEFT_BYTES); ++i) {
#pragma HLS PIPELINE II = 1
#pragma HLS DEPENDENCE variable = his inter false
                compressd_dt inv = inStream.read();
                ap_uint<8>  ch = inv.range(7, 0);
                ap_uint<8>  len = inv.range(15, 8);
                ap_uint<16> off = inv.range(31, 16);

                boostFlag = (off < (ap_uint<16>)BOOSTER_OFFSET_WINDOW);
                ap_uint<8> mch = nextCh;
                his[i % BOOSTER_OFFSET_WINDOW] = ch;
                outFlag = false;

                if (skip) {
                    --skip;
                }
                else if (matchFlag && (match_len < (uint32_t)MAX_MATCH_LEN) && (ch == mch)) {
                    ++match_len; ++match_loc; outv.range(15, 8) = (ap_uint<8>)match_len;
                }
                else {
                    match_len = 1; match_loc = i - (uint32_t)off;
                    if (i) outFlag = true;
                    outq = outv; outv = inv;
                    if (len) {
                        if (boostFlag) { matchFlag = true;  skip = 0; }
                        else { matchFlag = false; skip = (ap_uint<16>)(len - 1); }
                    }
                    else {
                        matchFlag = false;
                    }
                }
                nextCh = his[match_loc % BOOSTER_OFFSET_WINDOW];
                if (outFlag) outStream << outq;
            }

            outStream << outv;

        tail:
            for (uint32_t i = 0; i < (uint32_t)LEFT_BYTES; ++i) {
#pragma HLS PIPELINE II = 1
                outStream << inStream.read();
            }
        }

    } // namespace compression
} // namespace xf
#endif // _XFCOMPRESSION_LZ_OPTIONAL_HPP_