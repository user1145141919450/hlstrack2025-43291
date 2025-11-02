#ifndef _XFCOMPRESSION_LZ4_COMPRESS_HPP_
#define _XFCOMPRESSION_LZ4_COMPRESS_HPP_

#include "hls_stream.h"
#include <ap_int.h>
#include <stdint.h>
#include "lz_compress.hpp"
#include "lz_optional.hpp"
#include "mm2s.hpp"
#include "s2mm.hpp"
#include "stream_downsizer.hpp"
#include "stream_upsizer.hpp"

const int c_gmemBurstSize = 32;

// 0: 不在 packer 中写块头（与官方最初版一致）；1: 写4字节“小端原始块大小”块头
#ifndef LZ4_KERNEL_WRITES_BLOCK_HEADER
#define LZ4_KERNEL_WRITES_BLOCK_HEADER 0
#endif

// 1: offset 写 distance-1 再 +1 落地（与官方最初版一致）；0: 直接写 distance-1（如需兼容别的对照）
#ifndef LZ4_OFFSET_PLUS1
#define LZ4_OFFSET_PLUS1 1
#endif

// 跟官方最初版一致：仅当 compressedSize < input_size 时才真正向 outStream 写字节
#ifndef LZ4_LIMIT_OUTPUT_TO_INPUTSIZE
#define LZ4_LIMIT_OUTPUT_TO_INPUTSIZE 1
#endif

namespace xf {
    namespace compression {
        namespace details {

            template <int MAX_LIT_COUNT, int PARALLEL_UNITS>
            static void lz4CompressPart1(hls::stream<ap_uint<32> >& inStream,
                hls::stream<uint8_t>& lit_outStream,
                hls::stream<ap_uint<64> >& lenOffset_Stream,
                uint32_t input_size,
                uint32_t max_lit_limit[PARALLEL_UNITS],
                uint32_t index) {
#pragma HLS INLINE off
                if (input_size == 0) return;

                uint32_t lit_count = 0;
                uint32_t lit_count_flag = 0;

                ap_uint<32> nextEncodedValue = inStream.read();
            lz4_divide:
                for (uint32_t i = 0; i < input_size;) {
#pragma HLS PIPELINE II = 1
                    ap_uint<32> tmpEncodedValue = nextEncodedValue;
                    if (i < (input_size - 1)) nextEncodedValue = inStream.read();

                    uint8_t  tCh = tmpEncodedValue.range(7, 0);
                    uint8_t  tLen = tmpEncodedValue.range(15, 8);
                    uint16_t tOffset = tmpEncodedValue.range(31, 16);

                    if (lit_count >= MAX_LIT_COUNT) {
                        lit_count_flag = 1;
                    }
                    else if (tLen) {
                        // meta: [63:32]=lit_count, [31:16]=match_offset(distance-1), [15:0]=(tLen-4)
                        ap_uint<64> tmpValue;
                        tmpValue.range(63, 32) = lit_count;
                        tmpValue.range(31, 16) = tOffset;
                        tmpValue.range(15, 0) = (ap_uint<16>)(tLen - 4);
                        lenOffset_Stream << tmpValue;
                        lit_count = 0;
                    }
                    else {
                        lit_outStream << tCh;
                        ++lit_count;
                    }
                    i += (tLen ? (uint32_t)tLen : 1);
                }
                if (lit_count) {
                    ap_uint<64> tmpValue;
                    tmpValue.range(63, 32) = lit_count;
                    if (lit_count == MAX_LIT_COUNT) {
                        lit_count_flag = 1;
                        tmpValue.range(15, 0) = 777;
                        tmpValue.range(31, 16) = 777;
                    }
                    else {
                        tmpValue.range(15, 0) = 0;
                        tmpValue.range(31, 16) = 0;
                    }
                    lenOffset_Stream << tmpValue;
                }
                max_lit_limit[index] = lit_count_flag;
            }

