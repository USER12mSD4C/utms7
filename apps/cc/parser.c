#include "parser.h"
#include "../../lib/libc.h"

static const char *parser_src_start = NULL;

void parser_init(Parser *p, Lexer *lex) {
    p->lex = lex;
    p->had_error = 0;
    p->error_msg[0] = '\0';
    parser_src_start = lex->src;
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

static int is_type_token(TokenType t) {
    return t == TOK_INT || t == TOK_VOID || t == TOK_CHAR ||
           t == TOK_LONG || t == TOK_SHORT || t == TOK_UNSIGNED;
}

static void skip_type_specifier(Parser *p) {
    if (p->current.type == TOK_UNSIGNED) advance_token(p);
    if (is_type_token(p->current.type)) advance_token(p);
}

static ASTNode *parse_expr(Parser *p);
static ASTNode *parse_stmt(Parser *p);
static ASTNode *parse_block(Parser *p);

static ASTNode *parse_primary(Parser *p) {
    if (p->had_error) return NULL;

    if (p->current.type == TOK_NUMBER) {
        ASTNode *node = ast_number(p->current.value);
        advance_token(p);
        return node;
    }

    if (p->current.type == TOK_STRING) {
        ASTNode *node = ast_string(p->current.text);
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
                    if (p->had_error) break;
                    args[arg_count++] = parse_expr(p);
                    if (p->had_error) break;
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

static ASTNode *parse_postfix(Parser *p) {
    ASTNode *node = parse_primary(p);
    if (!node) return NULL;

    while (1) {
        if (p->had_error) return node;

        if (match(p, TOK_LBRACKET)) {
            ASTNode *index = parse_expr(p);
            expect(p, TOK_RBRACKET, "expected ']'");
            node = ast_index(node, index);
        } else if (match(p, TOK_DOT)) {
            if (p->current.type != TOK_IDENT) {
                parser_error(p, "expected field name after '.'");
                return node;
            }
            char field[256];
            strcpy(field, p->current.text);
            advance_token(p);
            node = ast_member(node, field, TOK_DOT);
        } else if (match(p, TOK_ARROW)) {
            if (p->current.type != TOK_IDENT) {
                parser_error(p, "expected field name after '->'");
                return node;
            }
            char field[256];
            strcpy(field, p->current.text);
            advance_token(p);
            node = ast_member(node, field, TOK_ARROW);
        } else {
            break;
        }
    }

    return node;
}

static ASTNode *parse_unary(Parser *p) {
    if (p->had_error) return NULL;

    if (match(p, TOK_MINUS)) {
        ASTNode *operand = parse_unary(p);
        return ast_unary('-', operand);
    }
    if (match(p, TOK_NOT)) {
        ASTNode *operand = parse_unary(p);
        return ast_unary('!', operand);
    }
    if (match(p, TOK_AMP)) {
        ASTNode *operand = parse_unary(p);
        return ast_addr(operand);
    }
    if (match(p, TOK_STAR)) {
        ASTNode *operand = parse_unary(p);
        return ast_deref(operand);
    }

    return parse_postfix(p);
}

static ASTNode *parse_multiplicative(Parser *p) {
    ASTNode *left = parse_unary(p);
    if (p->had_error) return left;
    while (p->current.type == TOK_STAR || p->current.type == TOK_SLASH || p->current.type == TOK_PERCENT) {
        int op = p->current.type == TOK_STAR ? '*' : (p->current.type == TOK_SLASH ? '/' : '%');
        advance_token(p);
        ASTNode *right = parse_unary(p);
        left = ast_binary(op, left, right);
        if (p->had_error) return left;
    }
    return left;
}

static ASTNode *parse_additive(Parser *p) {
    ASTNode *left = parse_multiplicative(p);
    if (p->had_error) return left;
    while (p->current.type == TOK_PLUS || p->current.type == TOK_MINUS) {
        int op = p->current.type == TOK_PLUS ? '+' : '-';
        advance_token(p);
        ASTNode *right = parse_multiplicative(p);
        left = ast_binary(op, left, right);
        if (p->had_error) return left;
    }
    return left;
}

static ASTNode *parse_relational(Parser *p) {
    ASTNode *left = parse_additive(p);
    if (p->had_error) return left;
    while (p->current.type == TOK_LT || p->current.type == TOK_GT ||
           p->current.type == TOK_LE || p->current.type == TOK_GE) {
        int op = p->current.type;
        advance_token(p);
        ASTNode *right = parse_additive(p);
        left = ast_binary(op, left, right);
        if (p->had_error) return left;
    }
    return left;
}

static ASTNode *parse_equality(Parser *p) {
    ASTNode *left = parse_relational(p);
    if (p->had_error) return left;
    while (p->current.type == TOK_EQ || p->current.type == TOK_NEQ) {
        int op = p->current.type;
        advance_token(p);
        ASTNode *right = parse_relational(p);
        left = ast_binary(op, left, right);
        if (p->had_error) return left;
    }
    return left;
}

static ASTNode *parse_logic_and(Parser *p) {
    ASTNode *left = parse_equality(p);
    if (p->had_error) return left;
    while (p->current.type == TOK_AND) {
        advance_token(p);
        ASTNode *right = parse_equality(p);
        left = ast_binary(TOK_AND, left, right);
        if (p->had_error) return left;
    }
    return left;
}

static ASTNode *parse_logic_or(Parser *p) {
    ASTNode *left = parse_logic_and(p);
    if (p->had_error) return left;
    while (p->current.type == TOK_OR) {
        advance_token(p);
        ASTNode *right = parse_logic_and(p);
        left = ast_binary(TOK_OR, left, right);
        if (p->had_error) return left;
    }
    return left;
}

static ASTNode *parse_assignment(Parser *p) {
    if (p->had_error) return NULL;

    ASTNode *left = parse_logic_or(p);
    if (p->had_error) return left;

    if (p->current.type == TOK_ASSIGN) {
        advance_token(p);
        ASTNode *value = parse_assignment(p);
        return ast_assign_expr(left, value);
    }

    if (p->current.type == TOK_PLUS_EQ || p->current.type == TOK_MINUS_EQ ||
        p->current.type == TOK_STAR_EQ || p->current.type == TOK_SLASH_EQ) {
        int op;
        if (p->current.type == TOK_PLUS_EQ) op = '+';
        else if (p->current.type == TOK_MINUS_EQ) op = '-';
        else if (p->current.type == TOK_STAR_EQ) op = '*';
        else op = '/';
        advance_token(p);
        ASTNode *value = parse_assignment(p);
        ASTNode *combined = ast_binary(op, left, value);
        return ast_assign_expr(left, combined);
    }

    return left;
}

static ASTNode *parse_expr(Parser *p) {
    return parse_assignment(p);
}

static ASTNode *parse_var_decl(Parser *p) {
    skip_type_specifier(p);

    while (p->current.type == TOK_STAR) {
        advance_token(p);
    }

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

static ASTNode *parse_for(Parser *p) {
    expect(p, TOK_FOR, "expected 'for'");
    expect(p, TOK_LPAREN, "expected '('");

    ASTNode *init = NULL;
    if (p->current.type != TOK_SEMICOLON) {
        if (is_type_token(p->current.type)) {
            init = parse_var_decl(p);
        } else {
            ASTNode *expr = parse_expr(p);
            expect(p, TOK_SEMICOLON, "expected ';'");
            init = ast_expr_stmt(expr);
        }
    } else {
        advance_token(p);
    }

    ASTNode *cond = NULL;
    if (p->current.type != TOK_SEMICOLON) {
        cond = parse_expr(p);
    }
    expect(p, TOK_SEMICOLON, "expected ';'");

    ASTNode *step = NULL;
    if (p->current.type != TOK_RPAREN) {
        step = parse_expr(p);
    }
    expect(p, TOK_RPAREN, "expected ')'");

    ASTNode *body = parse_stmt(p);
    return ast_for(init, cond, step, body);
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

static ASTNode *parse_break(Parser *p) {
    expect(p, TOK_BREAK, "expected 'break'");
    expect(p, TOK_SEMICOLON, "expected ';'");
    return ast_break();
}

static ASTNode *parse_continue(Parser *p) {
    expect(p, TOK_CONTINUE, "expected 'continue'");
    expect(p, TOK_SEMICOLON, "expected ';'");
    return ast_continue();
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
    if (is_type_token(p->current.type)) return parse_var_decl(p);
    if (p->current.type == TOK_IF) return parse_if(p);
    if (p->current.type == TOK_WHILE) return parse_while(p);
    if (p->current.type == TOK_FOR) return parse_for(p);
    if (p->current.type == TOK_RETURN) return parse_return(p);
    if (p->current.type == TOK_BREAK) return parse_break(p);
    if (p->current.type == TOK_CONTINUE) return parse_continue(p);
    if (p->current.type == TOK_LBRACE) return parse_block(p);
    if (p->current.type == TOK_SEMICOLON) {
        advance_token(p);
        return NULL;
    }
    return parse_expr_stmt(p);
}

static ASTNode *parse_func_def(Parser *p) {
    skip_type_specifier(p);

    while (p->current.type == TOK_STAR) {
        advance_token(p);
    }

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
            if (p->current.type == TOK_VOID && param_count == 0) {
                Token next_saved;
                advance_token(p);
                if (p->current.type == TOK_RPAREN) break;
                parser_error(p, "expected ')' after void");
                return NULL;
            }

            skip_type_specifier(p);

            while (p->current.type == TOK_STAR) {
                advance_token(p);
            }

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
        if (is_type_token(p->current.type)) {
            ASTNode *item = parse_func_def(p);
            if (item) {
                program->children[program->child_count++] = item;
            }
        } else if (p->current.type == TOK_SEMICOLON) {
            advance_token(p);
        } else {
            parser_error(p, "expected declaration");
            break;
        }
        if (p->had_error) break;
    }
    return program;
}
