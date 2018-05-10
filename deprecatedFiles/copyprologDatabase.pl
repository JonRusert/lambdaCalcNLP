% DCG/CFG
    s('@'(NpS,VpS)) --> np(NpS), vp(VpS).

    np('@'(DetS,NS)) --> det(DetS), n(NS).

    vp(l(X,'@'(VSem,X))) --> vi(VSem).
    
    vp(l(Y,'@'(NpS,l(X,'@'('@'(VtS,Y),X))))) --> vt(VtS), np(NpS).



% Beta reduction

    beta_reduce(F,Reduced) :- atom(F), !, Reduced = F.
    beta_reduce(F,Reduced) :- var(F), !, Reduced = F.

    beta_reduce('@'(l(X,F),Y),Reduced) :- !, beta_reduce(Y,YR), YR=X, beta_reduce(F,Reduced).
    beta_reduce('@'(F,X),Reduced) :- !, beta_reduce(F,FR), beta_reduce(X,XR), beta_reduce('@'(FR,XR),Reduced).

    beta_reduce(Term,Reduced) :-
    Term =.. [Functor|Args],
    reduce_args(Args,RArgs),
    Reduced =.. [Functor|RArgs].

    reduce_args([],[]).
    reduce_args([Head|Rest],[RHead|RRest]) :-
    beta_reduce(Head,RHead),
    reduce_args(Rest,RRest).


% Lexicon

    det(l(R, l(S, exists(X,'@'(R,X),'@'(S,X))))) --> [a].
    det(l(R, l(S, exists(X,'@'(R,X),'@'(S,X))))) --> [the].
    det(l(R, l(S, all(X, '@'(R,X),'@'(S,X))))) --> [every].

    n(l(X,bride(X))) --> [bride].
    n(l(X,nurse(X))) --> [nurse].
    n(l(X,yakuza(X))) --> [yakuza].
    n(l(X,whiskey(X))) --> [whiskey].


    vi(l(X,whistle(X))) --> [whistles].
    vi(l(X,fight(X))) --> [fights].

    vt(l(X,l(Y,drink(Y,X)))) --> [drinks].
    vt(l(X,l(Y,kill(Y,X)))) --> [kills].

    
    
