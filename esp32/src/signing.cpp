#include "signing.h"

#include <string.h>

#include "config.h"
#include "mbedtls/md.h"

static mbedtls_md_context_t s_ctx;
static bool s_ctx_ready = false;

void signing_begin() {
  if (s_ctx_ready) {
    return;
  }
  const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  mbedtls_md_init(&s_ctx);
  mbedtls_md_setup(&s_ctx, info, /*hmac=*/1);
  s_ctx_ready = true;
}

void signing_end() {
  if (!s_ctx_ready) {
    return;
  }
  mbedtls_md_free(&s_ctx);
  s_ctx_ready = false;
}

void hmac_sig(const char *data, size_t len, char out[9]) {
  unsigned char digest[32];
  if (!s_ctx_ready) {
    signing_begin();
  }
  mbedtls_md_hmac_reset(&s_ctx);
  mbedtls_md_hmac_starts(&s_ctx, (const unsigned char *)FD_HMAC_KEY,
                         strlen(FD_HMAC_KEY));
  mbedtls_md_hmac_update(&s_ctx, (const unsigned char *)data, len);
  mbedtls_md_hmac_finish(&s_ctx, digest);

  // First 4 bytes -> 8 hex chars.
  static const char hexd[] = "0123456789abcdef";
  for (int i = 0; i < 4; i++) {
    out[i * 2] = hexd[(digest[i] >> 4) & 0xF];
    out[i * 2 + 1] = hexd[digest[i] & 0xF];
  }
  out[8] = '\0';
}
