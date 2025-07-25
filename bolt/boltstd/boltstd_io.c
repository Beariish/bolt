#include "boltstd_io.h"

#include "../bt_embedding.h"

#include "boltstd_core.h"

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

static const char* io_file_type_name = "File";
static const char* close_error_reason = "File already closed";

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

		bt_Module* module = bt_get_module(thread);
		bt_Type* file_type = (bt_Type*)bt_object(bt_module_get_storage(module, BT_VALUE_CSTRING(ctx, io_file_type_name)));
		
		bt_Userdata* result = bt_make_userdata(ctx, file_type, &state, sizeof(btio_FileState));
		bt_return(thread, BT_VALUE_OBJECT(result));
	}
	else {
		bt_return(thread, boltstd_make_error(ctx, error_to_desc(errno)));
	}
}

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
		bt_return(thread, boltstd_make_error(ctx, close_error_reason));
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
		bt_return(thread, boltstd_make_error(ctx, close_error_reason));
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
		bt_return(thread, boltstd_make_error(ctx, close_error_reason));
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
		bt_return(thread, boltstd_make_error(ctx, close_error_reason));
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
		bt_return(thread, boltstd_make_error(ctx, close_error_reason));
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
		bt_return(thread, boltstd_make_error(ctx, close_error_reason));
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
				bt_return(thread, boltstd_make_error(ctx, error_to_desc(errno)));
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
		bt_return(thread, boltstd_make_error(ctx, close_error_reason));
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
			bt_return(thread, boltstd_make_error(ctx, error_to_desc(errno)));
		}
		else {
			bt_return(thread, BT_VALUE_NULL);
		}
	}
	else {
		bt_return(thread, boltstd_make_error(ctx, close_error_reason));
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
		bt_return(thread, boltstd_make_error(ctx, error_to_desc(errno)));
	}
	else {
		bt_return(thread, BT_VALUE_NULL);
	}
}

void boltstd_open_io(bt_Context* context)
{
	bt_Module* module = bt_make_module(context);

	bt_Type* string = bt_type_string(context);
	bt_Type* number = bt_type_number(context);
	bt_Type* boolean = bt_type_bool(context);
	
	bt_Type* io_file_type = bt_make_userdata_type(context, io_file_type_name);
	bt_userdata_type_set_finalizer(io_file_type, btio_file_finalizer);

	bt_module_export(context, module, bt_make_alias_type(context, io_file_type_name, io_file_type),
		BT_VALUE_CSTRING(context, io_file_type_name), bt_value((bt_Object*)io_file_type));
	bt_module_set_storage(module, BT_VALUE_CSTRING(context, io_file_type_name), bt_value((bt_Object*)io_file_type));

	bt_Module* core_module = bt_find_module(context, BT_VALUE_CSTRING(context, "core"), BT_FALSE);
	bt_Type* bt_error_type = (bt_Type*)bt_object(bt_module_get_storage(core_module, BT_VALUE_CSTRING(context, bt_error_type_name)));

	bt_Type* open_args[] = { string, string };
	bt_Type* open_returns[] = { io_file_type, bt_error_type };
	bt_Type* open_return_type = bt_make_union_from(context, open_returns, 2);
	bt_module_export_native(context, module, "open", btio_open, open_return_type, open_args, 2);

	bt_Type* optional_error = bt_type_make_nullable(context, bt_error_type);
	bt_module_export_native(context, module, "close", btio_close, optional_error, &io_file_type, 1);

	bt_Type* number_or_error_types[] = { number, bt_error_type };
	bt_Type* number_or_error = bt_make_union_from(context, number_or_error_types, 2);
	bt_module_export_native(context, module, "get_size", btio_get_size, number_or_error, &io_file_type, 1);

	bt_Type* seek_args[] = { io_file_type, number };
	bt_module_export_native(context, module, "seek_set", btio_seek_set, optional_error, seek_args, 2);
	bt_module_export_native(context, module, "seek_relative", btio_seek_relative, optional_error, seek_args, 2);
	bt_module_export_native(context, module, "seek_end", btio_seek_end, optional_error, &io_file_type, 1);
	bt_module_export_native(context, module, "tell", btio_tell, number_or_error, &io_file_type, 1);

	bt_Type* string_or_error_types[] = { string, bt_error_type };
	bt_Type* string_or_error = bt_make_union_from(context, string_or_error_types, 2);

	bt_Type* read_args[] = { io_file_type, number };
	bt_module_export_native(context, module, "read", btio_read, string_or_error, read_args, 2);

	bt_Type* write_args[] = { io_file_type, string };
	bt_module_export_native(context, module, "write", btio_write, optional_error, write_args, 2);

	bt_module_export_native(context, module, "is_eof", btio_iseof, boolean, &io_file_type, 1);
	bt_module_export_native(context, module, "delete", btio_delete, optional_error, &string, 1);

	bt_register_module(context, BT_VALUE_CSTRING(context, "io"), module);
}
