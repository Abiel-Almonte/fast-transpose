#include <cstdint>

#if defined(__SSE__) || defined(__AVX2__)

    #include <immintrin.h>

    #if defined(FORCE_AVX2)
        //256 bit vectors
        constexpr uint32_t TILE_DIM= 8;
    #else
        //128 bit vectors
        constexpr uint32_t TILE_DIM= 4; 
    #endif

    //simd consts
    constexpr uint32_t SHFL_LO_MASK= 0b01000100; //get even pair
    constexpr uint32_t SHFL_HI_MASK= 0b11101110; //get odd pair
    constexpr uint32_t PERMUTE_LO_MASK= 0b00100000; //get first half of each
    constexpr uint32_t PERMUTE_HI_MASK= 0b00110001; //get second half of each

#elif defined(__ARM_NEON__) || defined(__ARM_NEON)

    #include <arm_neon.h>
    //128 bit vectors
    constexpr uint32_t TILE_DIM= 4;
    
#else

    #error "Only SSE/AVX2 and NEON are supported"
    
#endif

constexpr uint32_t BLOCK_DIM= 32;
constexpr uint32_t TILES_PER_BLOCK_DIM= BLOCK_DIM / TILE_DIM;

inline void transpose_inplace_tiled_simd_impl(float* A, const uint32_t m, const float alpha, const uint32_t stride);
inline void transpose_inplace_tiled_simd_impl(float* A, const uint32_t m, const uint32_t stride);

template <bool scale> inline void swap_full(float* alignedA, uint32_t m, float alpha, uint32_t stride, uint32_t full_blocks, float row_buffer[TILE_DIM][TILE_DIM]);
template <bool scale> inline void swap_edge(float* alignedA, uint32_t m, float alpha, uint32_t stride, uint32_t full_blocks, float row_buffer[TILE_DIM][TILE_DIM]);
template <bool scale> inline void swap_tile(float* alignedA, float buffer[TILE_DIM][TILE_DIM], const uint32_t block_base_addr, const uint32_t block_base_addr_T, const uint32_t ti, const uint32_t tj, const float alpha, const uint32_t stride);
template <bool scale> inline void swap_scalar(float* alignedA, const uint32_t tile_base_addr, const uint32_t tile_base_addr_T, const uint32_t i, const uint32_t j, const float alpha, const uint32_t stride);

template <bool scale> inline void simd_transpose_tile_128(const float* src, const uint32_t src_stride, float* dst, const uint32_t dst_stride, const float alpha);
template <bool scale> inline void simd_transpose_tile_256(const float* src, const uint32_t src_stride, float* dst, const uint32_t dst_stride, const float alpha);

extern "C" {
    void transpose_inplace_tiled_simd(float* A, const uint32_t m, const float alpha, const uint32_t stride) {
        if (alpha == 1.0f){
            transpose_inplace_tiled_simd_impl(
            A, m, stride
            );
        }else{
            transpose_inplace_tiled_simd_impl(
            A, m, alpha, stride
            );
        }
    }
}

inline void transpose_inplace_tiled_simd_impl(float* A, const uint32_t m, const float alpha, const uint32_t stride){
    float* alignedA= static_cast<float*>(__builtin_assume_aligned(A, 64));
    alignas(32) float row_buffer[TILE_DIM][TILE_DIM];

    uint32_t full_blocks= m/BLOCK_DIM;
    //Full Blocks
    swap_full<true>(
        alignedA, m, alpha, stride,
        full_blocks, row_buffer
    );

    //Edge Blocks 
    uint32_t num_blocks= (m + BLOCK_DIM - 1)/BLOCK_DIM;
    if(num_blocks > full_blocks){
        swap_edge<true>(
            alignedA, m, alpha, stride,
            full_blocks, row_buffer
        );
    }
}


inline void transpose_inplace_tiled_simd_impl(float* A, const uint32_t m, const uint32_t stride){
    float* alignedA= static_cast<float*>(__builtin_assume_aligned(A, 64));
    alignas(32) float row_buffer[TILE_DIM][TILE_DIM];

    uint32_t full_blocks= m/BLOCK_DIM;
    //Full Blocks
    swap_full<false>(
        alignedA, m, 0.0f, stride,
        full_blocks, row_buffer
    );

    //Edge Blocks 
    uint32_t num_blocks= (m + BLOCK_DIM - 1)/BLOCK_DIM;
    if(num_blocks > full_blocks){
        swap_edge<false>(
            alignedA, m, 0.0f, stride,
            full_blocks, row_buffer
        );
    }
}

