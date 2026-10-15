%option yylineno

%{
#include <stdio.h>
#include <string.h>
#include "y.tab.h"

void yyerror(const char *s);
%}

%x COMMENT

DIGIT       [0-9]
HEXDIG      [0-9a-fA-F]
ID          [A-Za-z][A-Za-z0-9_]*
HEXINT      0[xX]{HEXDIG}+
DOUBLE      {DIGIT}+\.[0-9]*([eE][+-]?{DIGIT}+)?
INT         {DIGIT}+

%%

[ \t\r]+      {  }
\n            { }

"//".* {  }
"/*"          { BEGIN(COMMENT); }
<COMMENT>"*/" { BEGIN(INITIAL); }
<COMMENT>\n   {  }
<COMMENT>.    { }

"void"        { return T_VOID; }
"int"         { return T_INT; }
"double"      { return T_DOUBLE; }
"bool"        { return T_BOOL; }
"string"      { return T_STRING; }
"class"       { return T_CLASS; }
"interface"   { return T_INTERFACE; }
"null"        { return T_NULL; }
"this"        { return T_THIS; }
"extends"     { return T_EXTENDS; }
"implements"  { return T_IMPLEMENTS; }
"for"         { return T_FOR; }
"while"       { return T_WHILE; }
"if"          { return T_IF; }
"else"        { return T_ELSE; }
"return"      { return T_RETURN; }
"break"       { return T_BREAK; }
"New"         { return T_NEW; }
"NewArray"    { return T_NEWARRAY; }
"Print"       { return T_PRINT; }
"ReadInteger" { return T_READINTEGER; }
"ReadLine"    { return T_READLINE; }
"true"        { yylval.bval = 1; return T_BOOL_LITERAL; }
"false"       { yylval.bval = 0; return T_BOOL_LITERAL; }

{INT}         { yylval.ival = atoi(yytext); return T_INT_LITERAL; }
{HEXINT}      { yylval.ival = (int)strtol(yytext, NULL, 0); return T_INT_LITERAL; }
{DOUBLE}      { yylval.dval = atof(yytext); return T_DOUBLE_LITERAL; }
\"([^\"\n])*\" { strncpy(yylval.sval, yytext, sizeof(yylval.sval)-1); yylval.sval[sizeof(yylval.sval)-1] = '\0'; return T_STRING_LITERAL; }

{ID}          { strncpy(yylval.sval, yytext, sizeof(yylval.sval)-1); yylval.sval[sizeof(yylval.sval)-1] = '\0'; return T_ID; }

"+"           { return T_PLUS; }
"-"           { return T_MINUS; }
"*"           { return T_MULT; }
"/"           { return T_DIV; }
"%"           { return T_MOD; }
"<"           { return T_LESS; }
"<="          { return T_LEQ; }
">"           { return T_GREATER; }
">="          { return T_GEQ; }
"=="          { return T_EQ; }
"!="          { return T_NEQ; }
"="           { return T_ASSIGN; }
"&&"          { return T_AND; }
"||"          { return T_OR; }
"!"           { return T_NOT; }
";"           { return T_SEMICOLON; }
","           { return T_COMMA; }
"."           { return T_DOT; }
"["           { return T_LBRACK; }
"]"           { return T_RBRACK; }
"("           { return T_LPAREN; }
")"           { return T_RPAREN; }
"{"           { return T_LBRACE; }
"}"           { return T_RBRACE; }

.             { yyerror("Invalid character"); }

%%

int yywrap() {
    return 1;
}

