## Steps to compile and run :-

lex c_cpp_lexical_analyzer.l
yacc -d c_cpp_parser.y

gcc lex.yy.c y.tab.c

./a.out test.txt



-> test.txt is the input file.
-> If you want to run it on some other test program, just replace the test.txt with the filename.

./a.out <filename>




# About the lexer and parser :-

This lexer is similar to the one created during assignment 1.
The lexer identifies tokens like keywords ( including void , int , string , true , if , else , return , break , ... etc ) , operators ( including + , - , * , % , / , brackets , > , ... and so on with unique token names ) , values ( including integers , digits , doubles , etc ) , ID ( identifier ) , comments ( single line and multi-line) , etc.
It ignores all the buffer between multi-line comments ( /* .. */ ).


In the parser, there are production rules for program ( as mentioned in the assignment description ), it includes different declarations like ( variable declaration , function declaration , class declaration ). Furthermore there are multiple rules for statements , blocks and expressions.

During the parsing, any error found is printed with the corresponding line number and error location.
If the parsing is successful, we get success message.




# Sample input and output:-



Input :-



int main(){
  
  int a;
  int i;
  
  for (i = 0; i < 10; i = i + 1) {
        if (i == 5) {
            break; // Break statement
        }
    }
  
  
  return 0

}



Output:-

Error at line 16 near '}': syntax error
Parsing failed.






output for the test.txt file:-


Parsing complete. No syntax errors found.












