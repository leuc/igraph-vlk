/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#define _GNU_SOURCE
#include "os/stream.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// ============================================================================
// Internal types
// ============================================================================

typedef struct StreamLineNode
{
	char *line;
	struct StreamLineNode *next;
} StreamLineNode;

struct OsStreamReader
{
	pthread_t thread;
	pthread_mutex_t queue_mutex;
	StreamLineNode *head;
	StreamLineNode *tail;
	int queue_len;
	_Atomic bool stop_requested;
	_Atomic bool eof_reached;
};

// ============================================================================
// stdin detection
// ============================================================================

bool os_stream_stdin_is_piped(void)
{
	return !isatty(fileno(stdin));
}

// ============================================================================
// Reader thread
// ============================================================================

static void *reader_thread_func(void *arg)
{
	OsStreamReader *r = (OsStreamReader *)arg;
	char *buf = NULL;
	size_t buf_cap = 0;

	while (!atomic_load_explicit(&r->stop_requested, memory_order_relaxed)) {
		ssize_t len = getline(&buf, &buf_cap, stdin);
		if (len < 0)
			break; // EOF or read error

		while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r'))
			buf[--len] = '\0';

		char *owned = strdup(buf);
		if (!owned) {
			fprintf(stderr, "[os_stream] strdup failed, dropping line\n");
			continue;
		}
		StreamLineNode *node = malloc(sizeof(StreamLineNode));
		if (!node) {
			fprintf(stderr, "[os_stream] malloc failed, dropping line\n");
			free(owned);
			continue;
		}
		node->line = owned;
		node->next = NULL;

		pthread_mutex_lock(&r->queue_mutex);
		if (r->tail)
			r->tail->next = node;
		else
			r->head = node;
		r->tail = node;
		r->queue_len++;
		pthread_mutex_unlock(&r->queue_mutex);
	}

	free(buf);
	atomic_store_explicit(&r->eof_reached, true, memory_order_release);
	return NULL;
}

OsStreamReader *os_stream_reader_start(void)
{
	OsStreamReader *r = malloc(sizeof(OsStreamReader));
	if (!r)
		return NULL;

	r->head = NULL;
	r->tail = NULL;
	r->queue_len = 0;
	atomic_init(&r->stop_requested, false);
	atomic_init(&r->eof_reached, false);

	if (pthread_mutex_init(&r->queue_mutex, NULL) != 0) {
		free(r);
		return NULL;
	}
	if (pthread_create(&r->thread, NULL, reader_thread_func, r) != 0) {
		pthread_mutex_destroy(&r->queue_mutex);
		free(r);
		return NULL;
	}
	pthread_detach(r->thread); // never joined — see os_stream_reader_destroy

	return r;
}

// ============================================================================
// Polling / lifecycle
// ============================================================================

int os_stream_reader_poll(OsStreamReader *reader, char **out_lines, int max_lines)
{
	if (!reader || max_lines <= 0)
		return 0;

	int popped = 0;
	pthread_mutex_lock(&reader->queue_mutex);
	while (popped < max_lines && reader->head) {
		StreamLineNode *node = reader->head;
		reader->head = node->next;
		if (!reader->head)
			reader->tail = NULL;
		reader->queue_len--;
		out_lines[popped++] = node->line;
		free(node);
	}
	pthread_mutex_unlock(&reader->queue_mutex);
	return popped;
}

bool os_stream_reader_at_eof(OsStreamReader *reader)
{
	if (!reader)
		return true;
	if (!atomic_load_explicit(&reader->eof_reached, memory_order_acquire))
		return false;

	pthread_mutex_lock(&reader->queue_mutex);
	bool drained = (reader->queue_len == 0);
	pthread_mutex_unlock(&reader->queue_mutex);
	return drained;
}

void os_stream_reader_request_stop(OsStreamReader *reader)
{
	if (reader)
		atomic_store_explicit(&reader->stop_requested, true, memory_order_relaxed);
}

void os_stream_reader_destroy(OsStreamReader *reader)
{
	if (!reader)
		return;

	pthread_mutex_lock(&reader->queue_mutex);
	for (StreamLineNode *n = reader->head; n;) {
		StreamLineNode *next = n->next;
		free(n->line);
		free(n);
		n = next;
	}
	pthread_mutex_unlock(&reader->queue_mutex);
	pthread_mutex_destroy(&reader->queue_mutex);
	free(reader);
}
