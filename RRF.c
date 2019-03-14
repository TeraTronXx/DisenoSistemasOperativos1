#include <stdio.h>
#include <sys/time.h>
#include <signal.h>
#include <stdlib.h>
#include <ucontext.h>
#include <unistd.h>

#include "queue.h"
#include "mythread.h"
#include "interrupt.h"

long hungry = 0L;

TCB* scheduler();
void activator();
void timer_interrupt(int sig);


//creamos una cola que usaremos para Round Robin
struct queue *RRcola;

//ahora tambien necesitamos una cola para los procesos de alta prioridad con planificacion FIFO
struct queue *FIFOcola;

/* Array of state thread control blocks: the process allows a maximum of N threads */
static TCB t_state[N]; 
/* Current running thread */
static TCB* running;
static int current = 0;
/* Variable indicating if the library is initialized (init == 1) or not (init == 0) */
static int init=0;

/* Initialize the thread library */
void init_mythreadlib() {
  int i;  
  t_state[0].state = INIT;
  t_state[0].priority = LOW_PRIORITY;
  t_state[0].ticks = QUANTUM_TICKS;
  if(getcontext(&t_state[0].run_env) == -1){
   // perror("getcontext in my_thread_create");
    exit(5);
  }	
  for(i=1; i<N; i++){
    t_state[i].state = FREE;
  }
  t_state[0].tid = 0;//aqui esta el tid del threaddd
  running = &t_state[0];
  //iniciamos nuestra cola de round robin
  RRcola = queue_new();
  //iniciamos la cola de FIFO para procesos de alta prioridad
  FIFOcola = queue_new();

  init_interrupt();
}


/* Create and intialize a new thread with body fun_addr and one integer argument */ 
int mythread_create (void (*fun_addr)(),int priority)
{
  int i;
  
  if (!init) { init_mythreadlib(); init=1;}
  for (i=0; i<N; i++)
    if (t_state[i].state == FREE) break;
  if (i == N) return(-1);
  if(getcontext(&t_state[i].run_env) == -1){
    //perror("getcontext in my_thread_create");
    exit(-1);
  }

  //tenemos que darle a los ticks del hilo el valor de QUANTUM_TICKS
  t_state[i].ticks = QUANTUM_TICKS;
  t_state[i].state = INIT;
  t_state[i].priority = priority;
  t_state[i].function = fun_addr;
  t_state[i].run_env.uc_stack.ss_sp = (void *)(malloc(STACKSIZE));
  if(t_state[i].run_env.uc_stack.ss_sp == NULL){
    //printf("thread failed to get stack space\n");
    exit(-1);
  }
  t_state[i].tid = i;	//este es el tid del proceso, por alguna razon esta separado
  t_state[i].run_env.uc_stack.ss_size = STACKSIZE;
  t_state[i].run_env.uc_stack.ss_flags = 0;
  makecontext(&t_state[i].run_env, fun_addr, 1);  

  //ahora metemos el nuevo hilo en nuestra cola para round roin
  TCB *newthread = &t_state[i];
 // printf("Creado hilo %i con prioridad %i\n", t_state[i].tid, t_state[i].priority);


  if (t_state[i].priority == 1) //si el proceso es de alta prioridad
  {
    /*si la cola de alta prioridad esta vacia y el proceso que se esta ejecutando es uno
    de baja prioridad, hay que echar al de baja prioridad y ejecutar el nuevo de alta prioridad*/ 
    if(running->priority == 0 && queue_empty(FIFOcola) == 1){
      activator(newthread);
    } else{/*si la cola de alta prioridad FIFO no esta vacia, encolamos el proceso en ella*/
      //deshabilitamos momentaneamente la proteccion de la cola
      disable_interrupt();
      enqueue(FIFOcola, newthread);
      enable_interrupt();
    }
  } else{//si es de baja prioridad, introducimos el nuevo proceso en la cola de Round Robin
      //deshabilitamos momentaneamente la proteccion de la cola
      disable_interrupt();
      enqueue(RRcola, newthread);
      enable_interrupt();
  }


  return i;
} /****** End my_thread_create() ******/


/* Free terminated thread and exits */
void mythread_exit() {
  int tid = mythread_gettid();	

  //printf("Thread %d finished\n ***************\n", tid);	
  t_state[tid].state = FREE;
  free(t_state[tid].run_env.uc_stack.ss_sp); 
  
  TCB* next = scheduler();
  activator(next);
}

/* Sets the priority of the calling thread */
void mythread_setpriority(int priority) {
  int tid = mythread_gettid();	
  t_state[tid].priority = priority;
}

/* Returns the priority of the calling thread */
int mythread_getpriority(int priority) {
  int tid = mythread_gettid();	
  return t_state[tid].priority;
}


