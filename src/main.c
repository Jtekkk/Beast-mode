#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "vm.h"

#define BEAST_VERSION "0.1.0"

static void repl(void) {
    printf("Beast %s — a small language in beast mode\n", BEAST_VERSION);
    printf("Type an expression, or Ctrl-D to exit.\n");
    vm.replMode = true;
    char line[4096];
    for (;;) {
        printf("> ");
        if (!fgets(line, sizeof(line), stdin)) {
            printf("\n");
            break;
        }
        interpret(line, "<repl>");
    }
}

static char* readFile(const char* path) {
    FILE* file = fopen(path, "rb");
    if (file == NULL) {
        fprintf(stderr, "beast: could not open file \"%s\".\n", path);
        exit(74);
    }
    fseek(file, 0L, SEEK_END);
    size_t fileSize = ftell(file);
    rewind(file);

    char* buffer = (char*)malloc(fileSize + 1);
    if (buffer == NULL) {
        fprintf(stderr, "beast: not enough memory to read \"%s\".\n", path);
        exit(74);
    }
    size_t bytesRead = fread(buffer, sizeof(char), fileSize, file);
    if (bytesRead < fileSize) {
        fprintf(stderr, "beast: could not read file \"%s\".\n", path);
        exit(74);
    }
    buffer[bytesRead] = '\0';
    fclose(file);
    return buffer;
}

static void runFile(const char* path) {
    char* source = readFile(path);
    InterpretResult result = interpret(source, path);
    free(source);
    if (result == INTERPRET_COMPILE_ERROR) exit(65);
    if (result == INTERPRET_RUNTIME_ERROR) exit(70);
}

static void usage(void) {
    fprintf(stderr,
            "usage: beast [options] [script.bst]\n"
            "\n"
            "options:\n"
            "  -d          dump compiled bytecode instead of running\n"
            "  -v          print version and exit\n"
            "  -h          print this help and exit\n"
            "\n"
            "environment:\n"
            "  BEAST_GC_STRESS=1   collect garbage on every allocation\n"
            "\n"
            "With no script, beast starts a REPL.\n");
}

int main(int argc, const char* argv[]) {
    const char* script = NULL;
    bool dump = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-d") == 0) {
            dump = true;
        } else if (strcmp(argv[i], "-v") == 0 ||
                   strcmp(argv[i], "--version") == 0) {
            printf("Beast %s\n", BEAST_VERSION);
            return 0;
        } else if (strcmp(argv[i], "-h") == 0 ||
                   strcmp(argv[i], "--help") == 0) {
            usage();
            return 0;
        } else if (argv[i][0] == '-' && argv[i][1] != '\0') {
            fprintf(stderr, "beast: unknown option '%s'\n", argv[i]);
            usage();
            return 64;
        } else if (script == NULL) {
            script = argv[i];
        } else {
            fprintf(stderr, "beast: only one script may be given\n");
            usage();
            return 64;
        }
    }

    initVM();
    vm.dumpBytecode = dump;

    if (script != NULL) {
        runFile(script);
    } else if (dump) {
        fprintf(stderr, "beast: -d requires a script\n");
        freeVM();
        return 64;
    } else {
        repl();
    }

    freeVM();
    return 0;
}