template <bool scale> 
inline void swap_full(float* alignedA, uint32_t m, float alpha, uint32_t stride, uint32_t full_blocks, float row_buffer[TILE_DIM][TILE_DIM]){
    //PATH 1: Full Block
    for (uint32_t bi= 0; bi < full_blocks; bi++) {
        for (uint32_t bj= bi+1; bj < full_blocks; bj++) {
            uint32_t block_base_addr= bi*BLOCK_DIM*stride + bj*BLOCK_DIM;
            uint32_t block_base_addr_T= bj*BLOCK_DIM*stride + bi*BLOCK_DIM;
            
            //Off Diag Block
            for (uint32_t ti= 0; ti < TILES_PER_BLOCK_DIM; ti++) {
                for (uint32_t tj= 0; tj < TILES_PER_BLOCK_DIM; tj++) {
                    swap_tile<scale>(
                        alignedA,
                        row_buffer,
                        block_base_addr,
                        block_base_addr_T,
                        ti, tj, alpha, stride
                    );
                }
            }
        }

        //Diag Block
        uint32_t block_base_addr= bi*BLOCK_DIM*stride + bi*BLOCK_DIM;
        for (uint32_t ti= 0; ti < TILES_PER_BLOCK_DIM; ti++) {
            for (uint32_t tj= ti; tj < TILES_PER_BLOCK_DIM; tj++) {
                swap_tile<scale>(
                    alignedA,
                    row_buffer,
                    block_base_addr,
                    block_base_addr,
                    ti, tj, alpha, stride
                );
            }
        }
    }
}

template <bool scale>
inline void swap_edge(float* alignedA, uint32_t m, float alpha, uint32_t stride, uint32_t full_blocks, float row_buffer[TILE_DIM][TILE_DIM]){
    uint32_t block_small_dim= m - full_blocks*BLOCK_DIM;

    uint32_t tile_small_dim= block_small_dim%TILE_DIM;
    uint32_t full_tiles= block_small_dim/TILE_DIM;

    //Edge Block, Full and Edge Tile
    for (uint32_t bi= 0; bi < full_blocks; bi++){
        uint32_t block_base_addr= bi*BLOCK_DIM*stride + full_blocks*BLOCK_DIM;
        uint32_t block_base_addr_T= full_blocks*BLOCK_DIM*stride + bi*BLOCK_DIM;

        //PATH 2: Full Tile
        for (uint32_t ti= 0; ti < TILES_PER_BLOCK_DIM; ti++){
            for (uint32_t tj= 0; tj < full_tiles; tj++){
                swap_tile<scale>(
                    alignedA,
                    row_buffer,
                    block_base_addr,
                    block_base_addr_T,
                    ti, tj, alpha, stride
                );
            }
        }

        //PATH 3: Edge Tile
        for (uint32_t ti= 0; ti < TILES_PER_BLOCK_DIM; ti++){
            uint32_t tile_base_addr= block_base_addr + ti*TILE_DIM*stride + full_tiles*TILE_DIM;
            uint32_t tile_base_addr_T= block_base_addr_T + full_tiles*TILE_DIM*stride + ti*TILE_DIM;
            
            for (uint8_t i= 0; i < TILE_DIM; i++){
                for (uint8_t j= 0; j < tile_small_dim; j++){
                    swap_scalar<scale>(
                    alignedA,
                    tile_base_addr,
                    tile_base_addr_T,
                    i, j, alpha, stride
                    );
                }
            } 
        }
    }

    //PATH 4: Diag Edge Block, Full Tile
    uint32_t block_base_addr= full_blocks*BLOCK_DIM*stride + full_blocks*BLOCK_DIM;
    for(uint32_t ti= 0; ti < full_tiles; ti++){ //can be cut off by bottom edge small dim
        for(uint32_t tj= ti; tj < full_tiles; tj++){
            swap_tile<scale>(
            alignedA,
            row_buffer,
            block_base_addr,
            block_base_addr,
            ti, tj, alpha, stride
            );
        }
    }

    //PATH 5: Diag Edge Block, Edge Tile
    for (uint32_t ti= 0; ti< full_tiles; ti++){
        uint32_t tile_base_addr= block_base_addr + ti*TILE_DIM*stride + full_tiles*TILE_DIM;
        uint32_t tile_base_addr_T= block_base_addr + full_tiles*TILE_DIM*stride+ ti*TILE_DIM;

        for (uint32_t i= 0; i < TILE_DIM; i++){
            for (uint32_t j= 0; j < tile_small_dim; j++){
                swap_scalar<scale>(
                    alignedA,
                    tile_base_addr,
                    tile_base_addr_T,
                    i, j, alpha, stride
                );
            }
        }
    }

    //PATH 6: Diag Edge Block, Diag Edge Tile
    uint32_t tile_base_addr= block_base_addr + full_tiles*TILE_DIM*stride + full_tiles*TILE_DIM;
    for (uint32_t i= 0; i < tile_small_dim; i++){
        for (uint32_t j= i + 1; j < tile_small_dim; j++){
            swap_scalar<scale>(
                alignedA,
                tile_base_addr,
                tile_base_addr,
                i, j, alpha, stride
            );
        }
    }

    if constexpr (scale){
        for (uint32_t i= 0; i < tile_small_dim; i++){
            alignedA[tile_base_addr + i*stride + i]*= alpha;
        }
    }
}

