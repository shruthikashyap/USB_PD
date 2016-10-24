/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef __EC_INCLUDE_TRNG_H
#define __EC_INCLUDE_TRNG_H

#include <stddef.h>

/**
 * Initialize the true random number generator.
 *
 * Not supported by all platforms.
 **/
void init_trng(void);

/**
 * Retrieve a 32 bit random value.
 *
 * Not supported on all platforms.
 **/
uint32_t rand(void);

/**
 * Output len random bytes into buffer.
 *
 * Not supported on all platforms.
 **/
void rand_bytes(void *buffer, size_t len);

#endif /* __EC_INCLUDE_TRNG_H */
