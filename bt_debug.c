#include "bt_debug.h"

#include "bt_value.h"

static const char* ast_node_type_to_string(bt_AstNode* node)
{
	switch (node->type) {
	case BT_AST_NODE_LITERAL: return "LITERAL";
	case BT_AST_NODE_IDENTIFIER: return "IDENTIFIER";
	case BT_AST_NODE_BINARY_OP: return "BINARY OP";
	case BT_AST_NODE_UNARY_OP: return "UNARY OP";
	case BT_AST_NODE_LET: return "LET";
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
}

static void recursive_print_ast_node(bt_AstNode* node, uint32_t depth)
{
	const char* name = ast_node_type_to_string(node);
	switch (node->type) {
	case BT_AST_NODE_LITERAL: case BT_AST_NODE_IDENTIFIER:
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

void bt_debug_print_compiler_output(bt_Compiler* compiler)
{
	printf("Constants: [%d]\n", compiler->constants.length);
	for (uint32_t i = 0; i < compiler->constants.length; ++i)
	{
		bt_Value val = *(bt_Value*)bt_buffer_at(&compiler->constants, i);
		if (BT_IS_NUMBER(val))
		{
			printf("[%d]: 0x%llx (%.f)\n", i, val, BT_AS_NUMBER(val));
		}
	}

	printf("Code: [%d]\n", compiler->output.length);

	for (uint32_t i = 0; i < compiler->output.length; ++i)
	{
		bt_Op op = *(bt_Op*)bt_buffer_at(&compiler->output, i);
		switch (op.op) {
		case BT_OP_LOAD:      printf("[%.3d]: LOAD %d, %d\n", i, op.a, op.b);           break;
		case BT_OP_LOAD_NULL: printf("[%.3d]: NULL %d\n", i, op.a);                     break;
		case BT_OP_ADD:       printf("[%.3d]: ADD  %d, %d, %d\n", i, op.a, op.b, op.c);	break;
		case BT_OP_SUB:       printf("[%.3d]: SUB  %d, %d, %d\n", i, op.a, op.b, op.c);	break;
		case BT_OP_MUL:       printf("[%.3d]: MUL  %d, %d, %d\n", i, op.a, op.b, op.c);	break;
		case BT_OP_DIV:       printf("[%.3d]: DIV  %d, %d, %d\n", i, op.a, op.b, op.c);	break;
		case BT_OP_MOVE:      printf("[%.3d]: MOVE %d, %d\n", i, op.a, op.b);	        break;
		default: printf("[%.3d]: ???\n", i); break;
		}
	}
}
