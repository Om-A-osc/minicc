/* parser.y - Bison LALR(1) grammar for MiniCC.
 *
 * Builds the AST defined in ast.h. Ambiguities (dangling else, member access
 * versus method call) are resolved with explicit precedence so the grammar
 * is conflict-free. `error` productions let the parser resynchronise at the
 * next ';' and keep reporting further errors instead of stopping at the
 * first one. */

%code requires {
#include "ast.h"
}

%code {
#include <stdio.h>
#include <string.h>

int yylex(void);
void yyerror(const char *s);

Program *g_program = NULL;

#define POS(loc) (loc).first_line, (loc).first_column
}

%locations
%define parse.error verbose
%token-table

%union {
    int ival;
    double dval;
    char *sval;
    Type *type;
    Expr *expr;
    Stmt *stmt;
    Vec *vec;
    VarDecl *var;
    FuncDecl *func;
    ClassDecl *cls;
    IfaceDecl *iface;
    Decl *decl;
}

%token <sval> T_ID "identifier" T_STRING_LITERAL "string literal"
%token <ival> T_INT_LITERAL "integer literal" T_BOOL_LITERAL "boolean literal"
%token <dval> T_DOUBLE_LITERAL "double literal"

%token T_VOID "void" T_INT "int" T_DOUBLE "double" T_BOOL "bool" T_STRING "string"
%token T_CLASS "class" T_INTERFACE "interface" T_NULL "null" T_THIS "this"
%token T_EXTENDS "extends" T_IMPLEMENTS "implements"
%token T_FOR "for" T_WHILE "while" T_IF "if" T_ELSE "else"
%token T_RETURN "return" T_BREAK "break" T_CONTINUE "continue"
%token T_NEW "New" T_NEWARRAY "NewArray" T_PRINT "Print"
%token T_READINTEGER "ReadInteger" T_READLINE "ReadLine"

%token T_DIMS "[]" T_INC "++" T_DEC "--"
%token T_PLUSEQ "+=" T_MINUSEQ "-=" T_MULEQ "*=" T_DIVEQ "/=" T_MODEQ "%="
%token T_LE "<=" T_GE ">=" T_EQ "==" T_NE "!=" T_AND "&&" T_OR "||"

/* precedence, lowest first */
%precedence IFX
%precedence T_ELSE
%precedence '=' T_PLUSEQ T_MINUSEQ T_MULEQ T_DIVEQ T_MODEQ
%left T_OR
%left T_AND
%left T_EQ T_NE
%left '<' T_LE '>' T_GE
%left '+' '-'
%left '*' '/' '%'
%right '!' UMINUS T_INC T_DEC
%precedence '[' '.'

%type <vec>   DeclList Members Prototypes StmtList Formals FormalList Actuals ExprList IdList Implements
%type <decl>  Decl Member
%type <type>  Type
%type <var>   VarDecl Param
%type <func>  FuncDecl Prototype
%type <cls>   ClassDecl
%type <iface> IfaceDecl
%type <sval>  Extends
%type <stmt>  Stmt Block IfStmt WhileStmt ForStmt ForInit
%type <expr>  Expr OptExpr LValue Call Constant

%start Program

%%

Program
    : DeclList                      { g_program = program_new($1); }
    ;

DeclList
    : %empty                        { $$ = vec_new(); }
    | DeclList Decl                 { if ($2) vec_push($1, $2); $$ = $1; }
    ;

Decl
    : VarDecl ';'                   { $$ = decl_var($1); }
    | FuncDecl                      { $$ = decl_func($1); }
    | ClassDecl                     { $$ = decl_class($1); }
    | IfaceDecl                     { $$ = decl_iface($1); }
    | error ';'                     { yyerrok; $$ = NULL; }
    | error '}'                     { yyerrok; $$ = NULL; }
    ;

/* ------------------------------------------------------------- types */

Type
    : T_INT                         { $$ = type_prim(TY_INT); }
    | T_DOUBLE                      { $$ = type_prim(TY_DOUBLE); }
    | T_BOOL                        { $$ = type_prim(TY_BOOL); }
    | T_STRING                      { $$ = type_prim(TY_STRING); }
    | T_ID                          { $$ = type_named($1, POS(@1)); }
    | Type T_DIMS                   { $$ = type_array($1, POS(@1)); }
    ;

/* ------------------------------------------------------ declarations */

VarDecl
    : Type T_ID                     { $$ = vardecl_new($1, $2, NULL, POS(@2)); }
    | Type T_ID '=' Expr            { $$ = vardecl_new($1, $2, $4, POS(@2)); }
    ;

FuncDecl
    : Type T_ID '(' Formals ')' Block
                                    { $$ = funcdecl_new($1, $2, $4, $6, POS(@2)); }
    | T_VOID T_ID '(' Formals ')' Block
                                    { $$ = funcdecl_new(type_prim(TY_VOID), $2, $4, $6, POS(@2)); }
    ;

Formals
    : %empty                        { $$ = vec_new(); }
    | FormalList                    { $$ = $1; }
    ;

