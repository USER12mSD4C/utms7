#include "parser.h"
#include "../../lib/libc.h"

void parser_init(Parser *p, Lexer *lex) {
    p->lex = lex;
    p->had_error = 0;
    p->error_msg[0] = '\0';
    p->current = lexer_next(lex);
}

static void parser_error(Parser *p, const char *msg) {
    if (!p->had_error) {
        p->had_error = 1;
        snprintf(p->error_msg, sizeof(p->error_msg), "line %d: %s", p->current.line, msg);
    }
}

static void advance_token(Parser *p) {
    p->current = lexer_next(p->lex);
}

static int match(Parser *p, TokenType type) {
    if (p->current.type == type) {
        advance_token(p);
        return 1;
    }
    return 0;
}

static void expect(Parser *p, TokenType type, const char *msg) {
    if (p->current.type != type) {
        parser_error(p, msg);
        return;
    }
    advance_token(p);
}

static ASTNode *parse_expr(Parser *p);
static ASTNode *parse_stmt(Parser *p);
static ASTNode *parse_block(Parser *p);

static ASTNode *parse_primary(Parser *p) {
    if (p->current.type == TOK_NUMBER) {
        ASTNode *node = ast_number(p->current.value);
        advance_token(p);
        return node;
    }
    if (p->current.type == TOK_IDENT) {
        char name[256];
        strcpy(name, p->current.text);
        advance_token(p);
        if (match(p, TOK_LPAREN)) {
            ASTNode **args = malloc(sizeof(ASTNode*) * 16);
            int arg_count = 0;
            if (p->current.type != TOK_RPAREN) {
                while (1) {
                    args[arg_count++] = parse_expr(p);
                    if (!match(p, TOK_COMMA)) break;
                }
            }
            expect(p, TOK_RPAREN, "expected ')' after arguments");
            return ast_call(name, args, arg_count);
        }
        return ast_ident(name);
    }
    if (match(p, TOK_LPAREN)) {
        ASTNode *expr = parse_expr(p);
        expect(p, TOK_RPAREN, "expected ')'");
        return expr;
    }
    parser_error(p, "expected expression");
    return NULL;
}

static ASTNode *parse_unary(Parser *p) {
    if (match(p, TOK_MINUS)) {
        ASTNode *operand = parse_unary(p);
        return ast_unary('-', operand);
    }
    if (match(p, TOK_NOT)) {
        ASTNode *operand = parse_unary(p);
        return ast_unary('!', operand);
    }
    return parse_primary(p);
}

static ASTNode *parse_multiplicative(Parser *p) {
    ASTNode *left = parse_unary(p);
    while (p->current.type == TOK_STAR || p->current.type == TOK_SLASH || p->current.type == TOK_PERCENT) {
        int op = p->current.type == TOK_STAR ? '*' : (p->current.type == TOK_SLASH ? '/' : '%');
        advance_token(p);
        ASTNode *right = parse_unary(p);
        left = ast_binary(op, left, right);
    }
    return left;
}

static ASTNode *parse_additive(Parser *p) {
    ASTNode *left = parse_multiplicative(p);
    while (p->current.type == TOK_PLUS || p->current.type == TOK_MINUS) {
        int op = p->current.type == TOK_PLUS ? '+' : '-';
        advance_token(p);
        ASTNode *right = parse_multiplicative(p);
        left = ast_binary(op, left, right);
    }
    return left;
}

static ASTNode *parse_relational(Parser *p) {
    ASTNode *left = parse_additive(p);
    while (p->current.type == TOK_LT || p->current.type == TOK_GT ||
           p->current.type == TOK_LE || p->current.type == TOK_GE) {
        int op = p->current.type;
        advance_token(p);
        ASTNode *right = parse_additive(p);
        left = ast_binary(op, left, right);
    }
    return left;
}

static ASTNode *parse_equality(Parser *p) {
    ASTNode *left = parse_relational(p);
    while (p->current.type == TOK_EQ || p->current.type == TOK_NEQ) {
        int op = p->current.type;
        advance_token(p);
        ASTNode *right = parse_relational(p);
        left = ast_binary(op, left, right);
    }
    return left;
}

static ASTNode *parse_logic_and(Parser *p) {
    ASTNode *left = parse_equality(p);
    while (p->current.type == TOK_AND) {
        advance_token(p);
        ASTNode *right = parse_equality(p);
        left = ast_binary(TOK_AND, left, right);
    }
    return left;
}

static ASTNode *parse_logic_or(Parser *p) {
    ASTNode *left = parse_logic_and(p);
    while (p->current.type == TOK_OR) {
        advance_token(p);
        ASTNode *right = parse_logic_and(p);
        left = ast_binary(TOK_OR, left, right);
    }
    return left;
}

