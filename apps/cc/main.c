#include "lexer.h"
#include "parser.h"
#include "codegen.h"
#include "elf.h"
#include "../../lib/libc.h"

int main(int argc, char **argv) {
    const char *input = NULL;
    char output[256];
    output[0] = '\0';

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0) {
            if (i + 1 >= argc) {
                printf("usage: cc <input.c> [-o output]\n");
                return 1;
            }
            i++;
            snprintf(output, sizeof(output), "%s", argv[i]);
        } else if (!input) {
            input = argv[i];
        } else if (output[0] == '\0') {
            snprintf(output, sizeof(output), "%s", argv[i]);
        } else {
            printf("usage: cc <input.c> [-o output]\n");
            return 1;
        }
    }

    if (!input) {
        printf("usage: cc <input.c> [-o output]\n");
        return 1;
    }

    if (output[0] == '\0') {
        snprintf(output, sizeof(output), "%s", input);
        int len = strlen(output);
        if (len > 2 && output[len - 2] == '.' && output[len - 1] == 'c') {
            output[len - 2] = '\0';
        }
        strncat(output, ".bin", sizeof(output) - strlen(output) - 1);
    }

    int fd = open(input, 0);
    if (fd < 0) {
        printf("cc: cannot open '%s'\n", input);
        return 1;
    }

    struct stat st;
    fstat(fd, &st);
    char *src = malloc(st.st_size + 1);
    read(fd, src, st.st_size);
    src[st.st_size] = '\0';
    close(fd);

    Lexer lex;
    lexer_init(&lex, src);

    Parser parser;
    parser_init(&parser, &lex);

    ASTNode *program = parser_parse(&parser);
    if (parser.had_error) {
        printf("cc: %s\n", parser.error_msg);
        free(src);
        return 1;
    }

    CodeGen *cg = malloc(sizeof(CodeGen));
    if (!cg) {
        printf("cc: out of memory\n");
        return 1;
    }
    codegen_init(cg);

    if (codegen_generate(cg, program) != 0) {
        printf("cc: main function not found\n");
        codegen_free(cg);
        free(cg);
        ast_free(program);
        free(src);
        return 1;
    }

    if (elf_write(output, cg) != 0) {
        printf("cc: failed to write '%s'\n", output);
        codegen_free(cg);
        free(cg);
        ast_free(program);
        free(src);
        return 1;
    }

    printf("cc: %s -> %s (%d bytes)\n", input, output, cg->size);
    printf("cc: rodata=%d bytes\n", cg->rodata_size);
    printf("cc: first bytes: ");
    for (int i = 0; i < 16 && i < cg->size; i++) {
        printf("%02x ", cg->code[i]);
    }
    printf("\n");

    codegen_free(cg);
    free(cg);
    ast_free(program);
    free(src);
    return 0;
}
