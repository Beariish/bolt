#pragma once

#include "bt_tokenizer.h"
#include "bt_type.h"

typedef enum {
	BT_AST_NODE_MODULE,

	BT_AST_NODE_EXPORT,

	BT_AST_NODE_LITERAL,
	BT_AST_NODE_IDENTIFIER,
	BT_AST_NODE_IMPORT_REFERENCE,
	BT_AST_NODE_ARRAY,
	BT_AST_NODE_TABLE,
	BT_AST_NODE_INDEX,

	BT_AST_NODE_FUNCTION,
	BT_AST_NODE_METHOD,
	BT_AST_NODE_BINARY_OP,
	BT_AST_NODE_UNARY_OP,
	BT_AST_NODE_TYPE,
	BT_AST_NODE_RETURN,
	BT_AST_NODE_IF,
	BT_AST_NODE_LOOP,
	BT_AST_NODE_LET,
	BT_AST_NODE_ASSIGN,
	BT_AST_NODE_CALL,
} bt_AstNodeType;

typedef struct bt_AstNode bt_AstNode;

typedef struct bt_FnArg {
	bt_StrSlice name;
	bt_Type* type;
} bt_FnArg;

typedef struct bt_AstNode {
	bt_AstNodeType type;
	bt_Token* source;
	bt_Type* resulting_type;

	union {
		struct {
			bt_Buffer body;
			bt_Buffer imports;
		} module;

		struct {
			bt_AstNode* left;
			bt_AstNode* right;
		} binary_op;

		struct {
			bt_AstNode* operand;
		} unary_op;

		struct {
			bt_StrSlice name;
			bt_AstNode* initializer;
			bt_bool is_const;
		} let;

		struct {
			bt_AstNode* expr;
		} ret;

		struct {
			bt_Buffer args;
			bt_Buffer body;
			bt_Type* ret_type;
		} fn;

		struct {
			bt_AstNode* fn;
			bt_Buffer args;
		} call;
	} as; 
} bt_AstNode;

typedef struct bt_ParseBinding {
	bt_StrSlice name;
	bt_Type* type;
	bt_bool is_const;
} bt_ParseBinding;

typedef struct bt_ParseScope {
	bt_Buffer bindings;
	struct bt_ParseScope* last;
} bt_ParseScope;

typedef struct {
	bt_Context* context;
	bt_Tokenizer* tokenizer;
	bt_AstNode* root;

	bt_ParseScope* scope;
} bt_Parser;

bt_Parser bt_open_parser(bt_Tokenizer* tkn);
bt_bool bt_parse(bt_Parser* parser);