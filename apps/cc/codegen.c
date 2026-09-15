#include "codegen.h"
#include "lexer.h"
#include "../../lib/libc.h"

void codegen_init(CodeGen *cg) {
    cg->capacity = 65536;
    cg->code = malloc(cg->capacity);
    if (!cg->code) exit(1);
    cg->size = 0;
    cg->stack_offset = 0;
    cg->label_counter = 0;
    cg->local_count = 0;
    cg->func_count = 0;
    cg->pending_call_count = 0;

    cg->break_depth = 0;
    cg->break_patch_count = 0;
    cg->break_patch_capacity = 256;
    cg->break_patches = malloc((u64)cg->break_patch_capacity * sizeof(int));
    if (!cg->break_patches) exit(1);
    memset(cg->break_begin, 0, sizeof(cg->break_begin));

    cg->continue_depth = 0;
    cg->continue_patch_count = 0;
    cg->continue_patch_capacity = 256;
    cg->continue_patches = malloc((u64)cg->continue_patch_capacity * sizeof(int));
    if (!cg->continue_patches) exit(1);
    memset(cg->continue_begin, 0, sizeof(cg->continue_begin));
    memset(cg->continue_target_known, 0, sizeof(cg->continue_target_known));
    memset(cg->continue_target, 0, sizeof(cg->continue_target));

    cg->rodata_capacity = 4096;
    cg->rodata = malloc(cg->rodata_capacity);
    if (!cg->rodata) exit(1);
    cg->rodata_size = 0;
}

void codegen_free(CodeGen *cg) {
    if (cg->code) free(cg->code);
    if (cg->rodata) free(cg->rodata);
    if (cg->break_patches) free(cg->break_patches);
    if (cg->continue_patches) free(cg->continue_patches);
}

static void emit_byte(CodeGen *cg, u8 byte) {
    if (cg->size >= cg->capacity) {
        cg->capacity *= 2;
        cg->code = realloc(cg->code, cg->capacity);
    }
    cg->code[cg->size++] = byte;
}

static void emit_dword(CodeGen *cg, u32 val) {
    emit_byte(cg, val & 0xFF);
    emit_byte(cg, (val >> 8) & 0xFF);
    emit_byte(cg, (val >> 16) & 0xFF);
    emit_byte(cg, (val >> 24) & 0xFF);
}

static void emit_qword(CodeGen *cg, u64 val) {
    emit_dword(cg, (u32)(val & 0xFFFFFFFF));
    emit_dword(cg, (u32)(val >> 32));
}

static void emit_rel32(CodeGen *cg, u32 from, u32 to) {
    u32 rel = to - (from + 4);
    memcpy(cg->code + from, &rel, 4);
}

static int find_local(CodeGen *cg, const char *name) {
    for (int i = 0; i < cg->local_count; i++) {
        if (strcmp(cg->locals[i].name, name) == 0) {
            return cg->locals[i].offset;
        }
    }
    return -1;
}

static int add_local(CodeGen *cg, const char *name) {
    cg->stack_offset -= 8;
    strcpy(cg->locals[cg->local_count].name, name);
    cg->locals[cg->local_count].offset = cg->stack_offset;
    cg->local_count++;
    return cg->stack_offset;
}

static void emit_prologue(CodeGen *cg) {
    emit_byte(cg, 0x55);
    emit_byte(cg, 0x48); emit_byte(cg, 0x89); emit_byte(cg, 0xE5);
}

static void emit_epilogue(CodeGen *cg) {
    emit_byte(cg, 0x48); emit_byte(cg, 0x89); emit_byte(cg, 0xEC);
    emit_byte(cg, 0x5D);
    emit_byte(cg, 0xC3);
}

static void gen_expr(CodeGen *cg, ASTNode *node);
static void gen_stmt(CodeGen *cg, ASTNode *node);

