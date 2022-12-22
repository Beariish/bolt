#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <stdio.h>

extern "C" {
#include "bolt.h"
#include "bt_parser.h"
#include "bt_op.h"
#include "bt_debug.h"
#include "bt_compiler.h"
#include "bt_embedding.h"
}

#include <malloc.h>
#include <Windows.h>
#include <math.h>

static size_t bytes_allocated = 0;
static void* malloc_tracked(size_t size) {
	if (size == 0) __debugbreak();
	bytes_allocated += size;
	return malloc(size);
}

static void free_tracked(void* mem) {
	bytes_allocated -= _msize(mem);
	free(mem);
}

static LARGE_INTEGER time_freq, time_start;
static void init_time()
{
	QueryPerformanceFrequency(&time_freq);
	QueryPerformanceCounter(&time_start);
}

static uint64_t get_time()
{
	LARGE_INTEGER time;
	QueryPerformanceCounter(&time);

	uint64_t in_micros = (uint64_t)time.QuadPart - (uint64_t)time_start.QuadPart;
	in_micros *= 1'000'000;
	in_micros /= time_freq.QuadPart;

	return in_micros;
}

static void bt_time(bt_Context* ctx, bt_Thread* thread)
{
	bt_return(thread, BT_VALUE_NUMBER(get_time()));
}

static void bt_print(bt_Context* ctx, bt_Thread* thread)
{
	static char buffer[4096];
	int32_t pos = 0;

	uint8_t argc = bt_argc(thread);
	for (uint8_t i = 0; i < argc; ++i) {
		bt_Value arg = bt_arg(thread, i);
		pos += bt_to_string_inplace(buffer + pos, 4096 - pos, arg);

		if (i < argc - 1) buffer[pos++] = ' ';
	}

	buffer[pos] = 0;
	printf("%s\n", buffer);
}

static void bt_max(bt_Context* ctx, bt_Thread* thread)
{
	uint8_t argc = bt_argc(thread);
	bt_number max = BT_AS_NUMBER(bt_arg(thread, 0));
	for (uint8_t i = 1; i < argc; ++i) {
		bt_number arg = BT_AS_NUMBER(bt_arg(thread, i));
		max = max > arg ? max : arg;
	}

	bt_return(thread, BT_VALUE_NUMBER(max));
}

static void bt_sqrt(bt_Context* ctx, bt_Thread* thread)
{
	bt_number tosqrt = BT_AS_NUMBER(bt_arg(thread, 0));
	bt_return(thread, BT_VALUE_NUMBER(sqrt(tosqrt)));
}

static void bt_tostring(bt_Context* ctx, bt_Thread* thread)
{
	bt_Value arg = bt_arg(thread, 0);
	bt_return(thread, BT_VALUE_STRING(bt_to_string(ctx, arg)));
}

int main(int argc, char** argv) {
	init_time();

	bt_Context context;
	bt_open(&context, malloc_tracked, free_tracked);

	bt_Module* core_module = bt_make_user_module(&context);

	bt_Type* print_sig = bt_make_vararg(&context, bt_make_signature(&context, NULL, NULL, 0), context.types.any);
	bt_module_export(&context, core_module,
		print_sig,
		BT_VALUE_CSTRING(&context, "print"),
		BT_VALUE_OBJECT(bt_make_native(&context, print_sig, bt_print)));

	bt_Type* tostring_args[] = { context.types.any };
	bt_Type* tostring_sig = bt_make_signature(&context, context.types.string, tostring_args, 1);
	bt_module_export(&context, core_module,
		tostring_sig,
		BT_VALUE_CSTRING(&context, "to_string"),
		BT_VALUE_OBJECT(bt_make_native(&context, tostring_sig, bt_tostring)));

	bt_Type* time_sig = bt_make_signature(&context, context.types.number, NULL, 0);
	bt_module_export(&context, core_module,
		time_sig,
		BT_VALUE_CSTRING(&context, "time"),
		BT_VALUE_OBJECT(bt_make_native(&context, time_sig, bt_time)));

	bt_Type* max_args[] = { context.types.number };
	bt_Type* max_sig = bt_make_vararg(&context, bt_make_signature(&context, context.types.number, max_args, 1), context.types.number);
	bt_module_export(&context, core_module,
		max_sig,
		BT_VALUE_CSTRING(&context, "max"),
		BT_VALUE_OBJECT(bt_make_native(&context, max_sig, bt_max)));

	bt_Type* sqrt_args[] = { context.types.number };
	bt_Type* sqrt_sig = bt_make_signature(&context, context.types.number, sqrt_args, 1);
	bt_module_export(&context, core_module,
		sqrt_sig,
		BT_VALUE_CSTRING(&context, "sqrt"),
		BT_VALUE_OBJECT(bt_make_native(&context, sqrt_sig, bt_sqrt)));

	bt_register_module(&context, BT_VALUE_CSTRING(&context, "core"), core_module);

	bt_Value module_name = BT_VALUE_STRING(bt_make_string(&context, "mandel"));
	bt_Module* loaded = bt_find_module(&context, module_name);
	
	printf("KB allocated during execution: %lld\n", bytes_allocated / 1024);
	
	while (bt_collect(&context.gc, 0));

	uint32_t cont = 1;
	while (cont) {
		uint64_t start = get_time();
		cont = bt_collect(&context.gc, 0);
		uint64_t end = get_time();
		printf("GC cycle took %.2fms\n", (double)(end - start) / 1000.0);
	}
	printf("KB allocated after gc: %lld\n", bytes_allocated / 1024);
	printf("-----------------------------------------------------\n");

 	return 0;
}

/*

let Vec2 = type {
	__proto: {
		__default = method {
			this.x = 0
			this.y = 0
		}

		__new = method(x: number, y: number) {
			this.x = x
			this.y = y
		}

		__add = method(rhs: Vec2): Vec2 {
			return new Vec2(this.x + rhs.x, this.y + rhs.y)
		}

		length = method: number {
			return math.sqrt(this.x * this.x + this.y * this.y)	
		}
	}

	x: number
	y: number
};

// This
let vec  = new Vec2(10, 10)
// is sugar for this
let vec2 = Vec2.__proto.__new(setproto({}, Vec2.__proto), 10, 10)

print((vec + vec2).length())

// This
let vec3 = new Vec2;
// is sugar for this
let vec4 = Vec2.__proto.__default(setproto({}, Vec2.__proto))

*/