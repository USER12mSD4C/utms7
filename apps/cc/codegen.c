#include "codegen.h"
#include "lexer.h"
#include "../../lib/libc.h"

void codegen_init(CodeGen *cg) {
    cg->capacity = 65536;
    cg->code = malloc(cg->capacity);
    cg->size = 0;
    cg->stack_offset = 0;
    cg->label_counter = 0;
    cg->local_count = 0;
}

void codegen_free(CodeGen *cg) {
    if (cg->code) free(cg->code);
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
    int offset = find_local(cg, node->name);
    if (offset == -1) return;
    gen_expr(cg, node->left);
    emit_byte(cg, 0x48); emit_byte(cg, 0x89); emit_byte(cg, 0x85);
    emit_dword(cg, (u32)offset);
}

static void emit_jmp_rel32(CodeGen *cg, u32 target) {
    emit_byte(cg, 0xE9);
    emit_dword(cg, target - (cg->size + 4));
}

static void emit_je_rel32(CodeGen *cg, u32 target) {
    emit_byte(cg, 0x0F);
    emit_byte(cg, 0x84);
    emit_dword(cg, target - (cg->size + 4));
}

static void gen_if(CodeGen *cg, ASTNode *node) {
    gen_expr(cg, node->cond);
    emit_byte(cg, 0x48); emit_byte(cg, 0x85); emit_byte(cg, 0xC0);

    int patch1 = cg->size;
    emit_je_rel32(cg, 0);

    gen_stmt(cg, node->then_body);

    if (node->else_body) {
        int patch2 = cg->size;
        emit_jmp_rel32(cg, 0);

        u32 else_addr = cg->size;
        u32 rel1 = else_addr - (patch1 + 6);
        memcpy(cg->code + patch1 + 2, &rel1, 4);

        gen_stmt(cg, node->else_body);

        u32 end_addr = cg->size;
        u32 rel2 = end_addr - (patch2 + 5);
        memcpy(cg->code + patch2 + 1, &rel2, 4);
    } else {
        u32 end_addr = cg->size;
        u32 rel1 = end_addr - (patch1 + 6);
        memcpy(cg->code + patch1 + 2, &rel1, 4);
    }
}

static void gen_while(CodeGen *cg, ASTNode *node) {
    u32 start_addr = cg->size;

    gen_expr(cg, node->cond);
    emit_byte(cg, 0x48); emit_byte(cg, 0x85); emit_byte(cg, 0xC0);

    int patch = cg->size;
    emit_je_rel32(cg, 0);

    gen_stmt(cg, node->body);

    emit_jmp_rel32(cg, start_addr);

    u32 end_addr = cg->size;
    u32 rel = end_addr - (patch + 6);
    memcpy(cg->code + patch + 2, &rel, 4);
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
    emit_dword(cg, 0);

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
        case AST_RETURN: gen_return(cg, node); break;
        case AST_BLOCK: gen_block(cg, node); break;
        case AST_EXPR_STMT: gen_expr(cg, node->left); break;
        default: break;
    }
}

static void gen_func(CodeGen *cg, ASTNode *node) {
    cg->local_count = 0;
    cg->stack_offset = 0;

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

int codegen_generate(CodeGen *cg, ASTNode *program) {
    for (int i = 0; i < program->child_count; i++) {
        if (program->children[i]->type == AST_FUNC_DEF) {
            gen_func(cg, program->children[i]);
        }
    }
    return 0;
}
