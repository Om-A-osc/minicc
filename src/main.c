/* main.c - the minicc driver.
 *
 *   source --lex/parse--> AST --sema--> typed AST --irgen--> IR --opt--> IR
 *          --emit_x86--> .s --cc--> executable      (default)
 *          --emit_llvm--> .ll --clang/llc--> executable / PTX / anything LLVM targets
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ast.h"
#include "emit_llvm.h"
#include "emit_x86.h"
#include "ir.h"
#include "irgen.h"
#include "opt.h"
#include "parser.h"
#include "sema.h"

#ifndef MINICC_RUNTIME_DIR
#define MINICC_RUNTIME_DIR "runtime"
#endif

extern Program *g_program;
extern int yyparse(void);
extern int yylex(void);
extern char *yytext;
extern YYLTYPE yylloc;
void lexer_set_input(FILE *f);
const char *token_name(int tok);

typedef enum { BACKEND_X86, BACKEND_LLVM } Backend;

typedef struct {
    const char *input;
    const char *output;
    int dump_tokens, dump_ast, dump_ir, parse_only, check_only;
    int opt_level;
    int opt_stats;
    int emit_asm;           /* -S: stop after generating assembly / LLVM IR text */
    int debug_info;         /* -g */
    int verbose;
    int keep_temps;
    Backend backend;
    X86Abi abi;
} Options;

static void usage(FILE *out) {
    fputs(
        "usage: minicc [options] file.dcf\n"
        "\n"
        "Output selection:\n"
        "  -o <file>          output path (default: input name without extension)\n"
        "  -S                 emit assembly (or LLVM IR with --backend=llvm) and stop\n"
        "  --emit-llvm        shorthand for --backend=llvm -S\n"
        "  --backend=x86|llvm code generator used for the executable (default x86)\n"
        "  --target=sysv|win64  x86-64 calling convention (default: host)\n"
        "\n"
        "Pipeline inspection:\n"
        "  --dump-tokens      print the token stream and exit\n"
        "  --dump-ast         print the syntax tree\n"
        "  --dump-ir          print the IR (after optimisation if -O is given)\n"
        "  --parse-only       stop after parsing\n"
        "  --check            stop after semantic analysis\n"
        "\n"
        "Code generation:\n"
        "  -O, -O1            constant folding, CSE, dead code elimination, CFG cleanup\n"
        "  -O0                no optimisation (default)\n"
        "  --opt-stats        report what the optimiser did\n"
        "  -g                 emit DWARF line tables (x86 backend) and pass -g to the linker\n"
        "  -v                 print external commands\n"
        "  --keep-temps       keep the intermediate .s / .ll file\n"
        "\n"
        "Environment: MINICC_CC (default cc), MINICC_CLANG (default clang),\n"
        "             MINICC_RUNTIME (directory containing runtime.c)\n",
        out);
}

static int parse_options(int argc, char **argv, Options *opt) {
    memset(opt, 0, sizeof *opt);
    opt->abi = x86_host_abi();
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "-h") || !strcmp(a, "--help")) { usage(stdout); exit(0); }
        else if (!strcmp(a, "--dump-tokens")) opt->dump_tokens = 1;
        else if (!strcmp(a, "--dump-ast")) opt->dump_ast = 1;
        else if (!strcmp(a, "--dump-ir")) opt->dump_ir = 1;
        else if (!strcmp(a, "--parse-only")) opt->parse_only = 1;
        else if (!strcmp(a, "--check")) opt->check_only = 1;
        else if (!strcmp(a, "-O") || !strcmp(a, "-O1") || !strcmp(a, "-O2")) opt->opt_level = 1;
        else if (!strcmp(a, "-O0")) opt->opt_level = 0;
        else if (!strcmp(a, "--opt-stats")) opt->opt_stats = 1;
        else if (!strcmp(a, "-S")) opt->emit_asm = 1;
        else if (!strcmp(a, "--emit-llvm")) { opt->backend = BACKEND_LLVM; opt->emit_asm = 1; }
        else if (!strcmp(a, "--backend=x86")) opt->backend = BACKEND_X86;
        else if (!strcmp(a, "--backend=llvm")) opt->backend = BACKEND_LLVM;
        else if (!strcmp(a, "--target=sysv")) opt->abi = ABI_SYSV;
        else if (!strcmp(a, "--target=win64")) opt->abi = ABI_WIN64;
        else if (!strcmp(a, "-g")) opt->debug_info = 1;
        else if (!strcmp(a, "-v")) opt->verbose = 1;
        else if (!strcmp(a, "--keep-temps")) opt->keep_temps = 1;
        else if (!strcmp(a, "-o")) {
            if (i + 1 >= argc) { fprintf(stderr, "minicc: -o needs an argument\n"); return 0; }
            opt->output = argv[++i];
        }
        else if (a[0] == '-' && a[1]) { fprintf(stderr, "minicc: unknown option '%s'\n", a); return 0; }
        else if (opt->input) { fprintf(stderr, "minicc: only one input file is supported\n"); return 0; }
        else opt->input = a;
    }
    if (!opt->input) { usage(stderr); return 0; }
    return 1;
}

