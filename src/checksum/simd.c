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

#include <stddef.h>
#include <stdint.h>

#include "aaruformat.h"
#include "log.h"

#if defined(__x86_64__) || defined(__amd64) || defined(_M_AMD64) || defined(_M_X64) || defined(__I386__) || \
    defined(__i386__) || defined(__THW_INTEL) || defined(_M_IX86)

#ifdef _MSC_VER
#include <intrin.h>
#else
/*
 * Newer versions of GCC and clang come with cpuid.h
 * (ftr GCC 4.7 in Debian Wheezy has this)
 */
#include <cpuid.h>

#endif

/**
 * @brief Executes the CPUID instruction to retrieve CPU information.
 *
 * Retrieves information about the CPU based on the specified info code.
 *
 * @param info The information code to query.
 * @param eax Pointer to store the EAX register result.
 * @param ebx Pointer to store the EBX register result.
 * @param ecx Pointer to store the ECX register result.
 * @param edx Pointer to store the EDX register result.
 */
static void cpuid(int info, unsigned *eax, unsigned *ebx, unsigned *ecx, unsigned *edx)
{
    TRACE("Entering cpuid(%d, %d, %d, %d, %d)", info, *eax, *ebx, *ecx, *edx);

#ifdef _MSC_VER
    unsigned int registers[4];
    __cpuid(registers, info);
    *eax = registers[0];
    *ebx = registers[1];
    *ecx = registers[2];
    *edx = registers[3];
#else
    /* GCC, clang */
    unsigned int _eax;
    unsigned int _ebx;
    unsigned int _ecx;
    unsigned int _edx;
    __cpuid(info, _eax, _ebx, _ecx, _edx);
    *eax = _eax;
    *ebx = _ebx;
    *ecx = _ecx;
    *edx = _edx;
#endif

    TRACE("Exiting cpuid(%d, %d, %d, %d, %d)", info, *eax, *ebx, *ecx, *edx);
}

/**
 * @brief Executes the CPUID instruction with the given info and count.
 *
 * @param info CPUID info code.
 * @param count CPUID count code.
 * @param eax Pointer to store EAX result.
 * @param ebx Pointer to store EBX result.
 * @param ecx Pointer to store ECX result.
 * @param edx Pointer to store EDX result.
 */
static void cpuidex(int info, int count, unsigned *eax, unsigned *ebx, unsigned *ecx, unsigned *edx)
{
    TRACE("Entering cpuidex(%d, %d, %d, %d, %d, %d)", info, count, *eax, *ebx, *ecx, *edx);

#ifdef _MSC_VER
    unsigned int registers[4];
    __cpuidex(registers, info, count);
    *eax = registers[0];
    *ebx = registers[1];
    *ecx = registers[2];
    *edx = registers[3];
#else
    /* GCC, clang */
    unsigned int _eax;
    unsigned int _ebx;
    unsigned int _ecx;
    unsigned int _edx;
    __cpuid_count(info, count, _eax, _ebx, _ecx, _edx);
    *eax = _eax;
    *ebx = _ebx;
    *ecx = _ecx;
    *edx = _edx;
#endif

    TRACE("Exiting cpuidex(%d, %d, %d, %d, %d, %d)", info, count, *eax, *ebx, *ecx, *edx);
}

/**
 * @brief Checks if the CPU supports PCLMULQDQ (carry-less multiplication) and SSE4.1.
 *
 * @return Non-zero if supported, zero otherwise.
 */
int have_clmul(void)
{
    TRACE("Entering have_clmul()");

    unsigned eax, ebx, ecx, edx;
    cpuid(1 /* feature bits */, &eax, &ebx, &ecx, &edx);

    int has_pclmulqdq = ecx & 0x2;     /* bit 1 */
    int has_sse41     = ecx & 0x80000; /* bit 19 */

    TRACE("Exiting have_clmul() = %d", has_pclmulqdq && has_sse41);
    return has_pclmulqdq && has_sse41;
}

/**
 * @brief Checks if the CPU supports SSSE3 instructions.
 *
 * @return Non-zero if supported, zero otherwise.
 */
int have_ssse3(void)
{
    TRACE("Entering have_ssse3()");
    unsigned eax, ebx, ecx, edx;
    cpuid(1 /* feature bits */, &eax, &ebx, &ecx, &edx);

    TRACE("Exiting have_ssse3() = %d", ecx & 0x200);
    return ecx & 0x200;
}

