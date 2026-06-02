// stats.h — runtime counters and heap telemetry for diagnostics.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "protocol.h"

void stats_begin();
void stats_loop();

void stats_note_queue_drop();
void stats_note_emit();

// Non-blocking enqueue; counts drops on failure.
bool stats_queue_send(const Detection &d);

uint32_t stats_queue_drops();
uint32_t stats_emits();
uint32_t stats_free_heap();
uint32_t stats_heap_total();
uint32_t stats_heap_used();
unsigned stats_heap_used_percent();

// e.g. "0.18 MB (68%)" into out (NUL-terminated).
void stats_format_ram(char *out, size_t outsz);

// JSON object body (no trailing newline) into buf; returns bytes written.
size_t stats_format_json(char *buf, size_t bufsz);