/* input "dir/prog.dcf" -> "dir/prog" + suffix */
static char *derive_output(const char *input, const char *suffix) {
    size_t n = strlen(input);
    const char *dot = strrchr(input, '.');
    const char *slash = strrchr(input, '/');
#ifdef _WIN32
    const char *bslash = strrchr(input, '\\');
    if (bslash && (!slash || bslash > slash)) slash = bslash;
#endif
    if (dot && (!slash || dot > slash)) n = (size_t)(dot - input);
    char *out = xmalloc(n + strlen(suffix) + 1);
    memcpy(out, input, n);
    strcpy(out + n, suffix);
    return out;
}

static const char *env_or(const char *name, const char *dflt) {
    const char *v = getenv(name);
    return v && *v ? v : dflt;
}

static int run_command(const char *cmd, int verbose) {
    if (verbose) fprintf(stderr, "[minicc] %s\n", cmd);
    fflush(stderr);
    int rc = system(cmd);
    if (rc != 0) fprintf(stderr, "minicc: command failed (%d): %s\n", rc, cmd);
    return rc;
}

static FILE *open_output(const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) { perror(path); exit(2); }
    return f;
}

int main(int argc, char **argv) {
    Options opt;
    if (!parse_options(argc, argv, &opt)) return 2;

    FILE *src = fopen(opt.input, "r");
    if (!src) { perror(opt.input); return 2; }
    /* forward slashes everywhere: the path ends up in diagnostics, in the
     * LLVM source_filename and in the assembler's .file directive */
    char *norm = xstrdup(opt.input);
    for (char *p = norm; *p; p++) if (*p == '\\') *p = '/';
    g_source_path = norm;
    lexer_set_input(src);

    /* ---- front end ---- */
    if (opt.dump_tokens) {
        int tok;
        while ((tok = yylex()) != 0)
            printf("%d:%d\t%-18s %s\n", yylloc.first_line, yylloc.first_column, token_name(tok), yytext);
        return g_error_count ? 1 : 0;
    }
    yyparse();
    fclose(src);
    if (g_error_count) {
        fprintf(stderr, "%d error%s generated.\n", g_error_count, g_error_count == 1 ? "" : "s");
        return 1;
    }
    if (opt.dump_ast) ast_dump(g_program, stdout);
    if (opt.parse_only) return 0;

    sema_check(g_program);
    if (g_error_count) {
        fprintf(stderr, "%d error%s generated.\n", g_error_count, g_error_count == 1 ? "" : "s");
        return 1;
    }
    if (opt.check_only) return 0;

    /* ---- middle end ---- */
    IrModule *m = irgen_program(g_program, norm);
    OptStats stats;
    memset(&stats, 0, sizeof stats);
    opt_run(m, opt.opt_level, &stats);
    if (opt.opt_stats)
        fprintf(stderr, "[opt] folded %d, cse %d, dce %d, branches %d, blocks %d\n",
                stats.folded, stats.cse, stats.dce, stats.branches, stats.blocks);
    if (opt.dump_ir) {
        ir_dump(m, stdout);
        if (!opt.output && !opt.emit_asm) return 0;
    }

    /* ---- back end ---- */
    const char *text_suffix = opt.backend == BACKEND_LLVM ? ".ll" : ".s";
    if (opt.emit_asm) {
        const char *path = opt.output ? opt.output : derive_output(opt.input, text_suffix);
        FILE *out = strcmp(path, "-") == 0 ? stdout : open_output(path);
        if (opt.backend == BACKEND_LLVM) emit_llvm(m, out);
        else emit_x86(m, out, opt.abi, opt.debug_info);
        if (out != stdout) fclose(out);
        return 0;
    }

#ifdef _WIN32
    const char *exe_suffix = ".exe";
#else
    const char *exe_suffix = "";
#endif
    const char *exe = opt.output ? opt.output : derive_output(opt.input, exe_suffix);
    char *tmp = xstrfmt("%s.minicc%s", exe, text_suffix);
    FILE *out = open_output(tmp);
    if (opt.backend == BACKEND_LLVM) emit_llvm(m, out);
    else emit_x86(m, out, opt.abi, opt.debug_info);
    fclose(out);

    const char *runtime_dir = env_or("MINICC_RUNTIME", MINICC_RUNTIME_DIR);
    const char *cc = opt.backend == BACKEND_LLVM ? env_or("MINICC_CLANG", "clang") : env_or("MINICC_CC", "cc");
    char *cmd = xstrfmt("%s %s %s-o \"%s\" \"%s\" \"%s/runtime.c\"",
                        cc,
                        opt.backend == BACKEND_LLVM ? "-Wno-override-module" : "",
                        opt.debug_info ? "-g " : "",
                        exe, tmp, runtime_dir);
    int rc = run_command(cmd, opt.verbose);
    if (!opt.keep_temps) remove(tmp);
    return rc == 0 ? 0 : 1;
}