FormalList
    : Param                         { $$ = vec_new(); vec_push($$, $1); }
    | FormalList ',' Param          { vec_push($1, $3); $$ = $1; }
    ;

Param
    : Type T_ID                     { $$ = vardecl_new($1, $2, NULL, POS(@2)); }
    ;

ClassDecl
    : T_CLASS T_ID Extends Implements '{' Members '}'
                                    { $$ = classdecl_new($2, $3, $4, $6, POS(@2)); }
    ;

Extends
    : %empty                        { $$ = NULL; }
    | T_EXTENDS T_ID                { $$ = $2; }
    ;

Implements
    : %empty                        { $$ = vec_new(); }
    | T_IMPLEMENTS IdList           { $$ = $2; }
    ;

IdList
    : T_ID                          { $$ = vec_new(); vec_push($$, $1); }
    | IdList ',' T_ID               { vec_push($1, $3); $$ = $1; }
    ;

Members
    : %empty                        { $$ = vec_new(); }
    | Members Member                { if ($2) vec_push($1, $2); $$ = $1; }
    ;

Member
    : VarDecl ';'                   { $$ = decl_var($1); }
    | FuncDecl                      { $$ = decl_func($1); }
    | error ';'                     { yyerrok; $$ = NULL; }
    ;

IfaceDecl
    : T_INTERFACE T_ID '{' Prototypes '}'
                                    { $$ = ifacedecl_new($2, $4, POS(@2)); }
    ;

Prototypes
    : %empty                        { $$ = vec_new(); }
    | Prototypes Prototype          { if ($2) vec_push($1, $2); $$ = $1; }
    ;

Prototype
    : Type T_ID '(' Formals ')' ';' { $$ = funcdecl_new($1, $2, $4, NULL, POS(@2)); }
    | T_VOID T_ID '(' Formals ')' ';'
                                    { $$ = funcdecl_new(type_prim(TY_VOID), $2, $4, NULL, POS(@2)); }
    | error ';'                     { yyerrok; $$ = NULL; }
    ;

/* --------------------------------------------------------- statements */

Block
    : '{' StmtList '}'              { $$ = stmt_new(ST_BLOCK, POS(@1)); $$->stmts = $2; }
    ;

StmtList
    : %empty                        { $$ = vec_new(); }
    | StmtList Stmt                 { if ($2) vec_push($1, $2); $$ = $1; }
    ;

Stmt
    : VarDecl ';'                   { $$ = stmt_new(ST_VARDECL, POS(@1)); $$->var = $1; }
    | Expr ';'                      { $$ = stmt_new(ST_EXPR, POS(@1)); $$->expr = $1; }
    | ';'                           { $$ = stmt_new(ST_BLOCK, POS(@1)); $$->stmts = vec_new(); }
    | IfStmt                        { $$ = $1; }
    | WhileStmt                     { $$ = $1; }
    | ForStmt                       { $$ = $1; }
    | T_BREAK ';'                   { $$ = stmt_new(ST_BREAK, POS(@1)); }
    | T_CONTINUE ';'                { $$ = stmt_new(ST_CONTINUE, POS(@1)); }
    | T_RETURN ';'                  { $$ = stmt_new(ST_RETURN, POS(@1)); }
    | T_RETURN Expr ';'             { $$ = stmt_new(ST_RETURN, POS(@1)); $$->expr = $2; }
    | T_PRINT '(' ExprList ')' ';'  { $$ = stmt_new(ST_PRINT, POS(@1)); $$->args = $3; }
    | Block                         { $$ = $1; }
    | error ';'                     { yyerrok; $$ = NULL; }
    ;

IfStmt
    : T_IF '(' Expr ')' Stmt %prec IFX
                                    { $$ = stmt_new(ST_IF, POS(@1)); $$->expr = $3; $$->body = $5; }
    | T_IF '(' Expr ')' Stmt T_ELSE Stmt
                                    { $$ = stmt_new(ST_IF, POS(@1)); $$->expr = $3; $$->body = $5; $$->else_body = $7; }
    ;

WhileStmt
    : T_WHILE '(' Expr ')' Stmt     { $$ = stmt_new(ST_WHILE, POS(@1)); $$->expr = $3; $$->body = $5; }
    ;

ForStmt
    : T_FOR '(' ForInit ';' OptExpr ';' OptExpr ')' Stmt
                                    { $$ = stmt_new(ST_FOR, POS(@1));
                                      $$->init = $3; $$->expr = $5; $$->step = $7; $$->body = $9; }
    ;

ForInit
    : %empty                        { $$ = NULL; }
    | Expr                          { $$ = stmt_new(ST_EXPR, POS(@1)); $$->expr = $1; }
    | VarDecl                       { $$ = stmt_new(ST_VARDECL, POS(@1)); $$->var = $1; }
    ;

OptExpr
    : %empty                        { $$ = NULL; }
    | Expr                          { $$ = $1; }
    ;

/* -------------------------------------------------------- expressions */