template<bool scale>
inline void swap_scalar(float* alignedA, const uint32_t tile_base_addr, const uint32_t tile_base_addr_T, const uint32_t i, const uint32_t j, const float alpha, const uint32_t stride){
    if constexpr (scale){
        float temp= alpha*alignedA[tile_base_addr + i*stride + j];
        alignedA[tile_base_addr + i*stride + j]= alpha*alignedA[tile_base_addr_T + j*stride + i];
        alignedA[tile_base_addr_T + j*stride +i]= temp; 
    }else{
        float temp= alignedA[tile_base_addr + i*stride + j];
        alignedA[tile_base_addr + i*stride + j]= alignedA[tile_base_addr_T + j*stride + i];
        alignedA[tile_base_addr_T + j*stride +i]= temp;
    }
}

template<bool scale>
inline void swap_tile(float* alignedA, float buffer[TILE_DIM][TILE_DIM], const uint32_t block_base_addr, const uint32_t block_base_addr_T, const uint32_t ti, const uint32_t tj, const float alpha, const uint32_t stride){
    float* tile= alignedA + block_base_addr + ti*TILE_DIM*stride + tj*TILE_DIM;
    float* tile_T= alignedA + block_base_addr_T + tj*TILE_DIM*stride + ti*TILE_DIM;

    for(uint8_t i= 0; i< TILE_DIM; i++){
        #if defined(__SSE__) || defined(__AVX2__)
            #if defined(FORCE_AVX2)
                _mm256_storeu_ps(buffer[i], _mm256_loadu_ps(tile + i*stride));
            #else
                _mm_storeu_ps(buffer[i], _mm_loadu_ps(tile + i*stride));
            #endif
        #elif defined(__ARM_NEON__) || defined(__ARM_NEON)
            vst1q_f32(buffer[i], vld1q_f32(tile + i*stride));
        #endif
    }

    #if defined(FORCE_AVX2)
        simd_transpose_tile_256<scale>(tile_T, stride, tile, stride, alpha);
        simd_transpose_tile_256<scale>(&buffer[0][0], TILE_DIM, tile_T, stride, alpha);
    #else
        simd_transpose_tile_128<scale>(tile_T, stride, tile, stride, alpha);
        simd_transpose_tile_128<scale>(&buffer[0][0], TILE_DIM, tile_T, stride, alpha);
    #endif
}

