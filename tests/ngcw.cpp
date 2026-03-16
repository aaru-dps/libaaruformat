/*
 * This file is part of the Aaru Data Preservation Suite.
 * Copyright (c) 2019-2026 Natalia Portillo.
 *
 * Tests for Nintendo GameCube/Wii LFG PRNG, junk map serialization,
 * and Wii partition key map serialization.
 * Uses synthetic data only (no disc images due to copyright).
 */

#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "gtest/gtest.h"

extern "C"
{
#include "../src/lib/aes128.h"
#include "../src/ngcw/lfg.h"
#include "../src/ngcw/ngcw_junk.h"
#include "../src/ngcw/wii_crypto.h"
}

/* ================================================================== */
/*  LFG PRNG Tests                                                     */
/* ================================================================== */

/* Known seed for testing — arbitrary 17-word seed */
static const uint32_t test_seed[NGC_LFG_SEED_SIZE] = {
    0x01234567, 0x89ABCDEF, 0xFEDCBA98, 0x76543210, 0xDEADBEEF, 0xCAFEBABE, 0x12345678, 0x9ABCDEF0, 0x0FEDCBA9,
    0x87654321, 0xBEEFCAFE, 0xBABEFACE, 0x11223344, 0x55667788, 0x99AABBCC, 0xDDEEFF00, 0xA5A5A5A5};

TEST(LFG, SetSeedAndGenerate)
{
    struct ngc_lfg_ctx lfg;
    ngc_lfg_set_seed(&lfg, test_seed);

    /* Generate some bytes */
    uint8_t out[256];
    ngc_lfg_get_bytes(&lfg, out, sizeof(out));

    /* Output should not be all zeros */
    bool all_zero = true;
    for(size_t i = 0; i < sizeof(out); i++)
    {
        if(out[i] != 0)
        {
            all_zero = false;
            break;
        }
    }
    EXPECT_FALSE(all_zero);
}

TEST(LFG, Deterministic)
{
    struct ngc_lfg_ctx lfg1, lfg2;
    ngc_lfg_set_seed(&lfg1, test_seed);
    ngc_lfg_set_seed(&lfg2, test_seed);

    uint8_t out1[1024], out2[1024];
    ngc_lfg_get_bytes(&lfg1, out1, sizeof(out1));
    ngc_lfg_get_bytes(&lfg2, out2, sizeof(out2));

    EXPECT_EQ(0, memcmp(out1, out2, sizeof(out1)));
}

TEST(LFG, ChunkedOutputMatchesBulk)
{
    struct ngc_lfg_ctx lfg1, lfg2;
    ngc_lfg_set_seed(&lfg1, test_seed);
    ngc_lfg_set_seed(&lfg2, test_seed);

    /* Bulk read */
    uint8_t bulk[4096];
    ngc_lfg_get_bytes(&lfg1, bulk, sizeof(bulk));

    /* Chunked read: various chunk sizes */
    uint8_t chunked[4096];
    size_t  pos           = 0;
    size_t  chunk_sizes[] = {1, 7, 13, 64, 100, 521 * 4 - 1, 3, 2048};
    int     ci            = 0;

    while(pos < sizeof(chunked))
    {
        size_t chunk = chunk_sizes[ci % (sizeof(chunk_sizes) / sizeof(chunk_sizes[0]))];
        if(pos + chunk > sizeof(chunked)) chunk = sizeof(chunked) - pos;
        ngc_lfg_get_bytes(&lfg2, chunked + pos, chunk);
        pos += chunk;
        ci++;
    }

    EXPECT_EQ(0, memcmp(bulk, chunked, sizeof(bulk)));
}

TEST(LFG, SeedExtraction)
{
    struct ngc_lfg_ctx lfg;
    ngc_lfg_set_seed(&lfg, test_seed);

    /* Generate a large buffer of known LFG output */
    size_t   buf_size = NGC_LFG_K * 4 * 2; /* 2 full buffer lengths */
    uint8_t *buf      = (uint8_t *)malloc(buf_size);
    ASSERT_NE(nullptr, buf);

    ngc_lfg_get_bytes(&lfg, buf, buf_size);

    /* Try to extract seed from offset 0 */
    uint32_t extracted_seed[NGC_LFG_SEED_SIZE];
    size_t   matched = ngc_lfg_get_seed(buf, buf_size, 0, extracted_seed);

    EXPECT_GT(matched, (size_t)0);

    if(matched > 0)
    {
        /* Verify extracted seed produces same output */
        struct ngc_lfg_ctx lfg2;
        ngc_lfg_set_seed(&lfg2, extracted_seed);

        uint8_t *verify = (uint8_t *)malloc(buf_size);
        ASSERT_NE(nullptr, verify);

        ngc_lfg_get_bytes(&lfg2, verify, buf_size);
        EXPECT_EQ(0, memcmp(buf, verify, matched));

        free(verify);
    }

    free(buf);
}

