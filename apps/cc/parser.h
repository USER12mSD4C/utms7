#ifndef CC_PARSER_H
#define CC_PARSER_H

#include "lexer.h"
#include "ast.h"

typedef struct {
    Lexer *lex;
    Token current;
    int had_error;
    char error_msg[256];
} Parser;

void parser_init(Parser *p, Lexer *lex);
ASTNode *parser_parse(Parser *p);

#endif
