/*
 * This file is part of the Aaru Data Preservation Suite.
 * Copyright (c) 2019-2026 Natalia Portillo.
 *
 * PS3 key derivation, IV derivation, and sector encrypt/decrypt tests.
 */

#include <cstdint>
#include <cstring>

#include "gtest/gtest.h"

extern "C"
{
#include "../src/ps3/ps3_crypto.h"
}

/* Test: IV derivation for sector 0 */
TEST(PS3Crypto, IvDeriveSector0)
{
    uint8_t iv[16];
    uint8_t expected[16] = {0};

    ps3_derive_iv(0, iv);
    EXPECT_EQ(0, memcmp(iv, expected, 16));
}

/* Test: IV derivation for sector 1 */
TEST(PS3Crypto, IvDeriveSector1)
{
    uint8_t iv[16];
    uint8_t expected[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};

    ps3_derive_iv(1, iv);
    EXPECT_EQ(0, memcmp(iv, expected, 16));
}

/* Test: IV derivation for sector 0xFF */
TEST(PS3Crypto, IvDeriveSectorFF)
{
    uint8_t iv[16];
    uint8_t expected[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF};

    ps3_derive_iv(0xFF, iv);
    EXPECT_EQ(0, memcmp(iv, expected, 16));
}

/* Test: IV derivation for sector 0x12345678 */
TEST(PS3Crypto, IvDeriveSectorMultiByte)
{
    uint8_t iv[16];
    uint8_t expected[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x12, 0x34, 0x56, 0x78};

    ps3_derive_iv(0x12345678, iv);
    EXPECT_EQ(0, memcmp(iv, expected, 16));
}

/* Test: IV derivation for large sector number spanning 8 bytes */
TEST(PS3Crypto, IvDeriveSectorLarge)
{
    uint8_t iv[16];
    uint8_t expected[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0xAB, 0xCD, 0xEF, 0x01, 0x23, 0x45, 0x67, 0x89};

    ps3_derive_iv(0xABCDEF0123456789ULL, iv);
    EXPECT_EQ(0, memcmp(iv, expected, 16));
}

/* Test: disc key derivation produces consistent output */
TEST(PS3Crypto, DiscKeyDerivation)
{
    uint8_t data1[16]    = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                            0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10};
    uint8_t disc_key[16] = {0};

    ps3_derive_disc_key(data1, disc_key);

    /* Disc key should not be all zeros (derivation produced output) */
    uint8_t zeros[16] = {0};
    EXPECT_NE(0, memcmp(disc_key, zeros, 16));

    /* Disc key should not equal data1 (it was actually transformed) */
    EXPECT_NE(0, memcmp(disc_key, data1, 16));

    /* Derivation should be deterministic */
    uint8_t disc_key2[16] = {0};
    ps3_derive_disc_key(data1, disc_key2);
    EXPECT_EQ(0, memcmp(disc_key, disc_key2, 16));
}

/* Test: different data1 values produce different disc keys */
TEST(PS3Crypto, DiscKeyDifferentData1)
{
    uint8_t data1_a[16] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                           0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10};
    uint8_t data1_b[16] = {0xFF, 0xFE, 0xFD, 0xFC, 0xFB, 0xFA, 0xF9, 0xF8,
                           0xF7, 0xF6, 0xF5, 0xF4, 0xF3, 0xF2, 0xF1, 0xF0};
    uint8_t disc_key_a[16], disc_key_b[16];

    ps3_derive_disc_key(data1_a, disc_key_a);
    ps3_derive_disc_key(data1_b, disc_key_b);

    EXPECT_NE(0, memcmp(disc_key_a, disc_key_b, 16));
}

/* Test: sector encrypt/decrypt round-trip */
TEST(PS3Crypto, SectorRoundTrip)
{
    uint8_t disc_key[16] = {0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0xBA, 0xBE,
                            0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF};

    uint8_t original[2048];
    uint8_t data[2048];

    for(int i = 0; i < 2048; i++) original[i] = (uint8_t)(i & 0xFF);
    memcpy(data, original, 2048);

    ps3_encrypt_sector(disc_key, 42, data, 2048);
    EXPECT_NE(0, memcmp(data, original, 2048));

    ps3_decrypt_sector(disc_key, 42, data, 2048);
    EXPECT_EQ(0, memcmp(data, original, 2048));
}

/* Test: different sectors produce different ciphertext */
TEST(PS3Crypto, DifferentSectorsDifferentCiphertext)
{
    uint8_t disc_key[16] = {0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0xBA, 0xBE,
                            0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF};

    uint8_t plaintext[2048];
    for(int i = 0; i < 2048; i++) plaintext[i] = (uint8_t)(i & 0xFF);

    uint8_t data_a[2048], data_b[2048];
    memcpy(data_a, plaintext, 2048);
    memcpy(data_b, plaintext, 2048);

    ps3_encrypt_sector(disc_key, 100, data_a, 2048);
    ps3_encrypt_sector(disc_key, 200, data_b, 2048);

    /* Same plaintext encrypted with different sector IVs should differ */
    EXPECT_NE(0, memcmp(data_a, data_b, 2048));
}

/* Test: full key derivation + sector encrypt/decrypt */
TEST(PS3Crypto, FullPipelineRoundTrip)
{
    uint8_t data1[16] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
                         0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00};
    uint8_t disc_key[16];

    ps3_derive_disc_key(data1, disc_key);

    uint8_t original[2048];
    uint8_t data[2048];
    for(int i = 0; i < 2048; i++) original[i] = (uint8_t)((i * 3 + 7) & 0xFF);
    memcpy(data, original, 2048);

    ps3_encrypt_sector(disc_key, 1000, data, 2048);
    ps3_decrypt_sector(disc_key, 1000, data, 2048);

    EXPECT_EQ(0, memcmp(data, original, 2048));
}