TEST(LFG, SeedExtractionWithOffset)
{
    struct ngc_lfg_ctx lfg;
    ngc_lfg_set_seed(&lfg, test_seed);

    /* Generate a large buffer starting from a known offset */
    size_t offset   = 2048; /* simulate a sector-aligned offset */
    size_t buf_size = NGC_LFG_K * 4 * 2;

    /* Generate and discard the first `offset` bytes */
    uint8_t discard[2048];
    ngc_lfg_get_bytes(&lfg, discard, offset);

    /* Now generate the data we'll test */
    uint8_t *buf = (uint8_t *)malloc(buf_size);
    ASSERT_NE(nullptr, buf);
    ngc_lfg_get_bytes(&lfg, buf, buf_size);

    /* Try to extract seed at the given offset */
    uint32_t extracted_seed[NGC_LFG_SEED_SIZE];
    size_t   matched = ngc_lfg_get_seed(buf, buf_size, offset, extracted_seed);

    EXPECT_GT(matched, (size_t)0);

    if(matched > 0)
    {
        /* Verify: seed + advance to offset should give same data */
        struct ngc_lfg_ctx lfg2;
        ngc_lfg_set_seed(&lfg2, extracted_seed);

        uint8_t skip[2048];
        ngc_lfg_get_bytes(&lfg2, skip, offset);

        uint8_t *verify = (uint8_t *)malloc(matched);
        ASSERT_NE(nullptr, verify);
        ngc_lfg_get_bytes(&lfg2, verify, matched);

        EXPECT_EQ(0, memcmp(buf, verify, matched));

        free(verify);
    }

    free(buf);
}

/* ================================================================== */
/*  Junk Map Serialization Tests                                       */
/* ================================================================== */

TEST(JunkMap, SerializeDeserializeEmpty)
{
    uint8_t *data   = NULL;
    uint32_t length = 0;

    int32_t ret = ngcw_serialize_junk_map(NULL, 0, &data, &length);
    EXPECT_EQ(0, ret);
    EXPECT_NE(nullptr, data);
    EXPECT_EQ(8u, length); /* header only */

    NgcwJunkEntry *entries   = NULL;
    uint32_t       count     = 0;
    uint16_t       seed_size = 0;

    ret = ngcw_deserialize_junk_map(data, length, &entries, &count, &seed_size);
    EXPECT_EQ(0, ret);
    EXPECT_EQ(0u, count);
    EXPECT_EQ(NGC_LFG_SEED_SIZE, seed_size);
    EXPECT_EQ(nullptr, entries);

    free(data);
}

TEST(JunkMap, SerializeDeserializeRoundTrip)
{
    NgcwJunkEntry entries[3];

    entries[0].offset          = 0x1000;
    entries[0].length          = 0x800;
    entries[0].partition_index = 0xFFFF;
    memset(entries[0].seed, 0xAA, sizeof(entries[0].seed));

    entries[1].offset          = 0x8000;
    entries[1].length          = 0x7C00;
    entries[1].partition_index = 0;
    memset(entries[1].seed, 0xBB, sizeof(entries[1].seed));

    entries[2].offset          = 0x100000;
    entries[2].length          = 0x2000;
    entries[2].partition_index = 1;
    memset(entries[2].seed, 0xCC, sizeof(entries[2].seed));

    uint8_t *data   = NULL;
    uint32_t length = 0;
    int32_t  ret    = ngcw_serialize_junk_map(entries, 3, &data, &length);
    EXPECT_EQ(0, ret);
    EXPECT_NE(nullptr, data);

    /* Expected size: 8 header + 3 * 86 = 266 */
    EXPECT_EQ(266u, length);

    NgcwJunkEntry *out_entries = NULL;
    uint32_t       out_count   = 0;
    uint16_t       out_seed    = 0;

    ret = ngcw_deserialize_junk_map(data, length, &out_entries, &out_count, &out_seed);
    EXPECT_EQ(0, ret);
    EXPECT_EQ(3u, out_count);
    EXPECT_EQ(NGC_LFG_SEED_SIZE, out_seed);

    for(int i = 0; i < 3; i++)
    {
        EXPECT_EQ(entries[i].offset, out_entries[i].offset);
        EXPECT_EQ(entries[i].length, out_entries[i].length);
        EXPECT_EQ(entries[i].partition_index, out_entries[i].partition_index);
        EXPECT_EQ(0, memcmp(entries[i].seed, out_entries[i].seed, sizeof(entries[i].seed)));
    }

    free(data);
    free(out_entries);
}

