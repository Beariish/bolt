#include "bt_debug.h"

#include "bt_value.h"

#include <stdio.h>
#include <assert.h>

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
		printf("%*s<fn: 0x%llx>\n", depth * 4, "", node);
		break;
	case BT_AST_NODE_CALL:
		printf("%*s%s\n", depth * 4, "", name);
		recursive_print_ast_node(node->as.call.fn, depth + 1);
		for (uint8_t i = 0; i < node->as.call.args.length; ++i) {
			bt_AstNode* arg = *(bt_AstNode**)bt_buffer_at(&node->as.call.args, i);
			recursive_print_ast_node(arg, depth + 1);
		}
		break;
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
	bt_Buffer* body = &parser->root->as.module.body;

	for (uint32_t index = 0; index < body->length; index++)
	{
		bt_AstNode* current = *(bt_AstNode**)bt_buffer_at(body, index);

		recursive_print_ast_node(current, 0);
	}
}

static void print_constants(bt_Context* ctx, bt_Buffer* constants)
{
	printf("Constants: [%d]\n", constants->length);
	for (uint32_t i = 0; i < constants->length; ++i)
	{
		bt_Value val = *(bt_Value*)bt_buffer_at(constants, i);
		bt_String* as_str = bt_to_string(ctx, val);
		printf("[%d]: %s\n", i, as_str->str);
	}
}

static void print_code(bt_Buffer* code)
{
	printf("Code: [%d]\n", code->length);

	for (uint32_t i = 0; i < code->length; ++i)
	{
		bt_Op op = *(bt_Op*)bt_buffer_at(code, i);
		switch (op.op) {
		case BT_OP_LOAD:        printf("[%.3d]: LOAD   %d, %d\n", i, op.a, op.b);           break;
		case BT_OP_LOAD_IMPORT: printf("[%.3d]: IMPORT %d, %d\n", i, op.a, op.ubc);         break;
		case BT_OP_LOAD_SMALL:  printf("[%.3d]: LOADS  %d, %d\n", i, op.a, op.ibc);         break;
		case BT_OP_LOAD_NULL:   printf("[%.3d]: NULL   %d\n", i, op.a);                     break;
		case BT_OP_LOAD_BOOL:   printf("[%.3d]: BOOL   %d, %d\n", i, op.a, op.b);           break;
		case BT_OP_LOAD_IDX:    printf("[%.3d]: LIDX   %d, %d, %d\n", i, op.a, op.b, op.c); break;
		case BT_OP_ADD:         printf("[%.3d]: ADD    %d, %d, %d\n", i, op.a, op.b, op.c); break;
		case BT_OP_SUB:         printf("[%.3d]: SUB    %d, %d, %d\n", i, op.a, op.b, op.c); break;
		case BT_OP_MUL:         printf("[%.3d]: MUL    %d, %d, %d\n", i, op.a, op.b, op.c); break;
		case BT_OP_DIV:         printf("[%.3d]: DIV    %d, %d, %d\n", i, op.a, op.b, op.c); break;
		case BT_OP_AND:         printf("[%.3d]: AND    %d, %d, %d\n", i, op.a, op.b, op.c); break;
		case BT_OP_OR:          printf("[%.3d]: OR     %d, %d, %d\n", i, op.a, op.b, op.c); break;
		case BT_OP_COALESCE:    printf("[%.3d]: COALES %d, %d, %d\n", i, op.a, op.b, op.c); break;
		case BT_OP_MOVE:        printf("[%.3d]: MOVE   %d, %d\n", i, op.a, op.b);	        break;
		case BT_OP_EXPORT:      printf("[%.3d]: EXPORT %d, %d, %d\n", i, op.a, op.b, op.c); break;
		case BT_OP_CALL:        printf("[%.3d]: CALL   %d, %d, %d\n", i, op.a, op.b, op.c); break;
		case BT_OP_RETURN:      printf("[%.3d]: RETURN %d\n", i, op.a);	                    break;
		case BT_OP_EXISTS:      printf("[%.3d]: EXISTS %d, %d\n", i, op.a, op.b);	        break;
		case BT_OP_NEG:         printf("[%.3d]: NEG    %d, %d\n", i, op.a, op.b);	        break;
		case BT_OP_END:         printf("[%.3d]: END\n", i);	                                break;
		case BT_OP_HALT:        printf("[%.3d]: HALT\n", i);	                            break;
		default: printf("[%.3d]: ???\n", i); __debugbreak(); break;
		}
	}
}

static void print_imports(bt_Context* ctx, bt_Buffer* imports)
{
	
	printf("Imports: [%d]\n", imports->length);
	for (uint32_t i = 0; i < imports->length; ++i)
	{
		bt_ModuleImport* import = *(bt_ModuleImport**)bt_buffer_at(imports, i);
		bt_String* as_str = bt_to_string(ctx, import->value);

		printf("[%d]: %s: %s = %s\n", i, import->name->str, import->type->name, as_str->str);
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
