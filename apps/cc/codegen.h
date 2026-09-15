#ifndef CC_CODEGEN_H
#define CC_CODEGEN_H

#include "ast.h"
#include "../../include/types.h"

typedef struct {
    char name[256];
    int offset;
} FuncInfo;

typedef struct {
    int patch_pos;
    char name[256];
} PendingCall;

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

    FuncInfo funcs[256];
    int func_count;

    PendingCall pending_calls[256];
    int pending_call_count;

    int break_depth;
    int break_begin[64];
    int *break_patches;
    int break_patch_count;
    int break_patch_capacity;

    int continue_depth;
    int continue_begin[64];
    int *continue_patches;
    int continue_patch_count;
    int continue_patch_capacity;
    int continue_target_known[64];
    u32 continue_target[64];

    u8 *rodata;
    int rodata_size;
    int rodata_capacity;
} CodeGen;

void codegen_init(CodeGen *cg);
void codegen_free(CodeGen *cg);
int codegen_generate(CodeGen *cg, ASTNode *program);

#endif