static void gen_block(CodeGen *cg, ASTNode *node) {
    if (!node || !node->children) return;
    int saved_local_count = cg->local_count;
    int saved_stack_offset = cg->stack_offset;
    for (int i = 0; i < node->child_count; i++) {
        gen_stmt(cg, node->children[i]);
    }
    cg->local_count = saved_local_count;
    cg->stack_offset = saved_stack_offset;
}

static void gen_var_decl(CodeGen *cg, ASTNode *node) {
    int offset = add_local(cg, node->name);
    if (node->left) {
        gen_expr(cg, node->left);
    } else {
        emit_byte(cg, 0x48); emit_byte(cg, 0x31); emit_byte(cg, 0xC0);
    }
    emit_byte(cg, 0x48); emit_byte(cg, 0x89); emit_byte(cg, 0x85);
    emit_dword(cg, (u32)offset);
}

static void gen_assign(CodeGen *cg, ASTNode *node) {
    if (node->name) {
        int offset = find_local(cg, node->name);
        if (offset == -1) {
            emit_byte(cg, 0x48); emit_byte(cg, 0x31); emit_byte(cg, 0xC0);
            return;
        }

        if (node->left) {
            gen_expr(cg, node->left);
        } else if (node->right) {
            gen_expr(cg, node->right);
        } else {
            emit_byte(cg, 0x48); emit_byte(cg, 0x31); emit_byte(cg, 0xC0);
        }

        emit_byte(cg, 0x48); emit_byte(cg, 0x89); emit_byte(cg, 0x85);
        emit_dword(cg, (u32)offset);
        return;
    }

    if (node->left && node->left->type == AST_IDENT && node->left->name) {
        int offset = find_local(cg, node->left->name);
        if (offset != -1) {
            if (node->right) {
                gen_expr(cg, node->right);
            } else {
                emit_byte(cg, 0x48); emit_byte(cg, 0x31); emit_byte(cg, 0xC0);
            }

            emit_byte(cg, 0x48); emit_byte(cg, 0x89); emit_byte(cg, 0x85);
            emit_dword(cg, (u32)offset);
            return;
        }
    }

    if (node->right) {
        gen_expr(cg, node->right);
    } else {
        emit_byte(cg, 0x48); emit_byte(cg, 0x31); emit_byte(cg, 0xC0);
    }
}

static int emit_jmp_rel32(CodeGen *cg) {
    emit_byte(cg, 0xE9);
    int patch_pos = cg->size;
    emit_dword(cg, 0);
    return patch_pos;
}

static int emit_je_rel32(CodeGen *cg) {
    emit_byte(cg, 0x0F);
    emit_byte(cg, 0x84);
    int patch_pos = cg->size;
    emit_dword(cg, 0);
    return patch_pos;
}

static void patch_jmp(CodeGen *cg, int patch_pos, u32 target) {
    emit_rel32(cg, patch_pos, target);
}

static void add_break_patch(CodeGen *cg, int patch) {
    if (cg->break_patch_count >= cg->break_patch_capacity) {
        cg->break_patch_capacity *= 2;
        cg->break_patches = realloc(cg->break_patches, (u64)cg->break_patch_capacity * sizeof(int));
        if (!cg->break_patches) exit(1);
    }
    cg->break_patches[cg->break_patch_count++] = patch;
}

static void add_continue_patch(CodeGen *cg, int patch) {
    if (cg->continue_patch_count >= cg->continue_patch_capacity) {
        cg->continue_patch_capacity *= 2;
        cg->continue_patches = realloc(cg->continue_patches, (u64)cg->continue_patch_capacity * sizeof(int));
        if (!cg->continue_patches) exit(1);
    }
    cg->continue_patches[cg->continue_patch_count++] = patch;
}

static void patch_breaks(CodeGen *cg, int depth_index, u32 target) {
    int begin = cg->break_begin[depth_index];
    for (int i = begin; i < cg->break_patch_count; i++) {
        patch_jmp(cg, cg->break_patches[i], target);
    }
    cg->break_patch_count = begin;
}

