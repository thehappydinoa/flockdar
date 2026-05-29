#include "signing.h"

#include <string.h>

#include "config.h"
#include "mbedtls/md.h"

void hmac_sig(const char *data, size_t len, char out[9]) {
  unsigned char digest[32];
  const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);

  mbedtls_md_context_t ctx;
  mbedtls_md_init(&ctx);
  mbedtls_md_setup(&ctx, info, /*hmac=*/1);
  mbedtls_md_hmac_starts(&ctx, (const unsigned char *)FD_HMAC_KEY,
                         strlen(FD_HMAC_KEY));
  mbedtls_md_hmac_update(&ctx, (const unsigned char *)data, len);
  mbedtls_md_hmac_finish(&ctx, digest);
  mbedtls_md_free(&ctx);

  // First 4 bytes -> 8 hex chars.
  static const char hexd[] = "0123456789abcdef";
  for (int i = 0; i < 4; i++) {
    out[i * 2] = hexd[(digest[i] >> 4) & 0xF];
    out[i * 2 + 1] = hexd[digest[i] & 0xF];
  }
  out[8] = '\0';
}
