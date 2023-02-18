#pragma once

#include "bt_prelude.h"

#include "bt_value.h"
#include "bt_buffer.h"

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
	BT_OBEJCT_TYPE_ARRAY,
	BT_OBJECT_TYPE_TABLE,
	BT_OBJECT_TYPE_USERDATA
} bt_ObjectType;

#define BT_OBJECT_NEXT(__obj) ((bt_Object*)__obj->next)
#define BT_OBJECT_SET_NEXT(__obj, __next) (__obj->next = (uint64_t)__next)

typedef struct bt_Object {
	struct bt_Object* next;
	uint64_t mark : 1;
	uint64_t type : 5;
} bt_Object;

typedef struct bt_Table {
	bt_Object obj;

	bt_Buffer pairs;
	struct bt_Table* prototype;
} bt_Table;

typedef struct bt_Module {
	bt_Object obj;

	bt_Buffer constants;
	bt_Buffer instructions;
	bt_Buffer imports;

	bt_Table* exports;
	bt_Type* type;
	uint8_t stack_size;
} bt_Module;

typedef struct bt_String {
	bt_Object obj;
	uint32_t len;
	char* str;
	uint64_t hash;
} bt_String;

typedef struct bt_TablePair {
	bt_Value key, value;
} bt_TablePair;

typedef struct bt_Fn {
	bt_Object obj;

	bt_Buffer constants;
	bt_Buffer instructions;

	bt_Type* signature;
	bt_Module* module;
	uint8_t stack_size;
} bt_Fn;

typedef struct bt_Closure {
	bt_Object obj;
	bt_Buffer upvals;
	bt_Fn* fn;
} bt_Closure;

typedef struct bt_ModuleImport {
	bt_Object obj;
	bt_String* name;
	bt_Type* type;
	bt_Value value;
} bt_ModuleImport;

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

typedef struct bt_Userdata {
	bt_Object obj;
	bt_Type* type;
	uint8_t* data;
} bt_Userdata;

#define BT_VALUE_CSTRING(ctx, str) BT_VALUE_OBJECT(bt_make_string_hashed(ctx, str))

bt_String* bt_to_string(bt_Context* ctx, bt_Value value);
int32_t bt_to_string_inplace(bt_Context* ctx, char* buffer, uint32_t size, bt_Value value);

bt_String* bt_make_string(bt_Context* ctx, const char* str);
bt_String* bt_make_string_len(bt_Context* ctx, const char* str, uint32_t len);
bt_String* bt_make_string_hashed(bt_Context* ctx, const char* str);
bt_String* bt_make_string_hashed_len(bt_Context* ctx, const char* str, uint32_t len);
bt_String* bt_make_string_moved(bt_Context* ctx, const char* str, uint32_t len);
bt_String* bt_hash_string(bt_String* str);
bt_StrSlice bt_as_strslice(bt_String* str);

bt_Table* bt_make_table(bt_Context* ctx, uint16_t initial_size);
bt_bool bt_table_set(bt_Context* ctx, bt_Table* tbl, bt_Value key, bt_Value value);
bt_bool bt_table_set_cstr(bt_Context* ctx, bt_Table* tbl, const char* key, bt_Value value);
bt_Value bt_table_get(bt_Table* tbl, bt_Value key);
bt_Value bt_table_get_cstr(bt_Context* ctx, bt_Table* tbl, const char* key);

bt_Fn* bt_make_fn(bt_Context* ctx, bt_Module* module, bt_Type* signature, bt_Buffer* constants, bt_Buffer* instructions, uint8_t stack_size);
bt_Module* bt_make_module(bt_Context* ctx, bt_Buffer* imports);
bt_Module* bt_make_user_module(bt_Context* ctx);

bt_NativeFn* bt_make_native(bt_Context* ctx, bt_Type* signature, bt_NativeProc proc);

bt_Userdata* bt_make_userdata(bt_Context* ctx, bt_Type* type, void* data, uint32_t size);

void bt_module_export(bt_Context* ctx, bt_Module* module, bt_Type* type, bt_Value key, bt_Value value);

bt_Value bt_get(bt_Context* ctx, bt_Object* obj, bt_Value key); 
void bt_set(bt_Context* ctx, bt_Object* obj, bt_Value key, bt_Value value);