            static void lz4CompressPart2(hls::stream<uint8_t>& in_lit_inStream,
                hls::stream<ap_uint<64> >& in_lenOffset_Stream,
                hls::stream<ap_uint<8> >& outStream,
                hls::stream<bool>& endOfStream,
                hls::stream<uint32_t>& compressdSizeStream,
                uint32_t input_size) {
#pragma HLS INLINE off
                // 与官方最初版一致的状态机顺序
                enum lz4CompressStates { WRITE_TOKEN, WRITE_LIT_LEN, WRITE_MATCH_LEN, WRITE_LITERAL, WRITE_OFFSET0, WRITE_OFFSET1 };

                uint32_t compressedSize = 0;
                lz4CompressStates next_state = WRITE_TOKEN;

                uint32_t lit_len = 0;
                uint16_t lit_length = 0;
                uint16_t match_length = 0;      // 存储的是 (tLen-4)，进入扩展时再扣 15
                uint16_t write_lit_length = 0;
                ap_uint<16> match_offset = 0;   // 存储的是 distance-1
                ap_uint<16> match_offset_plus_one = 0;

                bool lit_ending = false;
                bool extra_match_len = false;
                bool readOffsetFlag = true;

                ap_uint<64> nextLenOffsetValue = 0;

#if LZ4_KERNEL_WRITES_BLOCK_HEADER
                // 可选：在 packer 里写“小端原始块大小”块头（注意避免与上层重复写）
                {
#pragma HLS PIPELINE II = 1
                    ap_uint<32> rawSz = input_size;
                    for (int b = 0; b < 4; ++b) {
#pragma HLS UNROLL
                        ap_uint<8> hb = (ap_uint<8>)((rawSz >> (8 * b)) & 0xFF);
#if LZ4_LIMIT_OUTPUT_TO_INPUTSIZE
                        if (compressedSize < input_size) {
                            outStream << hb;
                            endOfStream << false;
                            ++compressedSize;
                        }
#else
                        outStream << hb;
                        endOfStream << false;
                        ++compressedSize;
#endif
                    }
                }
#endif

            lz4_compress:
                for (uint32_t inIdx = 0; (inIdx < input_size) || (!readOffsetFlag);) {
#pragma HLS PIPELINE II = 1
#pragma HLS DEPENDENCE variable=match_offset inter false
#pragma HLS DEPENDENCE variable=lit_length inter false
#pragma HLS DEPENDENCE variable=match_length inter false

                    ap_uint<8> outValue = 0;

                    if (readOffsetFlag) {
                        nextLenOffsetValue = in_lenOffset_Stream.read();
                        readOffsetFlag = false;
                    }

                    ap_uint<32> lit_len_tmp = nextLenOffsetValue.range(63, 32);
                    ap_uint<16> match_off_tmp = nextLenOffsetValue.range(31, 16);
                    ap_uint<16> match_len_tmp = nextLenOffsetValue.range(15, 0);

                    if (next_state == WRITE_TOKEN) {
                        lit_length = (uint16_t)lit_len_tmp;
                        match_length = (uint16_t)match_len_tmp; // (tLen-4)
                        match_offset = (ap_uint<16>)match_off_tmp;

                        // 推进原文计数：lit + (matchLen+4)
                        uint32_t idx_increment = (uint32_t)match_length + (uint32_t)lit_length + 4;
                        inIdx += idx_increment;

                        bool is_special_end = (match_length == 777) && (match_offset == 777);
                        bool is_normal_end = (match_offset == 0) && (match_length == 0);
                        if (is_special_end) {
                            inIdx = input_size;
                            lit_ending = true;
                        }

                        lit_len = lit_length;
                        write_lit_length = lit_length;
                        lit_ending = lit_ending || is_normal_end;

                        // token 上半字节：literal
                        bool lit_len_ge_15 = (lit_length >= 15);
                        bool lit_len_gt_0 = (lit_length > 0);
                        outValue.range(7, 4) = lit_len_ge_15 ? (ap_uint<4>)15
                            : (lit_len_gt_0 ? (ap_uint<4>)lit_length : (ap_uint<4>)0);
                        if (lit_len_ge_15) {
                            lit_length -= 15;
                            next_state = WRITE_LIT_LEN;
                            readOffsetFlag = false;
                        }
                        else if (lit_len_gt_0) {
                            lit_length = 0;
                            next_state = WRITE_LITERAL;
                            readOffsetFlag = false;
                        }
                        else {
                            next_state = WRITE_OFFSET0;
                            readOffsetFlag = false;
                        }

                        // token 下半字节：match
                        bool match_len_ge_15 = (match_length >= 15);
                        outValue.range(3, 0) = match_len_ge_15 ? (ap_uint<4>)15 : (ap_uint<4>)match_length;
                        if (match_len_ge_15) {
                            match_length -= 15;
                            extra_match_len = true;
                        }
                        else {
                            match_length = 0;
                            extra_match_len = false;
                        }

#if LZ4_OFFSET_PLUS1
                        match_offset_plus_one = (ap_uint<16>)(match_offset + 1);
#else
                        match_offset_plus_one = match_offset;
#endif

                    }
                    else if (next_state == WRITE_LIT_LEN) {
                        // 余数为 0 也要写一个 0x00 扩展字节（与官方/样例一致）
                        bool lit_len_ge_255 = (lit_length >= 255);
                        outValue = lit_len_ge_255 ? (ap_uint<8>)255 : (ap_uint<8>)lit_length;
                        if (lit_len_ge_255) {
                            lit_length -= 255;
                        }
                        else {
                            next_state = WRITE_LITERAL;
                            readOffsetFlag = false;
                        }

                    }
                    else if (next_state == WRITE_LITERAL) {
                        outValue = in_lit_inStream.read();
                        --write_lit_length;
                        if (write_lit_length == 0) {
                            next_state = lit_ending ? WRITE_TOKEN : WRITE_OFFSET0;
                            readOffsetFlag = lit_ending;
                        }

                    }
                    else if (next_state == WRITE_OFFSET0) {
                        outValue = (ap_uint<8>)(match_offset_plus_one & 0xFF);
                        next_state = WRITE_OFFSET1;
                        readOffsetFlag = false;

                    }
                    else if (next_state == WRITE_OFFSET1) {
                        outValue = (ap_uint<8>)((match_offset_plus_one >> 8) & 0xFF);
                        next_state = extra_match_len ? WRITE_MATCH_LEN : WRITE_TOKEN;
                        readOffsetFlag = !extra_match_len;

                    }
                    else { // WRITE_MATCH_LEN
                        // 余数为 0 也写一个 0x00 扩展字节（与官方/样例一致）
                        bool match_len_ge_255 = (match_length >= 255);
                        outValue = match_len_ge_255 ? (ap_uint<8>)255 : (ap_uint<8>)match_length;
                        if (match_len_ge_255) {
                            match_length -= 255;
                        }
                        else {
                            next_state = WRITE_TOKEN;
                            readOffsetFlag = true;
                        }
                    }

                    // 与官方最初版一致：按 input_size 限制输出（一般不会触发，但要保持行为一致）
#if LZ4_LIMIT_OUTPUT_TO_INPUTSIZE
                    if (compressedSize < input_size) {
                        outStream << outValue;
                        endOfStream << false;
                        ++compressedSize;
                    }
#else
                    outStream << outValue;
                    endOfStream << false;
                    ++compressedSize;
#endif
                }

                compressdSizeStream << compressedSize;
                outStream << (ap_uint<8>)0;
                endOfStream << true;
            }

        } // namespace details
    } // namespace compression
} // namespace xf

