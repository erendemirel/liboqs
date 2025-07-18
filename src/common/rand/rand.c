// SPDX-License-Identifier: MIT

#include <stdio.h>
#if defined(_WIN32)
#include <windows.h>
#include <wincrypt.h>
#define strcasecmp _stricmp
#else
#include <unistd.h>
#include <strings.h>
#if !defined(__APPLE__)
#include <unistd.h>
#endif
#endif
#include <fcntl.h>
#include <stdlib.h>

#include <oqs/oqs.h>

void OQS_randombytes_system(uint8_t *random_array, size_t bytes_to_read);
#ifdef OQS_USE_OPENSSL
void OQS_randombytes_openssl(uint8_t *random_array, size_t bytes_to_read);
#endif

#ifdef OQS_USE_OPENSSL
#include "../ossl_helpers.h"
// Use OpenSSL's RAND_bytes as the default PRNG
static void (*oqs_randombytes_algorithm)(uint8_t *, size_t) = &OQS_randombytes_openssl;
#else
static void (*oqs_randombytes_algorithm)(uint8_t *, size_t) = &OQS_randombytes_system;
#endif
OQS_API OQS_STATUS OQS_randombytes_switch_algorithm(const char *algorithm) {
	if (0 == strcasecmp(OQS_RAND_alg_system, algorithm)) {
		oqs_randombytes_algorithm = &OQS_randombytes_system;
		return OQS_SUCCESS;
	} else if (0 == strcasecmp(OQS_RAND_alg_openssl, algorithm)) {
#ifdef OQS_USE_OPENSSL
		oqs_randombytes_algorithm = &OQS_randombytes_openssl;
		return OQS_SUCCESS;
#else
		return OQS_ERROR;
#endif
	} else {
		return OQS_ERROR;
	}
}

OQS_API void OQS_randombytes_custom_algorithm(void (*algorithm_ptr)(uint8_t *, size_t)) {
	oqs_randombytes_algorithm = algorithm_ptr;
}

OQS_API void OQS_randombytes(uint8_t *random_array, size_t bytes_to_read) {
	oqs_randombytes_algorithm(random_array, bytes_to_read);
}

// Select the implementation for OQS_randombytes_system
#if defined(_WIN32)
void OQS_randombytes_system(uint8_t *random_array, size_t bytes_to_read) {
	HCRYPTPROV hCryptProv;
	if (!CryptAcquireContext(&hCryptProv, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT) ||
	        !CryptGenRandom(hCryptProv, (DWORD) bytes_to_read, random_array)) {
		exit(EXIT_FAILURE); // better to fail than to return bad random data
	}
	CryptReleaseContext(hCryptProv, 0);
}
#elif defined(__APPLE__)
void OQS_randombytes_system(uint8_t *random_array, size_t bytes_to_read) {
	arc4random_buf(random_array, bytes_to_read);
}
#elif defined(OQS_EMBEDDED_BUILD)
void OQS_randombytes_system(uint8_t *random_array, size_t bytes_to_read) {
	fprintf(stderr, "OQS_randombytes_system is not available in an embedded build.\n");
	fprintf(stderr, "Call OQS_randombytes_custom_algorithm() to set a custom method for your system.\n");
	exit(EXIT_FAILURE);
}
#elif defined(OQS_HAVE_GETENTROPY)
void OQS_randombytes_system(uint8_t *random_array, size_t bytes_to_read) {
	while (bytes_to_read > 256) {
		if (getentropy(random_array, 256)) {
			exit(EXIT_FAILURE);
		}
		random_array += 256;
		bytes_to_read -= 256;
	}
	if (getentropy(random_array, bytes_to_read)) {
		exit(EXIT_FAILURE);
	}
}
#else
void OQS_randombytes_system(uint8_t *random_array, size_t bytes_to_read) {
	FILE *handle;
	size_t bytes_read;

	handle = fopen("/dev/urandom", "rb");
	if (!handle) {
		perror("OQS_randombytes");
		exit(EXIT_FAILURE);
	}

	bytes_read = fread(random_array, 1, bytes_to_read, handle);
	if (bytes_read < bytes_to_read || ferror(handle)) {
		perror("OQS_randombytes");
		exit(EXIT_FAILURE);
	}

	fclose(handle);
}
#endif

#ifdef OQS_USE_OPENSSL
#define OQS_RAND_POLL_RETRY 3 // in case failure to get randomness is a temporary problem, allow some repeats
void OQS_randombytes_openssl(uint8_t *random_array, size_t bytes_to_read) {
	int rep = OQS_RAND_POLL_RETRY;
	SIZE_T_TO_INT_OR_EXIT(bytes_to_read, bytes_to_read_int)
	do {
		if (OQS_OSSL_FUNC(RAND_status)() == 1) {
			break;
		}
		OQS_OSSL_FUNC(RAND_poll)();
	} while (rep-- >= 0);
	if (OQS_OSSL_FUNC(RAND_bytes)(random_array, bytes_to_read_int) != 1) {
		fprintf(stderr, "No OpenSSL randomness retrieved. DRBG available?\n");
		// because of void signature we have no other way to signal the problem
		// we cannot possibly return without randomness
		exit(EXIT_FAILURE);
	}
}
#endif
