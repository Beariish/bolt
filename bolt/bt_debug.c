#include "bt_debug.h"

#include "bt_value.h"
#include "bt_gc.h"

#include <stdio.h>

static const char* ast_node_type_to_string(bt_AstNode* node)
{
	switch (node->type) {
	case BT_AST_NODE_LITERAL: return "LITERAL";
	case BT_AST_NODE_IDENTIFIER: return "IDENTIFIER";
	case BT_AST_NODE_IMPORT_REFERENCE: return "IMPORT";
	case BT_AST_NODE_BINARY_OP: return "BINARY OP";
	case BT_AST_NODE_UNARY_OP: return "UNARY OP";
	case BT_AST_NODE_LET: return "LET";
	case BT_AST_NODE_RETURN: return "RETURN";
	case BT_AST_NODE_CALL: return "CALL";
	case BT_AST_NODE_EXPORT: return "EXPORT";
	case BT_AST_NODE_IF: return "IF";
	default: return "<UNKNOWN>";
	}
}

static const char* ast_node_op_to_string(bt_AstNode* node)
{
	switch (node->type) {
	case BT_AST_NODE_BINARY_OP: {
		switch (node->source->type) {
		case BT_TOKEN_ASSIGN: return "=";
		case BT_TOKEN_PLUSEQ: return "+=";
		case BT_TOKEN_MINUSEQ: return "-=";
		case BT_TOKEN_MULEQ: return "*=";
		case BT_TOKEN_DIVEQ: return "/=";
		case BT_TOKEN_PLUS: return "+";
		case BT_TOKEN_MINUS: return "-";
		case BT_TOKEN_MUL: return "*";
		case BT_TOKEN_DIV: return "/";
		case BT_TOKEN_PERIOD: return ".";
		case BT_TOKEN_AND: return "and";
		case BT_TOKEN_OR: return "or";
		case BT_TOKEN_EQUALS: return "==";
		case BT_TOKEN_NOTEQ: return "!=";
		case BT_TOKEN_LT: return "<";
		case BT_TOKEN_LTE: return "<=";
		case BT_TOKEN_GT: return ">";
		case BT_TOKEN_GTE: return ">=";
		case BT_TOKEN_NULLCOALESCE: return "??";
		case BT_TOKEN_LEFTBRACKET: return "[]";
		default: return "[???]";
		}
	} break;
	case BT_AST_NODE_UNARY_OP: {
		switch (node->source->type) {
		case BT_TOKEN_NOT: return "not";
		case BT_TOKEN_PLUS: return "+";
		case BT_TOKEN_MINUS: return "-";
		case BT_TOKEN_QUESTION: return "?";
		default: return "[???]";
		}
	} break;
	}

	return "[WHAT]";
}

static void recursive_print_ast_node(bt_AstNode* node, uint32_t depth)
{
	const char* name = ast_node_type_to_string(node);
	switch (node->type) {
	case BT_AST_NODE_LITERAL: case BT_AST_NODE_IDENTIFIER: case BT_AST_NODE_IMPORT_REFERENCE:
		printf("%*s%s %.*s\n", depth * 4, "", name, node->source->source.length, node->source->source.source);
		break;
	case BT_AST_NODE_UNARY_OP:
		printf("%*s%s %s\n", depth * 4, "", name, ast_node_op_to_string(node));
		recursive_print_ast_node(node->as.unary_op.operand, depth + 1);
		break;
	case BT_AST_NODE_BINARY_OP:
		printf("%*s%s %s\n", depth * 4, "", name, ast_node_op_to_string(node));
		recursive_print_ast_node(node->as.binary_op.left, depth + 1);
		recursive_print_ast_node(node->as.binary_op.right, depth + 1);
		break;
	case BT_AST_NODE_LET:
		printf("%*s%s %s\n", depth * 4, "", name, node->as.let.is_const ? "const" : "");
		printf("%*sname: %.*s\n", (depth + 1) * 4, "", node->as.let.name.length, node->as.let.name.source);
		printf("%*stype: %s\n", (depth + 1) * 4, "", node->resulting_type->name);
		recursive_print_ast_node(node->as.let.initializer, depth + 1);
		break;
	case BT_AST_NODE_RETURN:
		printf("%*s%s\n", depth * 4, "", name);
		recursive_print_ast_node(node->as.ret.expr, depth + 1);
		break;
	case BT_AST_NODE_FUNCTION:
		printf("%*s<fn: 0x%llx>\n", depth * 4, "", (uint64_t)node);
		break;
	case BT_AST_NODE_CALL:
		printf("%*s%s\n", depth * 4, "", name);
		recursive_print_ast_node(node->as.call.fn, depth + 1);
		for (uint8_t i = 0; i < node->as.call.args.length; ++i) {
			bt_AstNode* arg = node->as.call.args.elements[i];
			recursive_print_ast_node(arg, depth + 1);
		}
		break;
	case BT_AST_NODE_IF: {
		bt_AstNode* last = 0;
		bt_AstNode* current = node;

		while (current) {
			if (last && current->as.branch.condition) name = "ELSE IF";
			else if (last) name = "ELSE";

			printf("%*s%s\n", depth * 4, "", name);
			if(current->as.branch.condition)
				recursive_print_ast_node(current->as.branch.condition, depth + 2);
			for (uint8_t i = 0; i < current->as.branch.body.length; ++i) {
				bt_AstNode* arg = current->as.branch.body.elements[i];
				recursive_print_ast_node(arg, depth + 1);
			}

			last = current;
			current = current->as.branch.next;
		}
	} break;
	case BT_AST_NODE_EXPORT: {
		printf("%*s%s\n", depth * 4, "", name);
		recursive_print_ast_node(node->as.exp.value, depth + 1);
	} break;
	default:
		printf("<unsupported node type!>\n");
	}
}