namespace xf {
    namespace compression {

        template <int MAX_LIT_COUNT, int PARALLEL_UNITS>
        static void lz4Compress(hls::stream<ap_uint<32> >& inStream,
            hls::stream<ap_uint<8> >& outStream,
            uint32_t max_lit_limit[PARALLEL_UNITS],
            uint32_t input_size,
            hls::stream<bool>& endOfStream,
            hls::stream<uint32_t>& compressdSizeStream,
            uint32_t index) {
#pragma HLS INLINE off
            hls::stream<uint8_t>      lit_outStream("lit_outStream");
            hls::stream<ap_uint<64> > lenOffset_Stream("lenOffset_Stream");
#pragma HLS STREAM       variable = lit_outStream    depth = MAX_LIT_COUNT
#pragma HLS STREAM       variable = lenOffset_Stream depth = c_gmemBurstSize
#pragma HLS BIND_STORAGE variable = lenOffset_Stream type = FIFO impl = SRL

#pragma HLS DATAFLOW
            xf::compression::details::lz4CompressPart1<MAX_LIT_COUNT, PARALLEL_UNITS>(
                inStream, lit_outStream, lenOffset_Stream, input_size, max_lit_limit, index);
            xf::compression::details::lz4CompressPart2(
                lit_outStream, lenOffset_Stream, outStream, endOfStream, compressdSizeStream, input_size);
        }

