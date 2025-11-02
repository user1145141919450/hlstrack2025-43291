#ifndef _XF_SOLVER_CHOLESKY_HPP_
#define _XF_SOLVER_CHOLESKY_HPP_

#include "ap_fixed.h"
#include "hls_x_complex.h"
#include <complex>
#include "utils/std_complex_utils.h"
#include "utils/x_matrix_utils.hpp"
#include "hls_stream.h"

// 强制架构选择：-1 按 traits；0/1/2 强制选择。默认强制 Alt2=2，确保跑到新架构
#ifndef XF_SOLVER_FORCE_ARCH
#define XF_SOLVER_FORCE_ARCH 2
#endif

// 行方向并行路数（IU）。默认 2；可用 -DXF_CHOLESKY_IU=3/4 测试更高并行（小矩阵建议）
#ifndef XF_CHOLESKY_IU
#define XF_CHOLESKY_IU 2
#endif

namespace xf {
namespace solver {

// =======================================================================
// Traits（加入 IU 并行）
template <bool LowerTriangularL, int RowsColsA, typename InputType, typename OutputType>
struct choleskyTraits {
    typedef InputType PROD_T;
    typedef InputType ACCUM_T;
    typedef InputType ADD_T;
    typedef InputType DIAG_T;
    typedef InputType RECIP_DIAG_T;
    typedef InputType OFF_DIAG_T;
    typedef OutputType L_OUTPUT_T;
    static const int ARCH = 2;
    static const int INNER_II = 1;
    static const int IU = XF_CHOLESKY_IU;
    static const int UNROLL_FACTOR = IU;
    static const int UNROLL_DIM = (LowerTriangularL ? 1 : 2);
    static const int ARCH2_ZERO_LOOP = true;
};

// hls::x_complex
template <bool LowerTriangularL, int RowsColsA, typename IB, typename OB>
struct choleskyTraits<LowerTriangularL, RowsColsA, hls::x_complex<IB>, hls::x_complex<OB> > {
    typedef hls::x_complex<IB> PROD_T;
    typedef hls::x_complex<IB> ACCUM_T;
    typedef hls::x_complex<IB> ADD_T;
    typedef hls::x_complex<IB> DIAG_T;
    typedef IB RECIP_DIAG_T;
    typedef hls::x_complex<IB> OFF_DIAG_T;
    typedef hls::x_complex<OB> L_OUTPUT_T;
    static const int ARCH = 2;
    static const int INNER_II = 1;
    static const int IU = XF_CHOLESKY_IU;
    static const int UNROLL_FACTOR = IU;
    static const int UNROLL_DIM = (LowerTriangularL ? 1 : 2);
    static const int ARCH2_ZERO_LOOP = true;
};

// std::complex
template <bool LowerTriangularL, int RowsColsA, typename IB, typename OB>
struct choleskyTraits<LowerTriangularL, RowsColsA, std::complex<IB>, std::complex<OB> > {
    typedef std::complex<IB> PROD_T;
    typedef std::complex<IB> ACCUM_T;
    typedef std::complex<IB> ADD_T;
    typedef std::complex<IB> DIAG_T;
    typedef IB RECIP_DIAG_T;
    typedef std::complex<IB> OFF_DIAG_T;
    typedef std::complex<OB> L_OUTPUT_T;
    static const int ARCH = 2;
    static const int INNER_II = 1;
    static const int IU = XF_CHOLESKY_IU;
    static const int UNROLL_FACTOR = IU;
    static const int UNROLL_DIM = (LowerTriangularL ? 1 : 2);
    static const int ARCH2_ZERO_LOOP = true;
};

// ap_fixed（保守）
template <bool LowerTriangularL,
          int RowsColsA,
          int W1,
          int I1,
          ap_q_mode Q1,
          ap_o_mode O1,
          int N1,
          int W2,
          int I2,
          ap_q_mode Q2,
          ap_o_mode O2,
          int N2>
struct choleskyTraits<LowerTriangularL, RowsColsA, ap_fixed<W1, I1, Q1, O1, N1>, ap_fixed<W2, I2, Q2, O2, N2> > {
    typedef ap_fixed<W1 + W1, I1 + I1, AP_RND_CONV, AP_SAT, 0> PROD_T;
    typedef ap_fixed<(W1 + W1) + BitWidth<RowsColsA>::Value,
                     (I1 + I1) + BitWidth<RowsColsA>::Value,
                     AP_RND_CONV,
                     AP_SAT,
                     0>
        ACCUM_T;
    typedef ap_fixed<W1 + 1, I1 + 1, AP_RND_CONV, AP_SAT, 0> ADD_T;
    typedef ap_fixed<(W1 + 1) * 2, I1 + 1, AP_RND_CONV, AP_SAT, 0> DIAG_T;
    typedef ap_fixed<(W1 + 1) * 2, I1 + 1, AP_RND_CONV, AP_SAT, 0> OFF_DIAG_T;
    typedef ap_fixed<2 + (W2 - I2) + W2, 2 + (W2 - I2), AP_RND_CONV, AP_SAT, 0> RECIP_DIAG_T;
    typedef ap_fixed<W2, I2, AP_RND_CONV, AP_SAT, 0> L_OUTPUT_T;
    static const int ARCH = 1; // 定点保守（如需强制Alt2，宏 XF_SOLVER_FORCE_ARCH=2）
    static const int INNER_II = 1;
    static const int IU = 1;
    static const int UNROLL_FACTOR = IU;
    static const int UNROLL_DIM = (LowerTriangularL ? 1 : 2);
    static const int ARCH2_ZERO_LOOP = true;
};

// hls::x_complex<ap_fixed>（保守）
template <bool LowerTriangularL,
          int RowsColsA,
          int W1,
          int I1,
          ap_q_mode Q1,
          ap_o_mode O1,
          int N1,
          int W2,
          int I2,
          ap_q_mode Q2,
          ap_o_mode O2,
          int N2>
struct choleskyTraits<LowerTriangularL,
                      RowsColsA,
                      hls::x_complex<ap_fixed<W1, I1, Q1, O1, N1> >,
                      hls::x_complex<ap_fixed<W2, I2, Q2, O2, N2> > > {
    typedef hls::x_complex<ap_fixed<W1 + W1, I1 + I1, AP_RND_CONV, AP_SAT, 0> > PROD_T;
    typedef hls::x_complex<ap_fixed<(W1 + W1) + BitWidth<RowsColsA>::Value,
                                    (I1 + I1) + BitWidth<RowsColsA>::Value,
                                    AP_RND_CONV,
                                    AP_SAT,
                                    0> >
        ACCUM_T;
    typedef hls::x_complex<ap_fixed<W1 + 1, I1 + 1, AP_RND_CONV, AP_SAT, 0> > ADD_T;
    typedef hls::x_complex<ap_fixed<(W1 + 1) * 2, I1 + 1, AP_RND_CONV, AP_SAT, 0> > DIAG_T;
    typedef hls::x_complex<ap_fixed<(W1 + 1) * 2, I1 + 1, AP_RND_CONV, AP_SAT, 0> > OFF_DIAG_T;
    typedef ap_fixed<2 + (W2 - I2) + W2, 2 + (W2 - I2), AP_RND_CONV, AP_SAT, 0> RECIP_DIAG_T;
    typedef hls::x_complex<ap_fixed<W2, I2, AP_RND_CONV, AP_SAT, 0> > L_OUTPUT_T;
    static const int ARCH = 1;
    static const int INNER_II = 1;
    static const int IU = 1;
    static const int UNROLL_FACTOR = IU;
    static const int UNROLL_DIM = (LowerTriangularL ? 1 : 2);
    static const int ARCH2_ZERO_LOOP = true;
};

// std::complex<ap_fixed>（保守）
template <bool LowerTriangularL,
          int RowsColsA,
          int W1,
          int I1,
          ap_q_mode Q1,
          ap_o_mode O1,
          int N1,
          int W2,
          int I2,
          ap_q_mode Q2,
          ap_o_mode O2,
          int N2>
struct choleskyTraits<LowerTriangularL,
                      RowsColsA,
                      std::complex<ap_fixed<W1, I1, Q1, O1, N1> >,
                      std::complex<ap_fixed<W2, I2, Q2, O2, N2> > > {
    typedef std::complex<ap_fixed<W1 + W1, I1 + I1, AP_RND_CONV, AP_SAT, 0> > PROD_T;
    typedef std::complex<ap_fixed<(W1 + W1) + BitWidth<RowsColsA>::Value,
                                  (I1 + I1) + BitWidth<RowsColsA>::Value,
                                  AP_RND_CONV,
                                  AP_SAT,
                                  0> >
        ACCUM_T;
    typedef std::complex<ap_fixed<W1 + 1, I1 + 1, AP_RND_CONV, AP_SAT, 0> > ADD_T;
    typedef std::complex<ap_fixed<(W1 + 1) * 2, I1 + 1, AP_RND_CONV, AP_SAT, 0> > DIAG_T;
    typedef std::complex<ap_fixed<(W1 + 1) * 2, I1 + 1, AP_RND_CONV, AP_SAT, 0> > OFF_DIAG_T;
    typedef ap_fixed<2 + (W2 - I2) + W2, 2 + (W2 - I2), AP_RND_CONV, AP_SAT, 0> RECIP_DIAG_T;
    typedef std::complex<ap_fixed<W2, I2, AP_RND_CONV, AP_SAT, 0> > L_OUTPUT_T;
    static const int ARCH = 1;
    static const int INNER_II = 1;
    static const int IU = 1;
    static const int UNROLL_FACTOR = IU;
    static const int UNROLL_DIM = (LowerTriangularL ? 1 : 2);
    static const int ARCH2_ZERO_LOOP = true;
};

// =======================================================================
// Helpers
template <typename T_IN, typename T_OUT>
int cholesky_sqrt_op(T_IN a, T_OUT& b) {
    const T_IN ZERO = 0;
    if (a < ZERO) {
        b = ZERO;
        return 1;
    }
    b = x_sqrt(a);
    return 0;
}
template <typename T_IN, typename T_OUT>
int cholesky_sqrt_op(hls::x_complex<T_IN> din, hls::x_complex<T_OUT>& dout) {
    const T_IN ZERO = 0;
    T_IN a = din.real();
    dout.imag(ZERO);
    if (a < ZERO) {
        dout.real(ZERO);
        return 1;
    }
    dout.real(x_sqrt(a));
    return 0;
}
template <typename T_IN, typename T_OUT>
int cholesky_sqrt_op(std::complex<T_IN> din, std::complex<T_OUT>& dout) {
    const T_IN ZERO = 0;
    T_IN a = din.real();
    dout.imag(ZERO);
    if (a < ZERO) {
        dout.real(ZERO);
        return 1;
    }
    dout.real(x_sqrt(a));
    return 0;
}

template <typename InputType, typename OutputType>
void cholesky_rsqrt(InputType x, OutputType& res) {
    res = x_rsqrt(x);
}
template <int W1, int I1, ap_q_mode Q1, ap_o_mode O1, int N1, int W2, int I2, ap_q_mode Q2, ap_o_mode O2, int N2>
void cholesky_rsqrt(ap_fixed<W1, I1, Q1, O1, N1> x, ap_fixed<W2, I2, Q2, O2, N2>& res) {
    ap_fixed<W2, I2, Q2, O2, N2> one = 1;
    ap_fixed<W1, I1, Q1, O1, N1> s = x_sqrt(x);
    ap_fixed<W2, I2, Q2, O2, N2> sc = s;
    res = one / sc;
}

template <typename AType, typename BType, typename CType>
void cholesky_prod_sum_mult(AType A, BType B, CType& C) {
    C = A * B;
}
template <typename AType, typename BType, typename CType>
void cholesky_prod_sum_mult(hls::x_complex<AType> A, BType B, hls::x_complex<CType>& C) {
    C.real(A.real() * B);
    C.imag(A.imag() * B);
}
template <typename AType, typename BType, typename CType>
void cholesky_prod_sum_mult(std::complex<AType> A, BType B, std::complex<CType>& C) {
    C.real(A.real() * B);
    C.imag(A.imag() * B);
}

// 将实数写入输出类型（可能是复数）
template <typename TOut, typename TReal>
inline void chol_assign_real(TOut& out, const TReal& r) {
    out = (TOut)r;
}
template <typename T, typename TReal>
inline void chol_assign_real(hls::x_complex<T>& out, const TReal& r) {
    out.real((T)r);
    out.imag((T)0);
}
template <typename T, typename TReal>
inline void chol_assign_real(std::complex<T>& out, const TReal& r) {
    out.real((T)r);
    out.imag((T)0);
}

// =======================================================================
// Basic（保持原有实现）
template <bool LowerTriangularL, int RowsColsA, typename CholeskyTraits, class InputType, class OutputType>
int choleskyBasic(const InputType A[RowsColsA][RowsColsA], OutputType L[RowsColsA][RowsColsA]) {
    int return_code = 0;
    typename CholeskyTraits::PROD_T prod;
    typename CholeskyTraits::ACCUM_T sum[RowsColsA];
    typename CholeskyTraits::ACCUM_T A_cast_to_sum;
    typename CholeskyTraits::ACCUM_T prod_cast_to_sum;
    typename CholeskyTraits::ADD_T A_minus_sum;
    typename CholeskyTraits::DIAG_T new_L_diag;
    typename CholeskyTraits::OFF_DIAG_T new_L_off_diag;
    typename CholeskyTraits::OFF_DIAG_T L_cast_to_new_L_off_diag;
    typename CholeskyTraits::L_OUTPUT_T new_L;
    OutputType retrieved_L;
    OutputType L_internal[RowsColsA][RowsColsA];

col_loop:
    for (int j = 0; j < RowsColsA; j++) {
        sum[j] = 0;
    diag_loop:
        for (int k = 0; k < RowsColsA; k++) {
            if (k <= (j - 1)) {
                if (LowerTriangularL)
                    retrieved_L = L_internal[j][k];
                else
                    retrieved_L = L_internal[k][j];
                sum[j] = hls::x_conj(retrieved_L) * retrieved_L;
            }
        }
        A_cast_to_sum = A[j][j];
        A_minus_sum = A_cast_to_sum - sum[j];
        if (cholesky_sqrt_op(A_minus_sum, new_L_diag)) {
            return_code = 1;
        }
        new_L = new_L_diag;
        if (LowerTriangularL) {
            L_internal[j][j] = new_L;
            L[j][j] = new_L;
        } else {
            L_internal[j][j] = hls::x_conj(new_L);
            L[j][j] = hls::x_conj(new_L);
        }
    off_diag_loop:
        for (int i = 0; i < RowsColsA; i++) {
            if (i > j) {
                if (LowerTriangularL)
                    sum[j] = A[i][j];
                else
                    sum[j] = hls::x_conj(A[j][i]);
            sum_loop:
                for (int k = 0; k < RowsColsA; k++) {
#pragma HLS PIPELINE II = CholeskyTraits::INNER_II
                    if (k <= (j - 1)) {
                        if (LowerTriangularL)
                            prod = -L_internal[i][k] * hls::x_conj(L_internal[j][k]);
                        else
                            prod = -hls::x_conj(L_internal[k][i]) * (L_internal[k][j]);
                        prod_cast_to_sum = prod;
                        sum[j] += prod_cast_to_sum;
                    }
                }
                new_L_off_diag = sum[j];
                L_cast_to_new_L_off_diag = L_internal[j][j];
                new_L_off_diag = new_L_off_diag / hls::x_real(L_cast_to_new_L_off_diag);
                new_L = new_L_off_diag;
                if (LowerTriangularL) {
                    L[i][j] = new_L;
                    L_internal[i][j] = new_L;
                } else {
                    L[j][i] = hls::x_conj(new_L);
                    L_internal[j][i] = hls::x_conj(new_L);
                }
            } else if (i < j) {
                if (LowerTriangularL)
                    L[i][j] = 0;
                else
                    L[j][i] = 0;
            }
        }
    }
    return return_code;
}

// =======================================================================
// Alt（保持原有实现）
template <bool LowerTriangularL, int RowsColsA, typename CholeskyTraits, class InputType, class OutputType>
int choleskyAlt(const InputType A[RowsColsA][RowsColsA], OutputType L[RowsColsA][RowsColsA]) {
    int return_code = 0;
    OutputType L_internal[(RowsColsA * RowsColsA - RowsColsA) / 2];
    typename CholeskyTraits::RECIP_DIAG_T diag_internal[RowsColsA];

    typename CholeskyTraits::ACCUM_T square_sum;
    typename CholeskyTraits::ACCUM_T A_cast_to_sum;
    typename CholeskyTraits::ADD_T A_minus_sum;
    typename CholeskyTraits::DIAG_T A_minus_sum_cast_diag;
    typename CholeskyTraits::DIAG_T new_L_diag;
    typename CholeskyTraits::RECIP_DIAG_T new_L_diag_recip;
    typename CholeskyTraits::PROD_T prod;
    typename CholeskyTraits::ACCUM_T prod_cast_to_sum;
    typename CholeskyTraits::ACCUM_T product_sum;
    typename CholeskyTraits::OFF_DIAG_T prod_cast_to_off_diag;
    typename CholeskyTraits::RECIP_DIAG_T L_diag_recip;
    typename CholeskyTraits::OFF_DIAG_T new_L_off_diag;
    typename CholeskyTraits::L_OUTPUT_T new_L;

row_loop:
    for (int i = 0; i < RowsColsA; i++) {
        int i_sub1 = i - 1;
        int i_off = ((i_sub1 * i_sub1 - i_sub1) / 2) + i_sub1;
        square_sum = 0;
    col_loop:
        for (int j = 0; j < i; j++) {
#pragma HLS loop_tripcount max = 1 + RowsColsA / 2
            int j_sub1 = j - 1;
            int j_off = ((j_sub1 * j_sub1 - j_sub1) / 2) + j_sub1;
            if (LowerTriangularL)
                product_sum = A[i][j];
            else
                product_sum = hls::x_conj(A[j][i]);
        sum_loop:
            for (int k = 0; k < j; k++) {
#pragma HLS loop_tripcount max = 1 + RowsColsA / 2
#pragma HLS PIPELINE II = CholeskyTraits::INNER_II
                prod = -L_internal[i_off + k] * hls::x_conj(L_internal[j_off + k]);
                prod_cast_to_sum = prod;
                product_sum += prod_cast_to_sum;
            }
            prod_cast_to_off_diag = product_sum;
            L_diag_recip = diag_internal[j];
            cholesky_prod_sum_mult(prod_cast_to_off_diag, L_diag_recip, new_L_off_diag);
            new_L = new_L_off_diag;
            square_sum += hls::x_conj(new_L) * new_L;
            L_internal[i_off + j] = new_L;
            if (LowerTriangularL) {
                L[i][j] = new_L;
                L[j][i] = 0;
            } else {
                L[j][i] = hls::x_conj(new_L);
                L[i][j] = 0;
            }
        }
        A_cast_to_sum = A[i][i];
        A_minus_sum = A_cast_to_sum - square_sum;
        if (cholesky_sqrt_op(A_minus_sum, new_L_diag)) {
            return_code = 1;
        }
        new_L = new_L_diag;
        A_minus_sum_cast_diag = A_minus_sum;
        cholesky_rsqrt(hls::x_real(A_minus_sum_cast_diag), new_L_diag_recip);
        diag_internal[i] = new_L_diag_recip;
        if (LowerTriangularL)
            L[i][i] = new_L;
        else
            L[i][i] = hls::x_conj(new_L);
    }
    return return_code;
}

// =======================================================================
// Alt2：k环 II=1 + i方向 IU 并行；对角用 rsqrt+mul（避免FSqrt），真降Latency
template <bool LowerTriangularL, int RowsColsA, typename CholeskyTraits, class InputType, class OutputType>
int choleskyAlt2(const InputType A[RowsColsA][RowsColsA], OutputType L[RowsColsA][RowsColsA]) {
    int return_code = 0;

    OutputType L_internal[RowsColsA][RowsColsA];
    typename CholeskyTraits::ACCUM_T diag_sum[RowsColsA];
#pragma HLS ARRAY_PARTITION variable = diag_sum complete

#pragma HLS ARRAY_PARTITION variable = A cyclic dim = CholeskyTraits::UNROLL_DIM factor = CholeskyTraits::IU
#pragma HLS ARRAY_PARTITION variable = L cyclic dim = CholeskyTraits::UNROLL_DIM factor = CholeskyTraits::IU
#pragma HLS ARRAY_PARTITION variable = L_internal cyclic dim = CholeskyTraits::UNROLL_DIM factor = CholeskyTraits::IU

// 初始化
init_diag:
    for (int i = 0; i < RowsColsA; i++) {
#pragma HLS PIPELINE II = 1
        diag_sum[i] = 0;
    }

col_loop:
    for (int j = 0; j < RowsColsA; j++) {
        // 1) 对角 L[j][j]：rsqrt + 乘法
        typename CholeskyTraits::ACCUM_T Ajj = A[j][j];
        typename CholeskyTraits::ADD_T Aminus = Ajj - diag_sum[j];

        typename CholeskyTraits::DIAG_T Aminus_cast = Aminus;
        typename CholeskyTraits::RECIP_DIAG_T inv_Ljj;
        cholesky_rsqrt(hls::x_real(Aminus_cast), inv_Ljj);

        typename CholeskyTraits::RECIP_DIAG_T Ljj_real = hls::x_real(Aminus_cast) * inv_Ljj;
        typename CholeskyTraits::L_OUTPUT_T Ljj_out;
        chol_assign_real(Ljj_out, Ljj_real);

        if (LowerTriangularL)
            L[j][j] = Ljj_out;
        else
            L[j][j] = hls::x_conj(Ljj_out);
        L_internal[j][j] = Ljj_out;

        // 2) 列 j 的非对角（i=j+1..）：按 IU 并行块化；k 环 II=1
    iblock_loop:
        for (int ib = j + 1; ib < RowsColsA; ib += CholeskyTraits::IU) {
            typename CholeskyTraits::ACCUM_T psum[CholeskyTraits::IU];
#pragma HLS ARRAY_PARTITION variable = psum complete

        init_ps:
            for (int u = 0; u < CholeskyTraits::IU; u++) {
#pragma HLS UNROLL
                int ii = ib + u;
                if (ii < RowsColsA) {
                    psum[u] = LowerTriangularL ? (typename CholeskyTraits::ACCUM_T)A[ii][j]
                                               : (typename CholeskyTraits::ACCUM_T)hls::x_conj(A[j][ii]);
                }
            }

        k_loop:
            for (int k = 0; k < j; k++) {
#pragma HLS PIPELINE II = 1
                OutputType top = -hls::x_conj(L_internal[j][k]);
            lanes:
                for (int u = 0; u < CholeskyTraits::IU; u++) {
#pragma HLS UNROLL
                    int ii = ib + u;
                    if (ii < RowsColsA) {
                        typename CholeskyTraits::PROD_T pr = (typename CholeskyTraits::PROD_T)(L_internal[ii][k] * top);
                        psum[u] = psum[u] + (typename CholeskyTraits::ACCUM_T)pr;
                    }
                }
            }

        finalize_lanes:
            for (int u = 0; u < CholeskyTraits::IU; u++) {
#pragma HLS UNROLL
                int ii = ib + u;
                if (ii < RowsColsA) {
                    typename CholeskyTraits::OFF_DIAG_T off = (typename CholeskyTraits::OFF_DIAG_T)psum[u];
                    typename CholeskyTraits::OFF_DIAG_T Lij;
                    // 乘法替代除法：Lij = off * inv_Ljj
                    cholesky_prod_sum_mult(off, inv_Ljj, Lij);

                    typename CholeskyTraits::L_OUTPUT_T Lij_out = Lij;
                    L_internal[ii][j] = Lij_out;
                    if (LowerTriangularL)
                        L[ii][j] = Lij_out;
                    else
                        L[j][ii] = hls::x_conj(Lij_out);

                    // 行 ii 的平方和（用于其未来对角）
                    diag_sum[ii] = diag_sum[ii] + (typename CholeskyTraits::ACCUM_T)(hls::x_conj(Lij_out) * Lij_out);
                }
            }
        }
    }

    // 3) 置零另一半三角
    if (CholeskyTraits::ARCH2_ZERO_LOOP) {
    zero_rows:
        for (int i = 0; i < RowsColsA - 1; i++) {
        zero_cols:
            for (int j = i + 1; j < RowsColsA; j++) {
#pragma HLS PIPELINE II = 1
                if (LowerTriangularL)
                    L[i][j] = 0;
                else
                    L[j][i] = 0;
            }
        }
    }
    return return_code;
}

// =======================================================================
// 架构选择
template <bool LowerTriangularL, int RowsColsA, typename CholeskyTraits, class InputType, class OutputType>
int choleskyTop(const InputType A[RowsColsA][RowsColsA], OutputType L[RowsColsA][RowsColsA]) {
    const int sel = (XF_SOLVER_FORCE_ARCH >= 0) ? XF_SOLVER_FORCE_ARCH : CholeskyTraits::ARCH;
    switch (sel) {
        case 0:
            return choleskyBasic<LowerTriangularL, RowsColsA, CholeskyTraits, InputType, OutputType>(A, L);
        case 1:
            return choleskyAlt<LowerTriangularL, RowsColsA, CholeskyTraits, InputType, OutputType>(A, L);
        case 2:
            return choleskyAlt2<LowerTriangularL, RowsColsA, CholeskyTraits, InputType, OutputType>(A, L);
        default:
            return choleskyBasic<LowerTriangularL, RowsColsA, CholeskyTraits, InputType, OutputType>(A, L);
    }
}

// =======================================================================
// 顶层：I/O 展平 + II=1
template <bool LowerTriangularL,
          int RowsColsA,
          class InputType,
          class OutputType,
          typename TRAITS = choleskyTraits<LowerTriangularL, RowsColsA, InputType, OutputType> >
int cholesky(hls::stream<InputType>& matrixAStrm, hls::stream<OutputType>& matrixLStrm) {
    InputType A[RowsColsA][RowsColsA];
    OutputType L[RowsColsA][RowsColsA];

read_loop:
    for (int i = 0; i < RowsColsA * RowsColsA; i++) {
#pragma HLS PIPELINE II = 1
        A[i / RowsColsA][i % RowsColsA] = matrixAStrm.read();
    }

    int ret = choleskyTop<LowerTriangularL, RowsColsA, TRAITS, InputType, OutputType>(A, L);

write_loop:
    for (int i = 0; i < RowsColsA * RowsColsA; i++) {
#pragma HLS PIPELINE II = 1
        matrixLStrm.write(L[i / RowsColsA][i % RowsColsA]);
    }
    return ret;
}

} // namespace solver
} // namespace xf

#endif // _XF_SOLVER_CHOLESKY_HPP_