static ASTNode *parse_assignment(Parser *p) {
    if (p->current.type == TOK_IDENT) {
        char name[256];
        strcpy(name, p->current.text);
        Token saved = p->current;
        advance_token(p);
        if (match(p, TOK_ASSIGN)) {
            ASTNode *value = parse_assignment(p);
            return ast_assign(name, value);
        }
        p->current = saved;
    }
    return parse_logic_or(p);
}

static ASTNode *parse_expr(Parser *p) {
    return parse_assignment(p);
}

static ASTNode *parse_var_decl(Parser *p) {
    expect(p, TOK_INT, "expected 'int'");
    if (p->current.type != TOK_IDENT) {
        parser_error(p, "expected variable name");
        return NULL;
    }
    char name[256];
    strcpy(name, p->current.text);
    advance_token(p);
    ASTNode *init = NULL;
    if (match(p, TOK_ASSIGN)) {
        init = parse_expr(p);
    }
    expect(p, TOK_SEMICOLON, "expected ';'");
    return ast_var_decl(name, init);
}

static ASTNode *parse_if(Parser *p) {
    expect(p, TOK_IF, "expected 'if'");
    expect(p, TOK_LPAREN, "expected '('");
    ASTNode *cond = parse_expr(p);
    expect(p, TOK_RPAREN, "expected ')'");
    ASTNode *then_body = parse_stmt(p);
    ASTNode *else_body = NULL;
    if (match(p, TOK_ELSE)) {
        else_body = parse_stmt(p);
    }
    return ast_if(cond, then_body, else_body);
}

static ASTNode *parse_while(Parser *p) {
    expect(p, TOK_WHILE, "expected 'while'");
    expect(p, TOK_LPAREN, "expected '('");
    ASTNode *cond = parse_expr(p);
    expect(p, TOK_RPAREN, "expected ')'");
    ASTNode *body = parse_stmt(p);
    return ast_while(cond, body);
}

static ASTNode *parse_return(Parser *p) {
    expect(p, TOK_RETURN, "expected 'return'");
    ASTNode *value = NULL;
    if (p->current.type != TOK_SEMICOLON) {
        value = parse_expr(p);
    }
    expect(p, TOK_SEMICOLON, "expected ';'");
    return ast_return(value);
}

static ASTNode *parse_block(Parser *p) {
    expect(p, TOK_LBRACE, "expected '{'");
    ASTNode *block = ast_block();
    while (p->current.type != TOK_RBRACE && p->current.type != TOK_EOF) {
        ASTNode *stmt = parse_stmt(p);
        if (stmt) ast_block_add(block, stmt);
        if (p->had_error) break;
    }
    expect(p, TOK_RBRACE, "expected '}'");
    return block;
}

static ASTNode *parse_expr_stmt(Parser *p) {
    ASTNode *expr = parse_expr(p);
    expect(p, TOK_SEMICOLON, "expected ';'");
    return ast_expr_stmt(expr);
}

static ASTNode *parse_stmt(Parser *p) {
    if (p->had_error) return NULL;
    if (p->current.type == TOK_INT) return parse_var_decl(p);
    if (p->current.type == TOK_IF) return parse_if(p);
    if (p->current.type == TOK_WHILE) return parse_while(p);
    if (p->current.type == TOK_RETURN) return parse_return(p);
    if (p->current.type == TOK_LBRACE) return parse_block(p);
    return parse_expr_stmt(p);
}

static ASTNode *parse_func_def(Parser *p) {
    expect(p, TOK_INT, "expected 'int'");
    if (p->current.type != TOK_IDENT) {
        parser_error(p, "expected function name");
        return NULL;
    }
    char name[256];
    strcpy(name, p->current.text);
    advance_token(p);
    expect(p, TOK_LPAREN, "expected '('");

    char **params = malloc(sizeof(char*) * 16);
    int param_count = 0;
    if (p->current.type != TOK_RPAREN) {
        while (1) {
            expect(p, TOK_INT, "expected 'int'");
            if (p->current.type != TOK_IDENT) {
                parser_error(p, "expected parameter name");
                return NULL;
            }
            params[param_count++] = strdup(p->current.text);
            advance_token(p);
            if (!match(p, TOK_COMMA)) break;
        }
    }
    expect(p, TOK_RPAREN, "expected ')'");
    ASTNode *body = parse_block(p);
    return ast_func_def(name, params, param_count, body);
}

ASTNode *parser_parse(Parser *p) {
    ASTNode *program = ast_new(AST_PROGRAM);
    program->children = malloc(sizeof(ASTNode*) * 64);
    program->child_count = 0;

    while (p->current.type != TOK_EOF) {
        ASTNode *func = parse_func_def(p);
        if (func) {
            program->children[program->child_count++] = func;
        }
        if (p->had_error) break;
    }
    return program;
}
