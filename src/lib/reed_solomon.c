/*
 * This file is part of the Aaru Data Preservation Suite.
 * Copyright (c) 2019-2026 Natalia Portillo.
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 2.1 of the
 * License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @file reed_solomon.c
 * @brief Reed-Solomon erasure codec over GF(2^8).
 *
 * Implements RS(K, M) encoding and decoding using a Vandermonde-derived
 * generator matrix. Encoding is incremental (one data shard at a time).
 * Decoding uses Gaussian elimination to reconstruct erased shards.
 *
 * The coding matrix is (K+M) x K where:
 *   - Top K rows form an identity matrix (data shards pass through unchanged)
 *   - Bottom M rows are the generator matrix (parity = G * data)
 *
 * Generator matrix construction:
 *   Start with a (K+M) x K Vandermonde matrix V where V[i][j] = i^j in GF(2^8).
 *   Invert the top K x K submatrix and multiply the entire matrix by the inverse
 *   so that the top K rows become identity. The bottom M rows are the generator.
 */

#include <stdlib.h>
#include <string.h>

#include "reed_solomon.h"
#include "gf256.h"

struct rs_context
{
    uint16_t K;        /**< Number of data shards. */
    uint16_t M;        /**< Number of parity shards. */
    uint8_t *gen;      /**< Generator matrix: M rows x K columns (row-major). */
    uint8_t *coding;   /**< Full coding matrix: (K+M) rows x K columns. */
};

/**
 * @brief Build a Vandermonde matrix (K+M) x K in GF(2^8).
 *
 * V[i][j] = i^j in GF(2^8), where i is the row index and j is the column index.
 * Row 0 is all zeros except column 0 (since 0^0 = 1 by convention, 0^j = 0 for j>0).
 * We use row indices 0..K+M-1.
 */
static uint8_t *build_vandermonde(uint16_t K, uint16_t M)
{
    const uint16_t N = K + M;
    uint8_t *V = calloc((size_t)N * K, sizeof(uint8_t));
    if(!V) return NULL;

    for(uint16_t i = 0; i < N; i++)
    {
        uint8_t val = 1; /* i^0 = 1 */
        for(uint16_t j = 0; j < K; j++)
        {
            V[(size_t)i * K + j] = val;
            val = gf256_mul(val, (uint8_t)i);
        }
    }
    return V;
}

/**
 * @brief Invert a K x K matrix in-place using Gaussian elimination over GF(2^8).
 *
 * @param mat The matrix to invert, stored row-major in K*K bytes.
 * @param inv Output inverse matrix (must be pre-initialized to identity).
 * @param K Matrix dimension.
 * @return 0 on success, -1 if singular.
 */
static int invert_matrix(const uint8_t *mat, uint8_t *inv, uint16_t K)
{
    /* Work on a copy to avoid modifying input */
    uint8_t *work = malloc((size_t)K * K);
    if(!work) return -1;
    memcpy(work, mat, (size_t)K * K);

    /* Initialize inv to identity */
    memset(inv, 0, (size_t)K * K);
    for(uint16_t i = 0; i < K; i++)
        inv[(size_t)i * K + i] = 1;

    /* Forward elimination */
    for(uint16_t col = 0; col < K; col++)
    {
        /* Find pivot */
        uint16_t pivot = col;
        while(pivot < K && work[(size_t)pivot * K + col] == 0)
            pivot++;
        if(pivot == K) { free(work); return -1; } /* Singular */

        /* Swap rows if needed */
        if(pivot != col)
        {
            for(uint16_t j = 0; j < K; j++)
            {
                uint8_t tmp = work[(size_t)col * K + j];
                work[(size_t)col * K + j] = work[(size_t)pivot * K + j];
                work[(size_t)pivot * K + j] = tmp;

                tmp = inv[(size_t)col * K + j];
                inv[(size_t)col * K + j] = inv[(size_t)pivot * K + j];
                inv[(size_t)pivot * K + j] = tmp;
            }
        }

        /* Scale pivot row to make diagonal element 1 */
        uint8_t diag = work[(size_t)col * K + col];
        if(diag != 1)
        {
            uint8_t inv_diag = gf256_inv(diag);
            for(uint16_t j = 0; j < K; j++)
            {
                work[(size_t)col * K + j] = gf256_mul(work[(size_t)col * K + j], inv_diag);
                inv[(size_t)col * K + j]  = gf256_mul(inv[(size_t)col * K + j], inv_diag);
            }
        }

        /* Eliminate column in all other rows */
        for(uint16_t row = 0; row < K; row++)
        {
            if(row == col) continue;
            uint8_t factor = work[(size_t)row * K + col];
            if(factor == 0) continue;
            for(uint16_t j = 0; j < K; j++)
            {
                work[(size_t)row * K + j] ^= gf256_mul(factor, work[(size_t)col * K + j]);
                inv[(size_t)row * K + j]  ^= gf256_mul(factor, inv[(size_t)col * K + j]);
            }
        }
    }

    free(work);
    return 0;
}

