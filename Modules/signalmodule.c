/***********************************************************
Copyright (c) 2000, BeOpen.com.
Copyright (c) 1995-2000, Corporation for National Research Initiatives.
Copyright (c) 1990-1995, Stichting Mathematisch Centrum.
All rights reserved.

See the file "Misc/COPYRIGHT" for information on usage and
redistribution of this file, and for a DISCLAIMER OF ALL WARRANTIES.
******************************************************************/

/* Signal module -- many thanks to Lance Ellinghaus */

/* XXX Signals should be recorded per thread, now we have thread state. */

#include "Python.h"
#include "intrcheck.h"

#ifdef MS_WIN32
#include <process.h>
#endif

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include <signal.h>

#ifndef SIG_ERR
#define SIG_ERR ((RETSIGTYPE (*)(int))-1)
#endif

#if defined(PYOS_OS2)
#define NSIG 12
#include <process.h>
#endif

#ifndef NSIG
# if defined(_NSIG)
#  define NSIG _NSIG		/* For BSD/SysV */
# elif defined(_SIGMAX)
#  define NSIG (_SIGMAX + 1)	/* For QNX */
# elif defined(SIGMAX)
#  define NSIG (SIGMAX + 1)	/* For djgpp */
# else
#  define NSIG 64		/* Use a reasonable default value */
# endif
#endif



/*
   NOTES ON THE INTERACTION BETWEEN SIGNALS AND THREADS

   When threads are supported, we want the following semantics:

   - only the main thread can set a signal handler
   - any thread can get a signal handler
   - signals are only delivered to the main thread

   I.e. we don't support "synchronous signals" like SIGFPE (catching
   this doesn't make much sense in Python anyway) nor do we support
   signals as a means of inter-thread communication, since not all
   thread implementations support that (at least our thread library
   doesn't).

   We still have the problem that in some implementations signals
   generated by the keyboard (e.g. SIGINT) are delivered to all
   threads (e.g. SGI), while in others (e.g. Solaris) such signals are
   delivered to one random thread (an intermediate possibility would
   be to deliver it to the main thread -- POSIX?).  For now, we have
   a working implementation that works in all three cases -- the
   handler ignores signals if getpid() isn't the same as in the main
   thread.  XXX This is a hack.

*/

#ifdef WITH_THREAD
#include <sys/types.h> /* For pid_t */
#include "pythread.h"
static long main_thread;
static pid_t main_pid;
#endif

static struct {
        int tripped;
        PyObject *func;
} Handlers[NSIG];

static int is_tripped = 0; /* Speed up sigcheck() when none tripped */

static PyObject *DefaultHandler;
static PyObject *IgnoreHandler;
static PyObject *IntHandler;

static RETSIGTYPE (*old_siginthandler)(int) = SIG_DFL;



static PyObject *
signal_default_int_handler(PyObject *self, PyObject *args)
{
	PyErr_SetNone(PyExc_KeyboardInterrupt);
	return NULL;
}

static char default_int_handler_doc[] =
"default_int_handler(...)\n\
\n\
The default handler for SIGINT instated by Python.\n\
It raises KeyboardInterrupt.";



static int
checksignals_witharg(void * unused)
{
	return PyErr_CheckSignals();
}

static RETSIGTYPE
signal_handler(int sig_num)
{
#ifdef WITH_THREAD
	/* See NOTES section above */
	if (getpid() == main_pid) {
#endif
		is_tripped++;
		Handlers[sig_num].tripped = 1;
		Py_AddPendingCall(checksignals_witharg, NULL);
#ifdef WITH_THREAD
	}
#endif
#ifdef SIGCHLD
	if (sig_num == SIGCHLD) {
		/* To avoid infinite recursion, this signal remains
		   reset until explicit re-instated.
		   Don't clear the 'func' field as it is our pointer
		   to the Python handler... */
		Py_RETURN_FROM_SIGNAL_HANDLER(0);
	}
#endif
#ifdef HAVE_SIGINTERRUPT
	siginterrupt(sig_num, 1);
#endif
	signal(sig_num, signal_handler);
	Py_RETURN_FROM_SIGNAL_HANDLER(0);
}



