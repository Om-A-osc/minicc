%{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int yylex();
extern int yylineno;
extern char* yytext;
extern FILE *yyin;
void yyerror(const char *s);
%}

%union {
    int ival;
    double dval;
    char sval[256];
    int bval;
}

%token <sval> T_ID T_STRING_LITERAL
%token <ival> T_INT_LITERAL T_BOOL_LITERAL
%token <dval> T_DOUBLE_LITERAL

%token T_VOID T_INT T_DOUBLE T_BOOL T_STRING T_CLASS T_INTERFACE T_NULL T_THIS
%token T_EXTENDS T_IMPLEMENTS T_FOR T_WHILE T_IF T_ELSE T_RETURN T_BREAK
%token T_NEW T_NEWARRAY T_PRINT T_READINTEGER T_READLINE

%token T_PLUS T_MINUS T_MULT T_DIV T_MOD
%token T_LESS T_LEQ T_GREATER T_GEQ T_EQ T_NEQ
%token T_ASSIGN T_AND T_OR T_NOT
%token T_SEMICOLON T_COMMA T_DOT
%token T_LBRACK T_RBRACK T_LPAREN T_RPAREN T_LBRACE T_RBRACE

%right T_ASSIGN
%left T_OR
%left T_AND
%left T_EQ T_NEQ
%left T_LESS T_LEQ T_GREATER T_GEQ
%left T_PLUS T_MINUS
%left T_MULT T_DIV T_MOD
%right T_NOT UMINUS
%left T_LBRACK T_RBRACK T_LPAREN T_RPAREN T_DOT
%nonassoc T_IFX
%nonassoc T_ELSE

%start Program

%%

Program:
    Declarations
    ;

Declarations:
    Declaration
    | Declarations Declaration
    ;

Declaration:
    VarDecl
    | FunDecl
    | ClassDecl
    ;

Type:
    T_INT | T_DOUBLE | T_BOOL | T_STRING | T_VOID
    ;

BaseType:
    T_INT | T_DOUBLE | T_BOOL | T_STRING
    ;

VarDecl:
    Type T_ID T_SEMICOLON
    ;

FunDecl:
    Type T_ID T_LPAREN Formals T_RPAREN StmtBlock
    ;

Formals:
    /* empty */
    | Type T_ID
    | Formals T_COMMA Type T_ID
    ;

ClassDecl:
    T_CLASS T_ID T_LBRACE Fields T_RBRACE
    ;

Fields:
    /* empty */
    | Fields Field
    ;

Field:
    VarDecl
    | FunDecl
    ;

StmtBlock:
    T_LBRACE VarDecls Stmts T_RBRACE
    ;

VarDecls:
    /* empty */
    | VarDecls VarDecl
    ;

Stmts:
    /* empty */
    | Stmts Stmt
    ;

Stmt:
    Expr T_SEMICOLON
    | IfStmt
    | WhileStmt
    | ForStmt
    | T_BREAK T_SEMICOLON
    | ReturnStmt
    | StmtBlock
    ;

IfStmt:
    T_IF T_LPAREN Expr T_RPAREN Stmt %prec T_IFX
    | T_IF T_LPAREN Expr T_RPAREN Stmt T_ELSE Stmt
    ;

WhileStmt:
    T_WHILE T_LPAREN Expr T_RPAREN Stmt
    ;

ForStmt:
    T_FOR T_LPAREN Expr T_SEMICOLON Expr T_SEMICOLON Expr T_RPAREN Stmt
    ;

ReturnStmt:
    T_RETURN Expr T_SEMICOLON
    ;

Expr:
    LValue T_ASSIGN Expr
    | Constant
    | LValue
    | T_THIS
    | Call
    | T_LPAREN Expr T_RPAREN
    | Expr T_PLUS Expr
    | Expr T_MINUS Expr
    | Expr T_MULT Expr
    | Expr T_DIV Expr
    | Expr T_MOD Expr
    | Expr T_LESS Expr
    | Expr T_LEQ Expr
    | Expr T_GREATER Expr
    | Expr T_GEQ Expr
    | Expr T_EQ Expr
    | Expr T_NEQ Expr
    | Expr T_AND Expr
    | Expr T_OR Expr
    | T_MINUS Expr %prec UMINUS
    | T_NOT Expr
    | T_NEW T_LPAREN T_ID T_RPAREN
    | T_NEWARRAY T_LPAREN T_INT_LITERAL T_COMMA BaseType T_RPAREN
    | T_READINTEGER T_LPAREN T_RPAREN
    | T_READLINE T_LPAREN T_RPAREN
    ;

LValue:
    T_ID
    | Expr T_DOT T_ID
    | Expr T_LBRACK Expr T_RBRACK
    ;

Call:
    T_ID T_LPAREN Actuals T_RPAREN
    | Expr T_DOT T_ID T_LPAREN Actuals T_RPAREN
    ;

Actuals:
    /* empty */
    | Expr
    | Actuals T_COMMA Expr
    ;

Constant:
    T_INT_LITERAL
    | T_DOUBLE_LITERAL
    | T_BOOL_LITERAL
    | T_STRING_LITERAL
    | T_NULL
    ;

%%

void yyerror(const char *s) {
    fprintf(stderr, "Error at line %d near '%s': %s\n", yylineno, yytext, s);
}

int main(int argc, char *argv[]) {
    if (argc > 1) {
        FILE *file = fopen(argv[1], "r");
        if (!file) {
            perror(argv[1]);
            return 1;
        }
        yyin = file;
    }
    
    if (yyparse() == 0) {
        printf("Parsing complete. No syntax errors found.\n");
    } else {
        printf("Parsing failed.\n");
    }

    return 0;
}