template<bool scale>
inline void simd_transpose_tile_128(const float* src, const uint32_t src_stride, float* dst, const uint32_t dst_stride, const float alpha){
    #if defined(__SSE__)

        __m128 r0= _mm_loadu_ps(src);   
        __m128 r1= _mm_loadu_ps(src + src_stride);
        __m128 r2= _mm_loadu_ps(src + 2*src_stride);
        __m128 r3= _mm_loadu_ps(src + 3*src_stride);

        if constexpr (scale){
            __m128 a= _mm_set1_ps(alpha);
            r0= _mm_mul_ps(a, r0);
            r1= _mm_mul_ps(a, r1);
            r2= _mm_mul_ps(a, r2);
            r3= _mm_mul_ps(a, r3);
        }

        __m128 half_0_1__0= _mm_unpacklo_ps(r0, r1);
        __m128 half_2_3__0= _mm_unpackhi_ps(r0, r1);

        __m128 half_0_1__1= _mm_unpacklo_ps(r2, r3);
        __m128 half_2_3__1= _mm_unpackhi_ps(r2, r3);

        __m128 c0= _mm_shuffle_ps(half_0_1__0, half_0_1__1, SHFL_LO_MASK);
        __m128 c1= _mm_shuffle_ps(half_0_1__0, half_0_1__1, SHFL_HI_MASK);

        __m128 c2= _mm_shuffle_ps(half_2_3__0, half_2_3__1, SHFL_LO_MASK);
        __m128 c3= _mm_shuffle_ps(half_2_3__0, half_2_3__1, SHFL_HI_MASK);

        _mm_storeu_ps(dst, c0);  
        _mm_storeu_ps(dst + dst_stride, c1);
        _mm_storeu_ps(dst + 2*dst_stride, c2);
        _mm_storeu_ps(dst + 3*dst_stride, c3);

    #elif  defined(__ARM_NEON__) || defined(__ARM_NEON)

        float32x4_t r0= vld1q_f32(src);
        float32x4_t r1= vld1q_f32(src + src_stride);
        float32x4_t r2= vld1q_f32(src + 2*src_stride);
        float32x4_t r3= vld1q_f32(src + 3*src_stride);

        if constexpr(scale){
            float32x4_t a= vdupq_n_f32(alpha);
            r0= vmulq_f32(a, r0);
            r1= vmulq_f32(a, r1);
            r2= vmulq_f32(a, r2);
            r3= vmulq_f32(a, r3);
        }

        float32x4_t half_0_2__0= vtrn1q_f32(r0, r1);
        float32x4_t half_1_3__0= vtrn2q_f32(r0, r1);

        float32x4_t half_0_2__1= vtrn1q_f32(r2, r3);
        float32x4_t half_1_3__1= vtrn2q_f32(r2, r3);

        float32x4_t c0= vcombine_f32(vget_low_f32(half_0_2__0), vget_low_f32(half_0_2__1));
        float32x4_t c2= vcombine_f32(vget_high_f32(half_0_2__0), vget_high_f32(half_0_2__1));

        float32x4_t c1= vcombine_f32(vget_low_f32(half_1_3__0), vget_low_f32(half_1_3__1));
        float32x4_t c3= vcombine_f32(vget_high_f32(half_1_3__0), vget_high_f32(half_1_3__1));

        vst1q_f32(dst, c0);
        vst1q_f32(dst + 2*dst_stride, c2);

        vst1q_f32(dst + dst_stride, c1);
        vst1q_f32(dst + 3*dst_stride, c3);

    #endif
}

