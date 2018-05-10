%% DCG/CFG
    
    %% apply the NP semantics to the VP semantics
    s('@'(NpS,VpS)) --> np(NpS), vp(VpS).

    %% apply the determiner semantics to the noun phrase semantics
    np('@'(DetS,NS)) --> det(DetS), n(NS).

    %% just take the proper name semantics
    np(PnSem) --> pn(PnSem).

    %% just take the semantics if the intransitive verb
    vp(l(X,'@'(ViSem,X))) --> vi(ViSem).

    %% Apply the semantics of the transitive verb to Y and X,
    %% such that the first argument of kill is filled by the
    %% variable Y and the second by X. Then lambda abstract over X and
    %% apply the semantics of the object NP to the result. Finally, lambda
    %% abstract over Y.
    vp(l(Y,'@'(NpS,l(X,'@'('@'(VtS,Y),X))))) --> vt(VtS), np(NpS).


%% Lexicon

    det(l(R, l(S, exists(X,'@'(R,X),'@'(S,X))))) --> [a].
    det(l(R, l(S, exists(X,'@'(R,X),'@'(S,X))))) --> [the].
    det(l(R, l(S, all(X, '@'(R,X),'@'(S,X))))) --> [every].
    
    n(l(X,bride(X))) --> [bride].
    n(l(X,nurse(X))) --> [nurse].
    n(l(X,yakuza(X))) --> [yakuza].
    n(l(X,whiskey(X))) --> [whiskey].

    pn(l(P,'@'(P,bill))) --> [bill].
    pn(l(P,'@'(P,gogo))) --> [gogo].

    vi(l(X,whistle(X))) --> [whistles].
    vi(l(X,fight(X))) --> [fights].

    vt(l(X,l(Y,drink(Y,X)))) --> [drinks].
    vt(l(X,l(Y,kill(Y,X)))) --> [kills].



%%%
%%% Predicate to do beta reduction
%%%
%%% Call beta_reduce(Sem,R) (with Sem being a semantic representation
%%% constructed by the grammar) to get a beta-reduced and therefore
%%% more readable version of the semantics.
%%%

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
