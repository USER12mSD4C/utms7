#ifndef CC_LEXER_H
#define CC_LEXER_H

typedef enum {
    TOK_EOF = 0,

    TOK_IDENT = 256,
    TOK_NUMBER,
    TOK_STRING,

    TOK_INT, TOK_VOID, TOK_CHAR, TOK_LONG, TOK_SHORT, TOK_UNSIGNED,
    TOK_RETURN, TOK_IF, TOK_ELSE, TOK_WHILE, TOK_FOR, TOK_BREAK, TOK_CONTINUE,
    TOK_STRUCT, TOK_TYPEDEF, TOK_SIZEOF,

    TOK_LPAREN = '(',
    TOK_RPAREN = ')',
    TOK_LBRACE = '{',
    TOK_RBRACE = '}',
    TOK_LBRACKET = '[',
    TOK_RBRACKET = ']',
    TOK_SEMICOLON = ';',
    TOK_COMMA = ',',
    TOK_PLUS = '+',
    TOK_MINUS = '-',
    TOK_STAR = '*',
    TOK_SLASH = '/',
    TOK_PERCENT = '%',
    TOK_AMP = '&',
    TOK_DOT = '.',
    TOK_QUESTION = '?',
    TOK_COLON = ':',

    TOK_ASSIGN = 300,
    TOK_EQ, TOK_NEQ, TOK_LT, TOK_GT, TOK_LE, TOK_GE,
    TOK_AND, TOK_OR, TOK_NOT,
    TOK_ARROW,
    TOK_PLUS_EQ, TOK_MINUS_EQ, TOK_STAR_EQ, TOK_SLASH_EQ,
    TOK_INC, TOK_DEC,

    TOK_ERROR = 500
} TokenType;

typedef struct {
    TokenType type;
    long long value;
    char text[256];
    int line;
} Token;

typedef struct {
    const char *src;
    int pos;
    int line;
    Token current;
} Lexer;

void lexer_init(Lexer *lex, const char *src);
Token lexer_next(Lexer *lex);
Token lexer_peek_token(Lexer *lex);
const char *token_type_name(TokenType type);

#endif
