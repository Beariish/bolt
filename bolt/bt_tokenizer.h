#pragma once

#include "bt_buffer.h"

typedef enum {
	BT_TOKEN_UNKNOWN,
	BT_TOKEN_EOS,
	BT_TOKEN_IDENTIFIER,

	BT_TOKEN_FALSE_LITERAL,
	BT_TOKEN_TRUE_LITERAL,
	BT_TOKEN_STRING_LITERAL,
	BT_TOKEN_IDENTIFER_LITERAL,
	BT_TOKEN_NUMBER_LITERAL,
	BT_TOKEN_NULL_LITERAL,

	BT_TOKEN_LEFTPAREN, BT_TOKEN_RIGHTPAREN,
	BT_TOKEN_LEFTBRACE, BT_TOKEN_RIGHTBRACE,
	BT_TOKEN_LEFTBRACKET, BT_TOKEN_RIGHTBRACKET,

	BT_TOKEN_COLON, BT_TOKEN_SEMICOLON,
	BT_TOKEN_PERIOD, BT_TOKEN_COMMA,
	BT_TOKEN_QUESTION,

	BT_TOKEN_VARARG, BT_TOKEN_NULLCOALESCE,

	BT_TOKEN_GT, BT_TOKEN_GTE,
	BT_TOKEN_LT, BT_TOKEN_LTE,

	BT_TOKEN_ASSIGN, BT_TOKEN_EQUALS,
	BT_TOKEN_BANG, BT_TOKEN_NOTEQ,
	BT_TOKEN_PLUS, BT_TOKEN_PLUSEQ,
	BT_TOKEN_MINUS, BT_TOKEN_MINUSEQ,
	BT_TOKEN_MUL, BT_TOKEN_MULEQ,
	BT_TOKEN_DIV, BT_TOKEN_DIVEQ,

	BT_TOKEN_LET, BT_TOKEN_CONST, BT_TOKEN_FN,
	BT_TOKEN_RETURN, BT_TOKEN_TYPE, BT_TOKEN_METHOD,
	BT_TOKEN_IF, BT_TOKEN_ELSE, BT_TOKEN_FOR, BT_TOKEN_IN,
	BT_TOKEN_TO, BT_TOKEN_BY, BT_TOKEN_IS, BT_TOKEN_AS,
	BT_TOKEN_FINAL, BT_TOKEN_UNSEALED, BT_TOKEN_FATARROW,
	BT_TOKEN_ENUM, BT_TOKEN_BREAK, BT_TOKEN_CONTINUE,
	BT_TOKEN_DO, BT_TOKEN_THEN,

	BT_TOKEN_OR, BT_TOKEN_AND, BT_TOKEN_NOT, BT_TOKEN_SATISFIES,

	BT_TOKEN_COMPOSE, BT_TOKEN_UNION, BT_TOKEN_TYPEOF,

	BT_TOKEN_IMPORT, BT_TOKEN_EXPORT, BT_TOKEN_FROM,

	BT_TOKEN_MAX,
} bt_TokenType;

typedef struct {
	bt_StrSlice source;
	uint16_t line, col, idx;
	bt_TokenType type : 8;
} bt_Token;

typedef struct {
	bt_TokenType type;
	union {
		bt_StrSlice as_str;
		bt_number as_num;
	};
} bt_Literal;

typedef bt_Buffer(bt_Token*) bt_TokenBuffer;

typedef struct {
	bt_Context* context;

	bt_TokenBuffer tokens;
	bt_Buffer(bt_Literal) literals;
	int32_t last_consumed;

	const char* source_name;
	const char* source;
	char* current;

	bt_Token* literal_zero;
	bt_Token* literal_one;
	bt_Token* literal_true;
	bt_Token* literal_false;
	bt_Token* literal_empty_string;
	bt_Token* literal_null;

	uint16_t line, col;
} bt_Tokenizer;

BOLT_API bt_Tokenizer bt_open_tokenizer(bt_Context* context);
BOLT_API void bt_close_tokenizer(bt_Tokenizer* tok);

BOLT_API void bt_tokenizer_set_source(bt_Tokenizer* tok, const char* source);
BOLT_API void bt_tokenizer_set_source_name(bt_Tokenizer* tok, const char* source_name);
BOLT_API bt_Token* bt_tokenizer_emit(bt_Tokenizer* tok);

BOLT_API bt_Token* bt_tokenizer_peek(bt_Tokenizer* tok);
BOLT_API bt_bool bt_tokenizer_expect(bt_Tokenizer* tok, bt_TokenType type);