#include "ast.h"
#include "../../lib/libc.h"

ASTNode *ast_new(ASTType type) {
    ASTNode *node = malloc(sizeof(ASTNode));
    memset(node, 0, sizeof(ASTNode));
    node->type = type;
    return node;
}

ASTNode *ast_number(long long val) {
    ASTNode *node = ast_new(AST_NUMBER);
    node->num_val = val;
    return node;
}

ASTNode *ast_ident(const char *name) {
    ASTNode *node = ast_new(AST_IDENT);
    node->name = strdup(name);
    return node;
}

ASTNode *ast_binary(int op, ASTNode *left, ASTNode *right) {
    ASTNode *node = ast_new(AST_BINARY);
    node->op = op;
    node->left = left;
    node->right = right;
    return node;
}

ASTNode *ast_unary(int op, ASTNode *operand) {
    ASTNode *node = ast_new(AST_UNARY);
    node->op = op;
    node->left = operand;
    return node;
}

ASTNode *ast_var_decl(const char *name, ASTNode *init) {
    ASTNode *node = ast_new(AST_VAR_DECL);
    node->name = strdup(name);
    node->left = init;
    return node;
}

ASTNode *ast_assign(const char *name, ASTNode *value) {
    ASTNode *node = ast_new(AST_ASSIGN);
    node->name = strdup(name);
    node->left = value;
    return node;
}

ASTNode *ast_return(ASTNode *value) {
    ASTNode *node = ast_new(AST_RETURN);
    node->left = value;
    return node;
}

ASTNode *ast_if(ASTNode *cond, ASTNode *then_body, ASTNode *else_body) {
    ASTNode *node = ast_new(AST_IF);
    node->cond = cond;
    node->then_body = then_body;
    node->else_body = else_body;
    return node;
}

ASTNode *ast_while(ASTNode *cond, ASTNode *body) {
    ASTNode *node = ast_new(AST_WHILE);
    node->cond = cond;
    node->body = body;
    return node;
}

ASTNode *ast_block(void) {
    ASTNode *node = ast_new(AST_BLOCK);
    node->children = malloc(sizeof(ASTNode*) * 64);
    node->child_count = 0;
    return node;
}

void ast_block_add(ASTNode *block, ASTNode *stmt) {
    if (block->child_count < 64) {
        block->children[block->child_count++] = stmt;
    }
}

ASTNode *ast_call(const char *name, ASTNode **args, int arg_count) {
    ASTNode *node = ast_new(AST_CALL);
    node->name = strdup(name);
    node->children = args;
    node->child_count = arg_count;
    return node;
}

ASTNode *ast_func_def(const char *name, char **params, int param_count, ASTNode *body) {
    ASTNode *node = ast_new(AST_FUNC_DEF);
    node->name = strdup(name);
    node->params = params;
    node->param_count = param_count;
    node->body = body;
    return node;
}

ASTNode *ast_expr_stmt(ASTNode *expr) {
    ASTNode *node = ast_new(AST_EXPR_STMT);
    node->left = expr;
    return node;
}

void ast_free(ASTNode *node) {
    if (!node) return;
    if (node->name) free(node->name);
    if (node->left) ast_free(node->left);
    if (node->right) ast_free(node->right);
    if (node->cond) ast_free(node->cond);
    if (node->then_body) ast_free(node->then_body);
    if (node->else_body) ast_free(node->else_body);
    if (node->body) ast_free(node->body);
    if (node->children) {
        for (int i = 0; i < node->child_count; i++) {
            ast_free(node->children[i]);
        }
        free(node->children);
    }
    free(node);
}