template<bool scale>
inline void simd_transpose_tile_256(const float* src, const uint32_t src_stride, float* dst, const uint32_t dst_stride, const float alpha){
    #if defined(__AVX2__)

        __m256 r0= _mm256_loadu_ps(src);
        __m256 r1= _mm256_loadu_ps(src + src_stride);
        __m256 r2= _mm256_loadu_ps(src + 2*src_stride);
        __m256 r3= _mm256_loadu_ps(src + 3*src_stride);
        __m256 r4= _mm256_loadu_ps(src + 4*src_stride);
        __m256 r5= _mm256_loadu_ps(src + 5*src_stride);
        __m256 r6= _mm256_loadu_ps(src + 6*src_stride);
        __m256 r7= _mm256_loadu_ps(src + 7*src_stride);

        if constexpr (scale){
            __m256 a= _mm256_set1_ps(alpha);
            r0= _mm256_mul_ps(a, r0);
            r1= _mm256_mul_ps(a, r1);
            r2= _mm256_mul_ps(a, r2);
            r3= _mm256_mul_ps(a, r3);
            r4= _mm256_mul_ps(a, r4);
            r5= _mm256_mul_ps(a, r5);
            r6= _mm256_mul_ps(a, r6);
            r7= _mm256_mul_ps(a, r7);
        }

        __m256 quarter_0_1_4_5__0= _mm256_unpacklo_ps(r0, r1);
        __m256 quarter_2_3_6_7__0= _mm256_unpackhi_ps(r0, r1);

        __m256 quarter_0_1_4_5__1= _mm256_unpacklo_ps(r2, r3);
        __m256 quarter_2_3_6_7__1= _mm256_unpackhi_ps(r2, r3);

        __m256 quarter_0_1_4_5__2= _mm256_unpacklo_ps(r4, r5);
        __m256 quarter_2_3_6_7__2= _mm256_unpackhi_ps(r4, r5);

        __m256 quarter_0_1_4_5__3= _mm256_unpacklo_ps(r6, r7);
        __m256 quarter_2_3_6_7__3= _mm256_unpackhi_ps(r6, r7);

        __m256 half_0_4__0= _mm256_shuffle_ps(quarter_0_1_4_5__0, quarter_0_1_4_5__1, SHFL_LO_MASK);
        __m256 half_0_4__1= _mm256_shuffle_ps(quarter_0_1_4_5__2, quarter_0_1_4_5__3, SHFL_LO_MASK);

        __m256 half_1_5__0= _mm256_shuffle_ps(quarter_0_1_4_5__0, quarter_0_1_4_5__1, SHFL_HI_MASK);
        __m256 half_1_5__1= _mm256_shuffle_ps(quarter_0_1_4_5__2, quarter_0_1_4_5__3, SHFL_HI_MASK);

        __m256 half_2_6__0= _mm256_shuffle_ps(quarter_2_3_6_7__0, quarter_2_3_6_7__1, SHFL_LO_MASK);
        __m256 half_2_6__1= _mm256_shuffle_ps(quarter_2_3_6_7__2, quarter_2_3_6_7__3, SHFL_LO_MASK);

        __m256 half_3_7__0= _mm256_shuffle_ps(quarter_2_3_6_7__0, quarter_2_3_6_7__1, SHFL_HI_MASK);
        __m256 half_3_7__1= _mm256_shuffle_ps(quarter_2_3_6_7__2, quarter_2_3_6_7__3, SHFL_HI_MASK);

        __m256 c0= _mm256_permute2f128_ps(half_0_4__0, half_0_4__1, PERMUTE_LO_MASK);
        __m256 c4= _mm256_permute2f128_ps(half_0_4__0, half_0_4__1, PERMUTE_HI_MASK);

        __m256 c1= _mm256_permute2f128_ps(half_1_5__0, half_1_5__1, PERMUTE_LO_MASK);
        __m256 c5= _mm256_permute2f128_ps(half_1_5__0, half_1_5__1, PERMUTE_HI_MASK);

        __m256 c2= _mm256_permute2f128_ps(half_2_6__0, half_2_6__1, PERMUTE_LO_MASK);
        __m256 c6= _mm256_permute2f128_ps(half_2_6__0, half_2_6__1, PERMUTE_HI_MASK);

        __m256 c3= _mm256_permute2f128_ps(half_3_7__0, half_3_7__1, PERMUTE_LO_MASK);
        __m256 c7= _mm256_permute2f128_ps(half_3_7__0, half_3_7__1, PERMUTE_HI_MASK);

        _mm256_storeu_ps(dst, c0);  
        _mm256_storeu_ps(dst + 4*dst_stride, c4);

        _mm256_storeu_ps(dst + dst_stride, c1);
        _mm256_storeu_ps(dst + 5*dst_stride, c5);

        _mm256_storeu_ps(dst + 2*dst_stride, c2);
        _mm256_storeu_ps(dst + 6*dst_stride, c6);

        _mm256_storeu_ps(dst + 3*dst_stride, c3);
        _mm256_storeu_ps(dst + 7*dst_stride, c7);

    #endif
}