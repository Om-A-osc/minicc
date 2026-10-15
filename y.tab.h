/* A Bison parser, made by GNU Bison 3.8.2.  */

/* Bison interface for Yacc-like parsers in C

   Copyright (C) 1984, 1989-1990, 2000-2015, 2018-2021 Free Software Foundation,
   Inc.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <https://www.gnu.org/licenses/>.  */

/* As a special exception, you may create a larger work that contains
   part or all of the Bison parser skeleton and distribute that work
   under terms of your choice, so long as that work isn't itself a
   parser generator using the skeleton or a modified version thereof
   as a parser skeleton.  Alternatively, if you modify or redistribute
   the parser skeleton itself, you may (at your option) remove this
   special exception, which will cause the skeleton and the resulting
   Bison output files to be licensed under the GNU General Public
   License without this special exception.

   This special exception was added by the Free Software Foundation in
   version 2.2 of Bison.  */

/* DO NOT RELY ON FEATURES THAT ARE NOT DOCUMENTED in the manual,
   especially those whose name start with YY_ or yy_.  They are
   private implementation details that can be changed or removed.  */

#ifndef YY_YY_Y_TAB_H_INCLUDED
# define YY_YY_Y_TAB_H_INCLUDED
/* Debug traces.  */
#ifndef YYDEBUG
# define YYDEBUG 0
#endif
#if YYDEBUG
extern int yydebug;
#endif

/* Token kinds.  */
#ifndef YYTOKENTYPE
# define YYTOKENTYPE
  enum yytokentype
  {
    YYEMPTY = -2,
    YYEOF = 0,                     /* "end of file"  */
    YYerror = 256,                 /* error  */
    YYUNDEF = 257,                 /* "invalid token"  */
    T_ID = 258,                    /* T_ID  */
    T_STRING_LITERAL = 259,        /* T_STRING_LITERAL  */
    T_INT_LITERAL = 260,           /* T_INT_LITERAL  */
    T_BOOL_LITERAL = 261,          /* T_BOOL_LITERAL  */
    T_DOUBLE_LITERAL = 262,        /* T_DOUBLE_LITERAL  */
    T_VOID = 263,                  /* T_VOID  */
    T_INT = 264,                   /* T_INT  */
    T_DOUBLE = 265,                /* T_DOUBLE  */
    T_BOOL = 266,                  /* T_BOOL  */
    T_STRING = 267,                /* T_STRING  */
    T_CLASS = 268,                 /* T_CLASS  */
    T_INTERFACE = 269,             /* T_INTERFACE  */
    T_NULL = 270,                  /* T_NULL  */
    T_THIS = 271,                  /* T_THIS  */
    T_EXTENDS = 272,               /* T_EXTENDS  */
    T_IMPLEMENTS = 273,            /* T_IMPLEMENTS  */
    T_FOR = 274,                   /* T_FOR  */
    T_WHILE = 275,                 /* T_WHILE  */
    T_IF = 276,                    /* T_IF  */
    T_ELSE = 277,                  /* T_ELSE  */
    T_RETURN = 278,                /* T_RETURN  */
    T_BREAK = 279,                 /* T_BREAK  */
    T_NEW = 280,                   /* T_NEW  */
    T_NEWARRAY = 281,              /* T_NEWARRAY  */
    T_PRINT = 282,                 /* T_PRINT  */
    T_READINTEGER = 283,           /* T_READINTEGER  */
    T_READLINE = 284,              /* T_READLINE  */
    T_PLUS = 285,                  /* T_PLUS  */
    T_MINUS = 286,                 /* T_MINUS  */
    T_MULT = 287,                  /* T_MULT  */
    T_DIV = 288,                   /* T_DIV  */
    T_MOD = 289,                   /* T_MOD  */
    T_LESS = 290,                  /* T_LESS  */
    T_LEQ = 291,                   /* T_LEQ  */
    T_GREATER = 292,               /* T_GREATER  */
    T_GEQ = 293,                   /* T_GEQ  */
    T_EQ = 294,                    /* T_EQ  */
    T_NEQ = 295,                   /* T_NEQ  */
    T_ASSIGN = 296,                /* T_ASSIGN  */
    T_AND = 297,                   /* T_AND  */
    T_OR = 298,                    /* T_OR  */
    T_NOT = 299,                   /* T_NOT  */
    T_SEMICOLON = 300,             /* T_SEMICOLON  */
    T_COMMA = 301,                 /* T_COMMA  */
    T_DOT = 302,                   /* T_DOT  */
    T_LBRACK = 303,                /* T_LBRACK  */
    T_RBRACK = 304,                /* T_RBRACK  */
    T_LPAREN = 305,                /* T_LPAREN  */
    T_RPAREN = 306,                /* T_RPAREN  */
    T_LBRACE = 307,                /* T_LBRACE  */
    T_RBRACE = 308,                /* T_RBRACE  */
    UMINUS = 309,                  /* UMINUS  */
    T_IFX = 310                    /* T_IFX  */
  };
  typedef enum yytokentype yytoken_kind_t;
#endif
/* Token kinds.  */
#define YYEMPTY -2
#define YYEOF 0
#define YYerror 256
#define YYUNDEF 257
#define T_ID 258
#define T_STRING_LITERAL 259
#define T_INT_LITERAL 260
#define T_BOOL_LITERAL 261
#define T_DOUBLE_LITERAL 262
#define T_VOID 263
#define T_INT 264
#define T_DOUBLE 265
#define T_BOOL 266
#define T_STRING 267
#define T_CLASS 268
#define T_INTERFACE 269
#define T_NULL 270
#define T_THIS 271
#define T_EXTENDS 272
#define T_IMPLEMENTS 273
#define T_FOR 274
#define T_WHILE 275
#define T_IF 276
#define T_ELSE 277
#define T_RETURN 278
#define T_BREAK 279
#define T_NEW 280
#define T_NEWARRAY 281
#define T_PRINT 282
#define T_READINTEGER 283
#define T_READLINE 284
#define T_PLUS 285
#define T_MINUS 286
#define T_MULT 287
#define T_DIV 288
#define T_MOD 289
#define T_LESS 290
#define T_LEQ 291
#define T_GREATER 292
#define T_GEQ 293
#define T_EQ 294
#define T_NEQ 295
#define T_ASSIGN 296
#define T_AND 297
#define T_OR 298
#define T_NOT 299
#define T_SEMICOLON 300
#define T_COMMA 301
#define T_DOT 302
#define T_LBRACK 303
#define T_RBRACK 304
#define T_LPAREN 305
#define T_RPAREN 306
#define T_LBRACE 307
#define T_RBRACE 308
#define UMINUS 309
#define T_IFX 310

/* Value type.  */
#if ! defined YYSTYPE && ! defined YYSTYPE_IS_DECLARED
union YYSTYPE
{
#line 13 "c_cpp_parser.y"

    int ival;
    double dval;
    char sval[256];
    int bval;

#line 184 "y.tab.h"

};
typedef union YYSTYPE YYSTYPE;
# define YYSTYPE_IS_TRIVIAL 1
# define YYSTYPE_IS_DECLARED 1
#endif


extern YYSTYPE yylval;


int yyparse (void);


#endif /* !YY_YY_Y_TAB_H_INCLUDED  */
