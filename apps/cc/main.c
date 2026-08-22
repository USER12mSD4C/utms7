#include "lexer.h"
#include "parser.h"
#include "codegen.h"
#include "elf.h"
#include "../../lib/libc.h"

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("usage: cc <input.c> [output.bin]\n");
        return 1;
    }

    const char *input = argv[1];
    char output[256];
    if (argc >= 3) {
        strcpy(output, argv[2]);
    } else {
        strcpy(output, input);
        int len = strlen(output);
        if (len > 2 && output[len-2] == '.' && output[len-1] == 'c') {
            output[len-2] = '\0';
        }
        strcat(output, ".bin");
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
    codegen_generate(cg, program);

    if (elf_write(output, cg) != 0) {
        printf("cc: failed to write '%s'\n", output);
        codegen_free(cg);
        free(cg);
        ast_free(program);
        free(src);
        return 1;
    }

    printf("cc: %s -> %s (%d bytes)\n", input, output, cg->size);

    codegen_free(cg);
    free(cg);
    ast_free(program);
    free(src);
    return 0;
}
