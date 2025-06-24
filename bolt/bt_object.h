#pragma once

#include "bt_prelude.h"

#include "bt_value.h"
#include "bt_buffer.h"
#include "bt_op.h"
#include "bt_tokenizer.h"

typedef struct bt_Type bt_Type;

typedef enum {
	BT_OBJECT_TYPE_NONE,
	BT_OBJECT_TYPE_TYPE,
	BT_OBJECT_TYPE_STRING,
	BT_OBJECT_TYPE_MODULE,
	BT_OBJECT_TYPE_IMPORT,
	BT_OBJECT_TYPE_FN,
	BT_OBJECT_TYPE_NATIVE_FN,
	BT_OBJECT_TYPE_CLOSURE,
	BT_OBJECT_TYPE_METHOD,
	BT_OBJECT_TYPE_ARRAY,
	BT_OBJECT_TYPE_TABLE,
	BT_OBJECT_TYPE_USERDATA,
	BT_OBJECT_TYPE_ANNOTATION,
} bt_ObjectType;

typedef bt_Buffer(uint32_t) bt_DebugLocBuffer;
typedef bt_Buffer(bt_Value) bt_ValueBuffer;
typedef bt_Buffer(bt_Op) bt_InstructionBuffer;

#ifdef BOLT_USE_MASKED_GC_HEADER
typedef struct bt_Object {
	uint64_t mask;
} bt_Object;

#define BT_OBJ_PTR_BITS 0b0000000000000000111111111111111111111111111111111111111111111100ull

#define BT_OBJECT_SET_TYPE(__obj, __type) ((bt_Object*)(__obj))->mask &= (BT_OBJ_PTR_BITS | 1ull); ((bt_Object*)(__obj))->mask |= (uint64_t)(__type) << 56ull
#define BT_OBJECT_GET_TYPE(__obj) ((((bt_Object*)(__obj))->mask) >> 56)

#define BT_OBJECT_NEXT(__obj) (((bt_Object*)(__obj))->mask & BT_OBJ_PTR_BITS)
#define BT_OBJECT_SET_NEXT(__obj, __next) ((__obj)->mask = ((__obj)->mask & ~BT_OBJ_PTR_BITS) | ((uint64_t)(__next)))

#define BT_OBJECT_GET_MARK(__obj) ((__obj)->mask & 1ull)
#define BT_OBJECT_MARK(__obj) (__obj)->mask |= 1ull
#define BT_OBJECT_CLEAR(__obj) (__obj)->mask &= ~1ull
#else
typedef struct bt_Object {
	struct bt_Object* next;
	uint64_t type : 5;
	uint64_t mark : 1;
} bt_Object;

#define BT_OBJECT_SET_TYPE(__obj, __type) ((bt_Object*)(__obj))->type = __type
#define BT_OBJECT_GET_TYPE(__obj) ((bt_Object*)(__obj))->type

#define BT_OBJECT_NEXT(__obj) ((bt_Object*)(__obj)->next)
#define BT_OBJECT_SET_NEXT(__obj, __next) (__obj)->next = __next

#define BT_OBJECT_GET_MARK(__obj) ((bt_Object*)(__obj)->mark)
#define BT_OBJECT_MARK(__obj) (__obj)->mark = 1
#define BT_OBJECT_CLEAR(__obj) (__obj)->mark = 0
#endif

typedef struct bt_TablePair {
	bt_Value key, value;
} bt_TablePair;

typedef bt_Buffer(bt_TablePair) bt_TablePairBuffer;

typedef struct bt_Table {
	bt_Object obj;
	struct bt_Table* prototype;
	uint16_t is_inline, length, capacity, inline_capacity;
	union {
		bt_TablePair* outline; bt_Value inline_first;
	};
} bt_Table;

#define BT_TABLE_PAIRS(t) (((bt_Table*)(t))->is_inline ? ((bt_TablePair*)(&((bt_Table*)t)->inline_first)) : ((bt_Table*)(t))->outline)