        template <class data_t, int DATAWIDTH = 512, int BURST_SIZE = 16, int NUM_BLOCK = 8,
            int M_LEN = 6, int MIN_MAT = 4, int LZ_MAX_OFFSET_LIM = 65536, int OFFSET_WIN = 65536,
            int MAX_M_LEN = 255, int MAX_LIT_CNT = 4096, int MIN_B_SIZE = 128>
        void hlsLz4Core(hls::stream<data_t>& inStream,
            hls::stream<data_t>& outStream,
            hls::stream<bool>& outStreamEos,
            hls::stream<uint32_t>& compressedSize,
            uint32_t max_lit_limit[NUM_BLOCK],
            uint32_t input_size,
            uint32_t core_idx) {
#pragma HLS INLINE off
            hls::stream<ap_uint<32> > compressdStream("compressdStream");
            hls::stream<ap_uint<32> > bestMatchStream("bestMatchStream");
            hls::stream<ap_uint<32> > boosterStream("boosterStream");
#pragma HLS STREAM variable = compressdStream depth = 32
#pragma HLS STREAM variable = bestMatchStream depth = 32
#pragma HLS STREAM variable = boosterStream  depth = 64
#pragma HLS BIND_STORAGE variable = compressdStream type = FIFO impl = SRL
#pragma HLS BIND_STORAGE variable = bestMatchStream type = FIFO impl = SRL
#pragma HLS BIND_STORAGE variable = boosterStream  type = FIFO impl = SRL

#pragma HLS DATAFLOW
            xf::compression::lzCompress<M_LEN, MIN_MAT, LZ_MAX_OFFSET_LIM>(inStream, compressdStream, input_size);
            xf::compression::lzBestMatchFilter<M_LEN, OFFSET_WIN>(compressdStream, bestMatchStream, input_size);
            xf::compression::lzBooster<MAX_M_LEN>(bestMatchStream, boosterStream, input_size);
            xf::compression::lz4Compress<MAX_LIT_CNT, NUM_BLOCK>(
                boosterStream, outStream, max_lit_limit, input_size, outStreamEos, compressedSize, core_idx);
        }

        template <class data_t, int DATAWIDTH = 512, int BURST_SIZE = 16, int NUM_BLOCK = 8,
            int M_LEN = 6, int MIN_MAT = 4, int LZ_MAX_OFFSET_LIM = 65536, int OFFSET_WIN = 65536,
            int MAX_M_LEN = 255, int MAX_LIT_CNT = 4096, int MIN_B_SIZE = 128>
        void hlsLz4(const data_t* in, data_t* out,
            const uint32_t input_idx[NUM_BLOCK],
            const uint32_t output_idx[NUM_BLOCK],
            const uint32_t input_size[NUM_BLOCK],
            uint32_t output_size[NUM_BLOCK],
            uint32_t max_lit_limit[NUM_BLOCK]) {
#pragma HLS INLINE off
            hls::stream<ap_uint<8> > inStream[NUM_BLOCK];
            hls::stream<bool>        outStreamEos[NUM_BLOCK];
            hls::stream<ap_uint<8> > outStream[NUM_BLOCK];
#pragma HLS STREAM variable = outStreamEos depth = 2
#pragma HLS STREAM variable = inStream     depth = c_gmemBurstSize
#pragma HLS STREAM variable = outStream    depth = c_gmemBurstSize
#pragma HLS BIND_STORAGE variable = outStreamEos type = FIFO impl = SRL
#pragma HLS BIND_STORAGE variable = inStream     type = FIFO impl = SRL
#pragma HLS BIND_STORAGE variable = outStream    type = FIFO impl = SRL

            hls::stream<uint32_t> compressedSize[NUM_BLOCK];

#pragma HLS DATAFLOW
            xf::compression::details::mm2multStreamSize<8, NUM_BLOCK, DATAWIDTH, BURST_SIZE>(
                in, input_idx, inStream, input_size);

            for (uint8_t i = 0; i < NUM_BLOCK; i++) {
#pragma HLS UNROLL
                hlsLz4Core<ap_uint<8>, DATAWIDTH, BURST_SIZE, NUM_BLOCK>(
                    inStream[i], outStream[i], outStreamEos[i], compressedSize[i],
                    max_lit_limit, input_size[i], i);
            }

            xf::compression::details::multStream2MM<8, NUM_BLOCK, DATAWIDTH, BURST_SIZE>(
                outStream, outStreamEos, compressedSize, output_idx, out, output_size);
        }