TEST(JunkMap, DeserializeTruncatedFails)
{
    uint8_t        truncated[4] = {0x01, 0x00, 0x01, 0x00}; /* incomplete header */
    NgcwJunkEntry *entries      = NULL;
    uint32_t       count        = 0;
    uint16_t       seed         = 0;

    int32_t ret = ngcw_deserialize_junk_map(truncated, sizeof(truncated), &entries, &count, &seed);
    EXPECT_NE(0, ret);
}

/* ================================================================== */
/*  Junk Regeneration Test                                             */
/* ================================================================== */

TEST(JunkMap, RegenerateMatchesLFG)
{
    /* Generate known LFG output */
    struct ngc_lfg_ctx lfg;
    ngc_lfg_set_seed(&lfg, test_seed);

    uint8_t expected[2048];
    ngc_lfg_get_bytes(&lfg, expected, sizeof(expected));

    /* Build a junk entry for offset 0, length 2048 */
    NgcwJunkEntry entry;
    entry.offset          = 0;
    entry.length          = 2048;
    entry.partition_index = 0xFFFF;
    memcpy(entry.seed, test_seed, sizeof(entry.seed));

    /* Regenerate */
    uint8_t output[2048];
    int     ret = ngcw_regenerate_junk_sector(&entry, 1, 0, output, 2048);
    EXPECT_EQ(0, ret);
    EXPECT_EQ(0, memcmp(expected, output, 2048));
}

TEST(JunkMap, RegenerateAtOffset)
{
    /* Generate known LFG output, skip first 4096 bytes */
    struct ngc_lfg_ctx lfg;
    ngc_lfg_set_seed(&lfg, test_seed);

    uint8_t discard[4096];
    ngc_lfg_get_bytes(&lfg, discard, sizeof(discard));

    uint8_t expected[2048];
    ngc_lfg_get_bytes(&lfg, expected, sizeof(expected));

    /* Build a junk entry starting at offset 0 with length covering both areas */
    NgcwJunkEntry entry;
    entry.offset          = 0;
    entry.length          = 4096 + 2048;
    entry.partition_index = 0xFFFF;
    memcpy(entry.seed, test_seed, sizeof(entry.seed));

    /* Regenerate at offset 4096 */
    uint8_t output[2048];
    int     ret = ngcw_regenerate_junk_sector(&entry, 1, 4096, output, 2048);
    EXPECT_EQ(0, ret);
    EXPECT_EQ(0, memcmp(expected, output, 2048));
}

TEST(JunkMap, RegenerateNotFoundReturnsError)
{
    NgcwJunkEntry entry;
    entry.offset          = 0x10000;
    entry.length          = 0x800;
    entry.partition_index = 0xFFFF;
    memset(entry.seed, 0, sizeof(entry.seed));

    uint8_t output[2048];
    int     ret = ngcw_regenerate_junk_sector(&entry, 1, 0x5000, output, 2048);
    EXPECT_EQ(-1, ret); /* not found */
}

/* ================================================================== */
/*  Wii Partition Key Map Serialization Tests                          */
/* ================================================================== */

TEST(WiiCrypto, KeyMapSerializeDeserializeRoundTrip)
{
    WiiPartitionRegion regions[2];
    regions[0].start_sector = 100;
    regions[0].end_sector   = 500;
    memset(regions[0].key, 0x11, 16);

    regions[1].start_sector = 600;
    regions[1].end_sector   = 1000;
    memset(regions[1].key, 0x22, 16);

    uint8_t *data   = NULL;
    uint32_t length = 0;
    int32_t  ret    = wii_serialize_partition_key_map(regions, 2, &data, &length);
    EXPECT_EQ(0, ret);
    EXPECT_NE(nullptr, data);
    EXPECT_EQ(52u, length); /* 4 + 2*24 */

    WiiPartitionRegion *out_regions = NULL;
    uint32_t            out_count   = 0;
    ret                             = wii_deserialize_partition_key_map(data, length, &out_regions, &out_count);
    EXPECT_EQ(0, ret);
    EXPECT_EQ(2u, out_count);

    for(int i = 0; i < 2; i++)
    {
        EXPECT_EQ(regions[i].start_sector, out_regions[i].start_sector);
        EXPECT_EQ(regions[i].end_sector, out_regions[i].end_sector);
        EXPECT_EQ(0, memcmp(regions[i].key, out_regions[i].key, 16));
    }

    free(data);
    free(out_regions);
}

