#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chunk.h"
#include "common.h"
#include "debug.h"
#include "object.h"
#include "scanner.h"
#include "value.h"

#ifdef DEBUG_PRINT_CODE
#include "debug.h"
#endif

typedef struct {
    Token current;
    Token previous;
    bool hadError;
    bool panicMode;
} Parser;

// 初期化式のコンパイル中であることを表す depth の値。
#define UNINITIALIZED_DEPTH (-1)

typedef struct {
    Token name;
    int depth;
} Local;

typedef enum {
    TYPE_FUNCTION,
    TYPE_SCRIPT,
} FunctionType;

typedef struct Compiler {
    struct Compiler *enclosing;
    ObjFunction *function;
    FunctionType type;

    Local locals[UINT8_COUNT];
    int localCount;
    int scopeDepth;
} Compiler;

typedef enum {
    PREC_NONE,
    PREC_ASSIGNMENT,
    PREC_OR,
    PREC_AND,
    PREC_EQUALITY,
    PREC_COMPARISON,
    PREC_TERM,
    PREC_FACTOR,
    PREC_UNARY,
    PREC_CALL,
    PREC_PRIMARY
} Precedence;

typedef void (*ParseFn)(bool canAssign);

typedef struct {
    ParseFn prefix;
    ParseFn infix;
    Precedence precedence;
} ParseRule;

Parser parser;
Compiler *current;

static Chunk *currentChunk() { return &current->function->chunk; }

static void initCompiler(Compiler *compiler, FunctionType type) {
    compiler->enclosing = current;
    compiler->function = NULL;
    compiler->type = type;
    compiler->localCount = 0;
    compiler->scopeDepth = 0;
    compiler->function = newFunction();
    current = compiler;

    if (type != TYPE_SCRIPT) {
	current->function->name =
	    copyString(parser.previous.start, parser.previous.length);
    }

    // スタックスロット 0 を VM 内部用に予約する。
    // 空の名前にすることで、Lox のコードからは参照できないようにする。
    Local *local = &current->locals[current->localCount++];
    local->depth = 0;
    local->name.start = "";
    local->name.length = 0;
}

static void errorAt(Token *token, const char *message) {
    if (parser.panicMode)
	return;
    parser.panicMode = true;

    fprintf(stderr, "[line %d] Error", token->line);

    if (token->type == TOKEN_EOF) {
	fprintf(stderr, " at end");
    } else if (token->type == TOKEN_ERROR) {
	// Skip
    } else {
	fprintf(stderr, " at '%.*s'", token->length, token->start);
    }

    fprintf(stderr, ": %s\n", message);
    parser.hadError = true;
}

static void errorAtCurrent(const char *message) {
    errorAt(&parser.current, message);
}

static void error(const char *message) { errorAt(&parser.previous, message); }

static void advance() {
    parser.previous = parser.current;

    for (;;) {
	parser.current = scanToken();
	if (parser.current.type != TOKEN_ERROR)
	    break;

	errorAtCurrent(parser.current.start);
    }
}

static void consume(TokenType type, const char *message) {
    if (parser.current.type == type) {
	advance();
	return;
    }

    errorAtCurrent(message);
}

static void emitByte(uint8_t byte) {
    writeChunk(currentChunk(), byte, parser.previous.line);
}

static void emitBytes(uint8_t byte1, uint8_t byte2) {
    emitByte(byte1);
    emitByte(byte2);
}

static void emitReturn() {
    // 暗黙の戻り値として nil を積んでおく (return を書かなかった場合の)
    emitByte(OP_NIL);
    emitByte(OP_RETURN);
}

static int emitJump(uint8_t instruction) {
    emitByte(instruction);
    emitByte(0xff);
    emitByte(0xff);
    return currentChunk()->count - 2;
}