/* Get the current thread id.  */
int mythread_gettid(){
  if (!init) { init_mythreadlib(); init=1;}
  return current;
}

/* Timer interrupt  */
void timer_interrupt(int sig)
{
  //en esta funcion, decrementaremos el valor del tick del thread(quantum) 
  //de forma que se reduzca hasta que se complete su rodaja


   /*si el proceso es de alta prioridad, no tenemos la necesidad de contar sus ticks de reloj
   ya que tiene que ejecutarse de principio a fin.*/
  if (running->priority == 1)
  {
    return;
  }
  //en el caso de que el proceso sea de baja prioridad:

  //para ello: disminuimos su tick:

  running -> ticks = (int)running->ticks -1;

  /*en el caso de que se haya acabado su rodaja, pedimos a 
  scheduler que nos proporcione el siguiente hilo y lo damos el valor de running
  con la funcion activator*/
  if (running->ticks <= 0)
  {
    TCB *siguientehilo = scheduler();
    activator(siguientehilo);
  }

} 

TCB* scheduler(){

   /*Comprobamos si la cola de alta prioridad tiene algun proceso, ya que estos tienen prioridad sobre los de baja prioridad*/
  /*deshabilitamos la proteccion de la cola por un momento*/
  if (queue_empty(FIFOcola) == 0)
  {
    disable_interrupt();
    TCB *siguiente = dequeue(FIFOcola);
    enable_interrupt();
    return siguiente;
  }
  //en caso de que este vacia, acudiremos a la cola de baja prioridad ROund Robin.
  
  /*primero comprobaremos que nuestra cola de Round Robin no esté vacía*/
  if(queue_empty(RRcola) == 0){//si no está vacía:

    /*En este punto "inhabilitaremos" la proteccion a nuestra cola para obtener el siguiente proceso que haya en 
    nuestra cola RRcola*/
    disable_interrupt();

    //ahora podemos desencolar un proceso de la cola
    TCB *siguiente = dequeue(RRcola);

    /*Volvemos a habilitar la interrupcion de la cola para volver a protegerla*/

    enable_interrupt();

    //devolvemos el siguiente hilo
    return siguiente;
  }
    //en caso de que la cola esté vacía:
    /**Comprobamos que el proceso actual ha acabado o no*/
    if (running->state == INIT)
    {
      //si el proceso no ha terminado aun, le devolvemos a él mismo
      return running;
    }

    /**llegado a este punto, significa que no hay elementos en la cola y que el proceso actual ha terminado
    En este caso, terminamos la ejecucción.*/
    printf(" *** THREAD %i FINISHED\n FINISH\n", running->tid);

    exit(1);

  
}

/* Activator */
void activator(TCB* next){
  /*Renemos los ticks de nuevo al valor de QUANTUM:TICKS*/
  running->ticks = QUANTUM_TICKS;

  /*si el proceso actual es igual al generado por schedule, no hacemos nada más hasta que acabe otra rodaja*/
  if (running == next)
  {
    return;
  }

  /*en caso de ser otro distinto, primero guardamos el actual en un auxiliar (antiguo) y establecemos 
  como running al nuevo proceso*/

  TCB *antiguo = running;
  running = next;
  /*recogemos el id del nuevo proceso para tenerlo como id del proceso actual*/
  current = running->tid;

  /*Si el proceso antiguo ha terminado, simplemente haremos un setcontext del nuevo. 
  Por otro lado, si el proceso antiguo no ha terminado, haremos un swap context*/

  if (antiguo->state == FREE)
  {
    printf("*** THREAD %i FINISHED --p%i--: SET CONTEXT OF %i --p%i--\n", antiguo->tid, antiguo->priority, running->tid, running->priority);
    setcontext (&(next->run_env));
    //printf("mythread_free: After setcontext, should never get here!!...\n");  
  }
    //y lo introducimos en la cola. para ello igual que antes, tenemos que deshabilitar la proteccion durante un 
  //breve periodo de tiempo.

  disable_interrupt();
  enqueue(RRcola, antiguo);
  enable_interrupt();

  //si el proceso que ha entrado es de alta prioridad, y el otro era de baja prioridad, se imprime este mensaje:

  if (running->priority == 1)
  {
    printf("*** THREAD %i EJECTED : SET CONTEXT OF %i \n",antiguo->tid, running->tid );
  }else{
      /*En el caso de que el proceso antiguo no haya terminado, haremos un swapcontext
      e incluiremos el proceso de nuevo en nuestra cola RRcola*/

     printf(" *** SWAPCONTEXT FROM %i --p%i-- to %i --p%i--\n",antiguo->tid, antiguo->priority, running->tid, running->priority);
  }



  swapcontext(&antiguo->run_env, &running->run_env);



  
}



