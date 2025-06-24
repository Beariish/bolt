#include "boltstd_io.h"

#include "../bt_embedding.h"

#include "boltstd_core.h"

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

static bt_Type* io_file_type;

typedef struct btio_FileState {
	FILE* handle;
	bt_bool is_open;
} btio_FileState;

static const char* error_to_desc(int32_t error)
{
	switch (error) {
	case EACCES: return "Access denied";
	case EBADF: return "Bad file number";
	case EBUSY: return "Device busy";
	case EDEADLK: return "Deadlock would occur";
	case EEXIST: return "File already exists";
	case EFBIG: return "File too big";
	case EINVAL: return "Invalid argument";
	case EIO: return "I/O error occured";
	case EISDIR: return "Path is directory";
	case EMFILE: return "No remaining file descriptors";
	case ENAMETOOLONG: return "Filename too long";
	case ENFILE: return "Too many files open";
	case ENODEV: return "No such device";
	case ENOENT: return "File not found";
	case ENOMEM: return "Not enough memory";
	case ENOSPC: return "No space left on device";
	case ENOSYS: return "Function not supported";
	case ENOTDIR: return "Path is not directory";
	case ENOTEMPTY: return "Directory is not empty";
	case ENOTTY: return "Inappropriate operation";
	case ENXIO: return "No device";
	case EPERM: return "Invalid permission";
	case EROFS: return "File system is read-only";
	case ESPIPE: return "Invalid seek";
	default: return "Unknown IO error";
	}
}

static void btio_file_finalizer(bt_Context* ctx, bt_Userdata* userdata)
{
	btio_FileState* state = bt_userdata_get(userdata);
	if (state->is_open) {
		fclose(state->handle);
		state->handle = 0;
		state->is_open = BT_FALSE;
	}
}

static void btio_open(bt_Context* ctx, bt_Thread* thread)
{
	bt_String* path = (bt_String*)BT_AS_OBJECT(bt_arg(thread, 0));
	bt_String* mode = (bt_String*)BT_AS_OBJECT(bt_arg(thread, 1));
	const char* cpath = BT_STRING_STR(path);
	const char* cmode = BT_STRING_STR(mode);
	FILE* file = fopen(cpath, cmode);

	if (file) {
		btio_FileState state;
		state.handle = file;
		state.is_open = BT_TRUE;

		bt_Userdata* result = bt_make_userdata(ctx, io_file_type, &state, sizeof(btio_FileState));
		bt_return(thread, BT_VALUE_OBJECT(result));
	}
	else {
		bt_String* what = bt_make_string(ctx, error_to_desc(errno));
		bt_Table* result = bt_make_table(ctx, 1);
		result->prototype = bt_type_get_proto(ctx, bt_error_type);
		bt_table_set(ctx, result, bt_error_what_key, BT_VALUE_OBJECT(what));
		bt_return(thread, BT_VALUE_OBJECT(result));
	}
}

static bt_Value bt_close_error_reason;

static void btio_close(bt_Context* ctx, bt_Thread* thread)
{
	bt_Userdata* file = (bt_Userdata*)BT_AS_OBJECT(bt_arg(thread, 0));
	btio_FileState* state = bt_userdata_get(file);
	
	if (state->is_open) {
		fclose(state->handle);
		state->handle = 0;
		state->is_open = BT_FALSE;
		bt_return(thread, BT_VALUE_NULL);
	}
	else {
		bt_Table* result = bt_make_table(ctx, 1);
		result->prototype = bt_type_get_proto(ctx, bt_error_type);
		bt_table_set(ctx, result, bt_error_what_key, bt_close_error_reason);
		bt_return(thread, BT_VALUE_OBJECT(result));
	}
}

static void btio_get_size(bt_Context* ctx, bt_Thread* thread)
{
	bt_Userdata* file = (bt_Userdata*)BT_AS_OBJECT(bt_arg(thread, 0));
	btio_FileState* state = bt_userdata_get(file);

	if (state->is_open) {
		int64_t pos = ftell(state->handle);
		fseek(state->handle, 0, SEEK_END);
		int64_t size = ftell(state->handle);
		fseek(state->handle, (long)pos, SEEK_SET);

		bt_return(thread, BT_VALUE_NUMBER(size));
	}
	else {
		bt_Table* result = bt_make_table(ctx, 1);
		result->prototype = bt_type_get_proto(ctx, bt_error_type);
		bt_table_set(ctx, result, bt_error_what_key, bt_close_error_reason);
		bt_return(thread, BT_VALUE_OBJECT(result));
	}
}

static void btio_seek_set(bt_Context* ctx, bt_Thread* thread)
{
	bt_Userdata* file = (bt_Userdata*)BT_AS_OBJECT(bt_arg(thread, 0));
	bt_number pos = BT_AS_NUMBER(bt_arg(thread, 1));
	btio_FileState* state = bt_userdata_get(file);

	if (state->is_open) {
		fseek(state->handle, (long)pos, SEEK_SET);
		bt_return(thread, BT_VALUE_NULL);
	}
	else {
		bt_Table* result = bt_make_table(ctx, 1);
		result->prototype = bt_type_get_proto(ctx, bt_error_type);
		bt_table_set(ctx, result, bt_error_what_key, bt_close_error_reason);
		bt_return(thread, BT_VALUE_OBJECT(result));
	}
}

