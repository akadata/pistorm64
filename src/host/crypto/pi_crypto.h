// SPDX-License-Identifier: MIT
#pragma once

#include <stddef.h>
#include <stdint.h>

int pi_random_bytes(uint8_t *buf, size_t len);

int pi_aes256_gcm_encrypt(
    const uint8_t *key,
    const uint8_t *iv,
    size_t iv_len,
    const uint8_t *aad,
    size_t aad_len,
    const uint8_t *plaintext,
    size_t plaintext_len,
    uint8_t *ciphertext,
    uint8_t tag[16]);

int pi_aes256_gcm_decrypt(
    const uint8_t *key,
    const uint8_t *iv,
    size_t iv_len,
    const uint8_t *aad,
    size_t aad_len,
    const uint8_t *ciphertext,
    size_t ciphertext_len,
    const uint8_t tag[16],
    uint8_t *plaintext);
