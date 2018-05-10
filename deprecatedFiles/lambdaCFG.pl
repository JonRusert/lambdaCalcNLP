%Lambda Calculus and Computational Linguistics
    
    s(app(NP,VP)) --> np(NP), vp(VP).

    np(app(Det, Noun)) --> det(Det), noun(Noun).  

    vp(IV) --> iv(IV).

    det(lambda(P,lambda(Q,all(X,imp(app(P,X),app(Q,X)))))) --> [every].

    noun(lambda(X,boxer(X)))-->[boxer].

    iv(lamda(Y,walk(Y))) -->[walks].


%Beta Conversion

    betaConvert(X,Y):- betaConvert(X,Y,[]).
  
    betaConvert(X,Y,[]):-var(X),!,Y==X.

    betaConvert(Expression, Result, Stack):- nonvar(Expression), Expression == app(Functor, Argument), alphaConvert(Functor, Converted), betaConvert(Converted, Result, [Argument|Stack]), !.

    betaConvert(Expression, Result, [X|Stack]):- nonvar(Expression), Expression == lambda(X,Formula), betaConvert(Formula, Result, Stack), !.

    betaConvert(Formula, Result, []):- nonvar(Formula), !, compose(Formula, Functor, Formulas), betaConvertList(Formulas, ResultFormulas), compose(Result, Function, ResultFormulas).
    






 
