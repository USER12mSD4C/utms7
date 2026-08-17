#ifndef CC_CODEGEN_H
#define CC_CODEGEN_H

#include "ast.h"
#include "../../include/types.h"

typedef struct {
    u8 *code;
    int size;
    int capacity;
    int stack_offset;
    int label_counter;
    struct {
        char name[256];
        int offset;
    } locals[256];
    int local_count;
} CodeGen;

void codegen_init(CodeGen *cg);
void codegen_free(CodeGen *cg);
int codegen_generate(CodeGen *cg, ASTNode *program);

#endif