void bt_debug_print_parse_tree(bt_Parser* parser)
{
	bt_AstBuffer* body = &parser->root->as.module.body;

	for (uint32_t index = 0; index < body->length; index++)
	{
		bt_AstNode* current = body->elements[index];

		recursive_print_ast_node(current, 0);
	}
}

static const char* op_to_mnemonic[] = {
#define X(op) #op,
	BT_OPS_X
#undef X
};

static bt_bool is_op_abc(uint8_t op) {
	switch (op) {
	case BT_OP_EXPORT: case BT_OP_CLOSE:
	case BT_OP_ADD: case BT_OP_SUB: case BT_OP_MUL: case BT_OP_DIV:
	case BT_OP_EQ: case BT_OP_NEQ: case BT_OP_LT: case BT_OP_LTE:
	case BT_OP_MFEQ: case BT_OP_MFNEQ:
	case BT_OP_LOAD_IDX: case BT_OP_LOAD_IDX_K: case BT_OP_STORE_IDX_K:
	case BT_OP_STORE_IDX: case BT_OP_LOAD_PROTO:
	case BT_OP_COALESCE: case BT_OP_TCHECK:
	case BT_OP_TCAST: case BT_OP_TSET:
	case BT_OP_CALL: case BT_OP_REC_CALL:
	case BT_OP_LOAD_SUB_F: case BT_OP_STORE_SUB_F:
		return BT_TRUE;
	default:
		return BT_FALSE;
	}
}

static bt_bool is_op_ab(uint8_t op) {
	switch (op) {
	case BT_OP_LOAD_BOOL: case BT_OP_MOVE:
	case BT_OP_LOADUP: case BT_OP_STOREUP:
	case BT_OP_NEG: case BT_OP_NOT:
	case BT_OP_EXPECT:
	case BT_OP_APPEND_F:
		return BT_TRUE;
	default:
		return BT_FALSE;
	}
}

static bt_bool is_op_a(uint8_t op) {
	switch (op) {
	case BT_OP_LOAD_NULL: case BT_OP_RETURN:
		return BT_TRUE;
	default:
		return BT_FALSE;
	}
}

static bt_bool is_op_aibc(uint8_t op) {
	switch (op) {
	case BT_OP_LOAD: case BT_OP_LOAD_SMALL:
	case BT_OP_LOAD_IMPORT: case BT_OP_TABLE:
	case BT_OP_ARRAY: case BT_OP_JMPF:
	case BT_OP_NUMFOR: case BT_OP_ITERFOR: 
	case BT_OP_TEST:
		return BT_TRUE;
	default:
		return BT_FALSE;
	}
}

static bt_bool is_op_ibc(uint8_t op) {
	switch (op) {
	case BT_OP_JMP:
	case BT_OP_IDX_EXT:
		return BT_TRUE;
	default:
		return BT_FALSE;
	}
}

#ifdef _MSC_VER
#pragma warning(disable: 4477)
#endif

static void format_single_instruction(char* buffer, bt_Op instruction)
{
	size_t len = 0;
	if (BT_IS_ACCELERATED(instruction)) {
		len = sprintf(buffer, "ACC ");
	}

	uint8_t op = BT_GET_OPCODE(instruction);
	len += sprintf(buffer + len, op_to_mnemonic[op]);
	
	if (is_op_abc(op)) {
		len += sprintf(buffer + len, "%*s%4d, %4d, %4d", (int)(15 - len), " ", BT_GET_A(instruction), BT_GET_B(instruction), BT_GET_C(instruction));
	}
	else if (is_op_ab(op)) {
		len += sprintf(buffer + len, "%*s%4d, %4d", (int)(15 - len), " ", BT_GET_A(instruction), BT_GET_B(instruction));
	}
	else if (is_op_a(op)) {
		len += sprintf(buffer + len, "%*s%4d", (int)(15 - len), " ", BT_GET_A(instruction));
	}
	else if (is_op_aibc(op)) {
		len += sprintf(buffer + len, "%*s%4d, %4d", (int)(15 - len), " ", BT_GET_A(instruction), BT_GET_IBC(instruction));
	}
	else if (is_op_ibc(op)) {
		len += sprintf(buffer + len, "%*s%4d", (int)(15 - len), " ", BT_GET_IBC(instruction));
	}

	buffer[len] = 0;
}

