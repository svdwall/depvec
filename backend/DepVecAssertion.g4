grammar DepVecAssertion;

// Top-level rule consumes the whole input, including any trailing-token check.
assertion
    : formula EOF
    ;

formula
    : implication
    ;

// Implication has the lowest precedence and associates to the right.
implication
    : disjunction (IMPLIES implication)?
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
    | pointsTo
    | comparison
    | LPAREN formula RPAREN
    ;

pointsTo
    : (AllocaIdentifier | GlobalIdentifier) POINTS_TO additive
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
    | STAR (LocalIdentifier | GlobalIdentifier)
    | LocalIdentifier
    | GlobalIdentifier
    | LPAREN additive RPAREN
    ;

LBRACE   : '{' ;
RBRACE   : '}' ;
LPAREN   : '(' ;
RPAREN   : ')' ;
OR       : '||' ;
AND      : '&&' ;
IMPLIES  : '->' ;
POINTS_TO : '|->' ;
EQ       : '=' ;
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
AllocaIdentifier : NameStart NameCharacter* ;
LocalIdentifier : '%' (NameStart NameCharacter* | [0-9]+) ;
GlobalIdentifier : '@' (NameStart NameCharacter* | [0-9]+) ;
fragment NameStart : [a-zA-Z_] ;
fragment NameCharacter : [a-zA-Z_0-9.] ;
Whitespace : [ \t\r\n]+ -> skip ;
