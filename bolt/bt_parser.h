#pragma once

#include "bt_tokenizer.h"
#include "bt_type.h"
#include "bt_object.h"

typedef enum {
	BT_AST_NODE_MODULE,

	BT_AST_NODE_EXPORT,

	BT_AST_NODE_LITERAL,
	BT_AST_NODE_ENUM_LITERAL,
	BT_AST_NODE_IDENTIFIER,
	BT_AST_NODE_IMPORT_REFERENCE,
	BT_AST_NODE_ARRAY,
	BT_AST_NODE_TABLE,
	BT_AST_NODE_TABLE_ENTRY,
	BT_AST_NODE_HOIST,

	BT_AST_NODE_FUNCTION,
	BT_AST_NODE_METHOD,
	BT_AST_NODE_BINARY_OP,
	BT_AST_NODE_UNARY_OP,
	BT_AST_NODE_TYPE,
	BT_AST_NODE_RETURN,
	BT_AST_NODE_IF,
	BT_AST_NODE_LOOP_WHILE,
	BT_AST_NODE_LOOP_ITERATOR,
	BT_AST_NODE_LOOP_NUMERIC,
	BT_AST_NODE_LET,
	BT_AST_NODE_CALL,
	BT_AST_NODE_ALIAS,

	BT_AST_NODE_BREAK,
	BT_AST_NODE_CONTINUE,
} bt_AstNodeType;

typedef struct bt_AstNode bt_AstNode;

typedef struct bt_FnArg {
	bt_StrSlice name;
	bt_Type* type;
} bt_FnArg;

typedef struct bt_ParseBinding {
	bt_StrSlice name;
	bt_Type* type;
	bt_AstNode* source;
	bt_bool is_const;
} bt_ParseBinding;

typedef bt_Buffer(bt_AstNode*) bt_AstBuffer;
typedef bt_Buffer(bt_FnArg) bt_ArgBuffer;

typedef struct bt_AstNode {
	union {
		struct {
			bt_AstBuffer body;
			bt_ImportBuffer imports;
		} module;

		struct {
			bt_AstNode* left;
			bt_AstNode* right;

			uint8_t idx;
			bt_bool accelerated;

			bt_Type* from;
			bt_Value key;
			bt_bool hoistable;
		} binary_op;

		struct {
			bt_AstNode* operand;
			bt_bool accelerated;
		} unary_op;

		struct {
			bt_StrSlice name;
			bt_AstNode* initializer;
			bt_bool is_const;
		} let;

		struct {
			bt_Type* type;
			bt_bool is_bound;
		} alias;

		struct {
			bt_AstNode* expr;
		} ret;

		struct {
			bt_ArgBuffer args;
			bt_AstBuffer body;
			bt_Buffer(bt_ParseBinding) upvals;
			bt_Type* ret_type;
			bt_AstNode* outer;
		} fn;

		struct {
			bt_Type* containing_type;
			bt_String* name;
			bt_AstNode* fn;
		} method;

		struct {
			bt_AstBuffer args;
			bt_AstNode* fn;
			bt_bool is_methodcall;
		} call;

		struct {
			bt_StrSlice name;
			bt_AstNode* value;
		} exp;

		struct {
			bt_AstBuffer body;
			bt_Token* identifier;
			bt_AstNode* condition;
			bt_AstNode* next;
			bt_Type* bound_type;
			bt_bool is_let;
		} branch;

		struct {
			bt_AstBuffer body;
			bt_AstNode* condition;
		} loop_while;

		struct {
			bt_AstBuffer body;
			bt_AstNode* identifier;
			bt_AstNode* iterator;
		} loop_iterator;

		struct {
			bt_AstBuffer body;
			bt_AstNode* identifier;
			bt_AstNode* start;
			bt_AstNode* stop;
			bt_AstNode* step;
		} loop_numeric;

		struct {
			bt_AstBuffer fields;
			bt_bool typed;
		} table;

		struct {
			bt_AstBuffer items;
			bt_Type* inner_type;
		} arr;

		struct {
			bt_Type* value_type;
			bt_Value key;
			bt_AstNode* value_expr;
		} table_field;

		struct {
			bt_Value value;
		} enum_literal;
	} as; 

	bt_Token* source;
	bt_Type* resulting_type;

	bt_AstNodeType type;
} bt_AstNode;

typedef struct bt_ParseScope {
	bt_Buffer(bt_ParseBinding) bindings;
	struct bt_ParseScope* last;
	bt_bool is_fn_boundary;
} bt_ParseScope;

#define BT_AST_NODE_POOL_SIZE 256

typedef struct bt_AstNodePool {
	bt_AstNode nodes[BT_AST_NODE_POOL_SIZE];
	struct bt_AstNodePool* prev;

	uint16_t count;
} bt_AstNodePool;

typedef struct {
	bt_Context* context;
	bt_Tokenizer* tokenizer;
	bt_AstNode* root;
	bt_AstNode* current_fn;

	bt_AstNodePool* current_pool;

	bt_ParseScope* scope;

	bt_bool has_errored;
} bt_Parser;

BOLT_API bt_Parser bt_open_parser(bt_Tokenizer* tkn);
BOLT_API void bt_close_parser(bt_Parser* parse);
BOLT_API bt_bool bt_parse(bt_Parser* parser);