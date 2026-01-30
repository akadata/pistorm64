// SPDX-License-Identifier: MIT
#include "pi_crypto.h"

#include <openssl/evp.h>
#include <openssl/rand.h>

int pi_random_bytes(uint8_t *buf, size_t len) {
  if (!buf || len == 0) {
    return 0;
  }
  return RAND_bytes(buf, (int)len) == 1;
}

int pi_aes256_gcm_encrypt(const uint8_t *key, const uint8_t *iv, size_t iv_len,
                          const uint8_t *aad, size_t aad_len,
                          const uint8_t *plaintext, size_t plaintext_len,
                          uint8_t *ciphertext, uint8_t tag[16]) {
  int ret = 0;
  int len = 0;
  int outlen = 0;
  EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
  if (!ctx) {
    return 0;
  }

  if (EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL) != 1) {
    goto out;
  }
  if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, (int)iv_len, NULL) != 1) {
    goto out;
  }
  if (EVP_EncryptInit_ex(ctx, NULL, NULL, key, iv) != 1) {
    goto out;
  }

  if (aad && aad_len > 0) {
    if (EVP_EncryptUpdate(ctx, NULL, &len, aad, (int)aad_len) != 1) {
      goto out;
    }
  }

  if (plaintext_len > 0) {
    if (EVP_EncryptUpdate(ctx, ciphertext, &len, plaintext,
                          (int)plaintext_len) != 1) {
      goto out;
    }
    outlen = len;
  }

  if (EVP_EncryptFinal_ex(ctx, ciphertext + outlen, &len) != 1) {
    goto out;
  }
  outlen += len;

  if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, tag) != 1) {
    goto out;
  }

  ret = 1;

out:
  EVP_CIPHER_CTX_free(ctx);
  return ret;
}

int pi_aes256_gcm_decrypt(const uint8_t *key, const uint8_t *iv, size_t iv_len,
                          const uint8_t *aad, size_t aad_len,
                          const uint8_t *ciphertext, size_t ciphertext_len,
                          const uint8_t tag[16], uint8_t *plaintext) {
  int ret = 0;
  int len = 0;
  int outlen = 0;
  EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
  if (!ctx) {
    return 0;
  }

  if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL) != 1) {
    goto out;
  }
  if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, (int)iv_len, NULL) != 1) {
    goto out;
  }
  if (EVP_DecryptInit_ex(ctx, NULL, NULL, key, iv) != 1) {
    goto out;
  }

  if (aad && aad_len > 0) {
    if (EVP_DecryptUpdate(ctx, NULL, &len, aad, (int)aad_len) != 1) {
      goto out;
    }
  }

  if (ciphertext_len > 0) {
    if (EVP_DecryptUpdate(ctx, plaintext, &len, ciphertext,
                          (int)ciphertext_len) != 1) {
      goto out;
    }
    outlen = len;
  }

  if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, 16, (void *)tag) != 1) {
    goto out;
  }

  if (EVP_DecryptFinal_ex(ctx, plaintext + outlen, &len) != 1) {
    goto out;
  }
  outlen += len;

  ret = 1;

out:
  EVP_CIPHER_CTX_free(ctx);
  return ret;
}
