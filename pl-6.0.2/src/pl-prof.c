/*  $Id$

    Part of SWI-Prolog

    Author:        Jan Wielemaker
    E-mail:        wielemak@science.uva.nl
    WWW:           http://www.swi-prolog.org
    Copyright (C): 1985-2007, University of Amsterdam

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
*/

/*#define O_DEBUG 1*/
#include "pl-incl.h"

#undef LD
#define LD LOCAL_LD

#ifdef O_PROFILE

#define current    (LD->profile.current)	/* current call-node */
#define roots      (LD->profile.roots)		/* roots (<spontaneous>) */
#define nodes      (LD->profile.nodes)		/* # nodes created */
#define accounting (LD->profile.accounting)	/* We are accounting */
#define sum_ok	   (LD->profile.sum_ok)		/* sibling count is ok */

#define PROFTYPE_MAGIC 0x639a2fb1

static int identify_def(term_t t, void *handle);
static int get_def(term_t t, void **handle);
static PL_prof_type_t prof_default_type =
{ identify_def,					/* unify a Definition */
  get_def,
  NULL,						/* dummy */
  PROFTYPE_MAGIC
};

#define MAX_PROF_TYPES 10
static PL_prof_type_t *types[MAX_PROF_TYPES] = { &prof_default_type };

#define PROFNODE_MAGIC 0x7ae38f24

typedef struct call_node
{ intptr_t	    magic;		/* PROFNODE_MAGIC */
  struct call_node *parent;
  void *            handle;		/* handle to procedure-id */
  PL_prof_type_t   *type;
  uintptr_t	    calls;		/* Calls from the parent */
  uintptr_t	    redos;		/* redos while here */
  uintptr_t	    exits;		/* exits to the parent */
  uintptr_t	    recur;		/* recursive calls */
  uintptr_t	    ticks;		/* time-statistics */
  uintptr_t	    sibling_ticks;	/* ticks in a siblings */
  struct call_node *next;		/* next in siblings chain */
  struct call_node *siblings;		/* my offspring */
} call_node;

static void	freeProfileData(void);
static void	collectSiblingsTime(void);

static void
activateProfiler(int active ARG_LD)
{ int i;

  LD->profile.active = active;
  for(i=0; i<MAX_PROF_TYPES; i++)
  { if ( types[i] && types[i]->activate )
      (*types[i]->activate)(active);
  }

  if ( active )
  { /* consider using thread-time? */
    LD->profile.time_at_start = CpuTime(CPU_USER);
  }
  updateAlerted(LD);

  sum_ok = FALSE;
}


#ifdef __WINDOWS__

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
MS-Windows version
- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

static void profile(intptr_t ticks, PL_local_data_t *ld);

#if (_MSC_VER < 1400)
typedef DWORD DWORD_PTR;
#endif

static LARGE_INTEGER last_profile;
static HANDLE	     mythread;
static PL_local_data_t *my_LD;
static UINT	     timer;
static intptr_t	     virtual_events;
static intptr_t	     events;

static intptr_t
prof_new_ticks(HANDLE thread)
{ FILETIME created, exit, kernel, user;
  LARGE_INTEGER u;
  intptr_t ticks;

  if ( !GetThreadTimes(thread,
		       &created,
		       &exit,
		       &kernel,
		       &user) )
    return -1;				/* Error condition */

  u.LowPart  = user.dwLowDateTime;
  u.HighPart = user.dwHighDateTime;

  ticks = (intptr_t)((u.QuadPart - last_profile.QuadPart)/10240);
  last_profile = u;

  virtual_events += ticks;
  events++;

  return ticks;
}

static void CALLBACK
callTimer(UINT id, UINT msg, DWORD_PTR dwuser, DWORD_PTR dw1, DWORD_PTR dw2)
{ intptr_t newticks;

  SuspendThread(mythread);		/* stop thread to avoid trouble */
  if ( (newticks = prof_new_ticks(mythread)) )
  { if ( newticks < 0 )			/* Windows 95/98/... */
      newticks = 1;
    profile(newticks, my_LD);
  }
  ResumeThread(mythread);
}


