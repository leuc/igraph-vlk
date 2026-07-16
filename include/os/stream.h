/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef OS_STREAM_H
#define OS_STREAM_H

#include <stdbool.h>

// Background stdin line reader + thread-safe FIFO queue.
// The reader thread does pure string I/O only (no igraph calls); it is safe
// to use from any single consumer thread that polls it non-blockingly.
typedef struct OsStreamReader OsStreamReader;

// Returns true if stdin is not a terminal (piped or redirected). Call once
// at startup to decide whether to enter streaming mode.
bool os_stream_stdin_is_piped(void);

// Starts a detached background thread that blockingly reads newline-
// delimited lines from stdin into a thread-safe FIFO queue. Returns NULL on
// failure (thread/mutex creation or allocation failure).
OsStreamReader *os_stream_reader_start(void);

// Non-blocking. Pops up to max_lines queued lines into out_lines (trailing
// \n/\r already stripped). Each popped line is a heap string; ownership
// transfers to the caller, who must free() each one. Returns the number of
// lines popped (0 if the queue is currently empty). Safe to call every
// frame from the consumer thread.
int os_stream_reader_poll(OsStreamReader *reader, char **out_lines, int max_lines);

// True once the reader thread has hit EOF/error on stdin AND the queue has
// been fully drained by the consumer — i.e. no more data will ever arrive.
bool os_stream_reader_at_eof(OsStreamReader *reader);

// Best-effort: asks the reader thread to stop enqueueing and exit at its
// next opportunity. Does NOT interrupt an in-flight blocking read on stdin.
// Does not block.
void os_stream_reader_request_stop(OsStreamReader *reader);

// Frees the reader's queue/mutex and the OsStreamReader itself. Only call
// once os_stream_reader_at_eof() is true (the reader thread has returned
// and will not touch shared state again); if the reader may still be
// blocked in a read(), this is skipped by the caller rather than risking a
// use-after-free of the detached thread's context.
void os_stream_reader_destroy(OsStreamReader *reader);

#endif // OS_STREAM_H