#ifdef HAVE_ALARM
static PyObject *
signal_alarm(PyObject *self, PyObject *args)
{
	int t;
	if (!PyArg_Parse(args, "i", &t))
		return NULL;
	/* alarm() returns the number of seconds remaining */
	return PyInt_FromLong(alarm(t));
}

static char alarm_doc[] =
"alarm(seconds)\n\
\n\
Arrange for SIGALRM to arrive after the given number of seconds.";
#endif

#ifdef HAVE_PAUSE
static PyObject *
signal_pause(PyObject *self, PyObject *args)
{
	if (!PyArg_NoArgs(args))
		return NULL;

	Py_BEGIN_ALLOW_THREADS
	(void)pause();
	Py_END_ALLOW_THREADS
	/* make sure that any exceptions that got raised are propagated
	 * back into Python
	 */
	if (PyErr_CheckSignals())
		return NULL;

	Py_INCREF(Py_None);
	return Py_None;
}
static char pause_doc[] =
"pause()\n\
\n\
Wait until a signal arrives.";

#endif


static PyObject *
signal_signal(PyObject *self, PyObject *args)
{
	PyObject *obj;
	int sig_num;
	PyObject *old_handler;
	RETSIGTYPE (*func)(int);
	if (!PyArg_Parse(args, "(iO)", &sig_num, &obj))
		return NULL;
#ifdef WITH_THREAD
	if (PyThread_get_thread_ident() != main_thread) {
		PyErr_SetString(PyExc_ValueError,
				"signal only works in main thread");
		return NULL;
	}
#endif
	if (sig_num < 1 || sig_num >= NSIG) {
		PyErr_SetString(PyExc_ValueError,
				"signal number out of range");
		return NULL;
	}
	if (obj == IgnoreHandler)
		func = SIG_IGN;
	else if (obj == DefaultHandler)
		func = SIG_DFL;
	else if (!PyCallable_Check(obj)) {
		PyErr_SetString(PyExc_TypeError,
"signal handler must be signal.SIG_IGN, signal.SIG_DFL, or a callable object");
		return NULL;
	}
	else
		func = signal_handler;
#ifdef HAVE_SIGINTERRUPT
	siginterrupt(sig_num, 1);
#endif
	if (signal(sig_num, func) == SIG_ERR) {
		PyErr_SetFromErrno(PyExc_RuntimeError);
		return NULL;
	}
	old_handler = Handlers[sig_num].func;
	Handlers[sig_num].tripped = 0;
	Py_INCREF(obj);
	Handlers[sig_num].func = obj;
	return old_handler;
}

static char signal_doc[] =
"signal(sig, action) -> action\n\
\n\
Set the action for the given signal.  The action can be SIG_DFL,\n\
SIG_IGN, or a callable Python object.  The previous action is\n\
returned.  See getsignal() for possible return values.\n\
\n\
*** IMPORTANT NOTICE ***\n\
A signal handler function is called with two arguments:\n\
the first is the signal number, the second is the interrupted stack frame.";


static PyObject *
signal_getsignal(PyObject *self, PyObject *args)
{
	int sig_num;
	PyObject *old_handler;
	if (!PyArg_Parse(args, "i", &sig_num))
		return NULL;
	if (sig_num < 1 || sig_num >= NSIG) {
		PyErr_SetString(PyExc_ValueError,
				"signal number out of range");
		return NULL;
	}
	old_handler = Handlers[sig_num].func;
	Py_INCREF(old_handler);
	return old_handler;
}

static char getsignal_doc[] =
"getsignal(sig) -> action\n\
\n\
Return the current action for the given signal.  The return value can be:\n\
SIG_IGN -- if the signal is being ignored\n\
SIG_DFL -- if the default action for the signal is in effect\n\
None -- if an unknown handler is in effect\n\
anything else -- the callable Python object used as a handler\n\
";


/* List of functions defined in the module */
static PyMethodDef signal_methods[] = {
#ifdef HAVE_ALARM
	{"alarm",	        signal_alarm, 0, alarm_doc},
#endif
	{"signal",	        signal_signal, 0, signal_doc},
	{"getsignal",	        signal_getsignal, 0, getsignal_doc},
#ifdef HAVE_PAUSE
	{"pause",	        signal_pause, 0, pause_doc},
#endif
	{"default_int_handler", signal_default_int_handler, 0,
				default_int_handler_doc},
	{NULL,			NULL}		/* sentinel */
};