static void patchJump(int offset) {
    int jump = currentChunk()->count - offset - 2;

    if (jump > UINT16_MAX) {
	error("Too much code to jump over.");
    }

    currentChunk()->code[offset] = (jump >> 8) & 0xff;
    currentChunk()->code[offset + 1] = jump & 0xff;
}

static void emitLoop(int loopStart) {
    emitByte(OP_LOOP);

    int offset = currentChunk()->count - loopStart + 2;
    if (offset > UINT16_MAX)
	error("Loop body too large.");

    emitByte((offset >> 8) & 0xff);
    emitByte(offset & 0xff);
}

static uint8_t makeConstant(Value value) {
    int constant = addConstant(currentChunk(), value);
    if (constant > UINT8_MAX) {
	error("Too many constants in one chunk.");
	return 0;
    }

    return (uint8_t)constant;
}

static void emitConstant(Value value) {
    emitBytes(OP_CONSTANT, makeConstant(value));
}

static void expression();
static void statement();
static void declaration();
static void varDeclaration();
static void expressionStatement();
static ParseRule *getRule(TokenType type);
static void parsePrecedence(Precedence precedence);
static uint8_t identifierConstant(Token *name);
static int resolveLocal(Compiler *compiler, Token *name);
static bool check(TokenType type);
static bool match(TokenType type);

static void parsePrecedence(Precedence precedence) {
    advance();
    ParseFn prefixRule = getRule(parser.previous.type)->prefix;
    if (prefixRule == NULL) {
	error("Expect expression.");
	return;
    }

    bool canAssign = precedence <= PREC_ASSIGNMENT;
    prefixRule(canAssign);

    while (precedence <= getRule(parser.current.type)->precedence) {
	advance();
	ParseFn infixRule = getRule(parser.previous.type)->infix;
	infixRule(canAssign);
    }

    if (canAssign && match(TOKEN_EQUAL)) {
	error("Invalid assignment target.");
    }
}

static ObjFunction *endCompiler() {
    emitReturn();
    ObjFunction *function = current->function;

#ifdef DEBUG_PRINT_CODE
    if (!parser.hadError) {
	disassembleChunk(currentChunk(),
	                  function->name != NULL ? function->name->chars
	                                          : "<script>");
    }
#endif

    current = current->enclosing;
    return function;
}

static void binary(bool canAssign) {
    (void)canAssign;
    TokenType operatorType = parser.previous.type;
    ParseRule *rule = getRule(operatorType);

    parsePrecedence((Precedence)(rule->precedence + 1));

    switch (operatorType) {
    case TOKEN_BANG_EQUAL:
	emitBytes(OP_EQUAL, OP_NOT);
	break;
    case TOKEN_EQUAL_EQUAL:
	emitByte(OP_EQUAL);
	break;
    case TOKEN_GREATER:
	emitByte(OP_GREATER);
	break;
    case TOKEN_GREATER_EQUAL:
	emitBytes(OP_LESS, OP_NOT);
	break;
    case TOKEN_LESS:
	emitByte(OP_LESS);
	break;
    case TOKEN_LESS_EQUAL:
	emitBytes(OP_GREATER, OP_NOT);
	break;
    case TOKEN_PLUS:
	emitByte(OP_ADD);
	break;
    case TOKEN_MINUS:
	emitByte(OP_SUBTRACT);
	break;
    case TOKEN_STAR:
	emitByte(OP_MULTIPLY);
	break;
    case TOKEN_SLASH:
	emitByte(OP_DIVIDE);
	break;
    default:
	return;
    }
}

static void and_(bool canAssign) {
    (void)canAssign;
    int endJump = emitJump(OP_JUMP_IF_FALSE);

    // 左辺が真のときだけ左辺を捨て、右辺を評価する。
    emitByte(OP_POP);
    parsePrecedence(PREC_AND);

    patchJump(endJump);
}

static void or_(bool canAssign) {
    (void)canAssign;
    int elseJump = emitJump(OP_JUMP_IF_FALSE);
    int endJump = emitJump(OP_JUMP);

    // 左辺が偽のときだけ左辺を捨て、右辺を評価する。
    patchJump(elseJump);
    emitByte(OP_POP);
    parsePrecedence(PREC_OR);
    patchJump(endJump);
}