static void patch_continues(CodeGen *cg, int depth_index, u32 target) {
    int begin = cg->continue_begin[depth_index];
    for (int i = begin; i < cg->continue_patch_count; i++) {
        patch_jmp(cg, cg->continue_patches[i], target);
    }
    cg->continue_patch_count = begin;
}

static void gen_if(CodeGen *cg, ASTNode *node) {
    gen_expr(cg, node->cond);
    emit_byte(cg, 0x48); emit_byte(cg, 0x85); emit_byte(cg, 0xC0);

    int patch1 = emit_je_rel32(cg);

    gen_stmt(cg, node->then_body);

    if (node->else_body) {
        int patch2 = emit_jmp_rel32(cg);

        patch_jmp(cg, patch1, cg->size);

        gen_stmt(cg, node->else_body);

        patch_jmp(cg, patch2, cg->size);
    } else {
        patch_jmp(cg, patch1, cg->size);
    }
}

static void gen_while(CodeGen *cg, ASTNode *node) {
    u32 start_addr = cg->size;

    cg->break_begin[cg->break_depth] = cg->break_patch_count;
    cg->break_depth++;

    cg->continue_begin[cg->continue_depth] = cg->continue_patch_count;
    cg->continue_target_known[cg->continue_depth] = 1;
    cg->continue_target[cg->continue_depth] = start_addr;
    cg->continue_depth++;

    gen_expr(cg, node->cond);
    emit_byte(cg, 0x48); emit_byte(cg, 0x85); emit_byte(cg, 0xC0);

    int patch_exit = emit_je_rel32(cg);

    gen_stmt(cg, node->body);

    int patch_back = emit_jmp_rel32(cg);
    patch_jmp(cg, patch_back, start_addr);

    patch_continues(cg, cg->continue_depth - 1, start_addr);
    cg->continue_depth--;

    patch_breaks(cg, cg->break_depth - 1, cg->size);
    cg->break_depth--;

    patch_jmp(cg, patch_exit, cg->size);
}

static void gen_for(CodeGen *cg, ASTNode *node) {
    if (node->init) {
        gen_stmt(cg, node->init);
    }

    cg->break_begin[cg->break_depth] = cg->break_patch_count;
    cg->break_depth++;

    cg->continue_begin[cg->continue_depth] = cg->continue_patch_count;
    cg->continue_target_known[cg->continue_depth] = 0;
    cg->continue_target[cg->continue_depth] = 0;
    cg->continue_depth++;

    u32 cond_addr = cg->size;

    if (node->cond) {
        gen_expr(cg, node->cond);
        emit_byte(cg, 0x48); emit_byte(cg, 0x85); emit_byte(cg, 0xC0);
    } else {
        emit_byte(cg, 0x48); emit_byte(cg, 0xC7); emit_byte(cg, 0xC0);
        emit_dword(cg, 1);
        emit_byte(cg, 0x48); emit_byte(cg, 0x85); emit_byte(cg, 0xC0);
    }

    int patch_exit = emit_je_rel32(cg);

    gen_stmt(cg, node->body);

    u32 continue_target = cond_addr;
    if (node->step) {
        continue_target = cg->size;
    }

    patch_continues(cg, cg->continue_depth - 1, continue_target);
    cg->continue_depth--;

    if (node->step) {
        gen_expr(cg, node->step);
    }

    int patch_back = emit_jmp_rel32(cg);
    patch_jmp(cg, patch_back, cond_addr);

    patch_breaks(cg, cg->break_depth - 1, cg->size);
    cg->break_depth--;

    patch_jmp(cg, patch_exit, cg->size);
}

static void gen_break(CodeGen *cg) {
    if (cg->break_depth > 0) {
        int patch = emit_jmp_rel32(cg);
        add_break_patch(cg, patch);
    }
}

