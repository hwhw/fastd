/*
  Copyright (c) 2012-2013, Matthias Schiffer <mschiffer@universe-factory.net>
  All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are met:

    1. Redistributions of source code must retain the above copyright notice,
       this list of conditions and the following disclaimer.
    2. Redistributions in binary form must reproduce the above copyright notice,
       this list of conditions and the following disclaimer in the documentation
       and/or other materials provided with the distribution.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
  DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
  SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
  OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/


#ifndef _FASTD_SHA256_H_
#define _FASTD_SHA256_H_

#include <stdbool.h>
#include <stdint.h>


#define FASTD_SHA256_HASH_WORDS 8
#define FASTD_SHA256_BLOCK_WORDS 8

#define FASTD_HMACSHA256_KEY_WORDS 8

#define FASTD_SHA256_HASH_BYTES (4*FASTD_SHA256_HASH_WORDS)
#define FASTD_SHA256_BLOCK_BYTES (4*FASTD_SHA256_BLOCK_WORDS)

#define FASTD_HMACSHA256_KEY_BYTES (4*FASTD_HMACSHA256_KEY_WORDS)


void fastd_sha256_blocks(uint32_t out[FASTD_SHA256_HASH_WORDS], ...);
void fastd_hmacsha256_blocks(uint32_t out[FASTD_SHA256_HASH_WORDS], const uint32_t key[FASTD_HMACSHA256_KEY_WORDS], ...);
bool fastd_hmacsha256_blocks_verify(const uint8_t mac[FASTD_SHA256_HASH_BYTES], const uint32_t key[FASTD_HMACSHA256_KEY_WORDS], ...);

#endif /* _FASTD_SHA256_H_ */