bt_String* bt_debug_dump_fn(bt_Context* ctx, bt_Callable* function)
{
	const char* name = "";
	const char* mod_name = "";
	uint32_t stack_size = 0;
	bt_ValueBuffer constants;
	bt_buffer_empty(&constants);
	bt_InstructionBuffer instructions;
	bt_buffer_empty(&instructions);
	bt_bool has_debug = 0;

	if (BT_OBJECT_GET_TYPE(function) == BT_OBJECT_TYPE_CLOSURE) {
		name = function->cl.fn->signature->name;
		mod_name = BT_STRING_STR(function->cl.fn->module->name);
		stack_size = function->cl.fn->stack_size;
		constants = function->cl.fn->constants;
		instructions = function->cl.fn->instructions;
		has_debug = function->cl.fn->debug != 0;
	}
	else if (BT_OBJECT_GET_TYPE(function) == BT_OBJECT_TYPE_FN) {
		name = function->fn.signature->name;
		mod_name = BT_STRING_STR(function->fn.module->name);
		stack_size = function->fn.stack_size;
		constants = function->fn.constants;
		instructions = function->fn.instructions;
		has_debug = function->fn.debug != 0;
	}
	else if (BT_OBJECT_GET_TYPE(function) == BT_OBJECT_TYPE_MODULE) {
		name = BT_STRING_STR(function->module.name);
		mod_name = BT_STRING_STR(function->module.name);
		stack_size = function->module.stack_size;
		constants = function->module.constants;
		instructions = function->module.instructions;
		has_debug = function->module.debug_locs != 0;
	}

	// this function does a lot of intermediate allocating, let's pause until end
	bt_gc_pause(ctx);

	bt_String* result = bt_make_string_empty(ctx, 0);
	result = bt_string_append_cstr(ctx, result, name);
	result = bt_string_append_cstr(ctx, result, "\n\tModule: ");
	result = bt_string_append_cstr(ctx, result, mod_name);
	result = bt_string_append_cstr(ctx, result, "\n\tStack size: ");
	result = bt_string_concat(ctx, result, bt_to_string(ctx, BT_VALUE_NUMBER(stack_size)));
	result = bt_string_append_cstr(ctx, result, "\n\tHas debug: ");
	result = bt_string_append_cstr(ctx, result, has_debug ? "YES" : "NO");
	result = bt_string_append_cstr(ctx, result, "\n");

	if (BT_OBJECT_GET_TYPE(function) == BT_OBJECT_TYPE_CLOSURE) {
		result = bt_string_append_cstr(ctx, result, "\tUpvals [");
		result = bt_string_concat(ctx, result, bt_to_string(ctx, BT_VALUE_NUMBER(function->cl.num_upv)));
		result = bt_string_append_cstr(ctx, result, "]:\n");

		for (uint32_t i = 0; i < function->cl.num_upv; ++i) {
			result = bt_string_append_cstr(ctx, result, "\t  [");
			result = bt_string_concat(ctx, result, bt_to_string(ctx, BT_VALUE_NUMBER(i)));
			result = bt_string_append_cstr(ctx, result, "]: ");
			result = bt_string_concat(ctx, result, bt_to_string(ctx, BT_CLOSURE_UPVALS(function)[i]));
			result = bt_string_append_cstr(ctx, result, "\n");
		}
	}

	result = bt_string_append_cstr(ctx, result, "\tConstants [");
	result = bt_string_concat(ctx, result, bt_to_string(ctx, BT_VALUE_NUMBER(constants.length)));
	result = bt_string_append_cstr(ctx, result, "]:\n");

	for (uint32_t i = 0; i < constants.length; ++i) {
		result = bt_string_append_cstr(ctx, result, "\t  [");
		result = bt_string_concat(ctx, result, bt_to_string(ctx, BT_VALUE_NUMBER(i)));
		result = bt_string_append_cstr(ctx, result, "]: ");
		result = bt_string_concat(ctx, result, bt_to_string(ctx, constants.elements[i]));
		result = bt_string_append_cstr(ctx, result, "\n");
	}

	result = bt_string_append_cstr(ctx, result, "\tCode [");
	result = bt_string_concat(ctx, result, bt_to_string(ctx, BT_VALUE_NUMBER(instructions.length)));
	result = bt_string_append_cstr(ctx, result, "]:\n");

	char buffer[128];
	for (uint32_t i = 0; i < instructions.length; ++i) {
		buffer[sprintf(buffer, "\t  [%03u]: ", i)] = 0;
		result = bt_string_append_cstr(ctx, result, buffer);

		format_single_instruction(buffer, instructions.elements[i]);
		result = bt_string_append_cstr(ctx, result, buffer);
		result = bt_string_append_cstr(ctx, result, "\n");
	}

	bt_gc_unpause(ctx);

	return result;
}