rs_context *rs_create(uint16_t K, uint16_t M)
{
    if(K == 0 || M == 0 || (uint32_t)K + M > 255) return NULL;

    rs_context *ctx = calloc(1, sizeof(rs_context));
    if(!ctx) return NULL;
    ctx->K = K;
    ctx->M = M;

    const uint16_t N = K + M;

    /* Build Vandermonde matrix */
    uint8_t *V = build_vandermonde(K, M);
    if(!V) { free(ctx); return NULL; }

    /* Invert top K x K submatrix */
    uint8_t *top_inv = malloc((size_t)K * K);
    if(!top_inv) { free(V); free(ctx); return NULL; }

    if(invert_matrix(V, top_inv, K) != 0)
    {
        free(top_inv);
        free(V);
        free(ctx);
        return NULL;
    }

    /* Compute coding matrix = V * top_inv^(-1) so top K rows become identity */
    ctx->coding = calloc((size_t)N * K, sizeof(uint8_t));
    if(!ctx->coding) { free(top_inv); free(V); free(ctx); return NULL; }

    for(uint16_t i = 0; i < N; i++)
    {
        for(uint16_t j = 0; j < K; j++)
        {
            uint8_t val = 0;
            for(uint16_t m = 0; m < K; m++)
                val ^= gf256_mul(V[(size_t)i * K + m], top_inv[(size_t)m * K + j]);
            ctx->coding[(size_t)i * K + j] = val;
        }
    }

    free(top_inv);
    free(V);

    /* Generator matrix = bottom M rows of the coding matrix */
    ctx->gen = ctx->coding + (size_t)K * K;

    return ctx;
}

void rs_free(rs_context *ctx)
{
    if(!ctx) return;
    free(ctx->coding); /* gen points inside coding, don't free separately */
    free(ctx);
}

uint8_t rs_get_coefficient(const rs_context *ctx, uint16_t m, uint16_t k)
{
    return ctx->gen[(size_t)m * ctx->K + k];
}

void rs_encode_incremental(uint8_t coeff, const uint8_t *data, uint8_t *parity, size_t shard_size)
{
    gf256_mul_region(parity, data, coeff, shard_size);
}

int rs_decode(const rs_context *ctx, uint8_t **shards, const uint8_t *present, size_t shard_size)
{
    const uint16_t K = ctx->K;
    const uint16_t M = ctx->M;
    const uint16_t N = K + M;

    /* Count erasures */
    uint16_t num_erased = 0;
    for(uint16_t i = 0; i < N; i++)
        if(!present[i]) num_erased++;

    if(num_erased == 0) return 0;         /* Nothing to do */
    if(num_erased > M)  return -1;        /* Too many erasures */

    /* Build the submatrix from rows of the coding matrix corresponding to
     * the K surviving shards. We need exactly K surviving shards to form
     * a K x K system. */

    /* Collect indices of surviving shards (pick first K) */
    uint16_t *surviving = malloc((size_t)K * sizeof(uint16_t));
    if(!surviving) return -2;

    uint16_t s = 0;
    for(uint16_t i = 0; i < N && s < K; i++)
    {
        if(present[i]) surviving[s++] = i;
    }

    if(s < K) { free(surviving); return -1; } /* Not enough surviving shards */

    /* Build K x K submatrix from surviving rows of the coding matrix */
    uint8_t *submat = malloc((size_t)K * K);
    if(!submat) { free(surviving); return -2; }

    for(uint16_t i = 0; i < K; i++)
        memcpy(submat + (size_t)i * K, ctx->coding + (size_t)surviving[i] * K, K);

    /* Invert the submatrix */
    uint8_t *submat_inv = malloc((size_t)K * K);
    if(!submat_inv) { free(submat); free(surviving); return -2; }

    if(invert_matrix(submat, submat_inv, K) != 0)
    {
        free(submat_inv);
        free(submat);
        free(surviving);
        return -1; /* Should not happen if coding matrix is MDS */
    }

    /* Reconstruct erased shards:
     * For each erased shard e, compute:
     *   shard[e] = sum over j=0..K-1 of (coding[e][j] * decoded_data[j])
     *
     * But decoded_data = submat_inv * surviving_shards
     * So: shard[e] = sum_j coding[e][j] * (sum_k submat_inv[j][k] * surviving_shards[k])
     *
     * Reorder: shard[e] = sum_k (sum_j coding[e][j] * submat_inv[j][k]) * surviving_shards[k]
     * Let repair_row[e][k] = sum_j coding[e][j] * submat_inv[j][k]
     */
    for(uint16_t e = 0; e < N; e++)
    {
        if(present[e]) continue;

        /* Compute repair coefficients for this erased shard */
        memset(shards[e], 0, shard_size);

        for(uint16_t k = 0; k < K; k++)
        {
            /* Compute combined coefficient: sum_j coding[e][j] * submat_inv[j][k] */
            uint8_t coeff = 0;
            for(uint16_t j = 0; j < K; j++)
                coeff ^= gf256_mul(ctx->coding[(size_t)e * K + j], submat_inv[(size_t)j * K + k]);

            if(coeff != 0)
                gf256_mul_region(shards[e], shards[surviving[k]], coeff, shard_size);
        }
    }

    free(submat_inv);
    free(submat);
    free(surviving);
    return 0;
}