static void gen_continue(CodeGen *cg) {
    if (cg->continue_depth > 0) {
        int patch = emit_jmp_rel32(cg);
        int d = cg->continue_depth - 1;

        if (cg->continue_target_known[d]) {
            patch_jmp(cg, patch, cg->continue_target[d]);
        } else {
            add_continue_patch(cg, patch);
        }
    }
}

static void gen_return(CodeGen *cg, ASTNode *node) {
    if (node->left) {
        gen_expr(cg, node->left);
    } else {
        emit_byte(cg, 0x48); emit_byte(cg, 0x31); emit_byte(cg, 0xC0);
    }
    emit_epilogue(cg);
}

static void gen_call(CodeGen *cg, ASTNode *node) {
    int arg_count = node->child_count;

    if (arg_count > 6) {
        emit_byte(cg, 0x48); emit_byte(cg, 0x81); emit_byte(cg, 0xEC);
        emit_dword(cg, (arg_count - 6) * 8);
    }

    for (int i = arg_count - 1; i >= 0; i--) {
        gen_expr(cg, node->children[i]);

        if (i == 0) {
            emit_byte(cg, 0x48); emit_byte(cg, 0x89); emit_byte(cg, 0xC7);
        } else if (i == 1) {
            emit_byte(cg, 0x48); emit_byte(cg, 0x89); emit_byte(cg, 0xC6);
        } else if (i == 2) {
            emit_byte(cg, 0x48); emit_byte(cg, 0x89); emit_byte(cg, 0xC2);
        } else if (i == 3) {
            emit_byte(cg, 0x49); emit_byte(cg, 0x89); emit_byte(cg, 0xC1);
        } else if (i == 4) {
            emit_byte(cg, 0x49); emit_byte(cg, 0x89); emit_byte(cg, 0xC0);
        } else if (i == 5) {
            emit_byte(cg, 0x48); emit_byte(cg, 0x89); emit_byte(cg, 0xC1);
        } else {
            emit_byte(cg, 0x48); emit_byte(cg, 0x89); emit_byte(cg, 0x44);
            emit_byte(cg, 0x24);
            emit_byte(cg, (i - 6) * 8);
        }
    }

    emit_byte(cg, 0x48); emit_byte(cg, 0x31); emit_byte(cg, 0xC0);
    emit_byte(cg, 0xE8);
    int call_pos = cg->size;
    emit_dword(cg, 0);

    int found = 0;
    for (int i = 0; i < cg->func_count; i++) {
        if (strcmp(cg->funcs[i].name, node->name) == 0) {
            emit_rel32(cg, call_pos, cg->funcs[i].offset);
            found = 1;
            break;
        }
    }

    if (!found) {
        strcpy(cg->pending_calls[cg->pending_call_count].name, node->name);
        cg->pending_calls[cg->pending_call_count].patch_pos = call_pos;
        cg->pending_call_count++;
    }

    if (arg_count > 6) {
        emit_byte(cg, 0x48); emit_byte(cg, 0x81); emit_byte(cg, 0xC4);
        emit_dword(cg, (arg_count - 6) * 8);
    }
}

