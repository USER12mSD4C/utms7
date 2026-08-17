#include "lexer.h"
#include "../../lib/libc.h"

void lexer_init(Lexer *lex, const char *src) {
    lex->src = src;
    lex->pos = 0;
    lex->line = 1;
    lex->current.type = TOK_EOF;
}

static char peek(Lexer *lex) {
    return lex->src[lex->pos];
}

static char advance(Lexer *lex) {
    char c = lex->src[lex->pos++];
    if (c == '\n') lex->line++;
    return c;
}

static void skip_whitespace(Lexer *lex) {
    while (1) {
        char c = peek(lex);
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            advance(lex);
        } else if (c == '/' && lex->src[lex->pos + 1] == '/') {
            while (peek(lex) && peek(lex) != '\n') advance(lex);
        } else if (c == '/' && lex->src[lex->pos + 1] == '*') {
            advance(lex); advance(lex);
            while (peek(lex)) {
                if (peek(lex) == '*' && lex->src[lex->pos + 1] == '/') {
                    advance(lex); advance(lex);
                    break;
                }
                advance(lex);
            }
        } else {
            break;
        }
    }
}

static Token make_token(TokenType type) {
    Token tok;
    memset(&tok, 0, sizeof(tok));
    tok.type = type;
    return tok;
}

static Token make_error(const char *msg) {
    Token tok = make_token(TOK_ERROR);
    strncpy(tok.text, msg, 255);
    return tok;
}

Token lexer_next(Lexer *lex) {
    skip_whitespace(lex);
    char c = peek(lex);

    if (c == '\0') return make_token(TOK_EOF);
    if (c == '(') { advance(lex); return make_token(TOK_LPAREN); }
    if (c == ')') { advance(lex); return make_token(TOK_RPAREN); }
    if (c == '{') { advance(lex); return make_token(TOK_LBRACE); }
    if (c == '}') { advance(lex); return make_token(TOK_RBRACE); }
    if (c == ';') { advance(lex); return make_token(TOK_SEMICOLON); }
    if (c == ',') { advance(lex); return make_token(TOK_COMMA); }
    if (c == '+') { advance(lex); return make_token(TOK_PLUS); }
    if (c == '-') { advance(lex); return make_token(TOK_MINUS); }
    if (c == '*') { advance(lex); return make_token(TOK_STAR); }
    if (c == '/') { advance(lex); return make_token(TOK_SLASH); }
    if (c == '%') { advance(lex); return make_token(TOK_PERCENT); }

    if (c == '=') {
        advance(lex);
        if (peek(lex) == '=') { advance(lex); return make_token(TOK_EQ); }
        return make_token(TOK_ASSIGN);
    }
    if (c == '!') {
        advance(lex);
        if (peek(lex) == '=') { advance(lex); return make_token(TOK_NEQ); }
        return make_token(TOK_NOT);
    }
    if (c == '<') {
        advance(lex);
        if (peek(lex) == '=') { advance(lex); return make_token(TOK_LE); }
        return make_token(TOK_LT);
    }
    if (c == '>') {
        advance(lex);
        if (peek(lex) == '=') { advance(lex); return make_token(TOK_GE); }
        return make_token(TOK_GT);
    }
    if (c == '&') {
        advance(lex);
        if (peek(lex) == '&') { advance(lex); return make_token(TOK_AND); }
        return make_error("expected &&");
    }
    if (c == '|') {
        advance(lex);
        if (peek(lex) == '|') { advance(lex); return make_token(TOK_OR); }
        return make_error("expected ||");
    }

    if (c >= '0' && c <= '9') {
        Token tok = make_token(TOK_NUMBER);
        long long val = 0;
        if (c == '0' && (lex->src[lex->pos + 1] == 'x' || lex->src[lex->pos + 1] == 'X')) {
            advance(lex); advance(lex);
            while (isxdigit(peek(lex))) {
                char h = advance(lex);
                int d = 0;
                if (h >= '0' && h <= '9') d = h - '0';
                else if (h >= 'a' && h <= 'f') d = h - 'a' + 10;
                else d = h - 'A' + 10;
                val = val * 16 + d;
            }
        } else {
            while (peek(lex) >= '0' && peek(lex) <= '9') {
                val = val * 10 + (advance(lex) - '0');
            }
        }
        tok.value = val;
        return tok;
    }

    if (isalpha(c) || c == '_') {
        Token tok;
        memset(&tok, 0, sizeof(tok));
        int i = 0;
        while (isalnum(peek(lex)) || peek(lex) == '_') {
            tok.text[i++] = advance(lex);
        }
        tok.text[i] = '\0';
        tok.line = lex->line;

        if (strcmp(tok.text, "int") == 0) tok.type = TOK_INT;
        else if (strcmp(tok.text, "void") == 0) tok.type = TOK_VOID;
        else if (strcmp(tok.text, "return") == 0) tok.type = TOK_RETURN;
        else if (strcmp(tok.text, "if") == 0) tok.type = TOK_IF;
        else if (strcmp(tok.text, "else") == 0) tok.type = TOK_ELSE;
        else if (strcmp(tok.text, "while") == 0) tok.type = TOK_WHILE;
        else if (strcmp(tok.text, "for") == 0) tok.type = TOK_FOR;
        else tok.type = TOK_IDENT;
        return tok;
    }

    advance(lex);
    return make_error("unexpected character");
}

Token lexer_peek_token(Lexer *lex) {
    return lex->current;
}

const char *token_type_name(TokenType type) {
    switch (type) {
        case TOK_EOF: return "EOF";
        case TOK_INT: return "int";
        case TOK_VOID: return "void";
        case TOK_RETURN: return "return";
        case TOK_IF: return "if";
        case TOK_ELSE: return "else";
        case TOK_WHILE: return "while";
        case TOK_FOR: return "for";
        case TOK_IDENT: return "identifier";
        case TOK_NUMBER: return "number";
        case TOK_LPAREN: return "(";
        case TOK_RPAREN: return ")";
        case TOK_LBRACE: return "{";
        case TOK_RBRACE: return "}";
        case TOK_SEMICOLON: return ";";
        case TOK_COMMA: return ",";
        case TOK_ASSIGN: return "=";
        case TOK_PLUS: return "+";
        case TOK_MINUS: return "-";
        case TOK_STAR: return "*";
        case TOK_SLASH: return "/";
        case TOK_PERCENT: return "%";
        case TOK_EQ: return "==";
        case TOK_NEQ: return "!=";
        case TOK_LT: return "<";
        case TOK_GT: return ">";
        case TOK_LE: return "<=";
        case TOK_GE: return ">=";
        case TOK_AND: return "&&";
        case TOK_OR: return "||";
        case TOK_NOT: return "!";
        case TOK_ERROR: return "error";
        default: return "unknown";
    }
}