static void literal(bool canAssign) {
    (void)canAssign;
    switch (parser.previous.type) {
    case TOKEN_FALSE:
	emitByte(OP_FALSE);
	break;
    case TOKEN_NIL:
	emitByte(OP_NIL);
	break;
    case TOKEN_TRUE:
	emitByte(OP_TRUE);
	break;
    default:
	return;
    }
}

static void expression() { parsePrecedence(PREC_ASSIGNMENT); }

static void grouping(bool canAssign) {
    (void)canAssign;
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
}

static void number(bool canAssign) {
    (void)canAssign;
    double value = strtod(parser.previous.start, NULL);
    emitConstant(NUMBER_VAL(value));
}

static void string(bool canAssign) {
    (void)canAssign;
    emitConstant(OBJ_VAL(
        copyString(parser.previous.start + 1, parser.previous.length - 2)));
}

static void namedVariable(Token name, bool canAssign) {
    uint8_t getOp, setOp;
    int arg = resolveLocal(current, &name);

    if (arg != -1) {
	// ローカル変数はスタックスロット番号で参照する。
	getOp = OP_GET_LOCAL;
	setOp = OP_SET_LOCAL;
    } else {
	// 見つからなければグローバル変数として名前で参照する。
	arg = identifierConstant(&name);
	getOp = OP_GET_GLOBAL;
	setOp = OP_SET_GLOBAL;
    }

    if (canAssign && match(TOKEN_EQUAL)) {
	expression();
	emitBytes(setOp, (uint8_t)arg);
    } else {
	emitBytes(getOp, (uint8_t)arg);
    }
}

static void variable(bool canAssign) {
    namedVariable(parser.previous, canAssign);
}

static uint8_t argumentList() {
    uint8_t argCount = 0;
    if (!check(TOKEN_RIGHT_PAREN)) {
	do {
	    expression();
	    if (argCount == 255) {
		error("Can't have more than 255 arguments.");
	    }
	    argCount++;
	} while (match(TOKEN_COMMA));
    }
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after arguments.");
    return argCount;
}

static void call(bool canAssign) {
    (void)canAssign;
    uint8_t argCount = argumentList();
    emitBytes(OP_CALL, argCount);
}

static void unary(bool canAssign) {
    (void)canAssign;
    TokenType operatorType = parser.previous.type;

    parsePrecedence(PREC_UNARY);

    switch (operatorType) {
    case TOKEN_BANG:
	emitByte(OP_NOT);
	break;
    case TOKEN_MINUS:
	emitByte(OP_NEGATE);
	break;
    default:
	return;
    }
}