        template <class data_t, int DATAWIDTH = 512, int BURST_SIZE = 16, int NUM_BLOCK = 8,
            int M_LEN = 6, int MIN_MAT = 4, int LZ_MAX_OFFSET_LIM = 65536, int OFFSET_WIN = 65536,
            int MAX_M_LEN = 255, int MAX_LIT_CNT = 4096, int MIN_B_SIZE = 128>
        void lz4CompressMM(const data_t* in, data_t* out, uint32_t* compressd_size, const uint32_t input_size) {
#pragma HLS INLINE off
            uint32_t block_idx = 0;
            uint32_t block_length = 64 * 1024;
            uint32_t no_blocks = (input_size - 1) / block_length + 1;
            uint32_t max_block_size = 64 * 1024;
            uint32_t readBlockSize = 0;

            bool     small_block[NUM_BLOCK];
            uint32_t input_block_size[NUM_BLOCK], input_idx[NUM_BLOCK], output_idx[NUM_BLOCK];
            uint32_t output_block_size[NUM_BLOCK], max_lit_limit[NUM_BLOCK], small_block_inSize[NUM_BLOCK];
#pragma HLS ARRAY_PARTITION variable = input_block_size  dim = 0 complete
#pragma HLS ARRAY_PARTITION variable = input_idx         dim = 0 complete
#pragma HLS ARRAY_PARTITION variable = output_idx        dim = 0 complete
#pragma HLS ARRAY_PARTITION variable = output_block_size dim = 0 complete
#pragma HLS ARRAY_PARTITION variable = max_lit_limit     dim = 0 complete

            for (uint32_t i = 0; i < no_blocks; i += NUM_BLOCK) {
                uint32_t nblocks = NUM_BLOCK;
                if ((i + NUM_BLOCK) > no_blocks) nblocks = no_blocks - i;

                for (uint32_t j = 0; j < NUM_BLOCK; j++) {
#pragma HLS PIPELINE II = 1
                    if (j < nblocks) {
                        uint32_t inBlockSize = block_length;
                        if (readBlockSize + block_length > input_size) inBlockSize = input_size - readBlockSize;
                        if (inBlockSize < MIN_B_SIZE) {
                            small_block[j] = 1;
                            small_block_inSize[j] = inBlockSize;
                            input_block_size[j] = 0;
                            input_idx[j] = 0;
                        }
                        else {
                            small_block[j] = 0;
                            input_block_size[j] = inBlockSize;
                            readBlockSize += inBlockSize;
                            input_idx[j] = (i + j) * max_block_size;
                            output_idx[j] = (i + j) * max_block_size;
                        }
                    }
                    else {
                        input_block_size[j] = 0;
                        input_idx[j] = 0;
                    }
                    output_block_size[j] = 0;
                    max_lit_limit[j] = 0;
                }

                hlsLz4<data_t, DATAWIDTH, BURST_SIZE, NUM_BLOCK>(in, out, input_idx, output_idx,
                    input_block_size, output_block_size, max_lit_limit);

                for (uint32_t k = 0; k < nblocks; k++) {
#pragma HLS PIPELINE II = 1
                    if (max_lit_limit[k]) compressd_size[block_idx] = input_block_size[k];
                    else                  compressd_size[block_idx] = output_block_size[k];
                    if (small_block[k] == 1) compressd_size[block_idx] = small_block_inSize[k];
                    block_idx++;
                }
            }
        }

    } // namespace compression
} // namespace xf
#endif // _XFCOMPRESSION_LZ4_COMPRESS_HPP_