static bool
startProfiler(void)
{ GET_LD
  MMRESULT rval;

  DuplicateHandle(GetCurrentProcess(),
		  GetCurrentThread(),
		  GetCurrentProcess(),
		  &mythread,
		  0,
		  FALSE,
		  DUPLICATE_SAME_ACCESS);

  if ( prof_new_ticks(mythread) < 0 )
  { printMessage(ATOM_informational,
		 PL_ATOM, ATOM_profile_no_cpu_time);
  }
  virtual_events = 0;
  events = 0;

  my_LD = LD;

  rval = timeSetEvent(10,
		      5,		/* resolution (milliseconds) */
		      callTimer,
		      (DWORD_PTR)0,
		      TIME_PERIODIC);
  if ( rval )
    timer = rval;
  else
    return PL_error(NULL, 0, NULL, ERR_SYSCALL, "timeSetEvent");

  activateProfiler(TRUE PASS_LD);

  succeed;
}


void
stopItimer(void)
{ if ( timer )
  { DEBUG(1, Sdprintf("%ld events, %ld virtual\n",
		      events, virtual_events));

    timeKillEvent(timer);
    timer = 0;
  }
  if ( mythread )
  { CloseHandle(mythread);
    mythread = 0;
    my_LD = NULL;
  }
}

#else /*__WINDOWS__*/

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
POSIX version