ParseRule rules[] = {
    // {prefixFn, infixFn, precedence}
    [TOKEN_LEFT_PAREN] = {grouping, call, PREC_CALL},
    [TOKEN_RIGHT_PAREN] = {NULL, NULL, PREC_NONE},
    [TOKEN_LEFT_BRACE] = {NULL, NULL, PREC_NONE},
    [TOKEN_RIGHT_BRACE] = {NULL, NULL, PREC_NONE},
    [TOKEN_COMMA] = {NULL, NULL, PREC_NONE},
    [TOKEN_DOT] = {NULL, NULL, PREC_NONE},
    [TOKEN_MINUS] = {unary, binary, PREC_TERM},
    [TOKEN_PLUS] = {NULL, binary, PREC_TERM},
    [TOKEN_SEMICOLON] = {NULL, NULL, PREC_NONE},
    [TOKEN_SLASH] = {NULL, binary, PREC_FACTOR},
    [TOKEN_STAR] = {NULL, binary, PREC_FACTOR},
    [TOKEN_BANG] = {unary, NULL, PREC_NONE},
    [TOKEN_BANG_EQUAL] = {NULL, binary, PREC_EQUALITY},
    [TOKEN_EQUAL] = {NULL, NULL, PREC_NONE},
    [TOKEN_EQUAL_EQUAL] = {NULL, binary, PREC_EQUALITY},
    [TOKEN_GREATER] = {NULL, binary, PREC_COMPARISON},
    [TOKEN_GREATER_EQUAL] = {NULL, binary, PREC_COMPARISON},
    [TOKEN_LESS] = {NULL, binary, PREC_COMPARISON},
    [TOKEN_LESS_EQUAL] = {NULL, binary, PREC_COMPARISON},
    [TOKEN_IDENTIFIER] = {variable, NULL, PREC_NONE},
    [TOKEN_STRING] = {string, NULL, PREC_NONE},
    [TOKEN_NUMBER] = {number, NULL, PREC_NONE},
    [TOKEN_AND] = {NULL, and_, PREC_AND},
    [TOKEN_CLASS] = {NULL, NULL, PREC_NONE},
    [TOKEN_ELSE] = {NULL, NULL, PREC_NONE},
    [TOKEN_FALSE] = {literal, NULL, PREC_NONE},
    [TOKEN_FOR] = {NULL, NULL, PREC_NONE},
    [TOKEN_FUN] = {NULL, NULL, PREC_NONE},
    [TOKEN_IF] = {NULL, NULL, PREC_NONE},
    [TOKEN_NIL] = {literal, NULL, PREC_NONE},
    [TOKEN_OR] = {NULL, or_, PREC_OR},
    [TOKEN_PRINT] = {NULL, NULL, PREC_NONE},
    [TOKEN_RETURN] = {NULL, NULL, PREC_NONE},
    [TOKEN_SUPER] = {NULL, NULL, PREC_NONE},
    [TOKEN_THIS] = {NULL, NULL, PREC_NONE},
    [TOKEN_TRUE] = {literal, NULL, PREC_NONE},
    [TOKEN_VAR] = {NULL, NULL, PREC_NONE},
    [TOKEN_WHILE] = {NULL, NULL, PREC_NONE},
    [TOKEN_ERROR] = {NULL, NULL, PREC_NONE},
    [TOKEN_EOF] = {NULL, NULL, PREC_NONE},
};

static ParseRule *getRule(TokenType type) { return &rules[type]; }

static bool check(TokenType type) { return parser.current.type == type; }

static bool match(TokenType type) {
    if (!check(type))
	return false;
    advance();
    return true;
}

static void synchronize() {
    parser.panicMode = false;

    while (parser.current.type != TOKEN_EOF) {
	if (parser.previous.type == TOKEN_SEMICOLON)
	    return;

	switch (parser.current.type) {
	case TOKEN_CLASS:
	case TOKEN_FUN:
	case TOKEN_VAR:
	case TOKEN_FOR:
	case TOKEN_IF:
	case TOKEN_WHILE:
	case TOKEN_PRINT:
	case TOKEN_RETURN:
	    return;
	default:; // Do nothing.
	}

	advance();
    }
}

static uint8_t identifierConstant(Token *name) {
    return makeConstant(OBJ_VAL(copyString(name->start, name->length)));
}

static bool identifiersEqual(Token *a, Token *b) {
    if (a->length != b->length)
	return false;

    return memcmp(a->start, b->start, a->length) == 0;
}

static int resolveLocal(Compiler *compiler, Token *name) {
    // 内側のスコープの変数が外側の同名変数を隠すよう、末尾から探す。
    for (int i = compiler->localCount - 1; i >= 0; i--) {
	Local *local = &compiler->locals[i];
	if (identifiersEqual(name, &local->name)) {
	    if (local->depth == UNINITIALIZED_DEPTH)
		error("Can't read local variable in its own initializer.");
	    return i;
	}
    }

    return -1;
}

