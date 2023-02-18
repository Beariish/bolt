#include "uperf.h"

#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>

static uperf_capture_t* uperf_current_capture = NULL;

static __forceinline uint64_t get_timestamp()
{
	struct timespec ts;
#ifdef _MSC_VER
	timespec_get(&ts, TIME_UTC);
#else
	clock_gettime(CLOCK_REALTIME, &ts);
#endif

	uint64_t seconds = ts.tv_sec;
	uint64_t nano_seconds = ts.tv_nsec;

	return (seconds * 1'000'000) + (nano_seconds / 1'000);
}

static destroy_current()
{
	if (uperf_current_capture) {
		free(uperf_current_capture->name_buffer);
		free(uperf_current_capture->event_buffer);
		free(uperf_current_capture);
		uperf_current_capture = NULL;
	}
}

bool uperf_capture_begin()
{
	if(uperf_current_capture && uperf_current_capture->active) return false;

	destroy_current();

	uperf_current_capture = malloc(sizeof(uperf_capture_t));
	uperf_capture_t* cc = uperf_current_capture;

	cc->name_buffer = malloc(UPERF_BUFFER_SIZE / 2);
	cc->name_loc = 0;
	cc->name_size = UPERF_BUFFER_SIZE / 2;

	cc->event_buffer = malloc(UPERF_BUFFER_SIZE / 2);
	cc->event_loc = 0;
	cc->event_size = (UPERF_BUFFER_SIZE / 2) / sizeof(uperf_event_t);

	cc->start = get_timestamp();
	cc->duration = -1;

	memset(cc->stack, 0, sizeof(uperf_event_t*) * UPERF_EVENT_STACK_SIZE);
	cc->depth = -1;
	
	cc->active = 1;
}

bool uperf_capture_end()
{
	if(!uperf_current_capture) return false;
	if (!uperf_current_capture->active) return false;

	uperf_capture_t* cc = uperf_current_capture;
	cc->active = 0;
	cc->duration = get_timestamp() - cc->start;

	if (cc->depth > 0) {
		assert(0 && "Capture was unbalanced!");
	}
}

bool uperf_is_capturing()
{
	return uperf_current_capture && uperf_current_capture->active;
}

static __forceinline uint32_t to_idx(uperf_event_t* event)
{
	ptrdiff_t offset = event - uperf_current_capture->event_buffer;
	return offset;
}

static __forceinline uperf_event_t* from_idx(uint32_t idx)
{
	return uperf_current_capture->event_buffer + idx;
}

static __forceinline uperf_event_t* get_event()
{
	uperf_capture_t* cc = uperf_current_capture;

	assert(cc->depth < UPERF_EVENT_STACK_SIZE);

	uperf_event_t* event = &cc->event_buffer[cc->event_loc++];
	event->child = 0; event->sibling = 0;
	event->start = get_timestamp();

	uperf_event_t* parent = NULL;
	if (cc->depth >= 0) {
		parent = cc->stack[cc->depth];
	}

	if (!parent) {
		cc->stack[++cc->depth] = event;
		return event;
	}

	if (parent->child) {
		uperf_event_t* sibling = cc->stack[cc->depth + 1];
		assert(sibling->sibling == 0);
		sibling->sibling = to_idx(event);
		cc->stack[++cc->depth] = event;
		return event;
	}

	parent->child = to_idx(event);
	cc->stack[++cc->depth] = event;
	return event;
}

void uperf_event(const char* name, size_t length)
{
	uperf_capture_t* cc = uperf_current_capture;
	if (!cc || !cc->active) return;

	uperf_event_t* event = get_event();
	assert(cc->name_loc + length < cc->name_size);
	memcpy(cc->name_buffer + cc->name_loc, name, length);

	event->name_loc = cc->name_loc;
	event->name_len = length;

	cc->name_loc += length;
}

void uperf_event_ptr(const char* name)
{
	size_t len = strlen(name);
	uperf_event(name, len);
}

void uperf_pop()
{
	uperf_capture_t* cc = uperf_current_capture;
	if (!cc || !cc->active) return;

	assert(cc->depth >= 0);

	uperf_event_t* event = cc->stack[cc->depth--];

	event->duration = get_timestamp() - event->start;
}

uperf_event_t* uperf_get_first()
{
	uperf_capture_t* cc = uperf_current_capture;
	if (!cc) return NULL;

	assert(!cc->active);
	assert(cc->event_loc > 0);

	return cc->event_buffer;
}

uperf_event_t* uperf_get_sibling(uperf_event_t* event)
{
	if (event->sibling) {
		return from_idx(event->sibling);
	}

	return NULL;
}

uperf_event_t* uperf_get_child(uperf_event_t* event)
{
	if (event->child) {
		return from_idx(event->child);
	}

	return NULL;
}

uperf_str_slice_t uperf_get_name(uperf_event_t* event)
{
	return (uperf_str_slice_t) {
		uperf_current_capture->name_buffer + event->name_loc,
		event->name_len
	};
}

static print_event(uperf_event_t* event, uint32_t depth)
{
	while (event) {
		uperf_str_slice_t name = uperf_get_name(event);
		printf("%*s [%.*s] %*s - %lldus\n", depth * 2, "", name.len, name.start, 60 - name.len - depth, "", event->duration);

		print_event(uperf_get_child(event), depth + 1);
		event = uperf_get_sibling(event);
	}
}

void uperf_print_capture()
{
	print_event(uperf_get_first(), 0);
}

#define UPERF_MAGIC "UPERF "
#define UPERF_MAGIC_LEN 6

bool uperf_to_file(const char* path)
{
	if (!uperf_current_capture) return false;
	
	FILE* dst;
	fopen_s(&dst, path, "wb");

	if (dst == 0) return false;

	fwrite(UPERF_MAGIC, 1, UPERF_MAGIC_LEN, dst);

	fwrite(&uperf_current_capture->start, sizeof(uint64_t), 1, dst);
	fwrite(&uperf_current_capture->duration, sizeof(uint64_t), 1, dst);

	fwrite(&uperf_current_capture->name_loc, sizeof(size_t), 1, dst);
	fwrite(&uperf_current_capture->event_loc, sizeof(size_t), 1, dst);

	fwrite(uperf_current_capture->name_buffer, uperf_current_capture->name_loc, 1, dst);
	fwrite(uperf_current_capture->event_buffer, uperf_current_capture->event_loc, sizeof(uperf_event_t), dst);

	fwrite(UPERF_MAGIC, 1, UPERF_MAGIC_LEN, dst);

	fclose(dst);
}

bool uperf_from_file(const char* path)
{
	FILE* src;
	fopen_s(&src, path, "rb");

	if (src == 0) return false;

	destroy_current();

	uperf_current_capture = malloc(sizeof(uperf_capture_t));

	if (!uperf_current_capture) return false;

	memset(uperf_current_capture, 0, sizeof(uperf_capture_t));

	char magic_buf[UPERF_MAGIC_LEN];
	fread(magic_buf, 1, UPERF_MAGIC_LEN, src);

	if (memcmp(magic_buf, UPERF_MAGIC, UPERF_MAGIC_LEN)) {
		return false;
	}

	fread(&uperf_current_capture->start, sizeof(uint64_t), 1, src);
	fread(&uperf_current_capture->duration, sizeof(uint64_t), 1, src);

	fread(&uperf_current_capture->name_loc, sizeof(uint64_t), 1, src);
	fread(&uperf_current_capture->event_loc, sizeof(uint64_t), 1, src);

	uperf_current_capture->name_size = uperf_current_capture->name_loc;
	uperf_current_capture->event_size = uperf_current_capture->event_loc;

	uperf_current_capture->name_buffer = malloc(uperf_current_capture->name_size);
	uperf_current_capture->event_buffer = malloc(uperf_current_capture->event_size * sizeof(uperf_event_t));

	if (!uperf_current_capture->name_buffer || !uperf_current_capture->event_buffer)
		return false;

	fread(uperf_current_capture->name_buffer, 1, uperf_current_capture->name_size, src);
	fread(uperf_current_capture->event_buffer, sizeof(uperf_event_t), uperf_current_capture->event_size, src);

	fread(magic_buf, 1, UPERF_MAGIC_LEN, src);

	if (memcmp(magic_buf, UPERF_MAGIC, UPERF_MAGIC_LEN)) {
		return false;
	}

	return true;
}