typedef struct bt_Array {
	bt_Object obj;
	bt_Value* items;
	uint32_t length, capacity;
} bt_Array;

typedef struct bt_String {
	bt_Object obj;
	uint64_t hash;
	uint32_t interned : 1;
	uint32_t len : 31;
} bt_String;

#define BT_STRING_STR(s) (((char*)(s)) + sizeof(bt_String))

typedef struct bt_ModuleImport {
	bt_Object obj;
	bt_String* name;
	bt_Type* type;
	bt_Value value;
} bt_ModuleImport;

typedef bt_Buffer(bt_ModuleImport*) bt_ImportBuffer;

typedef struct bt_Module {
	bt_Object obj;

	bt_ValueBuffer constants;
	bt_InstructionBuffer instructions;
	bt_ImportBuffer imports;

	bt_TokenBuffer debug_tokens;
	char* debug_source;
	bt_DebugLocBuffer* debug_locs;

	bt_String* path;
	bt_String* name;

	bt_Table* exports;
	bt_Type* type;
	uint8_t stack_size;
} bt_Module;

typedef struct bt_Fn {
	bt_Object obj;

	bt_ValueBuffer constants;
	bt_InstructionBuffer instructions;

	bt_Type* signature;
	bt_Module* module;
	bt_DebugLocBuffer* debug;

	uint8_t stack_size;
} bt_Fn;

typedef struct bt_Closure {
	bt_Object obj;
	bt_Fn* fn;
	uint32_t num_upv;
} bt_Closure;

#define BT_CLOSURE_UPVALS(c) ((bt_Value*)(((intptr_t*)(c)) + (sizeof(bt_Closure) / sizeof(intptr_t))))

typedef void (*bt_NativeProc)(bt_Context* ctx, bt_Thread* thread);

typedef struct bt_NativeFn {
	bt_Object obj;
	bt_Type* type;
	bt_NativeProc fn;
} bt_NativeFn;

typedef union {
	bt_Object obj;
	bt_Fn fn;
	bt_Module module;
	bt_NativeFn native;
	bt_Closure cl;
} bt_Callable;

struct bt_Userdata;
typedef void (*bt_UserdataFinalizer)(bt_Context* ctx, struct bt_Userdata* userdata);

typedef struct bt_Userdata {
	bt_Object obj;
	bt_Type* type;
	bt_UserdataFinalizer finalizer;
	size_t size;
} bt_Userdata;

#define BT_USERDATA_VALUE(ud) ((void*)((uint8_t*)(ud) + sizeof(bt_Userdata)))

typedef struct bt_Annotation {
	bt_Object obj;
	bt_String* name;
	bt_Array* args;
	struct bt_Annotation* next;
} bt_Annotation;


#define BT_VALUE_CSTRING(ctx, str) BT_VALUE_OBJECT(bt_make_string_hashed(ctx, str))

BOLT_API uint64_t bt_hash_str(const char* key, uint32_t len);
BOLT_API bt_String* bt_to_string(bt_Context* ctx, bt_Value value);
BOLT_API int32_t bt_to_string_inplace(bt_Context* ctx, char* buffer, uint32_t size, bt_Value value);

BOLT_API bt_String* bt_make_string(bt_Context* ctx, const char* str);
BOLT_API bt_String* bt_make_string_len(bt_Context* ctx, const char* str, uint32_t len);
BOLT_API bt_String* bt_make_string_hashed(bt_Context* ctx, const char* str);
BOLT_API bt_String* bt_make_string_hashed_len(bt_Context* ctx, const char* str, uint32_t len);
BOLT_API bt_String* bt_make_string_hashed_len_escape(bt_Context* ctx, const char* str, uint32_t len);
BOLT_API bt_String* bt_make_string_empty(bt_Context* ctx, uint32_t len);
BOLT_API bt_String* bt_hash_string(bt_String* str);
BOLT_API bt_StrSlice bt_as_strslice(bt_String* str);
BOLT_API const char* const bt_get_string(bt_String* str);
BOLT_API bt_String* bt_concat_strings(bt_Context* ctx, bt_String* a, bt_String* b);
BOLT_API bt_String* bt_append_cstr(bt_Context* ctx, bt_String* a, const char* b);
BOLT_API uint64_t bt_string_length(bt_String* str);

