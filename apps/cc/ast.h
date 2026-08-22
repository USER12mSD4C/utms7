#ifndef CC_AST_H
#define CC_AST_H

typedef enum {
    AST_PROGRAM,
    AST_FUNC_DEF,
    AST_VAR_DECL,
    AST_ASSIGN,
    AST_BINARY,
    AST_UNARY,
    AST_NUMBER,
    AST_IDENT,
    AST_RETURN,
    AST_IF,
    AST_WHILE,
    AST_BLOCK,
    AST_CALL,
    AST_EXPR_STMT,
} ASTType;

typedef struct ASTNode {
    ASTType type;
    long long num_val;
    char *name;
    int op;
    struct ASTNode *left;
    struct ASTNode *right;
    struct ASTNode *cond;
    struct ASTNode *then_body;
    struct ASTNode *else_body;
    struct ASTNode *body;
    struct ASTNode **children;
    int child_count;
    char **params;
    int param_count;
} ASTNode;

ASTNode *ast_new(ASTType type);
ASTNode *ast_number(long long val);
ASTNode *ast_ident(const char *name);
ASTNode *ast_binary(int op, ASTNode *left, ASTNode *right);
ASTNode *ast_unary(int op, ASTNode *operand);
ASTNode *ast_var_decl(const char *name, ASTNode *init);
ASTNode *ast_assign(const char *name, ASTNode *value);
ASTNode *ast_return(ASTNode *value);
ASTNode *ast_if(ASTNode *cond, ASTNode *then_body, ASTNode *else_body);
ASTNode *ast_while(ASTNode *cond, ASTNode *body);
ASTNode *ast_block(void);
void ast_block_add(ASTNode *block, ASTNode *stmt);
ASTNode *ast_call(const char *name, ASTNode **args, int arg_count);
ASTNode *ast_func_def(const char *name, char **params, int param_count, ASTNode *body);
ASTNode *ast_expr_stmt(ASTNode *expr);
void ast_free(ASTNode *node);

#endif
