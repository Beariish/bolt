#pragma once

#if __cplusplus
extern "C" {
#endif

#include "bt_prelude.h"

#include "bt_value.h"
#include "bt_buffer.h"
#include "bt_op.h"
#include "bt_tokenizer.h"

typedef struct bt_Type bt_Type;

/** Denotes the discriminated type of object. `BT_OBJECT_TYPE_NONE` is used to represent a base `bt_Object*` */
typedef enum {
	BT_OBJECT_TYPE_NONE,
	BT_OBJECT_TYPE_TYPE,
	BT_OBJECT_TYPE_STRING,
	BT_OBJECT_TYPE_MODULE,
	BT_OBJECT_TYPE_IMPORT,
	BT_OBJECT_TYPE_FN,
	BT_OBJECT_TYPE_NATIVE_FN,
	BT_OBJECT_TYPE_CLOSURE,
	BT_OBJECT_TYPE_ARRAY,
	BT_OBJECT_TYPE_TABLE,
	BT_OBJECT_TYPE_USERDATA,
	BT_OBJECT_TYPE_ANNOTATION,
} bt_ObjectType;

typedef bt_Buffer(uint32_t) bt_DebugLocBuffer;
typedef bt_Buffer(bt_Value) bt_ValueBuffer;
typedef bt_Buffer(bt_Op) bt_InstructionBuffer;
typedef bt_Buffer(uint8_t) bt_TopBuffer;

/**
 * Every `bt_Object` contains a pointer to the next object in the list of managed objects, set by the context when it's first allocated.
 * Objects are allocated at dynamic sizes depending on their contents, though, so the first member of each subtype
 * should be a `bt_Object` such that it's safe to cast to/from it.
 *
 * When the `BOLT_USE_MASKED_GC_HEADER` macro is defined, we use the empty bits in the `next` pointer to store
 * type information as well as the object's GC mark
 */
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

typedef bt_Buffer(bt_Object*) bt_ObjectBuffer;
	
typedef struct bt_TablePair {
	bt_Value key, value;
} bt_TablePair;

/**
 * Stores a continuous list of key-value pairs
 * If a lookup fails, `prototype` is fell back upon if present
 * Tables with inline allocations will evict to a second allocation
 * on growth, as moving the existing allocation would break the gc chain
 */
typedef struct bt_Table {
	bt_Object obj;
	struct bt_Table* prototype;
	uint32_t length, capacity;
	uint16_t is_inline, inline_capacity;
	union {
		bt_TablePair* outline; bt_Value inline_first;
	};
} bt_Table;

/** Returns a pointer to the first pair in the table, whether it's inline allocated or not */
#define BT_TABLE_PAIRS(t) (((bt_Table*)(t))->is_inline ? ((bt_TablePair*)(&((bt_Table*)t)->inline_first)) : ((bt_Table*)(t))->outline)

/** Simple dynamic array, resizes with a growth percentage when full */
typedef struct bt_Array {
	bt_Object obj;
	bt_Value* items;
	uint32_t length, capacity;
} bt_Array;

/**
 * Immutable string object, character data is allocated inline at the end of the structure
 * `hash` is computed and cached for statically defined strings, or calculated later when needed
 * `interned` is set to 1 if the string exists in the global deduplication table
 */
typedef struct bt_String {
	bt_Object obj;
	uint64_t hash;
	uint32_t interned : 1;
	uint32_t len : 31;
} bt_String;

/** Gets a pointer to the first character of the string data's inline allocation */
#define BT_STRING_STR(s) (((char*)(s)) + sizeof(bt_String))

/** Module import reference, stored at the top level of modules to keep imports alive */
typedef struct bt_ModuleImport {
	bt_Object obj;
	bt_String* name;
	bt_Type* type;
	bt_Value value;
} bt_ModuleImport;

typedef bt_Buffer(bt_ModuleImport*) bt_ImportBuffer;

/**
 * A compiled bolt (or native) module
 * Is owned by the surrounding context
 * Contains information for running the body if applicable
 * Contains debug information unless turned off
 * Contains import/export information
 */
typedef struct bt_Module {
	bt_Object obj;

	bt_Context* context;
	
	bt_ValueBuffer constants;
	bt_InstructionBuffer instructions;
	bt_ImportBuffer imports;
	bt_TopBuffer tops;

	bt_TokenBuffer debug_tokens;
	char* debug_source;
	bt_DebugLocBuffer* debug_locs;

	bt_String* path;
	bt_String* name;

	bt_Table* exports;
	bt_Table* storage;
	bt_Type* type;
	uint8_t stack_size;
} bt_Module;

/** A bolt-defined function, containing debug information if turned on */
typedef struct bt_Fn {
	bt_Object obj;

	bt_ValueBuffer constants;
	bt_InstructionBuffer instructions;
	bt_TopBuffer tops;

	bt_Type* signature;
	bt_Module* module;
	bt_DebugLocBuffer* debug;

	uint8_t stack_size;
} bt_Fn;

/** A bolt-defined closure, containing both the enclosed function and an inline allocation of upvalues */
typedef struct bt_Closure {
	bt_Object obj;
	bt_Fn* fn;
	uint32_t num_upv;
} bt_Closure;

/** Returns a pointer to the first upvalue contained in closure `c` */
#define BT_CLOSURE_UPVALS(c) ((bt_Value*)(((intptr_t*)(c)) + (sizeof(bt_Closure) / sizeof(intptr_t))))

typedef void (*bt_NativeProc)(bt_Context* ctx, bt_Thread* thread);

/** A native function reference that can be invoked by bolt */
typedef struct bt_NativeFn {
	bt_Object obj;
	bt_Module* module;
	bt_Type* type;
	bt_NativeProc fn;
} bt_NativeFn;

/** Union of all callable types */
typedef union {
	bt_Object obj;
	bt_Fn fn;
	bt_Module module;
	bt_NativeFn native;
	bt_Closure cl;
} bt_Callable;

struct bt_Userdata;
typedef void (*bt_UserdataFinalizer)(bt_Context* ctx, struct bt_Userdata* userdata);

/**
 * Opaque userdata object, contains an inline allocation of the user object
 * Bolt field-accessors can be defined on the type directly (see bt_userdata.h)
 * Related functions can be defined as prototypical for the type
 * `finalizer` is a function ran whenever the userdata object is collected, allowing cleanup of unmanaged resources
 */
typedef struct bt_Userdata {
	bt_Object obj;
	bt_Type* type;
	bt_UserdataFinalizer finalizer;
	size_t size;
} bt_Userdata;

/** Get a pointer to the start of the userdata allocation */
#define BT_USERDATA_VALUE(ud) ((void*)((uint8_t*)(ud) + sizeof(bt_Userdata)))

/** A type- or field annotation in bolt, stored as a linked list through `next` */
typedef struct bt_Annotation {
	bt_Object obj;
	bt_String* name;
	bt_Array* args;
	struct bt_Annotation* next;
} bt_Annotation;

/** Convenience macro to make bt_value's out of constant c strings */
#define BT_VALUE_CSTRING(ctx, str) BT_VALUE_OBJECT(bt_make_string_hashed(ctx, str))


/** STRINGS */

/** Allocate a managed string from `str`. Measures the length and copies the content */
BOLT_API bt_String* bt_make_string(bt_Context* ctx, const char* str);
/** Allocate a managed string from `len` bytes of `str` + nul byte. Content is copied */
BOLT_API bt_String* bt_make_string_len(bt_Context* ctx, const char* str, uint32_t len);
/** Allocate a managed string from `len` bytes of `str` + nul byte. Content is copied. This string is never interned for performance */
BOLT_API bt_String* bt_make_string_len_uninterned(bt_Context* ctx, const char* str, uint32_t len);
/** Allocate a managed string from `str` and then hash the contents. Measures the length and copies the content */
BOLT_API bt_String* bt_make_string_hashed(bt_Context* ctx, const char* str);
/** Allocate a managed string from `len` bytes of `str` + nul byte and then hash it. Content is copied */
BOLT_API bt_String* bt_make_string_hashed_len(bt_Context* ctx, const char* str, uint32_t len);
/** Allocate a managed string from `len` bytes of `str` + nul byte and then hash it. Standard escape characters are parsed instead of interpreted literally. Content is copied */
BOLT_API bt_String* bt_make_string_hashed_len_escape(bt_Context* ctx, const char* str, uint32_t len);
/** Allocates an empty managed string with specified length. All contents are zeroed */
BOLT_API bt_String* bt_make_string_empty(bt_Context* ctx, uint32_t len);
	
/** Convert any bt_Value into a string representation - allocated a temporary stack buffer */
BOLT_API bt_String* bt_to_string(bt_Context* ctx, bt_Value value);
/** Convert any bt_Value into a string representation - allocated a temporary stack buffer. String is interned if possible for runtime performance */
BOLT_API bt_String* bt_to_static_string(bt_Context* ctx, bt_Value value);
/** Convert any bt_Value into a string in-place, making zero allocations */
BOLT_API int32_t bt_to_string_inplace(bt_Context* ctx, char* buffer, uint32_t size, bt_Value value);

/** Calculates the hash of a string */
BOLT_API uint64_t bt_hash_str(const char* key, uint32_t len);
/** Compute the hash of a managed string if it's not already cached. Returns the input parameter */
BOLT_API bt_String* bt_hash_string(bt_String* str);
/** Represent the string as an unmanaged, unowned string slice */
BOLT_API bt_StrSlice bt_as_strslice(bt_String* str);
/** Get the character data from this managed string */
BOLT_API const char* const bt_string_get(bt_String* str);
/** Make a new string out of substrings `a` and `b` */
BOLT_API bt_String* bt_string_concat(bt_Context* ctx, bt_String* a, bt_String* b);
/** Make a new string out of managed string `a` and character data `b` */
BOLT_API bt_String* bt_string_append_cstr(bt_Context* ctx, bt_String* a, const char* b);
/** Get the length of the string */
BOLT_API uint64_t bt_string_length(bt_String* str);

/** TABLES */
	
/** Creates a new, empty table with `initial_size` capacity. 0 is a valid value */
BOLT_API bt_Table* bt_make_table(bt_Context* ctx, uint16_t initial_size);
/**
 * Create a new table using a prototype as a template
 * Preallocates enough slots for the prototype's layout
 * If the prototype has a value template, it's copied over
 * Otherwise, sensible default values are set for every layout key
 *
 * This function is primarily used when creating tables of types defined in user code, as the user is
 * otherwise responsible for setting all the keys directly.
 * Calling this will always ensure you have a valid table to pass back to the bolt environment,
 * at the cost of some performance.
 */
BOLT_API bt_Table* bt_make_table_from_proto(bt_Context* ctx, bt_Type* prototype);
/** Set the key `key` in `tbl` to `value` */
BOLT_API bt_bool bt_table_set(bt_Context* ctx, bt_Table* tbl, bt_Value key, bt_Value value);
/** Get the value at `key` in `tbl`. Returns BT_VALUE_NULL if key wasn't found */
BOLT_API bt_Value bt_table_get(bt_Table* tbl, bt_Value key);
/** Returns the numeric index of the `key` in `tbl`. Used internally in the compiler for precomputing hash slots */
BOLT_API int16_t bt_table_get_idx(bt_Table* tbl, bt_Value key);
/** Remove the entry for `key` in `tbl` */
BOLT_API bt_bool bt_table_delete_key(bt_Table* tbl, bt_Value key);

/** ARRAYS */

/** Creates a new, empty array with `initial_capacity` unused slots */
BOLT_API bt_Array* bt_make_array(bt_Context* ctx, uint32_t initial_capacity);
/** Pushes `value` to the end of `arr`. Will allocate and move contents if out of capacity */
BOLT_API uint64_t bt_array_push(bt_Context* ctx, bt_Array* arr, bt_Value value);
/** Reserve at least `capacity` slots in `arr`, allocating if necessary - returns the new capacity */
BOLT_API uint64_t bt_array_reserve(bt_Context* ctx, bt_Array* arr, uint64_t capacity);
/** Pops the last value from `arr`, returning BT_VALUE_NULL if empty */
BOLT_API bt_Value bt_array_pop(bt_Array* arr);
/** Gets the number of elements in `arr` */
BOLT_API uint64_t bt_array_length(bt_Array* arr);
/** Directly set an index in `arr`. Raises a runtime error if index is out of bounds */
BOLT_API bt_bool bt_array_set(bt_Context* ctx, bt_Array* arr, uint64_t index, bt_Value value);
/** Directly get an index in `arr`. Raises a runtime error if index is out of bounds */
BOLT_API bt_Value bt_array_get(bt_Context* ctx, bt_Array* arr, uint64_t index);

/** MODULES */
	
/** Create a new, empty module */
BOLT_API bt_Module* bt_make_module(bt_Context* ctx);
/** Create a new, empty module with imports set up */
BOLT_API bt_Module* bt_make_module_with_imports(bt_Context* ctx, bt_ImportBuffer* imports);

/** Move debug source tokens from `tok` into `module` */
BOLT_API void bt_module_set_debug_info(bt_Module* module, bt_Tokenizer* tok);

/** Push an export into the module's export table */
BOLT_API void bt_module_export(bt_Context* ctx, bt_Module* module, bt_Type* type, bt_Value key, bt_Value value);
/** Convenience function to export a native function in a single step */
BOLT_API void bt_module_export_native(bt_Context* ctx, bt_Module* module, const char* name, bt_NativeProc proc, bt_Type* ret_type, bt_Type** args, uint8_t arg_count);

/** Get the type of export at `key` from `module`, or NULL if none is found */
BOLT_API bt_Type* bt_module_get_export_type(bt_Module* module, bt_Value key);
/** Get the exported value at `key` from `module`, or BT_VALUE_NULL if none is found */
BOLT_API bt_Value bt_module_get_export(bt_Module* module, bt_Value key);

/** Set the value `value` at `key` in the local module storage */
BOLT_API void bt_module_set_storage(bt_Module* module, bt_Value key, bt_Value value);
/** Get the value at `key` from the local module storage, or BT_VALUE_NULL if none is found */
BOLT_API bt_Value bt_module_get_storage(bt_Module* module, bt_Value key);

/** FUNCTIONS */

/**
 * Creates a managed, in-language function
 * `module` is the owning module this function was defined in
 * `signature` is the callable type of this function
 * `constants` contains all constant values addressable by the function
 * `instructions` contains a list of opcodes that make up the function body
 * `tops` is a buffer as long as `instructions` that tells the GC how many stack slots to preserve
 * `stack_size` refers to the maximum number of stack slots required for the function, these are prealloacted on call
 *
 * The constant and instruction buffers are copied on call
 */ 
BOLT_API bt_Fn* bt_make_fn(bt_Context* ctx, bt_Module* module, bt_Type* signature, bt_ValueBuffer* constants, bt_InstructionBuffer* instructions, bt_TopBuffer* tops, uint8_t stack_size);

/**
 * Creates a managed reference to a native function
 * `module` refers to the owning module this function was intended to be exported from
 * `signature`must be a valid callable type, it's safe to assume within the function that this type is satisfied
 * `proc` is the native function pointer
 */
BOLT_API bt_NativeFn* bt_make_native(bt_Context* ctx, bt_Module* module, bt_Type* signature, bt_NativeProc proc);

/** Finds the return type of the signature of `callable` */
BOLT_API bt_Type* bt_get_return_type(bt_Callable* callable);
/** Finds the module responsible for owning `callable` */
BOLT_API bt_Module* bt_get_owning_module(bt_Callable* callable);
/** Return the number of registers used at `ip` */
BOLT_API uint8_t bt_get_top_at(bt_Callable* callable, bt_Op* ip);
	
/** USERDATA */

/**
 * Creates a new managed userdata object of `type`
 * `type` must be a valid userdata type
 * `data` must be at least `size` long
 * `size` bytes of `data` will be COPIED into a new allocation 
 */
BOLT_API bt_Userdata* bt_make_userdata(bt_Context* ctx, bt_Type* type, void* data, uint32_t size);
/** Returns a pointer to the first byte of the allocation owned by `userdata` */
BOLT_API void* bt_userdata_get(bt_Userdata* userdata);

/** ANNOTATIONS */

/** Creates a new annotation object with `name` and no args */
BOLT_API bt_Annotation* bt_make_annotation(bt_Context* ctx, bt_String* name);
/** Pushes an argument to `annotation` */
BOLT_API void bt_annotation_push(bt_Context* ctx, bt_Annotation* annotation, bt_Value value);
/** Appends a new annotation to the `next` link in `annotation` with `next_name` */
BOLT_API bt_Annotation* bt_annotation_next(bt_Context* ctx, bt_Annotation* annotation, bt_String* next_name);

/** MISC */

/** Attempt to index `obj` with `key` regardless of type. Raises a runtime error on invalid operation */
BOLT_API bt_Value bt_get(bt_Context* ctx, bt_Object* obj, bt_Value key);
/** Attempt to set index `key` in `obj` to `value` regardless of type. Raises a runtime error on invalid operation */
BOLT_API void bt_set(bt_Context* ctx, bt_Object* obj, bt_Value key, bt_Value value);

#if __cplusplus
}
#endif