static void addLocal(Token name) {
    if (current->localCount == UINT8_COUNT) {
	error("Too many local variables in function.");
	return;
    }

    Local *local = &current->locals[current->localCount++];
    local->name = name;
    // 初期化式のコンパイル中は未初期化として扱い、自分自身の参照を検出する。
    local->depth = UNINITIALIZED_DEPTH;
}

static void markInitialized() {
    // グローバルスコープでは locals に追加されないため、何もしない。
    if (current->scopeDepth == 0)
	return;
    current->locals[current->localCount - 1].depth = current->scopeDepth;
}

static void declareVariable() {
    // グローバル変数は実行時にハッシュテーブルへ名前で登録するため
    // コンパイラの locals に追加する必要がないので何もしない。
    if (current->scopeDepth == 0)
	return;

    Token *name = &parser.previous;

    // 同じスコープの変数は locals の末尾に並んでいるため後ろから走査する。
    // 現在見ている変数のスコープが、コンパイラが見ているスコープより
    // 浅くなったところで検索を打ち切る。(スコープ外の変数に到達したところ)
    for (int i = current->localCount - 1; i >= 0; i--) {
	Local *local = &current->locals[i];
	if (local->depth != UNINITIALIZED_DEPTH &&
	    local->depth < current->scopeDepth)
	    break;

	if (identifiersEqual(name, &local->name))
	    error("Already a variable with this name in this scope.");
    }

    addLocal(*name);
}

static uint8_t parseVariable(const char *errorMessage) {
    consume(TOKEN_IDENTIFIER, errorMessage);

    declareVariable();
    // ローカル変数は定数表に追加する必要がないためここで早期リターンする
    if (current->scopeDepth > 0)
	return 0;

    return identifierConstant(&parser.previous);
}

static void defineVariable(uint8_t global) {
    // ローカル変数がグローバル変数としてスタックに追加されないようにするため
    if (current->scopeDepth > 0) {
	// 初期化式のコンパイルが終わったので、参照可能な状態にする。
	markInitialized();
	return;
    }

    emitBytes(OP_DEFINE_GLOBAL, global);
}

static void beginScope() { current->scopeDepth++; }

static void endScope() {
    current->scopeDepth--;

    while (current->localCount > 0 &&
           current->locals[current->localCount - 1].depth >
               current->scopeDepth) {
	emitByte(OP_POP);
	current->localCount--;
    }
}

static void block() {
    while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
	declaration();
    }

    consume(TOKEN_RIGHT_BRACE, "Expect '}' after block.");
}

static void printStatement() {
    expression();
    consume(TOKEN_SEMICOLON, "Expect ';' after value.");
    emitByte(OP_PRINT);
}

static void ifStatement() {
    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'if'.");
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition.");

    int thenJump = emitJump(OP_JUMP_IF_FALSE);
    emitByte(OP_POP);
    statement();

    int elseJump = emitJump(OP_JUMP);

    patchJump(thenJump);
    emitByte(OP_POP);

    if (match(TOKEN_ELSE))
	statement();
    patchJump(elseJump);
}

static void whileStatement() {
    int loopStart = currentChunk()->count;

    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'while'.");
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition.");

    int exitJump = emitJump(OP_JUMP_IF_FALSE);
    emitByte(OP_POP);
    statement();
    emitLoop(loopStart);

    patchJump(exitJump);
    emitByte(OP_POP);
}

