% DCG/CFG
    s('@'(NpS,VpS)) --> np(NpS), vp(VpS).

    np('@'(DetS,NS)) --> det(DetS), n(NS).

    np(NS) --> n(NS).
    
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

% Dets
    det(l(R, l(S, exists(X,'@'(R,X),'@'(S,X))))) --> [a].
    det(l(R, l(S, exists(X,'@'(R,X),'@'(S,X))))) --> [the].
    det(l(R, l(S, all(X, '@'(R,X),'@'(S,X))))) --> [every].


% Lexicon
    vt(l(X,l(Y,scare(Y,X)))) --> [scare].
    vt(l(X,l(Y,saves(Y,X)))) --> [saves].
    vt(l(X,l(Y,needs(Y,X)))) --> [needs].
    vt(l(X,l(Y,hurt(Y,X)))) --> [hurt].
    vt(l(X,l(Y,gives(Y,X)))) --> [gives].
    vt(l(X,l(Y,finds(Y,X)))) --> [finds].
    vt(l(X,l(Y,exposes(Y,X)))) --> [exposes].
    vt(l(X,l(Y,collects(Y,X)))) --> [collects].
    vi(l(X,wins(X))) --> [wins].
    vi(l(X,visits(X))) --> [visits].
    vi(l(X,trapped(X))) --> [trapped].
    vi(l(X,runs(X))) --> [runs].
    vi(l(X,receives(X))) --> [receives].
    vi(l(X,outsmarts(X))) --> [outsmarts].
    vi(l(X,laughs(X))) --> [laughs].
    vi(l(X,is(X))) --> [is].
    vi(l(X,captures(X))) --> [captures].
    n(l(X,vacuum(X))) --> [vacuum].
    n(l(X,trick(X))) --> [trick].
    n(l(X,professor(X))) --> [professor].
    n(l(X,painting(X))) --> [painting].
    n(l(X,money(X))) --> [money].
    n(l(X,mario(X))) --> [mario].
    n(l(X,mansion(X))) --> [mansion].
    n(l(X,luigi(X))) --> [luigi].
    n(l(X,king_boo(X))) --> [king_boo].
    n(l(X,hides(X))) --> [hides].
    n(l(X,help(X))) --> [help].
    n(l(X,ghosts(X))) --> [ghosts].
    n(l(X,fights(X))) --> [fights].
    n(l(X,defeats(X))) --> [defeats].
    n(l(X,bowser(X))) --> [bowser].
    n(l(X,boos(X))) --> [boos].
