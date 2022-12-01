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
}

#include <malloc.h>

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

int main(int argc, char** argv) {
	bt_Context context;
	bt_open(&context, malloc_tracked, free_tracked);

	bt_Tokenizer tokenizer = bt_open_tokenizer(&context);

	const char* source = "let a: number = 10 + 10\n"
		"let const b = \"this is also a string\"\n"
		"let c = null\n"
		"let d: string? = \"this is a string!\"\n"
		"let e = d ?? b";

	bt_tokenizer_set_source(&tokenizer, source);

	bt_Token* current = NULL;

	bt_Parser parser = bt_open_parser(&tokenizer);
	bt_parse(&parser);

	printf("-----------------------------------------------------\n");
	printf("%s\n", source);
	printf("-----------------------------------------------------\n");

	bt_debug_print_parse_tree(&parser);
	printf("-----------------------------------------------------\n");

	bt_Compiler compiler = bt_open_compiler(&parser);
	bt_compile(&compiler);

	bt_debug_print_compiler_output(&compiler);

	printf("-----------------------------------------------------\n");
	printf("Bytes allocated during execution: %lld\n", bytes_allocated);

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