Expr
    : LValue '=' Expr               { $$ = expr_assign($1, $3, 0, BIN_ADD, POS(@2)); }
    | LValue T_PLUSEQ Expr          { $$ = expr_assign($1, $3, 1, BIN_ADD, POS(@2)); }
    | LValue T_MINUSEQ Expr         { $$ = expr_assign($1, $3, 1, BIN_SUB, POS(@2)); }
    | LValue T_MULEQ Expr           { $$ = expr_assign($1, $3, 1, BIN_MUL, POS(@2)); }
    | LValue T_DIVEQ Expr           { $$ = expr_assign($1, $3, 1, BIN_DIV, POS(@2)); }
    | LValue T_MODEQ Expr           { $$ = expr_assign($1, $3, 1, BIN_MOD, POS(@2)); }
    | T_INC Expr                    { $$ = expr_incdec($2, 1, 1, POS(@1)); }
    | T_DEC Expr                    { $$ = expr_incdec($2, 1, 0, POS(@1)); }
    | Expr T_INC                    { $$ = expr_incdec($1, 0, 1, POS(@2)); }
    | Expr T_DEC                    { $$ = expr_incdec($1, 0, 0, POS(@2)); }
    | Constant                      { $$ = $1; }
    | LValue                        { $$ = $1; }
    | T_THIS                        { $$ = expr_new(EX_THIS, POS(@1)); }
    | Call                          { $$ = $1; }
    | '(' Expr ')'                  { $$ = $2; }
    | Expr '+' Expr                 { $$ = expr_binary(BIN_ADD, $1, $3, POS(@2)); }
    | Expr '-' Expr                 { $$ = expr_binary(BIN_SUB, $1, $3, POS(@2)); }
    | Expr '*' Expr                 { $$ = expr_binary(BIN_MUL, $1, $3, POS(@2)); }
    | Expr '/' Expr                 { $$ = expr_binary(BIN_DIV, $1, $3, POS(@2)); }
    | Expr '%' Expr                 { $$ = expr_binary(BIN_MOD, $1, $3, POS(@2)); }
    | Expr '<' Expr                 { $$ = expr_binary(BIN_LT, $1, $3, POS(@2)); }
    | Expr T_LE Expr                { $$ = expr_binary(BIN_LE, $1, $3, POS(@2)); }
    | Expr '>' Expr                 { $$ = expr_binary(BIN_GT, $1, $3, POS(@2)); }
    | Expr T_GE Expr                { $$ = expr_binary(BIN_GE, $1, $3, POS(@2)); }
    | Expr T_EQ Expr                { $$ = expr_binary(BIN_EQ, $1, $3, POS(@2)); }
    | Expr T_NE Expr                { $$ = expr_binary(BIN_NE, $1, $3, POS(@2)); }
    | Expr T_AND Expr               { $$ = expr_binary(BIN_AND, $1, $3, POS(@2)); }
    | Expr T_OR Expr                { $$ = expr_binary(BIN_OR, $1, $3, POS(@2)); }
    | '-' Expr %prec UMINUS         { $$ = expr_unary(UN_NEG, $2, POS(@1)); }
    | '!' Expr                      { $$ = expr_unary(UN_NOT, $2, POS(@1)); }
    | T_NEW '(' T_ID ')'            { $$ = expr_new_obj($3, POS(@1)); }
    | T_NEWARRAY '(' Expr ',' Type ')'
                                    { $$ = expr_new_array($3, $5, POS(@1)); }
    | T_READINTEGER '(' ')'         { $$ = expr_new(EX_READ_INTEGER, POS(@1)); }
    | T_READLINE '(' ')'            { $$ = expr_new(EX_READ_LINE, POS(@1)); }
    ;

LValue
    : T_ID                          { $$ = expr_ident($1, POS(@1)); }
    | Expr '.' T_ID                 { $$ = expr_field($1, $3, POS(@2)); }
    | Expr '[' Expr ']'             { $$ = expr_index($1, $3, POS(@2)); }
    ;

Call
    : T_ID '(' Actuals ')'          { $$ = expr_call($1, $3, POS(@1)); }
    | Expr '.' T_ID '(' Actuals ')' { $$ = expr_method_call($1, $3, $5, POS(@2)); }
    ;

Actuals
    : %empty                        { $$ = vec_new(); }
    | ExprList                      { $$ = $1; }
    ;

ExprList
    : Expr                          { $$ = vec_new(); vec_push($$, $1); }
    | ExprList ',' Expr             { vec_push($1, $3); $$ = $1; }
    ;

Constant
    : T_INT_LITERAL                 { $$ = expr_int($1, POS(@1)); }
    | T_DOUBLE_LITERAL              { $$ = expr_double($1, POS(@1)); }
    | T_BOOL_LITERAL                { $$ = expr_bool($1, POS(@1)); }
    | T_STRING_LITERAL              { $$ = expr_string($1, POS(@1)); }
    | T_NULL                        { $$ = expr_new(EX_NULL_LIT, POS(@1)); }
    ;

%%

void yyerror(const char *s) {
    diag_error(yylloc.first_line, yylloc.first_column, "%s", s);
}

/* Human readable token name, used by --dump-tokens. */
const char *token_name(int tok) {
    if (tok == 0) return "end of file";
    return yytname[YYTRANSLATE(tok)];
}
