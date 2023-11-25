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
		default: return "<???>";
		}
	} break;
	case BT_AST_NODE_UNARY_OP: {
		switch (node->source->type) {
		case BT_TOKEN_NOT: return "not";
		case BT_TOKEN_PLUS: return "+";
		case BT_TOKEN_MINUS: return "-";
		case BT_TOKEN_QUESTION: return "?";
		default: return "<???>";
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

static void print_constants(bt_Context* ctx, bt_ValueBuffer* constants)
{
	printf("Constants: [%d]\n", constants->length);
	for (uint32_t i = 0; i < constants->length; ++i)
	{
		bt_Value val = constants->elements[i];
		bt_String* as_str = bt_to_string(ctx, val);
		printf("[%d]: %s\n", i, BT_STRING_STR(as_str));
	}
}

static void print_code(bt_InstructionBuffer* code)
{
	printf("Code: [%d]\n", code->length);

	for (uint32_t i = 0; i < code->length; ++i)
	{
		bt_Op op = code->elements[i];
		uint8_t code = BT_GET_OPCODE(op);
		uint8_t a = BT_GET_A(op);
		uint8_t b = BT_GET_B(op);
		uint8_t c = BT_GET_C(op);
		int16_t ibc = BT_GET_IBC(op);
		uint16_t ubc = BT_GET_UBC(op);

		switch (code) {
		case BT_OP_LOAD:        printf("[%.3d]: LOAD   %d, %d    \n", i, a, b);    break;
		case BT_OP_LOAD_IMPORT: printf("[%.3d]: IMPORT %d, %d    \n", i, a, b);    break;
		case BT_OP_LOAD_SMALL:  printf("[%.3d]: LOADS  %d, %d    \n", i, a, ibc);  break;
		case BT_OP_LOAD_NULL:   printf("[%.3d]: NULL   %d        \n", i, a);       break;
		case BT_OP_LOAD_BOOL:   printf("[%.3d]: BOOL   %d, %d    \n", i, a, b);    break;
		case BT_OP_LOAD_IDX:    printf("[%.3d]: LIDX   %d, %d, %d\n", i, a, b, c); break;
		case BT_OP_LOAD_IDX_K:  printf("[%.3d]: LIDXK  %d, %d, %d\n", i, a, b, c); break;
		case BT_OP_ADD:         printf("[%.3d]: ADD    %d, %d, %d\n", i, a, b, c); break;
		case BT_OP_SUB:         printf("[%.3d]: SUB    %d, %d, %d\n", i, a, b, c); break;
		case BT_OP_MUL:         printf("[%.3d]: MUL    %d, %d, %d\n", i, a, b, c); break;
		case BT_OP_DIV:         printf("[%.3d]: DIV    %d, %d, %d\n", i, a, b, c); break;
		case BT_OP_EQ:          printf("[%.3d]: EQ     %d, %d, %d\n", i, a, b, c); break;
		case BT_OP_NEQ:         printf("[%.3d]: NEQ    %d, %d, %d\n", i, a, b, c); break;
		case BT_OP_LT:          printf("[%.3d]: LT     %d, %d, %d\n", i, a, b, c); break;
		case BT_OP_LTE:         printf("[%.3d]: LTE    %d, %d, %d\n", i, a, b, c); break;
		case BT_OP_AND:         printf("[%.3d]: AND    %d, %d, %d\n", i, a, b, c); break;
		case BT_OP_OR:          printf("[%.3d]: OR     %d, %d, %d\n", i, a, b, c); break;
		case BT_OP_NOT:         printf("[%.3d]: NOT    %d, %d    \n", i, a, b);    break;
		case BT_OP_COALESCE:    printf("[%.3d]: COALES %d, %d, %d\n", i, a, b, c); break;
		case BT_OP_MOVE:        printf("[%.3d]: MOVE   %d, %d    \n", i, a, b);	   break;
		case BT_OP_EXPORT:      printf("[%.3d]: EXPORT %d, %d, %d\n", i, a, b, c); break;
		case BT_OP_CLOSE:       printf("[%.3d]: CLOSE  %d, %d, %d\n", i, a, b, c); break;
		case BT_OP_LOADUP:      printf("[%.3d]: LOADUP %d, %d    \n", i, a, b);    break;
		case BT_OP_STOREUP:     printf("[%.3d]: STORUP %d, %d    \n", i, a, b);    break;
		case BT_OP_CALL:        printf("[%.3d]: CALL   %d, %d, %d\n", i, a, b, c); break;
		case BT_OP_RETURN:      printf("[%.3d]: RETURN %d        \n", i, a);	   break;
		case BT_OP_EXISTS:      printf("[%.3d]: EXISTS %d, %d    \n", i, a, b);	   break;
		case BT_OP_EXPECT:      printf("[%.3d]: EXPECT %d, %d    \n", i, a, b);	   break;
		case BT_OP_TCHECK:      printf("[%.3d]: TCHECK %d, %d, %d\n", i, a, b, c); break;
		case BT_OP_TSATIS:      printf("[%.3d]: TSATIS %d, %d, %d\n", i, a, b, c); break;
		case BT_OP_TCAST:       printf("[%.3d]: TCAST  %d, %d, %d\n", i, a, b, c); break;
		case BT_OP_COMPOSE:     printf("[%.3d]: TCMP   %d, %d, %d\n", i, a, b, c); break;
		case BT_OP_TSET:        printf("[%.3d]: TSET   %d, %d, %d\n", i, a, b, c); break;
		case BT_OP_NEG:         printf("[%.3d]: NEG    %d, %d    \n", i, a, b);	   break;
		case BT_OP_JMP:         printf("[%.3d]: JMP    %d        \n", i, ibc);     break;
		case BT_OP_JMPF:        printf("[%.3d]: JMPF   %d, %d    \n", i, a, ibc);  break;
		case BT_OP_END:         printf("[%.3d]: END              \n", i);	       break;
		case BT_OP_NUMFOR:      printf("[%.3d]: NUMFOR %d, %d    \n", i, a, ibc);  break;
		case BT_OP_ITERFOR:     printf("[%.3d]: ITRFOR %d, %d    \n", i, a, ibc);  break;
		case BT_OP_TABLE:       printf("[%.3d]: TABLE  %d, %d    \n", i, a, ibc);  break;
		case BT_OP_ARRAY:       printf("[%.3d]: ARRAY  %d, %d    \n", i, a, ibc);  break;
		case BT_OP_STORE_IDX:   printf("[%.3d]: SIDX   %d, %d, %d\n", i, a, b, c); break;
		case BT_OP_STORE_IDX_K: printf("[%.3d]: SIDXK  %d, %d, %d\n", i, a, b, c); break;
		case BT_OP_LOAD_SUB_F:  printf("[%.3d]: LSUBF  %d, %d, %d\n", i, a, b, c); break;
		case BT_OP_STORE_SUB_F: printf("[%.3d]: SSUBF  %d, %d, %d\n", i, a, b, c); break;
		default: printf("[%.3d]: ???\n", i); break;
		}
	}
}

static void print_imports(bt_Context* ctx, bt_ImportBuffer* imports)
{
	
	printf("Imports: [%d]\n", imports->length);
	for (uint32_t i = 0; i < imports->length; ++i)
	{
		bt_ModuleImport* import = imports->elements[i];
		bt_String* as_str = bt_to_string(ctx, import->value);

		printf("[%d]: %s: %s = %s\n", i, BT_STRING_STR(import->name), import->type->name, BT_STRING_STR(as_str));
	}
}

void bt_debug_print_module(bt_Context* ctx, bt_Module* module)
{
	print_imports(ctx, &module->imports);
	print_constants(ctx, &module->constants);
	print_code(&module->instructions);
}

void bt_debug_print_fn(bt_Context* ctx, bt_Fn* fn)
{
	printf("<%s>\n", fn->signature->name);
	print_constants(ctx, &fn->constants);
	print_code(&fn->instructions);
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
	case BT_OP_AND: case BT_OP_OR: case BT_OP_LOAD_IDX:
	case BT_OP_LOAD_IDX_K: case BT_OP_STORE_IDX_K:
	case BT_OP_STORE_IDX: case BT_OP_LOAD_PROTO:
	case BT_OP_COALESCE: case BT_OP_TCHECK:
	case BT_OP_TSATIS: case BT_OP_TCAST:
	case BT_OP_TSET: case BT_OP_COMPOSE:
	case BT_OP_CALL: case BT_OP_LOAD_SUB_F:
	case BT_OP_STORE_SUB_F:
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
	case BT_OP_EXISTS: case BT_OP_EXPECT:
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
		return BT_TRUE;
	default:
		return BT_FALSE;
	}
}

static bt_bool is_op_ibc(uint8_t op) {
	switch (op) {
	case BT_OP_JMP:
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
		len += sprintf(buffer + len, "%*s%3d, %3d, %3d", 15 - len, " ", BT_GET_A(instruction), BT_GET_B(instruction), BT_GET_C(instruction));
	}
	else if (is_op_ab(op)) {
		len += sprintf(buffer + len, "%*s%3d, %3d", 15 - len, " ", BT_GET_A(instruction), BT_GET_B(instruction));
	}
	else if (is_op_a(op)) {
		len += sprintf(buffer + len, "%*s%3d", 15 - len, " ", BT_GET_A(instruction));
	}
	else if (is_op_aibc(op)) {
		len += sprintf(buffer + len, "%*s%3d, %3d", 15 - len, " ", BT_GET_A(instruction), BT_GET_IBC(instruction));
	}
	else if (is_op_ibc(op)) {
		len += sprintf(buffer + len, "%*s%3d", 15 - len, " ", BT_GET_IBC(instruction));
	}

	buffer[len] = 0;
}

bt_String* bt_debug_dump_fn(bt_Context* ctx, bt_Callable* function)
{
	bt_Fn* underlying = (bt_Fn*)function;
	if (BT_OBJECT_GET_TYPE(function) == BT_OBJECT_TYPE_CLOSURE) {
		underlying = function->cl.fn;
	}

	// this function does a lot of intermediate allocating, let's pause until end
	bt_gc_pause(ctx);

	bt_String* result = bt_make_string_empty(ctx, 0);
	result = bt_append_cstr(ctx, result, underlying->signature->name);
	result = bt_append_cstr(ctx, result, "\n\tModule: ");
	result = bt_concat_strings(ctx, result, underlying->module->name);
	result = bt_append_cstr(ctx, result, "\n\tStack size: ");
	result = bt_concat_strings(ctx, result, bt_to_string(ctx, BT_VALUE_NUMBER(underlying->stack_size)));
	result = bt_append_cstr(ctx, result, "\n\tHas debug: ");
	result = bt_append_cstr(ctx, result, underlying->debug ? "YES" : "NO");
	result = bt_append_cstr(ctx, result, "\n");

	if (BT_OBJECT_GET_TYPE(function) == BT_OBJECT_TYPE_CLOSURE) {
		result = bt_append_cstr(ctx, result, "\tUpvals [");
		result = bt_concat_strings(ctx, result, bt_to_string(ctx, BT_VALUE_NUMBER(function->cl.num_upv)));
		result = bt_append_cstr(ctx, result, "]:\n");

		for (uint32_t i = 0; i < function->cl.num_upv; ++i) {
			result = bt_append_cstr(ctx, result, "\t  [");
			result = bt_concat_strings(ctx, result, bt_to_string(ctx, BT_VALUE_NUMBER(i)));
			result = bt_append_cstr(ctx, result, "]: ");
			result = bt_concat_strings(ctx, result, bt_to_string(ctx, BT_CLOSURE_UPVALS(function)[i]));
			result = bt_append_cstr(ctx, result, "\n");
		}
	}

	result = bt_append_cstr(ctx, result, "\tConstants [");
	result = bt_concat_strings(ctx, result, bt_to_string(ctx, BT_VALUE_NUMBER(underlying->constants.length)));
	result = bt_append_cstr(ctx, result, "]:\n");

	for (uint32_t i = 0; i < underlying->constants.length; ++i) {
		result = bt_append_cstr(ctx, result, "\t  [");
		result = bt_concat_strings(ctx, result, bt_to_string(ctx, BT_VALUE_NUMBER(i)));
		result = bt_append_cstr(ctx, result, "]: ");
		result = bt_concat_strings(ctx, result, bt_to_string(ctx, underlying->constants.elements[i]));
		result = bt_append_cstr(ctx, result, "\n");
	}

	result = bt_append_cstr(ctx, result, "\tCode [");
	result = bt_concat_strings(ctx, result, bt_to_string(ctx, BT_VALUE_NUMBER(underlying->instructions.length)));
	result = bt_append_cstr(ctx, result, "]:\n");

	char buffer[128];
	for (uint32_t i = 0; i < underlying->instructions.length; ++i) {
		buffer[sprintf(buffer, "\t  [%03d]: ", i)] = 0;
		result = bt_append_cstr(ctx, result, buffer);

		format_single_instruction(buffer, underlying->instructions.elements[i]);
		result = bt_append_cstr(ctx, result, buffer);
		result = bt_append_cstr(ctx, result, "\n");
	}

	bt_gc_unpause(ctx);

	return result;
}