static void forStatement() {
    beginScope();
    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'for'.");

    if (match(TOKEN_SEMICOLON)) {
	// 初期化子なしなので何もしない
    } else if (match(TOKEN_VAR)) {
	varDeclaration();
    } else {
	expressionStatement();
    }

    int loopStart = currentChunk()->count;
    int exitJump = -1;
    if (!match(TOKEN_SEMICOLON)) {
	expression();
	consume(TOKEN_SEMICOLON, "Expect ';' after loop condition.");

	// 条件が偽なら、本体と増分を飛び越えてループを終了する。
	exitJump = emitJump(OP_JUMP_IF_FALSE);
	emitByte(OP_POP);
    }

    if (!match(TOKEN_RIGHT_PAREN)) {
	// 初回は本体へ進み、以降は増分から条件へ戻る。
	int bodyJump = emitJump(OP_JUMP);
	int incrementStart = currentChunk()->count;
	expression();
	emitByte(OP_POP);
	consume(TOKEN_RIGHT_PAREN, "Expect ')' after for clauses.");

	emitLoop(loopStart);
	loopStart = incrementStart;
	patchJump(bodyJump);
    }

    statement();
	emitLoop(loopStart);

    if (exitJump != -1) {
	patchJump(exitJump);
	emitByte(OP_POP);
    }

    endScope();
}

static void expressionStatement() {
    expression();
    consume(TOKEN_SEMICOLON, "Expect ';' after expression.");
    emitByte(OP_POP);
}

static void returnStatement() {
    if (current->type == TYPE_SCRIPT) {
	error("Can't return from top-level code.");
    }

    if (match(TOKEN_SEMICOLON)) {
	emitReturn();
    } else {
	expression();
	consume(TOKEN_SEMICOLON, "Expect ';' after return value.");
	emitByte(OP_RETURN);
    }
}

static void statement() {
    if (match(TOKEN_PRINT)) {
	printStatement();
    } else if (match(TOKEN_RETURN)) {
	returnStatement();
    } else if (match(TOKEN_IF)) {
	ifStatement();
    } else if (match(TOKEN_WHILE)) {
	whileStatement();
	} else if (match(TOKEN_FOR)) {
	forStatement();
    } else if (match(TOKEN_LEFT_BRACE)) {
	beginScope();
	block();
	endScope();
    } else {
	expressionStatement();
    }
}

static void varDeclaration() {
    uint8_t global = parseVariable("Expect variable name.");

    if (match(TOKEN_EQUAL)) {
	expression();
    } else {
	emitByte(OP_NIL);
    }
    consume(TOKEN_SEMICOLON, "Expect ';' after variable declaration.");

    defineVariable(global);
}

static void function(FunctionType type) {
    Compiler compiler;
    initCompiler(&compiler, type);
    beginScope();

    consume(TOKEN_LEFT_PAREN, "Expect '(' after function name.");
    if (!check(TOKEN_RIGHT_PAREN)) {
	do {
	    current->function->arity++;
	    if (current->function->arity > 255) {
		errorAtCurrent("Can't have more than 255 parameters.");
	    }
	    uint8_t constant = parseVariable("Expect parameter name.");
	    defineVariable(constant);
	} while (match(TOKEN_COMMA));
    }
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after parameters.");

    consume(TOKEN_LEFT_BRACE, "Expect '{' before function body.");
    block();

    // 関数本体の実行時にはこの Compiler ごとフレームが破棄されるため、
    // ここで endScope() によるローカル変数の OP_POP は不要。
    ObjFunction *function = endCompiler();
    emitBytes(OP_CLOSURE, makeConstant(OBJ_VAL(function)));
}

static void funDeclaration() {
    uint8_t global = parseVariable("Expect function name.");
    // 関数本体の中から自分自身を再帰的に参照できるようにする。
    markInitialized();
    function(TYPE_FUNCTION);
    defineVariable(global);
}

static void declaration() {
    if (match(TOKEN_FUN)) {
	funDeclaration();
    } else if (match(TOKEN_VAR)) {
	varDeclaration();
    } else {
	statement();
    }

    if (parser.panicMode)
	synchronize();
}

ObjFunction *compile(const char *source) {
    initScanner(source);
    Compiler compiler;
    initCompiler(&compiler, TYPE_SCRIPT);

    parser.hadError = false;
    parser.panicMode = false;

    advance();

    while (!match(TOKEN_EOF)) {
	declaration();
    }

    ObjFunction *function = endCompiler();
    return parser.hadError ? NULL : function;
}