BOLT_API bt_Table* bt_make_table(bt_Context* ctx, uint16_t initial_size);
BOLT_API bt_Table* bt_make_table_from_proto(bt_Context* ctx, bt_Type* prototype);
BOLT_API bt_bool bt_table_set(bt_Context* ctx, bt_Table* tbl, bt_Value key, bt_Value value);
BOLT_API bt_bool bt_table_set_cstr(bt_Context* ctx, bt_Table* tbl, const char* key, bt_Value value);
BOLT_API bt_Value bt_table_get(bt_Table* tbl, bt_Value key);
BOLT_API bt_Value bt_table_get_cstr(bt_Context* ctx, bt_Table* tbl, const char* key);
BOLT_API int16_t bt_table_get_idx(bt_Table* tbl, bt_Value key);
BOLT_API bt_bool bt_table_delete_key(bt_Table* tbl, bt_Value key);

BOLT_API bt_Array* bt_make_array(bt_Context* ctx, uint32_t initial_capacity);
BOLT_API uint64_t bt_array_push(bt_Context* ctx, bt_Array* arr, bt_Value value);
BOLT_API bt_Value bt_array_pop(bt_Array* arr);
BOLT_API uint64_t bt_array_length(bt_Array* arr);
BOLT_API bt_bool bt_array_set(bt_Context* ctx, bt_Array* arr, uint64_t index, bt_Value value);
BOLT_API bt_Value bt_array_get(bt_Context* ctx, bt_Array* arr, uint64_t index);

BOLT_API bt_Fn* bt_make_fn(bt_Context* ctx, bt_Module* module, bt_Type* signature, bt_ValueBuffer* constants, bt_InstructionBuffer* instructions, uint8_t stack_size);
BOLT_API bt_Module* bt_make_module(bt_Context* ctx, bt_ImportBuffer* imports);
BOLT_API bt_Module* bt_make_user_module(bt_Context* ctx);
BOLT_API void bt_module_set_debug_info(bt_Module* module, bt_Tokenizer* tok);

BOLT_API bt_NativeFn* bt_make_native(bt_Context* ctx, bt_Type* signature, bt_NativeProc proc);
BOLT_API bt_Type* bt_get_return_type(bt_Callable* callable);

BOLT_API bt_Userdata* bt_make_userdata(bt_Context* ctx, bt_Type* type, void* data, uint32_t size);
BOLT_API void* bt_userdata_get(bt_Userdata* userdata);

BOLT_API bt_Annotation* bt_make_annotation(bt_Context* ctx, bt_String* name);
BOLT_API void bt_annotation_push(bt_Context* ctx, bt_Annotation* annotation, bt_Value value);
BOLT_API bt_Annotation* bt_annotation_next(bt_Context* ctx, bt_Annotation* annotation, bt_String* next_name);

BOLT_API void bt_module_export(bt_Context* ctx, bt_Module* module, bt_Type* type, bt_Value key, bt_Value value);
BOLT_API void bt_module_export_native(bt_Context* ctx, bt_Module* module, const char* name, bt_NativeProc proc, bt_Type* ret_type, bt_Type** args, uint8_t arg_count);

BOLT_API bt_Type* bt_module_get_export_type(bt_Module* module, bt_Value key);
BOLT_API bt_Value bt_module_get_export(bt_Module* module, bt_Value key);

BOLT_API bt_Value bt_get(bt_Context* ctx, bt_Object* obj, bt_Value key);
BOLT_API void bt_set(bt_Context* ctx, bt_Object* obj, bt_Value key, bt_Value value);