static void btio_seek_relative(bt_Context* ctx, bt_Thread* thread)
{
	bt_Userdata* file = (bt_Userdata*)BT_AS_OBJECT(bt_arg(thread, 0));
	bt_number pos = BT_AS_NUMBER(bt_arg(thread, 1));
	btio_FileState* state = bt_userdata_get(file);

	if (state->is_open) {
		fseek(state->handle, (long)pos, SEEK_CUR);
		bt_return(thread, BT_VALUE_NULL);
	}
	else {
		bt_Table* result = bt_make_table(ctx, 1);
		result->prototype = bt_type_get_proto(ctx, bt_error_type);
		bt_table_set(ctx, result, bt_error_what_key, bt_close_error_reason);
		bt_return(thread, BT_VALUE_OBJECT(result));
	}
}

static void btio_seek_end(bt_Context* ctx, bt_Thread* thread)
{
	bt_Userdata* file = (bt_Userdata*)BT_AS_OBJECT(bt_arg(thread, 0));
	btio_FileState* state = bt_userdata_get(file);

	if (state->is_open) {
		fseek(state->handle, 0, SEEK_END);
		bt_return(thread, BT_VALUE_NULL);
	}
	else {
		bt_Table* result = bt_make_table(ctx, 1);
		result->prototype = bt_type_get_proto(ctx, bt_error_type);
		bt_table_set(ctx, result, bt_error_what_key, bt_close_error_reason);
		bt_return(thread, BT_VALUE_OBJECT(result));
	}
}

static void btio_tell(bt_Context* ctx, bt_Thread* thread)
{
	bt_Userdata* file = (bt_Userdata*)BT_AS_OBJECT(bt_arg(thread, 0));
	btio_FileState* state = bt_userdata_get(file);

	if (state->is_open) {
		int64_t pos = ftell(state->handle);
		bt_return(thread, BT_VALUE_NUMBER(pos));
	}
	else {
		bt_Table* result = bt_make_table(ctx, 1);
		result->prototype = bt_type_get_proto(ctx, bt_error_type);
		bt_table_set(ctx, result, bt_error_what_key, bt_close_error_reason);
		bt_return(thread, BT_VALUE_OBJECT(result));
	}
}

static void btio_read(bt_Context* ctx, bt_Thread* thread)
{
	bt_gc_pause(ctx);
	
	bt_Userdata* file = (bt_Userdata*)BT_AS_OBJECT(bt_arg(thread, 0));
	size_t size = (size_t)BT_AS_NUMBER(bt_arg(thread, 1));
	btio_FileState* state = bt_userdata_get(file);

	if (state->is_open) {
		if (size == 0) {
			int64_t pos = ftell(state->handle);
			fseek(state->handle, 0, SEEK_END);
			size = (size_t)ftell(state->handle);
			fseek(state->handle, (long)pos, SEEK_SET);
		}

		char* buffer = bt_gc_alloc(ctx, size);

		size_t n_read = fread(buffer, 1, size, state->handle);

		bt_String* as_string = bt_make_string_len(ctx, buffer, (uint32_t)n_read);
		bt_gc_free(ctx, buffer, size);

		if (n_read != size) {
			if (!feof(state->handle)) {
				bt_String* what = bt_make_string(ctx, error_to_desc(errno));
				bt_Table* result = bt_make_table(ctx, 1);
				result->prototype = bt_type_get_proto(ctx, bt_error_type);
				bt_table_set(ctx, result, bt_error_what_key, BT_VALUE_OBJECT(what));
				bt_return(thread, BT_VALUE_OBJECT(result));
			}
			else {
				bt_return(thread, BT_VALUE_OBJECT(as_string));
			}
		}
		else {
			bt_return(thread, BT_VALUE_OBJECT(as_string));
		}
	}
	else {
		bt_Table* result = bt_make_table(ctx, 1);
		result->prototype = bt_type_get_proto(ctx, bt_error_type);
		bt_table_set(ctx, result, bt_error_what_key, bt_close_error_reason);
		bt_return(thread, BT_VALUE_OBJECT(result));
	}

	bt_gc_unpause(ctx);
}

