#pragma once

#include "bt_prelude.h"

typedef enum {
	/*
		R: function-local register array, starting from 0
		L: function-specific literal array, containing precomputed literal values
		frame: vm call frame, has info about current invocation 
	*/

	BT_OP_LOAD,              // R(a) = L[ubc]
	BT_OP_LOAD_SMALL,		 // R(a) = tonumber(ibc)
	BT_OP_LOAD_NULL,		 // R(a) = null
	BT_OP_LOAD_BOOL,		 // R(a) = b ? BT_TRUE : BT_FALSE
	BT_OP_TABLE,             // R(a) = new tablesize(b)
	BT_OP_ARRAY,             // R(a) = new array[b]
							 
	BT_OP_MOVE,				 // R(a) = R(b)
							 
	BT_OP_NEG,				 // R(a) = -R(b)
	BT_OP_ADD,				 // R(a) = R(b) + R(c)
	BT_OP_SUB,				 // R(a) = R(b) - R(c)
	BT_OP_MUL,				 // R(a) = R(b) * R(c)
	BT_OP_DIV,				 // R(a) = R(b) / R(c)
							 
	BT_OP_EQ,                // R(a) = R(b) == R(c)
	BT_OP_NEQ,	             // R(a) = R(b) != R(c)
	BT_OP_AND,               // R(a) = R(b) and R(c)
	BT_OP_OR,	             // R(a) = R(b) or R(c)
	BT_OP_NOT,				 // R(a) = not R(b)
							 
	BT_OP_LOAD_IDX,			 // R(a) = b[c]
	BT_OP_STORE_IDX,		 // b[c] = R(a)
							 
	BT_OP_EXISTS,			 // R(a) = R(b) != null
	BT_OP_COALESCE,			 // R(a) = R(b) == null ? R(c) : R(b)
							 
	BT_OP_CALL,              // R(a) = R(b)(R(b + 1) .. R(b + c))

	BT_OP_RETURN,			 // R(frame->ret_pos) = R(a)
	BT_OP_END,               // return without value

	// sentinel opcode that is inserted at the end of buffers for safety - 
	// should alwayas be preceeded by a return op, and thus never invoked
	BT_OP_HALT, 
} bt_OpCode;

typedef struct bt_Op {
	uint8_t op, a;
	union {
		struct { uint8_t b, c; };
		struct { uint16_t ubc; };
		struct { int16_t ibc; };
	};
} bt_Op;