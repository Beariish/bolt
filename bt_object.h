#pragma once

#include "bt_prelude.h"

#include "bt_value.h"
#include "bt_buffer.h"

typedef enum {
	BT_OBJECT_TYPE_NONE,
	BT_OBJECT_TYPE_TYPE,
	BT_OBJECT_TYPE_STRING,
	BT_OBJECT_TYPE_FN,
	BT_OBJECT_TYPE_CLOSURE,
	BT_OBJECT_TYPE_METHOD,
	BT_OBEJCT_TYPE_ARRAY,
	BT_OBJECT_TYPE_TABLE,
	BT_OBJECT_TYPE_SHARED,
	BT_OBJECT_TYPE_USERDATA
} bt_ObjectType;

typedef struct bt_Object {
	uint32_t heap_idx : 25;
	bt_ObjectType type : 5;
	uint32_t mark : 1;
	uint32_t gray : 1;
} bt_Object;

typedef struct bt_String {
	bt_Object obj;
	uint32_t len;
	char* str;
	uint64_t hash;
} bt_String;

typedef struct bt_TablePair {
	bt_Value key, value;
} bt_TablePair;

typedef struct bt_Table {
	bt_Object obj;

	bt_Buffer pairs;
	struct bt_Table* prototype;
} bt_Table;

bt_String* bt_make_string(bt_Context* ctx, char* str);
bt_String* bt_make_string_len(bt_Context* ctx, char* str, uint32_t len);
bt_String* bt_make_string_hashed(bt_Context* ctx, char* str);
bt_String* bt_make_string_hashed_len(bt_Context* ctx, char* str, uint32_t len);
bt_String* bt_hash_string(bt_String* str);

bt_Table* bt_make_table(bt_Context* ctx, uint16_t initial_size);
bt_bool bt_table_set(bt_Context* ctx, bt_Table* tbl, bt_Value key, bt_Value value); 
bt_Value bt_table_get(bt_Table* tbl, bt_Value key);