static char module_doc[] =
"This module provides mechanisms to use signal handlers in Python.\n\
\n\
Functions:\n\
\n\
alarm() -- cause SIGALRM after a specified time [Unix only]\n\
signal() -- set the action for a given signal\n\
getsignal() -- get the signal action for a given signal\n\
pause() -- wait until a signal arrives [Unix only]\n\
default_int_handler() -- default SIGINT handler\n\
\n\
Constants:\n\
\n\
SIG_DFL -- used to refer to the system default handler\n\
SIG_IGN -- used to ignore the signal\n\
NSIG -- number of defined signals\n\
\n\
SIGINT, SIGTERM, etc. -- signal numbers\n\
\n\
*** IMPORTANT NOTICE ***\n\
A signal handler function is called with two arguments:\n\
the first is the signal number, the second is the interrupted stack frame.";

DL_EXPORT(void)
initsignal(void)
{
	PyObject *m, *d, *x;
	int i;

#ifdef WITH_THREAD
	main_thread = PyThread_get_thread_ident();
	main_pid = getpid();
#endif

	/* Create the module and add the functions */
	m = Py_InitModule3("signal", signal_methods, module_doc);

	/* Add some symbolic constants to the module */
	d = PyModule_GetDict(m);

	x = DefaultHandler = PyLong_FromVoidPtr((void *)SIG_DFL);
        if (!x || PyDict_SetItemString(d, "SIG_DFL", x) < 0)
                goto finally;

	x = IgnoreHandler = PyLong_FromVoidPtr((void *)SIG_IGN);
        if (!x || PyDict_SetItemString(d, "SIG_IGN", x) < 0)
                goto finally;

        x = PyInt_FromLong((long)NSIG);
        if (!x || PyDict_SetItemString(d, "NSIG", x) < 0)
                goto finally;
        Py_DECREF(x);

	x = IntHandler = PyDict_GetItemString(d, "default_int_handler");
        if (!x)
                goto finally;
	Py_INCREF(IntHandler);

	Handlers[0].tripped = 0;
	for (i = 1; i < NSIG; i++) {
		RETSIGTYPE (*t)(int);
#ifdef HAVE_SIGACTION
		struct sigaction act;
		sigaction(i,  0, &act);
		t = act.sa_handler;
#else
		t = signal(i, SIG_IGN);
		signal(i, t);
#endif
		Handlers[i].tripped = 0;
		if (t == SIG_DFL)
			Handlers[i].func = DefaultHandler;
		else if (t == SIG_IGN)
			Handlers[i].func = IgnoreHandler;
		else
			Handlers[i].func = Py_None; /* None of our business */
		Py_INCREF(Handlers[i].func);
	}
	if (Handlers[SIGINT].func == DefaultHandler) {
		/* Install default int handler */
		Py_INCREF(IntHandler);
		Py_DECREF(Handlers[SIGINT].func);
		Handlers[SIGINT].func = IntHandler;
		old_siginthandler = signal(SIGINT, &signal_handler);
	}

#ifdef SIGHUP
	x = PyInt_FromLong(SIGHUP);
	PyDict_SetItemString(d, "SIGHUP", x);
        Py_XDECREF(x);
#endif
#ifdef SIGINT
	x = PyInt_FromLong(SIGINT);
	PyDict_SetItemString(d, "SIGINT", x);
        Py_XDECREF(x);
#endif
#ifdef SIGQUIT
	x = PyInt_FromLong(SIGQUIT);
	PyDict_SetItemString(d, "SIGQUIT", x);
        Py_XDECREF(x);
#endif
#ifdef SIGILL
	x = PyInt_FromLong(SIGILL);
	PyDict_SetItemString(d, "SIGILL", x);
        Py_XDECREF(x);
#endif
#ifdef SIGTRAP
	x = PyInt_FromLong(SIGTRAP);
	PyDict_SetItemString(d, "SIGTRAP", x);
        Py_XDECREF(x);
#endif
#ifdef SIGIOT
	x = PyInt_FromLong(SIGIOT);
	PyDict_SetItemString(d, "SIGIOT", x);
        Py_XDECREF(x);
#endif
#ifdef SIGABRT
	x = PyInt_FromLong(SIGABRT);
	PyDict_SetItemString(d, "SIGABRT", x);
        Py_XDECREF(x);
#endif
#ifdef SIGEMT
	x = PyInt_FromLong(SIGEMT);
	PyDict_SetItemString(d, "SIGEMT", x);
        Py_XDECREF(x);
#endif
#ifdef SIGFPE
	x = PyInt_FromLong(SIGFPE);
	PyDict_SetItemString(d, "SIGFPE", x);
        Py_XDECREF(x);
#endif
#ifdef SIGKILL
	x = PyInt_FromLong(SIGKILL);
	PyDict_SetItemString(d, "SIGKILL", x);
        Py_XDECREF(x);
#endif
#ifdef SIGBUS
	x = PyInt_FromLong(SIGBUS);
	PyDict_SetItemString(d, "SIGBUS", x);
        Py_XDECREF(x);
#endif
#ifdef SIGSEGV
	x = PyInt_FromLong(SIGSEGV);
	PyDict_SetItemString(d, "SIGSEGV", x);
        Py_XDECREF(x);
#endif
#ifdef SIGSYS
	x = PyInt_FromLong(SIGSYS);
	PyDict_SetItemString(d, "SIGSYS", x);
        Py_XDECREF(x);
#endif
#ifdef SIGPIPE
	x = PyInt_FromLong(SIGPIPE);
	PyDict_SetItemString(d, "SIGPIPE", x);
        Py_XDECREF(x);
#endif
#ifdef SIGALRM
	x = PyInt_FromLong(SIGALRM);
	PyDict_SetItemString(d, "SIGALRM", x);
        Py_XDECREF(x);
#endif
#ifdef SIGTERM
	x = PyInt_FromLong(SIGTERM);
	PyDict_SetItemString(d, "SIGTERM", x);
        Py_XDECREF(x);
#endif
#ifdef SIGUSR1
	x = PyInt_FromLong(SIGUSR1);
	PyDict_SetItemString(d, "SIGUSR1", x);
        Py_XDECREF(x);
#endif
#ifdef SIGUSR2
	x = PyInt_FromLong(SIGUSR2);
	PyDict_SetItemString(d, "SIGUSR2", x);
        Py_XDECREF(x);
#endif
#ifdef SIGCLD
	x = PyInt_FromLong(SIGCLD);
	PyDict_SetItemString(d, "SIGCLD", x);
        Py_XDECREF(x);
#endif
#ifdef SIGCHLD
	x = PyInt_FromLong(SIGCHLD);
	PyDict_SetItemString(d, "SIGCHLD", x);
        Py_XDECREF(x);
#endif
#ifdef SIGPWR
	x = PyInt_FromLong(SIGPWR);
	PyDict_SetItemString(d, "SIGPWR", x);
        Py_XDECREF(x);
#endif
#ifdef SIGIO
	x = PyInt_FromLong(SIGIO);
	PyDict_SetItemString(d, "SIGIO", x);
        Py_XDECREF(x);
#endif
#ifdef SIGURG
	x = PyInt_FromLong(SIGURG);
	PyDict_SetItemString(d, "SIGURG", x);
        Py_XDECREF(x);
#endif
#ifdef SIGWINCH
	x = PyInt_FromLong(SIGWINCH);
	PyDict_SetItemString(d, "SIGWINCH", x);
        Py_XDECREF(x);
#endif
#ifdef SIGPOLL
	x = PyInt_FromLong(SIGPOLL);
	PyDict_SetItemString(d, "SIGPOLL", x);
        Py_XDECREF(x);
#endif
#ifdef SIGSTOP
	x = PyInt_FromLong(SIGSTOP);
	PyDict_SetItemString(d, "SIGSTOP", x);
        Py_XDECREF(x);
#endif
#ifdef SIGTSTP
	x = PyInt_FromLong(SIGTSTP);
	PyDict_SetItemString(d, "SIGTSTP", x);
        Py_XDECREF(x);
#endif
#ifdef SIGCONT
	x = PyInt_FromLong(SIGCONT);
	PyDict_SetItemString(d, "SIGCONT", x);
        Py_XDECREF(x);
#endif
#ifdef SIGTTIN
	x = PyInt_FromLong(SIGTTIN);
	PyDict_SetItemString(d, "SIGTTIN", x);
        Py_XDECREF(x);
#endif
#ifdef SIGTTOU
	x = PyInt_FromLong(SIGTTOU);
	PyDict_SetItemString(d, "SIGTTOU", x);
        Py_XDECREF(x);
#endif
#ifdef SIGVTALRM
	x = PyInt_FromLong(SIGVTALRM);
	PyDict_SetItemString(d, "SIGVTALRM", x);
        Py_XDECREF(x);
#endif
#ifdef SIGPROF
	x = PyInt_FromLong(SIGPROF);
	PyDict_SetItemString(d, "SIGPROF", x);
        Py_XDECREF(x);
#endif
#ifdef SIGXCPU
	x = PyInt_FromLong(SIGXCPU);
	PyDict_SetItemString(d, "SIGXCPU", x);
        Py_XDECREF(x);
#endif
#ifdef SIGXFSZ
	x = PyInt_FromLong(SIGXFSZ);
	PyDict_SetItemString(d, "SIGXFSZ", x);
        Py_XDECREF(x);
#endif
        if (!PyErr_Occurred())
                return;

	/* Check for errors */
  finally:
        return;
}

