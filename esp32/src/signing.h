// signing.h — HMAC-SHA256 frame signing.
//
// The signature lets the Python receiver reject corrupted or spoofed frames.
// It is computed over the complete JSON object *without* the sig field, then
// truncated to the first 4 bytes (8 lowercase hex chars).
//
// Verification on the Python side: take the raw line and strip the trailing
//   ,"sig":"<8 hex>"
// (i.e. regex-replace  ,"sig":"[0-9a-f]{8}"}  with  }  ), then recompute the
// HMAC over that reconstructed object with the shared key and compare.
#pragma once

#include <stddef.h>

void signing_begin();
void signing_end();

// Writes 8 lowercase hex chars + NUL into out (>= 9 bytes).
void hmac_sig(const char *data, size_t len, char out[9]);