static void gen_expr(CodeGen *cg, ASTNode *node) {
    if (!node) return;

    switch (node->type) {
        case AST_NUMBER:
            emit_byte(cg, 0x48); emit_byte(cg, 0xC7); emit_byte(cg, 0xC0);
            emit_dword(cg, (u32)node->num_val);
            break;

        case AST_IDENT: {
            int offset = find_local(cg, node->name);
            if (offset != -1) {
                emit_byte(cg, 0x48); emit_byte(cg, 0x8B); emit_byte(cg, 0x85);
                emit_dword(cg, (u32)offset);
            } else {
                emit_byte(cg, 0x48); emit_byte(cg, 0x31); emit_byte(cg, 0xC0);
            }
            break;
        }

        case AST_STRING: {
            int rodata_offset = cg->rodata_size;
            int len = strlen(node->name) + 1;
            if (cg->rodata_size + len > cg->rodata_capacity) {
                cg->rodata_capacity *= 2;
                cg->rodata = realloc(cg->rodata, cg->rodata_capacity);
            }
            memcpy(cg->rodata + cg->rodata_size, node->name, len);
            cg->rodata_size += len;

            emit_byte(cg, 0x48); emit_byte(cg, 0x8D); emit_byte(cg, 0x05);
            emit_dword(cg, 0);
            break;
        }

        case AST_ADDR: {
            if (node->left && node->left->type == AST_IDENT) {
                int offset = find_local(cg, node->left->name);
                if (offset != -1) {
                    emit_byte(cg, 0x48); emit_byte(cg, 0x8D); emit_byte(cg, 0x85);
                    emit_dword(cg, (u32)offset);
                } else {
                    emit_byte(cg, 0x48); emit_byte(cg, 0x31); emit_byte(cg, 0xC0);
                }
            } else {
                emit_byte(cg, 0x48); emit_byte(cg, 0x31); emit_byte(cg, 0xC0);
            }
            break;
        }

        case AST_DEREF: {
            gen_expr(cg, node->left);
            emit_byte(cg, 0x48); emit_byte(cg, 0x8B); emit_byte(cg, 0x00);
            break;
        }

        case AST_INDEX: {
            gen_expr(cg, node->left);
            emit_byte(cg, 0x50);
            gen_expr(cg, node->right);
            emit_byte(cg, 0x59);
            emit_byte(cg, 0x48); emit_byte(cg, 0xC1); emit_byte(cg, 0xE1);
            emit_byte(cg, 0x03);
            emit_byte(cg, 0x48); emit_byte(cg, 0x01); emit_byte(cg, 0xC8);
            emit_byte(cg, 0x48); emit_byte(cg, 0x8B); emit_byte(cg, 0x00);
            break;
        }

        case AST_MEMBER: {
            gen_expr(cg, node->left);
            break;
        }

        case AST_SIZEOF: {
            emit_byte(cg, 0x48); emit_byte(cg, 0xC7); emit_byte(cg, 0xC0);
            emit_dword(cg, 8);
            break;
        }

        case AST_BINARY: {
            gen_expr(cg, node->right);
            emit_byte(cg, 0x50);
            gen_expr(cg, node->left);
            emit_byte(cg, 0x59);

            switch (node->op) {
                case '+':
                    emit_byte(cg, 0x48); emit_byte(cg, 0x01); emit_byte(cg, 0xC8);
                    break;
                case '-':
                    emit_byte(cg, 0x48); emit_byte(cg, 0x29); emit_byte(cg, 0xC8);
                    break;
                case '*':
                    emit_byte(cg, 0x48); emit_byte(cg, 0xF7); emit_byte(cg, 0xE9);
                    break;
                case '/':
                    emit_byte(cg, 0x48); emit_byte(cg, 0x99);
                    emit_byte(cg, 0x48); emit_byte(cg, 0xF7); emit_byte(cg, 0xF9);
                    break;
                case '%':
                    emit_byte(cg, 0x48); emit_byte(cg, 0x99);
                    emit_byte(cg, 0x48); emit_byte(cg, 0xF7); emit_byte(cg, 0xF9);
                    emit_byte(cg, 0x48); emit_byte(cg, 0x89); emit_byte(cg, 0xD0);
                    break;
                case TOK_EQ:
                case TOK_NEQ:
                case TOK_LT:
                case TOK_GT:
                case TOK_LE:
                case TOK_GE: {
                    emit_byte(cg, 0x48); emit_byte(cg, 0x39); emit_byte(cg, 0xC8);
                    emit_byte(cg, 0x0F);
                    u8 setcc = 0;
                    switch (node->op) {
                        case TOK_EQ: setcc = 0x94; break;
                        case TOK_NEQ: setcc = 0x95; break;
                        case TOK_LT: setcc = 0x9C; break;
                        case TOK_GT: setcc = 0x9F; break;
                        case TOK_LE: setcc = 0x9E; break;
                        case TOK_GE: setcc = 0x9D; break;
                    }
                    emit_byte(cg, setcc); emit_byte(cg, 0xC0);
                    emit_byte(cg, 0x48); emit_byte(cg, 0x0F); emit_byte(cg, 0xB6); emit_byte(cg, 0xC0);
                    break;
                }
                case TOK_AND: {
                    emit_byte(cg, 0x48); emit_byte(cg, 0x85); emit_byte(cg, 0xC0);
                    emit_byte(cg, 0x0F); emit_byte(cg, 0x95); emit_byte(cg, 0xC0);
                    emit_byte(cg, 0x50);
                    emit_byte(cg, 0x48); emit_byte(cg, 0x85); emit_byte(cg, 0xC9);
                    emit_byte(cg, 0x0F); emit_byte(cg, 0x95); emit_byte(cg, 0xC1);
                    emit_byte(cg, 0x58);
                    emit_byte(cg, 0x48); emit_byte(cg, 0x21); emit_byte(cg, 0xC8);
                    break;
                }
                case TOK_OR: {
                    emit_byte(cg, 0x48); emit_byte(cg, 0x85); emit_byte(cg, 0xC0);
                    emit_byte(cg, 0x0F); emit_byte(cg, 0x95); emit_byte(cg, 0xC0);
                    emit_byte(cg, 0x50);
                    emit_byte(cg, 0x48); emit_byte(cg, 0x85); emit_byte(cg, 0xC9);
                    emit_byte(cg, 0x0F); emit_byte(cg, 0x95); emit_byte(cg, 0xC1);
                    emit_byte(cg, 0x58);
                    emit_byte(cg, 0x48); emit_byte(cg, 0x09); emit_byte(cg, 0xC8);
                    break;
                }
                case TOK_INC: {
                    emit_byte(cg, 0x48); emit_byte(cg, 0x01); emit_byte(cg, 0xC8);
                    break;
                }
                case TOK_DEC: {
                    emit_byte(cg, 0x48); emit_byte(cg, 0x29); emit_byte(cg, 0xC8);
                    break;
                }
            }
            break;
        }

        case AST_UNARY: {
            gen_expr(cg, node->left);
            if (node->op == '-') {
                emit_byte(cg, 0x48); emit_byte(cg, 0xF7); emit_byte(cg, 0xD8);
            } else if (node->op == '!') {
                emit_byte(cg, 0x48); emit_byte(cg, 0x85); emit_byte(cg, 0xC0);
                emit_byte(cg, 0x0F); emit_byte(cg, 0x94); emit_byte(cg, 0xC0);
                emit_byte(cg, 0x48); emit_byte(cg, 0x0F); emit_byte(cg, 0xB6); emit_byte(cg, 0xC0);
            }
            break;
        }

        case AST_CALL:
            gen_call(cg, node);
            break;

        case AST_ASSIGN:
            gen_assign(cg, node);
            break;

        default:
            break;
    }
}