/**
 * @brief Checks if the CPU supports AVX2 instructions.
 *
 * @return Non-zero if supported, zero otherwise.
 */
int have_avx2(void)
{
    TRACE("Entering have_avx2()");
    unsigned eax, ebx, ecx, edx;
    cpuidex(7 /* extended feature bits */, 0, &eax, &ebx, &ecx, &edx);

    TRACE("Exiting have_avx2() = %d", ebx & 0x20);
    return ebx & 0x20;
}
#endif

#if defined(__aarch64__) || defined(_M_ARM64) || defined(__arm__) || defined(_M_ARM)
#if defined(_WIN32)
#include <windows.h>

#include <processthreadsapi.h>
#elif defined(__APPLE__)
#include <sys/sysctl.h>
#else
#include <sys/auxv.h>
#endif
#endif

#if (defined(__aarch64__) || defined(_M_ARM64) || defined(__arm__) || defined(_M_ARM)) && defined(__APPLE__)
/**
 * @brief Checks if the CPU supports NEON instructions on Apple platforms.
 *
 * @return Non-zero if supported, zero otherwise.
 */
int have_neon_apple(void)
{
    TRACE("Entering have_neon_apple()");
    int    value = 0;
    size_t len   = sizeof(int);
    int    ret   = sysctlbyname("hw.optional.neon", &value, &len, NULL, 0);

    if(ret != 0)
    {
        TRACE("Exiting have_neon_apple() = 0");
        return 0;
    }

    TRACE("Exiting have_neon_apple() = %d", value == 1);
    return value == 1;
}

/**
 * @brief Checks if the CPU supports CRC32 instructions on Apple platforms.
 *
 * @return Non-zero if supported, zero otherwise.
 */
int have_crc32_apple(void)
{
    TRACE("Entering have_crc32_apple()");
    int    value = 0;
    size_t len   = sizeof(int);
    int    ret   = sysctlbyname("hw.optional.crc32", &value, &len, NULL, 0);

    if(ret != 0)
    {
        TRACE("Exiting have_crc32_apple() = 0");
        return 0;
    }

    TRACE("Exiting have_crc32_apple() = %d", value == 1);
    return value == 1;
}

/**
 * @brief Checks if the CPU supports cryptographic instructions on Apple platforms.
 *
 * @return Non-zero if supported, zero otherwise.
 */
int have_crypto_apple(void) { return 0; }
#endif

#if defined(__aarch64__) || defined(_M_ARM64)
int have_neon(void)
{
    return 1;  // ARMv8-A made it mandatory
}

int have_arm_crc32(void)
{
#if defined(_WIN32)
    return IsProcessorFeaturePresent(PF_ARM_V8_CRC32_INSTRUCTIONS_AVAILABLE) != 0;
#elif defined(__APPLE__)
    return have_crc32_apple();
#else
    return getauxval(AT_HWCAP) & HWCAP_CRC32;
#endif
}

int have_arm_crypto(void)
{
#if defined(_WIN32)
    return IsProcessorFeaturePresent(PF_ARM_V8_CRYPTO_INSTRUCTIONS_AVAILABLE) != 0;
#elif defined(__APPLE__)
    return have_crypto_apple();
#else
    return getauxval(AT_HWCAP) & HWCAP_AES;
#endif
}
#endif

#if defined(__arm__) || defined(_M_ARM)
int have_neon(void)
{
#if defined(_WIN32)
    return IsProcessorFeaturePresent(PF_ARM_VFP_32_REGISTERS_AVAILABLE) != 0;
#elif defined(__APPLE__)
    return have_neon_apple();
#else
    return getauxval(AT_HWCAP) & HWCAP_NEON;
#endif
}

int have_arm_crc32(void)
{
#if defined(_WIN32)
    return IsProcessorFeaturePresent(PF_ARM_V8_CRC32_INSTRUCTIONS_AVAILABLE) != 0;
#elif defined(__APPLE__)
    return have_crc32_apple();
#else
    return getauxval(AT_HWCAP2) & HWCAP2_CRC32;
#endif
}

int have_arm_crypto(void)
{
#if defined(_WIN32)
    return IsProcessorFeaturePresent(PF_ARM_V8_CRYPTO_INSTRUCTIONS_AVAILABLE) != 0;
#elif defined(__APPLE__)
    return have_crypto_apple();
#else
    return getauxval(AT_HWCAP2) & HWCAP2_AES;
#endif
}
#endif