TEST(WiiCrypto, KeyMapInvalidRangeFails)
{
    WiiPartitionRegion region;
    region.start_sector = 500;
    region.end_sector   = 100; /* invalid: start > end */
    memset(region.key, 0, 16);

    uint8_t *data   = NULL;
    uint32_t length = 0;
    wii_serialize_partition_key_map(&region, 1, &data, &length);

    WiiPartitionRegion *out = NULL;
    uint32_t            cnt = 0;
    int32_t             ret = wii_deserialize_partition_key_map(data, length, &out, &cnt);
    EXPECT_NE(0, ret);

    free(data);
}

/* ================================================================== */
/*  Wii Group Encrypt/Decrypt Round-Trip Test                          */
/* ================================================================== */

TEST(WiiCrypto, GroupEncryptDecryptRoundTrip)
{
    uint8_t key[16];
    memset(key, 0x42, 16);

    /* Create synthetic hash block and data */
    uint8_t hash_block[WII_GROUP_HASH_SIZE];
    uint8_t data_in[WII_GROUP_DATA_SIZE];

    for(int i = 0; i < WII_GROUP_HASH_SIZE; i++) hash_block[i] = (uint8_t)(i & 0xFF);
    for(int i = 0; i < WII_GROUP_DATA_SIZE; i++) data_in[i] = (uint8_t)((i * 7 + 3) & 0xFF);

    /* Encrypt */
    uint8_t encrypted[WII_GROUP_SIZE];
    wii_encrypt_group(key, hash_block, data_in, encrypted);

    /* Encrypted should differ from plaintext */
    EXPECT_NE(0, memcmp(encrypted, hash_block, WII_GROUP_HASH_SIZE));

    /* Decrypt */
    uint8_t hash_out[WII_GROUP_HASH_SIZE];
    uint8_t data_out[WII_GROUP_DATA_SIZE];
    wii_decrypt_group(key, encrypted, hash_out, data_out);

    /* Should match originals */
    EXPECT_EQ(0, memcmp(hash_block, hash_out, WII_GROUP_HASH_SIZE));
    EXPECT_EQ(0, memcmp(data_in, data_out, WII_GROUP_DATA_SIZE));
}

TEST(WiiCrypto, SectorKeyLookup)
{
    WiiPartitionRegion regions[2];
    regions[0].start_sector = 10;
    regions[0].end_sector   = 100;
    memset(regions[0].key, 0xAA, 16);

    regions[1].start_sector = 200;
    regions[1].end_sector   = 300;
    memset(regions[1].key, 0xBB, 16);

    /* Sector in first partition's header (plaintext) */
    EXPECT_EQ(nullptr, wii_get_sector_key(regions, 2, 10 * WII_LOGICAL_PER_GROUP));

    /* Sector in first partition's data */
    const uint8_t *key = wii_get_sector_key(regions, 2, 11 * WII_LOGICAL_PER_GROUP);
    ASSERT_NE(nullptr, key);
    EXPECT_EQ(0xAA, key[0]);

    /* Sector in second partition */
    key = wii_get_sector_key(regions, 2, 201 * WII_LOGICAL_PER_GROUP);
    ASSERT_NE(nullptr, key);
    EXPECT_EQ(0xBB, key[0]);

    /* Sector outside any partition (plaintext) */
    EXPECT_EQ(nullptr, wii_get_sector_key(regions, 2, 150 * WII_LOGICAL_PER_GROUP));

    /* Encrypted check */
    EXPECT_TRUE(wii_is_sector_encrypted(regions, 2, 50 * WII_LOGICAL_PER_GROUP));
    EXPECT_FALSE(wii_is_sector_encrypted(regions, 2, 150 * WII_LOGICAL_PER_GROUP));
}