static void gen_stmt(CodeGen *cg, ASTNode *node) {
    if (!node) return;
    switch (node->type) {
        case AST_VAR_DECL: gen_var_decl(cg, node); break;
        case AST_ASSIGN: gen_assign(cg, node); break;
        case AST_IF: gen_if(cg, node); break;
        case AST_WHILE: gen_while(cg, node); break;
        case AST_FOR: gen_for(cg, node); break;
        case AST_BREAK: gen_break(cg); break;
        case AST_CONTINUE: gen_continue(cg); break;
        case AST_RETURN: gen_return(cg, node); break;
        case AST_BLOCK: gen_block(cg, node); break;
        case AST_EXPR_STMT: gen_expr(cg, node->left); break;
        default: break;
    }
}

static void gen_func(CodeGen *cg, ASTNode *node) {
    cg->local_count = 0;
    cg->stack_offset = 0;

    strcpy(cg->funcs[cg->func_count].name, node->name);
    cg->funcs[cg->func_count].offset = cg->size;
    cg->func_count++;

    for (int i = node->param_count - 1; i >= 0; i--) {
        add_local(cg, node->params[i]);
    }

    emit_prologue(cg);

    for (int i = 0; i < node->param_count && i < 6; i++) {
        int offset = find_local(cg, node->params[i]);
        if (i == 0) {
            emit_byte(cg, 0x48); emit_byte(cg, 0x89); emit_byte(cg, 0xBD);
        } else if (i == 1) {
            emit_byte(cg, 0x48); emit_byte(cg, 0x89); emit_byte(cg, 0xB5);
        } else if (i == 2) {
            emit_byte(cg, 0x48); emit_byte(cg, 0x89); emit_byte(cg, 0x95);
        } else if (i == 3) {
            emit_byte(cg, 0x48); emit_byte(cg, 0x89); emit_byte(cg, 0x8D);
        } else if (i == 4) {
            emit_byte(cg, 0x4C); emit_byte(cg, 0x89); emit_byte(cg, 0x85);
        } else if (i == 5) {
            emit_byte(cg, 0x4C); emit_byte(cg, 0x89); emit_byte(cg, 0x8D);
        }
        emit_dword(cg, (u32)offset);
    }

    if (cg->stack_offset < 0) {
        emit_byte(cg, 0x48); emit_byte(cg, 0x81); emit_byte(cg, 0xEC);
        emit_dword(cg, (u32)(-cg->stack_offset));
    }

    gen_block(cg, node->body);

    emit_byte(cg, 0x48); emit_byte(cg, 0x31); emit_byte(cg, 0xC0);
    emit_epilogue(cg);
}

