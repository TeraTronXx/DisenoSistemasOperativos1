#include <stdio.h>
#include <sys/time.h>
#include <signal.h>
#include <stdlib.h>
#include <ucontext.h>
#include <unistd.h>
#include <interrupt.h>

static sigset_t maskval_interrupt,oldmask_interrupt;

void reset_timer(long usec) {
  struct itimerval quantum;

  /* Intialize an interval corresponding to round-robin quantum*/
  quantum.it_interval.tv_sec = usec / 1000000;
  quantum.it_interval.tv_usec = usec % 1000000;
  /* Initialize the remaining time from current quantum */
  quantum.it_value.tv_sec = usec / 1000000;
  quantum.it_value.tv_usec = usec % 1000000;
  /* Activate the virtual timer to generate a SIGVTALRM signal when the quantum is over */
  if(setitimer(ITIMER_VIRTUAL, &quantum, (struct itimerval *)0) == -1){
    perror("setitimer");
    exit(3);
  }
}

void enable_interrupt(){
  sigprocmask(SIG_SETMASK, &oldmask_interrupt, NULL);
}

void disable_interrupt(){
  sigaddset(&maskval_interrupt, SIGVTALRM);
  sigprocmask(SIG_BLOCK, &maskval_interrupt, &oldmask_interrupt);
}



void my_handler ()
{
   reset_timer(TICK_TIME) ;
   timer_interrupt() ;
}


void init_interrupt()
{
  void timer_interrupt(int sig);
  struct sigaction sigdat;
  /* Initializes the signal mask to empty */
  sigemptyset(&maskval_interrupt); 
  /* Prepare a virtual time alarm */
  sigdat.sa_handler = my_handler;
  sigemptyset(&sigdat.sa_mask);
  sigdat.sa_flags = SA_RESTART;
  if(sigaction(SIGVTALRM, &sigdat, (struct sigaction *)0) == -1){
    perror("signal set error");
    exit(2);
  }
   reset_timer(TICK_TIME) ;
}


