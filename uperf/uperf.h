#pragma once

#include <stdint.h>
#include <stdbool.h>

#define UPERF_BUFFER_SIZE 1024 * 1024 * 64
#define UPERF_EVENT_STACK_SIZE 64

typedef struct {
	char* start;
	size_t len;
} uperf_str_slice_t;

typedef struct {
	uint64_t start, duration;
	uint32_t name_loc, name_len;
	uint32_t sibling, child;
} uperf_event_t;

typedef struct {
	char* name_buffer;
	size_t name_loc, name_size;

	uperf_event_t* event_buffer;
	size_t event_loc, event_size;

	uint64_t start, duration;

	uperf_event_t* stack[UPERF_EVENT_STACK_SIZE];
	int16_t depth, active;
} uperf_capture_t;

extern uperf_capture_t* uperf_current_capture;

bool uperf_capture_begin();
bool uperf_capture_end();
bool uperf_is_capturing();

void uperf_event(const char* name, size_t length);
void uperf_event_ptr(const char* name);

void uperf_pop();

uperf_event_t* uperf_get_first();
uperf_event_t* uperf_get_sibling(uperf_event_t* event);
uperf_event_t* uperf_get_child(uperf_event_t* event);
uperf_str_slice_t uperf_get_name(uperf_event_t* event);

void uperf_print_capture();

bool uperf_to_file(const char* path);
bool uperf_from_file(const char* path);

#define UPERF_STRLEN(s) (sizeof(s) / sizeof(s[0]) - 1)
#define UPERF_EVENT(name) uperf_event(name, UPERF_STRLEN(name))
#define UPERF_POP() uperf_pop()

#define UPERF_BLOCK(S) for (int uperf__guard__ = (UPERF_EVENT(S), 1); uperf__guard__; UPERF_POP(), uperf__guard__ = 0)