static void gen_startup_stub(CodeGen *cg) {
    emit_byte(cg, 0x48); emit_byte(cg, 0x8B); emit_byte(cg, 0x3C); emit_byte(cg, 0x24);
    emit_byte(cg, 0x48); emit_byte(cg, 0x8D); emit_byte(cg, 0x74); emit_byte(cg, 0x24); emit_byte(cg, 0x08);
    emit_byte(cg, 0xE8);
    int call_pos = cg->size;
    emit_dword(cg, 0);
    strcpy(cg->pending_calls[cg->pending_call_count].name, "main");
    cg->pending_calls[cg->pending_call_count].patch_pos = call_pos;
    cg->pending_call_count++;
    emit_byte(cg, 0x48); emit_byte(cg, 0x89); emit_byte(cg, 0xC7);
    emit_byte(cg, 0x48); emit_byte(cg, 0x31); emit_byte(cg, 0xC0);
    emit_byte(cg, 0x0F); emit_byte(cg, 0x05);
}

int codegen_generate(CodeGen *cg, ASTNode *program) {
    int has_main = 0;

    for (int i = 0; i < program->child_count; i++) {
        if (program->children[i]->type == AST_FUNC_DEF &&
            program->children[i]->name &&
            strcmp(program->children[i]->name, "main") == 0) {
            has_main = 1;
            break;
        }
    }

    if (!has_main) return -1;

    gen_startup_stub(cg);

    for (int i = 0; i < program->child_count; i++) {
        if (program->children[i]->type == AST_FUNC_DEF) {
            gen_func(cg, program->children[i]);
        }
    }

    for (int i = 0; i < cg->pending_call_count; i++) {
        for (int j = 0; j < cg->func_count; j++) {
            if (strcmp(cg->pending_calls[i].name, cg->funcs[j].name) == 0) {
                emit_rel32(cg, cg->pending_calls[i].patch_pos, cg->funcs[j].offset);
                break;
            }
        }
    }

    return 0;
}