static void
finisignal(void)
{
	int i;
	PyObject *func;

	signal(SIGINT, old_siginthandler);
	old_siginthandler = SIG_DFL;

	for (i = 1; i < NSIG; i++) {
		func = Handlers[i].func;
		Handlers[i].tripped = 0;
		Handlers[i].func = NULL;
		if (i != SIGINT && func != NULL && func != Py_None &&
		    func != DefaultHandler && func != IgnoreHandler)
			signal(i, SIG_DFL);
		Py_XDECREF(func);
	}

	Py_XDECREF(IntHandler);
	IntHandler = NULL;
	Py_XDECREF(DefaultHandler);
	DefaultHandler = NULL;
	Py_XDECREF(IgnoreHandler);
	IgnoreHandler = NULL;
}



/* Declared in pyerrors.h */
int
PyErr_CheckSignals(void)
{
	int i;
	PyObject *f;

	if (!is_tripped)
		return 0;
#ifdef WITH_THREAD
	if (PyThread_get_thread_ident() != main_thread)
		return 0;
#endif
	if (!(f = PyEval_GetFrame()))
		f = Py_None;
	
	for (i = 1; i < NSIG; i++) {
		if (Handlers[i].tripped) {
			PyObject *result = NULL;
			PyObject *arglist = Py_BuildValue("(iO)", i, f);
			Handlers[i].tripped = 0;

			if (arglist) {
				result = PyEval_CallObject(Handlers[i].func,
							   arglist);
				Py_DECREF(arglist);
			}
			if (!result)
				return -1;

			Py_DECREF(result);
		}
	}
	is_tripped = 0;
	return 0;
}


/* Replacements for intrcheck.c functionality
 * Declared in pyerrors.h
 */
void
PyErr_SetInterrupt(void)
{
	is_tripped++;
	Handlers[SIGINT].tripped = 1;
	Py_AddPendingCall((int (*)(ANY *))PyErr_CheckSignals, NULL);
}

void
PyOS_InitInterrupts(void)
{
	initsignal();
	_PyImport_FixupExtension("signal", "signal");
}

void
PyOS_FiniInterrupts(void)
{
	finisignal();
}

int
PyOS_InterruptOccurred(void)
{
	if (Handlers[SIGINT].tripped) {
#ifdef WITH_THREAD
		if (PyThread_get_thread_ident() != main_thread)
			return 0;
#endif
		Handlers[SIGINT].tripped = 0;
		return 1;
	}
	return 0;
}

void
PyOS_AfterFork(void)
{
#ifdef WITH_THREAD
	main_thread = PyThread_get_thread_ident();
	main_pid = getpid();
#endif
}
