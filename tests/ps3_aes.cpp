/*
 * This file is part of the Aaru Data Preservation Suite.
 * Copyright (c) 2019-2026 Natalia Portillo.
 *
 * AES-128 CBC encrypt/decrypt tests using NIST test vectors.
 */

#include <cstdint>
#include <cstring>

#include "gtest/gtest.h"

extern "C"
{
#include "../src/lib/aes128.h"
}

/* NIST SP 800-38A F.2.1 CBC-AES128.Encrypt */
static const uint8_t nist_key[16] = {0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
                                     0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c};

static const uint8_t nist_iv[16] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                                    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};

static const uint8_t nist_plaintext[64] = {
    0x6b, 0xc1, 0xbe, 0xe2, 0x2e, 0x40, 0x9f, 0x96, 0xe9, 0x3d, 0x7e, 0x11, 0x73, 0x93, 0x17, 0x2a,
    0xae, 0x2d, 0x8a, 0x57, 0x1e, 0x03, 0xac, 0x9c, 0x9e, 0xb7, 0x6f, 0xac, 0x45, 0xaf, 0x8e, 0x51,
    0x30, 0xc8, 0x1c, 0x46, 0xa3, 0x5c, 0xe4, 0x11, 0xe5, 0xfb, 0xc1, 0x19, 0x1a, 0x0a, 0x52, 0xef,
    0xf6, 0x9f, 0x24, 0x45, 0xdf, 0x4f, 0x9b, 0x17, 0xad, 0x2b, 0x41, 0x7b, 0xe6, 0x6c, 0x37, 0x10};

static const uint8_t nist_ciphertext[64] = {
    0x76, 0x49, 0xab, 0xac, 0x81, 0x19, 0xb2, 0x46, 0xce, 0xe9, 0x8e, 0x9b, 0x12, 0xe9, 0x19, 0x7d,
    0x50, 0x86, 0xcb, 0x9b, 0x50, 0x72, 0x19, 0xee, 0x95, 0xdb, 0x11, 0x3a, 0x91, 0x76, 0x78, 0xb2,
    0x73, 0xbe, 0xd6, 0xb8, 0xe3, 0xc1, 0x74, 0x3b, 0x71, 0x16, 0xe6, 0x9e, 0x22, 0x22, 0x95, 0x16,
    0x3f, 0xf1, 0xca, 0xa1, 0x68, 0x1f, 0xac, 0x09, 0x12, 0x0e, 0xca, 0x30, 0x75, 0x86, 0xe1, 0xa7};

/* Test: CBC encrypt matches NIST test vector */
TEST(AES128, CbcEncryptNist)
{
    uint8_t data[64];
    memcpy(data, nist_plaintext, 64);

    aes128_cbc_encrypt(nist_key, nist_iv, data, 64);

    EXPECT_EQ(0, memcmp(data, nist_ciphertext, 64));
}

/* Test: CBC decrypt matches NIST test vector */
TEST(AES128, CbcDecryptNist)
{
    uint8_t data[64];
    memcpy(data, nist_ciphertext, 64);

    aes128_cbc_decrypt(nist_key, nist_iv, data, 64);

    EXPECT_EQ(0, memcmp(data, nist_plaintext, 64));
}

/* Test: encrypt then decrypt is identity */
TEST(AES128, CbcRoundTrip)
{
    uint8_t original[64];
    uint8_t data[64];
    memcpy(original, nist_plaintext, 64);
    memcpy(data, nist_plaintext, 64);

    aes128_cbc_encrypt(nist_key, nist_iv, data, 64);
    /* After encrypt, data should differ from original */
    EXPECT_NE(0, memcmp(data, original, 64));

    aes128_cbc_decrypt(nist_key, nist_iv, data, 64);
    /* After decrypt, data should match original */
    EXPECT_EQ(0, memcmp(data, original, 64));
}

/* Test: single block encrypt/decrypt */
TEST(AES128, CbcSingleBlock)
{
    uint8_t data[16];
    uint8_t original[16];

    memcpy(data, nist_plaintext, 16);
    memcpy(original, nist_plaintext, 16);

    aes128_cbc_encrypt(nist_key, nist_iv, data, 16);
    /* First block of NIST ciphertext */
    EXPECT_EQ(0, memcmp(data, nist_ciphertext, 16));

    aes128_cbc_decrypt(nist_key, nist_iv, data, 16);
    EXPECT_EQ(0, memcmp(data, original, 16));
}

/* Test: zero IV encrypt/decrypt round-trip */
TEST(AES128, CbcZeroIv)
{
    uint8_t zero_iv[16] = {0};
    uint8_t data[32];
    uint8_t original[32];

    /* Some arbitrary data */
    for(int i = 0; i < 32; i++) original[i] = (uint8_t)(i * 7 + 3);
    memcpy(data, original, 32);

    aes128_cbc_encrypt(nist_key, zero_iv, data, 32);
    aes128_cbc_decrypt(nist_key, zero_iv, data, 32);

    EXPECT_EQ(0, memcmp(data, original, 32));
}

/* Test: 2048 bytes (sector-sized) round-trip */
TEST(AES128, CbcSectorSize)
{
    uint8_t key[16] = {0x38, 0x0B, 0xCF, 0x0B, 0x53, 0x45, 0x5B, 0x3C,
                       0x78, 0x17, 0xAB, 0x4F, 0xA3, 0xBA, 0x90, 0xED};
    uint8_t iv[16]  = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                       0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01};

    uint8_t data[2048];
    uint8_t original[2048];

    for(int i = 0; i < 2048; i++) original[i] = (uint8_t)(i & 0xFF);
    memcpy(data, original, 2048);

    aes128_cbc_encrypt(key, iv, data, 2048);
    EXPECT_NE(0, memcmp(data, original, 2048));

    aes128_cbc_decrypt(key, iv, data, 2048);
    EXPECT_EQ(0, memcmp(data, original, 2048));
}
