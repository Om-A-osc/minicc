/* lexer.l - Flex scanner for MiniCC.
 *
 * Tracks line and column positions for every token (feeding Bison's
 * %locations), unescapes string literals, and reports lexical errors
 * through the shared diagnostics module so a bad character fails the
 * compilation instead of being silently dropped. */

%option yylineno noyywrap nounput noinput never-interactive

%{
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ast.h"
#include "parser.h"

static int yycol = 1;
static int comment_start_line, comment_start_col;

#define YY_USER_ACTION                                   \
    yylloc.first_line = yylloc.last_line = yylineno;     \
    yylloc.first_column = yycol;                         \
    yylloc.last_column = yycol + (int)yyleng - 1;        \
    yycol += (int)yyleng;

static char *unescape_string(const char *text, int line, int col);
static int parse_int_literal(const char *text, int base, int line, int col);
%}

%x COMMENT

DIGIT       [0-9]
HEXDIG      [0-9a-fA-F]
ID          [A-Za-z_][A-Za-z0-9_]*
HEXINT      0[xX]{HEXDIG}+
DOUBLE      {DIGIT}+\.{DIGIT}*([eE][+-]?{DIGIT}+)?
INT         {DIGIT}+

%%

[ \t\r\f]+      { }
\n              { yycol = 1; }

"//".*          { }
"/*"            { comment_start_line = yylloc.first_line;
                  comment_start_col = yylloc.first_column;
                  BEGIN(COMMENT); }
<COMMENT>"*/"   { BEGIN(INITIAL); }
<COMMENT>\n     { yycol = 1; }
<COMMENT>.      { }
<COMMENT><<EOF>> { diag_error(comment_start_line, comment_start_col, "unterminated comment");
                   BEGIN(INITIAL);
                   yyterminate(); }

"void"          { return T_VOID; }
"int"           { return T_INT; }
"double"        { return T_DOUBLE; }
"bool"          { return T_BOOL; }
"string"        { return T_STRING; }
"class"         { return T_CLASS; }
"interface"     { return T_INTERFACE; }
"null"          { return T_NULL; }
"this"          { return T_THIS; }
"extends"       { return T_EXTENDS; }
"implements"    { return T_IMPLEMENTS; }
"for"           { return T_FOR; }
"while"         { return T_WHILE; }
"if"            { return T_IF; }
"else"          { return T_ELSE; }
"return"        { return T_RETURN; }
"break"         { return T_BREAK; }
"continue"      { return T_CONTINUE; }
"New"           { return T_NEW; }
"NewArray"      { return T_NEWARRAY; }
"Print"         { return T_PRINT; }
"ReadInteger"   { return T_READINTEGER; }
"ReadLine"      { return T_READLINE; }
"true"          { yylval.ival = 1; return T_BOOL_LITERAL; }
"false"         { yylval.ival = 0; return T_BOOL_LITERAL; }

{HEXINT}        { yylval.ival = parse_int_literal(yytext + 2, 16, yylloc.first_line, yylloc.first_column);
                  return T_INT_LITERAL; }
{INT}           { yylval.ival = parse_int_literal(yytext, 10, yylloc.first_line, yylloc.first_column);
                  return T_INT_LITERAL; }
{DOUBLE}        { yylval.dval = strtod(yytext, NULL); return T_DOUBLE_LITERAL; }

\"(\\.|[^\\\"\n])*\"  { yylval.sval = unescape_string(yytext, yylloc.first_line, yylloc.first_column);
                        return T_STRING_LITERAL; }
\"(\\.|[^\\\"\n])*    { diag_error(yylloc.first_line, yylloc.first_column, "unterminated string literal");
                        yylval.sval = xstrdup("");
                        return T_STRING_LITERAL; }

{ID}            { yylval.sval = xstrdup(yytext); return T_ID; }

"[]"            { return T_DIMS; }
"++"            { return T_INC; }
"--"            { return T_DEC; }
"+="            { return T_PLUSEQ; }
"-="            { return T_MINUSEQ; }
"*="            { return T_MULEQ; }
"/="            { return T_DIVEQ; }
"%="            { return T_MODEQ; }
"<="            { return T_LE; }
">="            { return T_GE; }
"=="            { return T_EQ; }
"!="            { return T_NE; }
"&&"            { return T_AND; }
"||"            { return T_OR; }

[-+*/%<>=!;,.()\[\]{}]  { return yytext[0]; }

.               { diag_error(yylloc.first_line, yylloc.first_column,
                             "stray '%s' in program", yytext); }

%%

/* Convert the quoted, escaped source text of a string literal into the
 * raw bytes it denotes. */
static char *unescape_string(const char *text, int line, int col) {
    size_t n = strlen(text);
    char *out = xmalloc(n);          /* result is never longer than the input */
    size_t o = 0;
    for (size_t i = 1; i + 1 < n; i++) {   /* skip surrounding quotes */
        char c = text[i];
        if (c != '\\') {
            out[o++] = c;
            continue;
        }
        char e = text[++i];
        switch (e) {
        case 'n': out[o++] = '\n'; break;
        case 't': out[o++] = '\t'; break;
        case 'r': out[o++] = '\r'; break;
        case '0': out[o++] = '\0'; break;
        case '\\': out[o++] = '\\'; break;
        case '"': out[o++] = '"'; break;
        default:
            diag_warning(line, col + (int)i - 1, "unknown escape sequence '\\%c'", e);
            out[o++] = e;
        }
    }
    out[o] = '\0';
    return out;
}

static int parse_int_literal(const char *text, int base, int line, int col) {
    errno = 0;
    long long v = strtoll(text, NULL, base);
    if (errno == ERANGE || v > INT_MAX) {
        diag_error(line, col, "integer literal is too large for type int");
        return 0;
    }
    return (int)v;
}

void lexer_set_input(FILE *f) {
    yyin = f;
    yycol = 1;
}
