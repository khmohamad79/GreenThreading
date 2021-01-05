#include <stdlib.h>
#include <unistd.h>
#include <ucontext.h>
#include <signal.h>
#include <assert.h>
#include <string.h>
#include <time.h>

#include "green.h"
#include "queue.h"


#define FALSE 0
#define TRUE 1

#define STACK_SIZE 4*1024

static ucontext_t main_cntx = {0};

static green_t main_green = {&main_cntx, NULL, NULL, NULL, NULL, NULL, FALSE};

static green_t * running = &main_green;


queue_t * ready_queue;

// timer handler
void timer_handler(union sigval v)
{
	green_yield();
}

// this function is called once program loads into memory regurdless of what function is called!
static void init( ) __attribute__((constructor));
void init(){
	getcontext(&main_cntx);
	ready_queue = createQueue();
	
	// init timer
	timer_t timerid;
    struct sigevent evp; 
    memset(&evp, 0, sizeof(struct sigevent));       // Clear initialization 
 
    evp.sigev_value.sival_int = 111;                // It also identifies the timer, the callback function can be obtained 
    evp.sigev_notify = SIGEV_THREAD;                // The way of thread notification, send a new thread 
    evp.sigev_notify_function = timer_handler;      // Thread function address 
 
    if (timer_create(CLOCK_REALTIME, &evp, &timerid) == -1) 
    {   
        // perror("fail to timer_create"); 
        exit(-1); 
    }   
 
    /* The first interval it.it_value is so long, every time it will be it.it_interval so long, that is, when it.it_value becomes 0, it will load the value of it.it_interval */
    struct itimerspec it; 
    it.it_interval.tv_sec = 0;  // The callback function is executed once every 1s
    it.it_interval.tv_nsec = 10000; 
    it.it_value.tv_sec = 0;     // Call back the callback function after 3 seconds
    it.it_value.tv_nsec = 10000; 

	if (timer_settime(timerid, 0, &it, NULL) == -1) 
    {   
        // perror("fail to timer_settime"); 
        exit(-1); 
    }
}
// this functions is mapped to every Context
void green_thread() {
	green_t * this = running;

	// call target function and save its result
	this->retval = this->fun(this->arg);

	// place waiting (joining) thread in ready queue
	if(this->join != NULL)
		enQueue(ready_queue, this->join);

	// save result of execution and zombie status
	this->zombie = TRUE;

	// find the next thread to run and write its address to next variable
	running = deQueue(ready_queue);

	this->next = running;

	setcontext(running->context);
}

// will create a new green thread
int green_create(green_t *new ,void *(*fun)(void *),void *arg) {
	ucontext_t * cntx = (ucontext_t *) malloc(sizeof(ucontext_t));
	
	// intialize cntx
	getcontext(cntx);

	// allocate stack on heap
	void * stack = malloc(STACK_SIZE);
	
	// assign allocated stack to cntx
	cntx->uc_stack.ss_sp = stack + STACK_SIZE;
	cntx->uc_stack.ss_size = STACK_SIZE;
	
	// assign green_thread function to cntx
	makecontext(cntx, (void(*) (void)) green_thread, 1, NULL);

	new->context = cntx ;
	new->fun = fun ;
	new->arg = arg ;
	new->next = NULL;
	new->join = NULL;
	new->retval = NULL;
	new->zombie = FALSE;
	
	// add this new thread to end of linked list
	// add new thread to the ready queue
	enQueue(ready_queue, new);

	return 0;
}

// will give other green thread opportunity of having CPU!
int green_yield(){
	green_t * susp = running;
	
	// add susp to ready queue
	enQueue(ready_queue, susp);
	
	// select the next thread for execution
	running = deQueue(ready_queue);
	
	// save current state into susp->context and switch to next->context
	swapcontext(susp->context, running->context);

	return 0;
}

// waits for specefied thread till it finishes and get result value
int green_join(green_t * thread ,void ** res) {
	green_t * susp = running;
	
	// check if target thread has finished
	if(!thread->zombie)
	{
		// add as joining thread
		thread->join = susp;

		// select the next thread for execution
		running = deQueue(ready_queue);

		// save current state into susp->context and switch to next->context
		swapcontext(susp->context, running->context);
	}

	// collect result
	*res = thread->retval;

	// free context
	// free(thread->context->uc_stack.ss_sp - thread->context->uc_stack.ss_size);
	free(thread->context);

	return 0;
}