static void btio_write(bt_Context* ctx, bt_Thread* thread)
{
	bt_Userdata* file = (bt_Userdata*)BT_AS_OBJECT(bt_arg(thread, 0));
	bt_String* content = (bt_String*)BT_AS_OBJECT(bt_arg(thread, 1));
	btio_FileState* state = bt_userdata_get(file);

	if (state->is_open) {
		size_t n_written = fwrite(BT_STRING_STR(content), 1, content->len, state->handle);

		if (n_written != content->len) {
			bt_String* what = bt_make_string(ctx, error_to_desc(errno));
			bt_Table* result = bt_make_table(ctx, 1);
			result->prototype = bt_type_get_proto(ctx, bt_error_type);
			bt_table_set(ctx, result, bt_error_what_key, BT_VALUE_OBJECT(what));
			bt_return(thread, BT_VALUE_OBJECT(result));
		}
		else {
			bt_return(thread, BT_VALUE_NULL);
		}
	}
	else {
		bt_Table* result = bt_make_table(ctx, 1);
		result->prototype = bt_type_get_proto(ctx, bt_error_type);
		bt_table_set(ctx, result, bt_error_what_key, bt_close_error_reason);
		bt_return(thread, BT_VALUE_OBJECT(result));
	}
}

static void btio_iseof(bt_Context* ctx, bt_Thread* thread)
{
	bt_Userdata* file = (bt_Userdata*)BT_AS_OBJECT(bt_arg(thread, 0));
	btio_FileState* state = bt_userdata_get(file);

	if (state->is_open) {
		int32_t result = feof(state->handle);
		bt_return(thread, bt_make_bool(result != 0));
	}
	else {
		bt_return(thread, BT_VALUE_FALSE);
	}
}

static void btio_delete(bt_Context* ctx, bt_Thread* thread)
{
	bt_String* path = (bt_String*)BT_AS_OBJECT(bt_arg(thread, 0));

	int32_t result = remove(BT_STRING_STR(path));

	if (result != 0) {
		bt_String* what = bt_make_string(ctx, error_to_desc(errno));
		bt_Table* result = bt_make_table(ctx, 1);
		result->prototype = bt_type_get_proto(ctx, bt_error_type);
		bt_table_set(ctx, result, bt_error_what_key, BT_VALUE_OBJECT(what));
		bt_return(thread, BT_VALUE_OBJECT(result));
	}
	else {
		bt_return(thread, BT_VALUE_NULL);
	}
}

void boltstd_open_io(bt_Context* context)
{
	bt_Module* module = bt_make_user_module(context);

	bt_close_error_reason = BT_VALUE_OBJECT(bt_make_string_hashed(context, "File already closed"));
	bt_add_ref(context, BT_AS_OBJECT(bt_close_error_reason));

	io_file_type = bt_make_userdata_type(context, "File");
	bt_userdata_type_set_finalizer(io_file_type, btio_file_finalizer);
	bt_module_export(context, module, bt_make_alias(context, "File", io_file_type),
		BT_VALUE_CSTRING(context, "File"), BT_VALUE_OBJECT(io_file_type));
	bt_add_ref(context, (bt_Object*)io_file_type);

	bt_Type* open_args[] = { context->types.string, context->types.string };
	bt_Type* open_return_type = bt_make_union(context);
	bt_push_union_variant(context, open_return_type, io_file_type);
	bt_push_union_variant(context, open_return_type, bt_error_type);
	bt_module_export_native(context, module, "open", btio_open, open_return_type, open_args, 2);

	bt_Type* optional_error = bt_make_nullable(context, bt_error_type);
	bt_module_export_native(context, module, "close", btio_close, optional_error, &io_file_type, 1);

	bt_Type* get_size_return_type = bt_make_union(context);
	bt_push_union_variant(context, get_size_return_type, context->types.number);
	bt_push_union_variant(context, get_size_return_type, bt_error_type);
	bt_module_export_native(context, module, "get_size", btio_get_size, get_size_return_type, &io_file_type, 1);

	bt_Type* seek_args[] = { io_file_type, context->types.number };
	bt_module_export_native(context, module, "seek_set", btio_seek_set, optional_error, seek_args, 2);
	bt_module_export_native(context, module, "seek_relative", btio_seek_relative, optional_error, seek_args, 2);
	bt_module_export_native(context, module, "seek_end", btio_seek_end, optional_error, &io_file_type, 1);

	bt_Type* number_or_error = bt_make_union(context);
	bt_push_union_variant(context, number_or_error, context->types.number);
	bt_push_union_variant(context, number_or_error, bt_error_type);

	bt_module_export_native(context, module, "tell", btio_tell, number_or_error, &io_file_type, 1);

	bt_Type* string_or_error = bt_make_union(context);
	bt_push_union_variant(context, string_or_error, context->types.string);
	bt_push_union_variant(context, string_or_error, bt_error_type);

	bt_Type* read_args[] = { io_file_type, context->types.number };
	bt_module_export_native(context, module, "read", btio_read, string_or_error, read_args, 2);

	bt_Type* write_args[] = { io_file_type, context->types.string };
	bt_module_export_native(context, module, "write", btio_write, optional_error, write_args, 2);

	bt_module_export_native(context, module, "is_eof", btio_iseof, context->types.boolean, &io_file_type, 1);
	bt_module_export_native(context, module, "delete", btio_delete, optional_error, &context->types.string, 1);

	bt_register_module(context, BT_VALUE_CSTRING(context, "io"), module);
}
