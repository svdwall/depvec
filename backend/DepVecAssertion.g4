grammar DepVecAssertion;

// Top-level rule consumes the whole input, including any trailing-token check.
assertion
    : formula EOF
    ;

formula
    : disjunction
    ;

disjunction
    : conjunction (OR conjunction)*
    ;

conjunction
    : formulaUnary (AND formulaUnary)*
    ;

formulaUnary
    : NOT formulaUnary
    | formulaPrimary
    ;

formulaPrimary
    : TRUE
    | FALSE
    | comparison
    | LPAREN formula RPAREN
    ;

comparison
    : additive (EQ | NE | LT | LE | GT | GE) additive
    ;

additive
    : multiplicative ((PLUS | MINUS) multiplicative)*
    ;

multiplicative
    : unary ((STAR | SLASH) unary)*
    ;

unary
    : (NOT | MINUS) unary
    | primary
    ;

primary
    : Integer
    | STAR Identifier
    | Identifier
    | LPAREN additive RPAREN
    ;

LBRACE   : '{' ;
RBRACE   : '}' ;
LPAREN   : '(' ;
RPAREN   : ')' ;
OR       : '||' ;
AND      : '&&' ;
EQ       : '==' ;
NE       : '!=' ;
LE       : '<=' ;
GE       : '>=' ;
LT       : '<' ;
GT       : '>' ;
PLUS     : '+' ;
MINUS    : '-' ;
STAR     : '*' ;
SLASH    : '/' ;
NOT      : '!' ;
TRUE     : 'true' ;
FALSE    : 'false' ;
Integer  : [0-9]+ ;
Identifier : [a-zA-Z_] [a-zA-Z_0-9.]* ;
Whitespace : [ \t\r\n]+ -> skip ;