(*) Originally this code used 1,  which   appeared  to  mean anytime the
system starts a new time slice for  the task. Hiroo Koshimoto discovered
this caused very  fast  interrupts   on  MacOSX  `Leopart',  effectively
causing the monitored program to stop  working.   We  now use 1000 us (1
millisecond). That should be safe on   most modern hardware. If hardware
becomes a lot faster we may wish to reduce this value.
- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#ifdef TIME_WITH_SYS_TIME
#include <sys/time.h>
#include <time.h>
#else
#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#else
#include <time.h>
#endif
#endif

static void profile(int sig);
static struct itimerval value, ovalue;	/* itimer controlling structures */

static bool
startProfiler(void)
{ GET_LD
  set_sighandler(SIGPROF, profile);

  value.it_interval.tv_sec  = 0;
  value.it_interval.tv_usec = 1000;	/* see (*) above */
  value.it_value.tv_sec  = 0;
  value.it_value.tv_usec = 1000;

  if (setitimer(ITIMER_PROF, &value, &ovalue) != 0)
    return PL_error(NULL, 0, MSG_ERRNO, ERR_SYSCALL, setitimer);
  activateProfiler(TRUE PASS_LD);

  succeed;
}

void
stopItimer(void)
{ GET_LD
  value.it_interval.tv_sec  = 0;
  value.it_interval.tv_usec = 0;
  value.it_value.tv_sec  = 0;
  value.it_value.tv_usec = 0;

  if ( !LD->profile.active )
    return;
  if (setitimer(ITIMER_PROF, &value, &ovalue) != 0)
  { warning("Failed to stop interval timer: %s", OsError());
    return;
  }
}

#endif /*__WINDOWS__*/

static bool
stopProfiler(void)
{ GET_LD
  if ( !LD->profile.active )
    succeed;

  LD->profile.time += CpuTime(CPU_USER) - LD->profile.time_at_start;

  stopItimer();
  activateProfiler(FALSE PASS_LD);
#ifndef __WINDOWS__
  set_sighandler(SIGPROF, SIG_IGN);
#endif

  succeed;
}


static
PRED_IMPL("profiler", 2, profiler, 0)
{ PRED_LD
  int val;

  if ( !PL_unify_bool_ex(A1, LD->profile.active) )
    fail;
  if ( !PL_get_bool_ex(A2, &val) )
    fail;

  if ( val == LD->profile.active )
    succeed;

  if ( val )
    return startProfiler();
  else
    return stopProfiler();
}


/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
Prolog query API:

$prof_sibling_of(?Child, ?Parent)
	Generate hierachy.  If Parent is '-', generate the roots

$prof_node(+Node, -Pred, -Calls, -Redos, -Exits, -TickFraction)

- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

static int
get_node(term_t t, call_node **node ARG_LD)
{ if ( PL_is_functor(t, FUNCTOR_dprof_node1) )
  { term_t a = PL_new_term_ref();
    void *ptr;

    _PL_get_arg(1, t, a);
    if ( PL_get_pointer(a, &ptr) )
    { call_node *n = ptr;

      if ( n->magic == PROFNODE_MAGIC )
      { *node = n;
        succeed;
      }
    }
  }

  return PL_error(NULL, 0, NULL, ERR_TYPE, ATOM_profile_node, t);
}


static int
unify_node(term_t t, call_node *node)
{ return PL_unify_term(t,
		       PL_FUNCTOR, FUNCTOR_dprof_node1,
		         PL_POINTER, node);
}


static
PRED_IMPL("$prof_sibling_of", 2, prof_sibling_of, PL_FA_NONDETERMINISTIC)
{ PRED_LD
  call_node *parent = NULL;
  call_node *sibling = NULL;

  switch( CTX_CNTRL )
  { case FRG_FIRST_CALL:
    { atom_t a;

      if ( !PL_is_variable(A1) )
      { if ( get_node(A1, &sibling PASS_LD) )
	{ if ( sibling->parent )
	    return unify_node(A2, sibling->parent);
	}
	fail;
      } else
      { if ( PL_get_atom(A2, &a) && a == ATOM_minus )
	  sibling = roots;
	else if ( get_node(A2, &parent PASS_LD) )
	  sibling = parent->siblings;
	else
	  fail;
      }

      if ( !sibling )
	fail;

      goto return_sibling;
    }
    case FRG_REDO:
    { sibling = CTX_PTR;

    return_sibling:
      if ( !unify_node(A1, sibling) )
	fail;
      if ( sibling->next )
	ForeignRedoPtr(sibling->next);
      succeed;
    }
    case FRG_CUTTED:
    default:
      succeed;
  }
}


static int
identify_def(term_t t, void *handle)
{ return unify_definition(MODULE_user, t, handle, 0, GP_QUALIFY);
}


static int
unify_node_id(term_t t, call_node *n)
{ if ( n->type->magic == PROFTYPE_MAGIC )
  { return (*n->type->unify)(t, n->handle);
  } else
  { GET_LD

    return PL_unify_pointer(t, n->handle);
  }
}


static
PRED_IMPL("$prof_node", 7, prof_node, 0)
{ PRED_LD
  call_node *n = NULL;

  if ( !get_node(A1, &n PASS_LD) )
    fail;

  if ( unify_node_id(A2, n) &&
       PL_unify_integer(A3, n->calls) &&
       PL_unify_integer(A4, n->redos) &&
       PL_unify_integer(A5, n->exits) &&
       PL_unify_integer(A6, n->recur) &&
       PL_unify_integer(A7, n->ticks) )
    succeed;

  fail;
}


		 /*******************************
		 *	    COLLECT DATA	*
		 *******************************/

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
prof_procedure_data(+Head,
		    -TimeSelf, -TimeSiblings, -Parents, -Siblings)
    Where Parents  = list_of(reference(Head, Calls, Redos))
      And Siblings = list_of(reference(Head, Calls, Redos))
- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

typedef struct prof_ref
{ struct prof_ref *next;		/* next in chain */
  void * handle;			/* Procedure handle */
  PL_prof_type_t   *type;
  int   cycle;
  uintptr_t ticks;
  uintptr_t sibling_ticks;
  uintptr_t calls;			/* calls to/from this predicate */
  uintptr_t redos;			/* redos to/from this predicate */
} prof_ref;


typedef struct
{ Definition def;
  uintptr_t ticks;
  uintptr_t sibling_ticks;
  uintptr_t calls;
  uintptr_t redos;
  uintptr_t recur;
  prof_ref *callers;
  prof_ref *callees;
} node_sum;


static void
free_relatives(prof_ref *r)
{ prof_ref *n;

  for( ; r; r=n)
  { n = r->next;
    freeHeap(r, sizeof(*r));
  }
}


#define DEF_SPONTANEOUS (Definition)0
#define DEF_RECURSIVE   (Definition)1


static void
add_parent_ref(node_sum *sum,
	       call_node *self,
	       void *handle, PL_prof_type_t *type,
	       int cycle)
{ prof_ref *r;

  sum->calls += self->calls;
  sum->redos += self->redos;

  for(r=sum->callers; r; r=r->next)
  { if ( r->handle == handle && r->cycle == cycle )
    { r->calls += self->calls;
      r->redos += self->redos;
      r->ticks += self->ticks;
      r->sibling_ticks += self->sibling_ticks;

      return;
    }
  }

  r = allocHeapOrHalt(sizeof(*r));
  r->calls = self->calls;
  r->redos = self->redos;
  r->ticks = self->ticks;
  r->sibling_ticks = self->sibling_ticks;
  r->handle = handle;
  r->type = type;
  r->cycle = cycle;
  r->next = sum->callers;
  sum->callers = r;
}


static void
add_recursive_ref(node_sum *sum, uintptr_t count, int cycle)
{ prof_ref *r;

  for(r=sum->callers; r; r=r->next)
  { if ( r->handle == DEF_RECURSIVE && r->cycle == cycle )
    { r->calls += count;

      return;
    }
  }

  r = allocHeapOrHalt(sizeof(*r));
  memset(r, 0, sizeof(*r));
  r->calls = count;
  r->handle = DEF_RECURSIVE;
  r->cycle = cycle;
  r->next = sum->callers;
  sum->callers = r;
}


static void
add_sibling_ref(node_sum *sum, call_node *sibling, int cycle)
{ prof_ref *r;

  for(r=sum->callees; r; r=r->next)
  { if ( r->handle == sibling->handle && r->cycle == cycle )
    { r->calls += sibling->calls;
      r->redos += sibling->redos;
      r->ticks += sibling->ticks;
      r->sibling_ticks += sibling->sibling_ticks;

      return;
    }
  }

  r = allocHeapOrHalt(sizeof(*r));
  r->calls = sibling->calls;
  r->redos = sibling->redos;
  r->ticks = sibling->ticks;
  r->sibling_ticks = sibling->sibling_ticks;
  r->handle = sibling->handle;
  r->type = sibling->type;
  r->cycle = cycle;
  r->next = sum->callees;
  sum->callees = r;
}




static int
sumProfile(call_node *n, void *handle, PL_prof_type_t *type,
	   node_sum *sum, int seen ARG_LD)
{ call_node *s;
  int count = 0;

  if ( n->handle == handle )
  { count++;
    if ( !seen )
    { sum->ticks         += n->ticks;
      sum->sibling_ticks += n->sibling_ticks;
    }

    if ( n->parent )
      add_parent_ref(sum, n, n->parent->handle, n->parent->type, seen);
    else
      add_parent_ref(sum, n, DEF_SPONTANEOUS, NULL, seen);

    if ( n->recur )
      add_recursive_ref(sum, n->recur, seen);

    for(s=n->siblings; s; s = s->next)
      add_sibling_ref(sum, s, seen);

    seen++;
  }

  for(s=n->siblings; s; s = s->next)
    count += sumProfile(s, handle, type, sum, seen PASS_LD);

  return count;
}


static int
unify_relatives(term_t list, prof_ref *r ARG_LD)
{ term_t tail = PL_copy_term_ref(list);
  term_t head = PL_new_term_ref();
  term_t tmp = PL_new_term_ref();
  static functor_t FUNCTOR_node6;

  if ( !FUNCTOR_node6 )
    FUNCTOR_node6 = PL_new_functor(PL_new_atom("node"), 6);

  for( ; r; r=r->next)
  { int rc;

    if ( !PL_unify_list(tail, head, tail) )
      fail;

    PL_put_variable(tmp);
    if ( r->handle == DEF_SPONTANEOUS )
      rc=PL_unify_atom_chars(tmp, "<spontaneous>");
    else if ( r->handle == DEF_RECURSIVE )
      rc=PL_unify_atom_chars(tmp, "<recursive>");
    else
      rc=(*r->type->unify)(tmp, r->handle);

    if ( !rc ||
	 !PL_unify_term(head, PL_FUNCTOR, FUNCTOR_node6,
			PL_TERM, tmp,
			PL_INT,  r->cycle,
			PL_LONG, r->ticks,
			PL_LONG, r->sibling_ticks,
			PL_LONG, r->calls,
			PL_LONG, r->redos) )
      fail;
  }

  return PL_unify_nil(tail);
}


static int
get_def(term_t t, void **handle)
{ Procedure proc;

  if ( get_procedure(t, &proc, 0, GP_FIND) )
  { *handle = proc->definition;
    succeed;
  }

  fail;
}


static int
get_handle(term_t t, void **handle)
{ int i;

  for(i=0; i<MAX_PROF_TYPES; i++)
  { if ( types[i] && types[i]->get )
    { switch( (*types[i]->get)(t, handle) )
      { case TRUE:
	  succeed;
	case FALSE:
	  break;
	default:
	  assert(0);
      }
    }
  }

  fail;
}

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
$prof_procedure_data(+Procedure,
		     -Ticks, -TicksSiblings,
		     -Calls, -Redos,
		     -Callers, -Callees)

- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */


static
PRED_IMPL("$prof_procedure_data", 7, prof_procedure_data, PL_FA_TRANSPARENT)
{ PRED_LD
  void *handle;
  node_sum sum;
  call_node *n;
  int rc;
  int count = 0;

  if ( !get_handle(A1, &handle) )
    fail;

  collectSiblingsTime();
  memset(&sum, 0, sizeof(sum));
  for(n=roots; n; n=n->next)
    count += sumProfile(n, handle, &prof_default_type, &sum, 0 PASS_LD);

  if ( count == 0 )
    fail;				/* nothing known about this one */

  rc = ( PL_unify_integer(A2, sum.ticks) &&
	 PL_unify_integer(A3, sum.sibling_ticks) &&
	 PL_unify_integer(A4, sum.calls) &&
	 PL_unify_integer(A5, sum.redos) &&
	 unify_relatives(A6, sum.callers PASS_LD) &&
	 unify_relatives(A7, sum.callees PASS_LD)
       );

  free_relatives(sum.callers);
  free_relatives(sum.callees);

  return rc ? TRUE : FALSE;
}


static
PRED_IMPL("$prof_statistics", 4, prof_statistics, 0)
{ PRED_LD
  if ( PL_unify_integer(A1, LD->profile.ticks) &&
       PL_unify_integer(A2, LD->profile.accounting_ticks) &&
       PL_unify_float(A3, LD->profile.time) &&
       PL_unify_integer(A4, nodes) )
    succeed;

  fail;
}


		 /*******************************
		 *	       RESET		*
		 *******************************/

bool
resetProfiler(void)
{ GET_LD
  stopProfiler();

  freeProfileData();
  LD->profile.ticks = 0;
  LD->profile.accounting_ticks = 0;
  LD->profile.time = 0.0;
  accounting = FALSE;

  succeed;
}


static
PRED_IMPL("reset_profiler", 0, reset_profiler, 0)
{ resetProfiler();

  succeed;
}


		 /*******************************
		 *	     TOPLEVEL		*
		 *******************************/

static
PRED_IMPL("$profile", 1, profile, PL_FA_TRANSPARENT)
{ int rc;

  resetProfiler();
  startProfiler();
  rc = callProlog(NULL, A1, PL_Q_PASS_EXCEPTION, NULL);
  stopProfiler();

  DEBUG(0,
	{ PRED_LD
	    Sdprintf("Created %ld nodes (%ld bytes); %ld ticks (%ld overhead)\n",
		     nodes, nodes*sizeof(call_node),
		     LD->profile.ticks, LD->profile.accounting_ticks);
	});

  return rc;
}


/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
This function is responsible for collection the profiling statistics  at
run time.  It is called by the UNIX interval timer on each clock tick of
the  machine  (every  20  milli seconds).  If profiling is plain we just
increment the profiling tick of the procedure on top of the stack.   For
cumulative  profiling  we  have  to  scan the entire local stack.  As we
don't want to increment each invokation of recursive  functions  on  the
stack  we  maintain a flag on each function.  This flag is set the first
time the function is found on the stack.  If is is found set the profile
counter will not be incremented.  We do a second pass over the frames to
clear the flags again.
- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#undef LD
#define LD LOCAL_LD

static void
#ifdef __WINDOWS__
profile(intptr_t count, PL_local_data_t *__PL_ld)
{
#else /*__WINDOWS__*/
profile(int sig)
{ GET_LD
  (void)sig;

#define count 1

  if ( !LD )
    return;

#if _AIX
  if ( !LD->profile.active )
    return;
#endif

#if !defined(BSD_SIGNALS) && !defined(HAVE_SIGACTION)
  signal(SIGPROF, profile);
#endif

#endif /*__WINDOWS__*/
  LD->profile.ticks += count;

  if ( accounting )
  { LD->profile.accounting_ticks += count;
  } else if ( current )
  { assert(current->magic == PROFNODE_MAGIC);
    current->ticks += count;
  }

#undef count
}

		 /*******************************
		 *     HIERARCHY ACCOUNTING	*
		 *******************************/

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
profCall(Definition handle)
    Make a call from the current node to handle.
- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

static call_node *
prof_call(void *handle, PL_prof_type_t *type ARG_LD)
{ call_node *node = current;

  accounting = TRUE;

  if ( !node )				/* root-node of the profile */
  { for(node = roots; node; node=node->next)
    { if ( node->handle == handle )
      { node->calls++;
	current = node;
	DEBUG(2, Sdprintf("existing root %p\n", current));

	return node;
      }
    }

    node = allocHeapOrHalt(sizeof(*node));
    memset(node, 0, sizeof(*node));
    nodes++;

    node->magic = PROFNODE_MAGIC;
    node->handle = handle;
    node->type = type;
    node->calls++;
    node->next = roots;
    roots = node;
    current = node;
    accounting = FALSE;
    DEBUG(2, Sdprintf("new root %p\n", node));

    return node;
  }

					/* straight recursion */
  if ( node->handle == handle )
  { node->recur++;
    DEBUG(2, Sdprintf("direct recursion\n"));
    accounting = FALSE;
    return node;
  } else				/* from same parent */
  { void *parent = node->handle;

    for(node=node->parent; node; node = node->parent)
    { if ( node->handle == handle &&
	   node->parent &&
	   node->parent->handle == parent )
      { node->recur++;

	current = node;
	DEBUG(2, Sdprintf("indirect recursion\n"));
	accounting = FALSE;
	return node;
      }
    }
  }


  for(node=current->siblings; node; node=node->next)
  { if ( node->handle == handle )
    { current = node;
      node->calls++;
      DEBUG(2, Sdprintf("existing child\n"));
      accounting = FALSE;
      return node;
    }
  }

  node = allocHeapOrHalt(sizeof(*node));
  memset(node, 0, sizeof(*node));
  nodes++;
  node->magic = PROFNODE_MAGIC;
  node->handle = handle;
  node->type = type;
  node->parent = current;
  node->calls++;
  node->next = current->siblings;
  current->siblings = node;
  current = node;
  DEBUG(2, Sdprintf("new child\n"));
  accounting = FALSE;

  return node;
}


call_node *
profCall(Definition def ARG_LD)
{ if ( true(def, P_NOPROFILE) )
    return current;

  return prof_call(def, &prof_default_type PASS_LD);
}


/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

Exit, resuming execution in node. Note that   we  ignore the node if the
magic doesn't fit. That can  happen   using  tprofile/1 because the stop
doesn't need to match the  start.   Actually,  we should clear prof_node
references when clearing the data, but  this is rather complicated (must
be synchornised with atom-gc) and  it   still  doesn't cope with foreign
code.  Considering this is development only, we'll leave this for now.
- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

void
profResumeParent(struct call_node *node ARG_LD)
{ call_node *n;

  if ( node && node->magic != PROFNODE_MAGIC )
    return;

  accounting = TRUE;
  for(n=current; n && n != node; n=n->parent)
  { n->exits++;
  }
  accounting = FALSE;

  current = node;
}


void
profExit(struct call_node *node ARG_LD)
{ if ( !node || node->magic != PROFNODE_MAGIC )
    return;

  profResumeParent(node->parent PASS_LD);
}


void
profRedo(struct call_node *node ARG_LD)
{ if ( node && node->magic != PROFNODE_MAGIC )
    return;

  if ( node )
  { node->redos++;
  }
  current = node;
}


void
profSetHandle(struct call_node *node, void *handle)
{ node->handle = handle;
}


		 /*******************************
		 *	 FOREIGN ACCESS		*
		 *******************************/

int
PL_register_profile_type(PL_prof_type_t *type)
{ int i;

  for(i=0; i<MAX_PROF_TYPES; i++)
  { if ( types[i] == type )
      return TRUE;
  }
  for(i=0; i<MAX_PROF_TYPES; i++)
  { if ( !types[i] )
    { types[i] = type;
      type->magic = PROFTYPE_MAGIC;
      return TRUE;
    }
  }

  assert(0);
  return FALSE;
}


void *
PL_prof_call(void *handle, PL_prof_type_t *type)
{ GET_LD

  return prof_call(handle, type PASS_LD);
}

void
PL_prof_exit(void *node)
{ GET_LD
  struct call_node *n = node;

  profResumeParent(n->parent PASS_LD);
}


		 /*******************************
		 *	       COLLECT		*
		 *******************************/


static uintptr_t
collectSiblingsNode(call_node *n)
{ call_node *s;
  uintptr_t count = 0;

  for(s=n->siblings; s; s=s->next)
  { count += collectSiblingsNode(s);
    n->sibling_ticks = count;
  }

  return count+n->ticks;
}


static void
collectSiblingsTime(void)
{ GET_LD

  if ( !sum_ok )
  { call_node *n;

    for(n=roots; n; n=n->next)
      collectSiblingsNode(n);

    sum_ok = TRUE;
  }
}


static void
freeProfileNode(call_node *node ARG_LD)
{ call_node *n, *next;

  assert(node->magic == PROFNODE_MAGIC);

  for(n=node->siblings; n; n=next)
  { next = n->next;

    freeProfileNode(n PASS_LD);
  }

  node->magic = 0;
  freeHeap(node, sizeof(*node));
  nodes--;
}


static void
freeProfileData(void)
{ GET_LD
  call_node *n, *next;

  n = roots;
  roots = NULL;
  current = NULL;

  for(; n; n=next)
  { next = n->next;
    freeProfileNode(n PASS_LD);
  }

  assert(nodes == 0);
}

#else /* O_PROFILE */

		 /*******************************
		 *	    NO PROFILER		*
		 *******************************/

void
stopItimer(void)
{
}

static
PRED_IMPL("profiler", 2, profiler, 0)
{ return notImplemented("profile", 2);
}

static
PRED_IMPL("reset_profiler", 0, reset_profiler, 0)
{ return notImplemented("reset_profile", 0);
}

static
PRED_IMPL("$prof_node", 7, prof_node, 0)
{ return notImplemented("profile_node", 7);
}

static
PRED_IMPL("$prof_sibling_of", 2, prof_sibling_of, PL_FA_NONDETERMINISTIC)
{ return notImplemented("profile_sibling_of", 2);
}

static
PRED_IMPL("$profile", 1, profile, PL_FA_TRANSPARENT)
{ return notImplemented("$profile", 1);
}

static
PRED_IMPL("$prof_procedure_data", 7, prof_procedure_data, PL_FA_TRANSPARENT)
{ return notImplemented("$prof_procedure_data", 7);
}

static
PRED_IMPL("$prof_statistics", 4, prof_statistics, 0)
{ return notImplemented("$prof_statistics", 4);
}

/* Foreign interface of the profiler
*/

int
PL_register_profile_type(PL_prof_type_t *type)
{ return FALSE;				/* not supported */
}

void *
PL_prof_call(void *handle, PL_prof_type_t *type)
{ return NULL;
}

void
PL_prof_exit(void *node)
{
}

#endif /* O_PROFILE */

#ifdef O_PROF_PENTIUM
#include "pentium.c"

PRED_IMPL("show_pentium_profile", 0, show_pentium_profile, 0)
{ prof_report();

  succeed;
}

PRED_IMPL("reset_pentium_profile", 0, reset_pentium_profile, 0)
{ prof_reset();

  succeed;
}
#endif

		 /*******************************
		 *      PUBLISH PREDICATES	*
		 *******************************/

BeginPredDefs(profile)
  PRED_DEF("$profile", 1, profile, PL_FA_TRANSPARENT)
  PRED_DEF("profiler", 2, profiler, 0)
  PRED_DEF("reset_profiler", 0, reset_profiler, 0)
  PRED_DEF("$prof_node", 7, prof_node, 0)
  PRED_DEF("$prof_sibling_of", 2, prof_sibling_of, PL_FA_NONDETERMINISTIC)
  PRED_DEF("$prof_procedure_data", 7, prof_procedure_data, PL_FA_TRANSPARENT)
  PRED_DEF("$prof_statistics", 4, prof_statistics, 0)
#ifdef O_PROF_PENTIUM
  PRED_DEF("show_pentium_profile", 0, show_pentium_profile, 0)
  PRED_DEF("reset_pentium_profile", 0, reset_pentium_profile, 0)
#endif
EndPredDefs
