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
    AST_STRING,
    AST_RETURN,
    AST_IF,
    AST_WHILE,
    AST_FOR,
    AST_BREAK,
    AST_CONTINUE,
    AST_BLOCK,
    AST_CALL,
    AST_EXPR_STMT,
    AST_INDEX,
    AST_MEMBER,
    AST_ADDR,
    AST_DEREF,
    AST_SIZEOF,
    AST_GLOBAL_VAR,
    AST_TYPE,
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
    struct ASTNode *init;
    struct ASTNode *step;
    struct ASTNode **children;
    int child_count;
    char **params;
    int param_count;
    int type_size;
} ASTNode;

ASTNode *ast_new(ASTType type);
ASTNode *ast_number(long long val);
ASTNode *ast_ident(const char *name);
ASTNode *ast_string(const char *str);
ASTNode *ast_binary(int op, ASTNode *left, ASTNode *right);
ASTNode *ast_unary(int op, ASTNode *operand);
ASTNode *ast_var_decl(const char *name, ASTNode *init);
ASTNode *ast_global_var(const char *name, ASTNode *init);
ASTNode *ast_assign(const char *name, ASTNode *value);
ASTNode *ast_assign_expr(ASTNode *target, ASTNode *value);
ASTNode *ast_return(ASTNode *value);
ASTNode *ast_if(ASTNode *cond, ASTNode *then_body, ASTNode *else_body);
ASTNode *ast_while(ASTNode *cond, ASTNode *body);
ASTNode *ast_for(ASTNode *init, ASTNode *cond, ASTNode *step, ASTNode *body);
ASTNode *ast_break(void);
ASTNode *ast_continue(void);
ASTNode *ast_block(void);
void ast_block_add(ASTNode *block, ASTNode *stmt);
ASTNode *ast_call(const char *name, ASTNode **args, int arg_count);
ASTNode *ast_func_def(const char *name, char **params, int param_count, ASTNode *body);
ASTNode *ast_expr_stmt(ASTNode *expr);
ASTNode *ast_index(ASTNode *array, ASTNode *index);
ASTNode *ast_member(ASTNode *object, const char *field, int op);
ASTNode *ast_addr(ASTNode *operand);
ASTNode *ast_deref(ASTNode *operand);
ASTNode *ast_sizeof(ASTNode *operand);
ASTNode *ast_type(int size);
void ast_free(ASTNode *node);

#endif
