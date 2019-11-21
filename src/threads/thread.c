#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/switch.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif
//Added imports for MLFQS
#include <fixed-point.h>
#include "devices/timer.h"
//Added code ended

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running i.e Ready Queue*/
static struct list ready_list;

/* List of all processes.  Processes are added to this list
   when they are first scheduled and removed when they exit. */
static struct list all_list;

/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

/* Stack frame for kernel_thread(). */
struct kernel_thread_frame 
  {
    void *eip;                  /* Return address. */
    thread_func *function;      /* Function to call. */
    void *aux;                  /* Auxiliary data for function. */
  };

/* Statistics. */
static long long idle_ticks;    /* # of timer ticks spent idle. */
static long long kernel_ticks;  /* # of timer ticks in kernel threads. */
static long long user_ticks;    /* # of timer ticks in user programs. */
static int load_avg = 0;

/* Scheduling. */
#define TIME_SLICE 4            /* # of timer ticks to give each thread. */
static unsigned thread_ticks;   /* # of timer ticks since last yield. */

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;

//Added Variables start
static struct list sleep_list_ordered;
static struct semaphore sleep_list_semaphore;
//End

static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static struct thread *running_thread (void);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static bool is_thread (struct thread *) UNUSED;
static void *alloc_frame (struct thread *, size_t size);
static void schedule (void);
void thread_schedule_tail (struct thread *prev);
static tid_t allocate_tid (void);

/* Initializes the threading system by transforming the code
   that's currently running into a thread. This can't work in
   general and it is possible in this case only because loader.S
   was careful to put the bottom of the stack at a page boundary.

   Also initializes the run queue and the tid lock. Run queue is referred to as the ready list.

   After calling this function, be sure to initialize the page
   allocator before trying to create any threads with
   thread_create().

   It is not safe to call thread_current() until this function
   finishes. */
void
thread_init (void) 
{
  ASSERT (intr_get_level () == INTR_OFF);

  lock_init (&tid_lock);
  list_init (&ready_list);
  list_init (&all_list);

  //Added Code starts
  list_init(&sleep_list_ordered);
  sema_init(&sleep_list_semaphore, 1);
  //Added Code ends

  /* Set up a thread structure for the running thread. */
  initial_thread = running_thread (); //transforms running code to a thread struct *
  init_thread (initial_thread, "main", PRI_DEFAULT);
  initial_thread->status = THREAD_RUNNING;
  initial_thread->tid = allocate_tid ();
}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
void
thread_start (void) 
{
  /* Create the idle thread. */
  struct semaphore idle_started;
  sema_init (&idle_started, 0);
  thread_create ("idle", PRI_MIN, idle, &idle_started);

  /* Start preemptive thread scheduling. */
  intr_enable (); //Enables Interrupts

  /* Wait for the idle thread to initialize idle_thread. */
  sema_down (&idle_started);
}

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
void
thread_tick (void) 
{
  struct thread *t = thread_current ();

  /* Update statistics. */
  if (t == idle_thread) //Idle Thread
    idle_ticks++;
#ifdef USERPROG //User Thread
  else if (t->pagedir != NULL)
    user_ticks++;
#endif
  else //Kernel Thread
    kernel_ticks++;

  //Increment Ticks of the thread
  thread_ticks++;

  //Added code for MLFQS
  /*
    - Recent cpu is incremented for only the running thread
    - Recent cpu is calculated for all threads every second (timer_ticks () % TIMER_FREQ == 0)
    - Load avg is calculated every second (timer_ticks () % TIMER_FREQ == 0)
    - Thread priority is calculated for all threads every fourth  tick
  */
  if(thread_mlfqs)
  {
    if(timer_ticks() % TIMER_FREQ == 0)
    {
      thread_calculate_load_avg();
      thread_foreach(thread_calculate_recent_cpu, NULL);
    }
    if(timer_ticks() % 4 == 0)
    {
      thread_foreach(thread_set_mlfqs_priority, NULL);
      //This serial priority updates will leave the ready_list in an unsorted manner.
      thread_sort_ready_list();
    }
    if(t->status == THREAD_RUNNING)
      thread_increment_recent_cpu(thread_current());
  }

  /* Enforce preemption. */
  if (thread_ticks >= TIME_SLICE)
    intr_yield_on_return ();
}

/* Prints thread statistics. */
void
thread_print_stats (void) 
{
  printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
          idle_ticks, kernel_ticks, user_ticks);
}

/* Creates a new kernel thread named NAME with the given initial
   PRIORITY, which executes FUNCTION passing AUX as the argument,
   and adds it to the ready queue.  Returns the thread identifier
   for the new thread, or TID_ERROR if creation fails.

   If thread_start() has been called, then the new thread may be
   scheduled before thread_create() returns.  It could even exit
   before thread_create() returns.  Contrariwise, the original
   thread may run for any amount of time before the new thread is
   scheduled.  Use a semaphore or some other form of
   synchronization if you need to ensure ordering.

   The code provided sets the new thread's `priority' member to
   PRIORITY, but no actual priority scheduling is implemented.
   Priority scheduling is the goal of Problem 1-3. */
tid_t
thread_create (const char *name, int priority,
               thread_func *function, void *aux) 
{
  struct thread *t;
  struct kernel_thread_frame *kf;
  struct switch_entry_frame *ef;
  struct switch_threads_frame *sf;
  tid_t tid;

  ASSERT (function != NULL);

  /* Allocate thread. */
  t = palloc_get_page (PAL_ZERO);
  if (t == NULL)
    return TID_ERROR;

  /* Initialize thread. */
  init_thread (t, name, priority);
  tid = t->tid = allocate_tid ();

  /* Stack frame for kernel_thread(). */
  kf = alloc_frame (t, sizeof *kf);
  kf->eip = NULL;
  kf->function = function;
  kf->aux = aux;

  /* Stack frame for switch_entry(). */
  ef = alloc_frame (t, sizeof *ef);
  ef->eip = (void (*) (void)) kernel_thread;

  /* Stack frame for switch_threads(). */
  sf = alloc_frame (t, sizeof *sf);
  sf->eip = switch_entry;
  sf->ebp = 0;

  /* Add to run queue. */
  thread_unblock (t);

  //Added Code starts
  if(thread_mlfqs)
  {
    struct thread *t = thread_current();
    t->nice = 0; // Initialise threads nice
    t->recent_cpu = 0; // Initialise thread's recent_cpu
    thread_set_mlfqs_priority(t, NULL);
  }

  //New thread's priority is greater then calling threads priority
  if (t->priority > thread_get_priority())
    thread_yield();
  //Added Code ends   

  return tid;
}

/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */
void
thread_block (void) 
{
  ASSERT (!intr_context ());
  ASSERT (intr_get_level () == INTR_OFF);

  thread_current ()->status = THREAD_BLOCKED;
  schedule ();
}

/* Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)

   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data. */
void
thread_unblock (struct thread *t) 
{
  enum intr_level old_level;

  ASSERT (is_thread (t));

  old_level = intr_disable ();
  ASSERT (t->status == THREAD_BLOCKED);
  
  /* Actual Code below
  list_push_back (&ready_list, &t->elem); */
  //Added Code begins
  list_insert_ordered(&ready_list, &t->elem, high_priority_condition, NULL);
  /* thread t inserted in ready queue in its proper position as per its priority */
  //Added Code Ends

  t->status = THREAD_READY;

  //Added Code Begins
  if (t->priority > thread_get_priority() && thread_tid() != 2) //if the running thread is not idle thread and has a lesser priority than awaken thread
    thread_yield();
  //Added Code Ends

  intr_set_level (old_level);
}

/* Returns the name of the running thread. */
const char *
thread_name (void) 
{
  return thread_current ()->name;
}

/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
struct thread *
thread_current (void) 
{
  struct thread *t = running_thread ();
  
  /* Make sure T is really a thread.
     If either of these assertions fire, then your thread may
     have overflowed its stack.  Each thread has less than 4 kB
     of stack, so a few big automatic arrays or moderate
     recursion can cause stack overflow. */
  ASSERT (is_thread (t));
  ASSERT (t->status == THREAD_RUNNING);

  return t;
}

/* Returns the running thread's tid. */
tid_t
thread_tid (void) 
{
  return thread_current ()->tid;
}

/* Deschedules the current thread and destroys it.  Never
   returns to the caller. */
void
thread_exit (void) 
{
  ASSERT (!intr_context ());

#ifdef USERPROG
  process_exit ();
#endif

  /* Remove thread from all threads list, set our status to dying,
     and schedule another process.  That process will destroy us
     when it calls thread_schedule_tail(). */
  intr_disable ();
  list_remove (&thread_current()->allelem);
  thread_current ()->status = THREAD_DYING;
  schedule ();
  NOT_REACHED (); //This line of code will never be reached.
}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
void
thread_yield (void) 
{
  struct thread *cur = thread_current ();
  enum intr_level old_level;
  
  ASSERT (!intr_context ());

  old_level = intr_disable ();
  if (cur != idle_thread) //Added Code Starts
    list_insert_ordered(&ready_list, &cur->elem, high_priority_condition, NULL);
    //Added Code Ends
    /* Actual Code below
    list_push_back (&ready_list, &cur->elem); */
  cur->status = THREAD_READY;
  schedule ();
  intr_set_level (old_level);
}

/* Invoke function 'func' on all threads, passing along 'aux'.
   This function must be called with interrupts off. */
void
thread_foreach (thread_action_func *func, void *aux)
{
  struct list_elem *e;

  ASSERT (intr_get_level () == INTR_OFF);

  for (e = list_begin (&all_list); e != list_end (&all_list);
       e = list_next (e))
    {
      struct thread *t = list_entry (e, struct thread, allelem);
      func (t, aux);
    }
}

/* Sets the current thread's priority to NEW_PRIORITY. */
void
thread_set_priority (int new_priority) 
{
  /* Actual Code 
  thread_current ()->priority = new_priority; */

  //Added Code starts
  // Ignore if MLFQS
  if (thread_mlfqs)
    return;
  struct thread *t = thread_current();
  t->initial_priority = new_priority;
  if (list_empty(&t->locks_held)) t->priority = new_priority;
  //t->priority = new_priority;
  thread_update_priority_and_yeild(t);
  //Added code ends
}

/* Returns the current thread's priority. */
int
thread_get_priority (void) 
{
  return thread_current ()->priority;
}

/* Sets the current thread's nice value to NICE. */
void
thread_set_nice (int nice UNUSED) 
{
  if (nice < -20 || nice > 20)
    return;
  struct thread* t = thread_current();
  t-> nice = nice;
  //Calculate priority
  thread_set_mlfqs_priority(t, NULL);
  thread_update_priority_and_yeild(t);
}

/* Returns the current thread's nice value. */
int
thread_get_nice (void)
{
  return thread_current()->nice;
}

/* Returns 100 times the system load average. */
int
thread_get_load_avg (void) 
{
    return CONVERT_FP_INT(load_avg * 100);
}

/* Returns 100 times the current thread's recent_cpu value. */
int
thread_get_recent_cpu (void) 
{
  return CONVERT_FP_INT(thread_current()->recent_cpu * 100);
}

/* Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. */
static void
idle (void *idle_started_ UNUSED) 
{
  struct semaphore *idle_started = idle_started_; //Cast genertic pointer to struct semaphore * type.
  idle_thread = thread_current (); // idle_thread initialized at this point
  sema_up (idle_started);

  for (;;) 
    {
      /* Let someone else run. */
      intr_disable ();
      thread_block ();

      /* Re-enable interrupts and wait for the next one.

         The `sti' instruction disables interrupts until the
         completion of the next instruction, so these two
         instructions are executed atomically.  This atomicity is
         important; otherwise, an interrupt could be handled
         between re-enabling interrupts and waiting for the next
         one to occur, wasting as much as one clock tick worth of
         time.

         See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
         7.11.1 "HLT Instruction". */
      asm volatile ("sti; hlt" : : : "memory");
    }
}

/* Function used as the basis for a kernel thread. */
static void
kernel_thread (thread_func *function, void *aux) 
{
  ASSERT (function != NULL);

  intr_enable ();       /* The scheduler runs with interrupts off. */
  function (aux);       /* Execute the thread function. */
  thread_exit ();       /* If function() returns, kill the thread. */
}

/* Returns the running thread. */
struct thread *
running_thread (void) 
{
  uint32_t *esp;

  /* Copy the CPU's stack pointer into `esp', and then round that
     down to the start of a page.  Because `struct thread' is
     always at the beginning of a page and the stack pointer is
     somewhere in the middle, this locates the curent thread. */
  asm ("mov %%esp, %0" : "=g" (esp));
  return pg_round_down (esp);
}

/* Returns true if T appears to point to a valid thread. */
static bool
is_thread (struct thread *t)
{
  return t != NULL && t->magic == THREAD_MAGIC;
}

/* Does basic initialization of T as a blocked thread named
   NAME. */
static void
init_thread (struct thread *t, const char *name, int priority)
{
  enum intr_level old_level;

  ASSERT (t != NULL);
  ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
  ASSERT (name != NULL);

  memset (t, 0, sizeof *t);
  t->status = THREAD_BLOCKED;
  strlcpy (t->name, name, sizeof t->name);
  t->stack = (uint8_t *) t + PGSIZE;
  if (!thread_mlfqs)
    t->priority = priority;
  t->magic = THREAD_MAGIC;
  
  //Added Code Starts
  t->wakeup_ticks = 0;
  t->initial_priority = priority;
  list_init(&t->locks_held);
  t->lock_waiting_for = NULL;

  if (thread_mlfqs)
  {
    if (t == initial_thread)
    {
      t->nice = 0;
      t->recent_cpu = 0;
    }

    else
    {
      t->nice = thread_get_nice();
      t->recent_cpu = thread_get_recent_cpu();
    }
  }

  #ifdef USERPROG
  t->parent = running_thread();
  sema_init(&t->parent_sema, 0);
  t->exec_called = false;
  t->exec_success = false;
  list_init(&t->child_list);
  list_init(&t->desc_map_list);
  t->exe = NULL;
  #endif
  //Added Code Ends

  old_level = intr_disable ();
  list_push_back (&all_list, &t->allelem);
  intr_set_level (old_level);
}

/* Allocates a SIZE-byte frame at the top of thread T's stack and
   returns a pointer to the frame's base. */
static void *
alloc_frame (struct thread *t, size_t size) 
{
  /* Stack data is always allocated in word-size units. */
  ASSERT (is_thread (t));
  ASSERT (size % sizeof (uint32_t) == 0);

  t->stack -= size;
  return t->stack;
}

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
static struct thread *
next_thread_to_run (void) 
{
  if (list_empty (&ready_list))
    return idle_thread;
  else
    return list_entry (list_pop_front (&ready_list), struct thread, elem);
}

/* Completes a thread switch by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.  This function is normally invoked by
   thread_schedule() as its final action before returning, but
   the first time a thread is scheduled it is called by
   switch_entry() (see switch.S).

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function.

   After this function and its caller returns, the thread switch
   is complete. */
void
thread_schedule_tail (struct thread *prev)
{
  struct thread *cur = running_thread ();
  
  ASSERT (intr_get_level () == INTR_OFF);

  /* Mark us as running. */
  cur->status = THREAD_RUNNING;

  /* Start new time slice. */
  thread_ticks = 0;

#ifdef USERPROG
  /* Activate the new address space. */
  process_activate ();
#endif

  /* If the thread we switched from is dying, destroy its struct
     thread.  This must happen late so that thread_exit() doesn't
     pull out the rug under itself.  (We don't free
     initial_thread because its memory was not obtained via
     palloc().) */
  if (prev != NULL && prev->status == THREAD_DYING && prev != initial_thread) 
    {
      ASSERT (prev != cur);
      palloc_free_page (prev);
    }
}

/* Schedules a new process.  At entry, interrupts must be off and
   the running process's state must have been changed from
   running to some other state.  This function finds another
   thread to run and switches to it.

   It's not safe to call printf() until thread_schedule_tail()
   has completed. */
static void
schedule (void) 
{
  struct thread *cur = running_thread (); //State of current thread has changed from running to either blocked, dying or ready.
  struct thread *next = next_thread_to_run ();
  struct thread *prev = NULL;

  ASSERT (intr_get_level () == INTR_OFF);
  ASSERT (cur->status != THREAD_RUNNING);
  ASSERT (is_thread (next));

  if (cur != next)
    prev = switch_threads (cur, next);
  thread_schedule_tail (prev); //The new thread destroys the previous(caller) thread when the caller is in the dying state. 
}

/* Returns a tid to use for a new thread. Generates incremental TID's acrosss multiple calls. */
static tid_t
allocate_tid (void) 
{
  static tid_t next_tid = 1; //This is important because its is initialized only once.
  tid_t tid;

  lock_acquire (&tid_lock);
  tid = next_tid++; //This line of code behaves as a Critical Section across multiple threads.
  lock_release (&tid_lock);

  return tid;
}

/* Offset of `stack' member within `struct thread'.
   Used by switch.S, which can't figure it out on its own. */
uint32_t thread_stack_ofs = offsetof (struct thread, stack);

/* Returns True if first param's wakeup_tick is lesser than second param's priority, otherwise False */
bool
less_wakeup_time(const struct list_elem *first, const struct list_elem *second, void *aux UNUSED)
{
  struct thread *fir = list_entry(first, struct thread, elem);
  struct thread *sec = list_entry(second, struct thread, elem);
  return ((int64_t) fir->wakeup_ticks < (int64_t) sec->wakeup_ticks);
}

/* Returns True if first param's priority is greater than second param's priority, otherwise False */
bool
high_priority_condition(const struct list_elem *first, const struct list_elem *second, void *aux UNUSED)
{
  struct thread *fir = list_entry(first, struct thread, elem);
  struct thread *sec = list_entry(second, struct thread, elem);
  return (fir->priority > sec->priority);
}

/* Removes a sleeping thread from Waiting List and inserts it into Ready List */
void
thread_unblock_without_yield (struct thread *t)
{
  enum intr_level old_level;
  ASSERT (is_thread (t));
  old_level = intr_disable ();
  ASSERT (t->status == THREAD_BLOCKED);
  list_insert_ordered(&ready_list, &t->elem, high_priority_condition, NULL);
  t->status = THREAD_READY;
  intr_set_level (old_level);
}

void
thread_sleep (int64_t ticks)
{
  enum intr_level old_level;
  struct thread *t = thread_current();
  t->wakeup_ticks = ticks;
  //Synchronize for sema_down
  sema_down(&sleep_list_semaphore);
  list_insert_ordered(&sleep_list_ordered, &t->elem, less_wakeup_time, NULL);
  sema_up(&sleep_list_semaphore);
  
  //Disabling interrupts before blocking current thread 
  old_level = intr_disable();
  thread_block(); //puts the thread to sleep.
  intr_set_level(old_level); //Enabling interrups back to old level.
}

void
thread_wake_up (int64_t wakeup_at_tick) //This is called by the interrupt handler.
{
  struct thread *t;
  struct list_elem *wake_this_up;
  
  //No Synchonization needed as this function is called by interrupt handler at every tick
  while(!list_empty(&sleep_list_ordered))
  {
    wake_this_up = list_begin(&sleep_list_ordered);
    t = list_entry(wake_this_up, struct thread, elem);
    //The wakeup time of the thread is greater than the current timestamp. Break this loop as list is sorted.
    if (t->wakeup_ticks > wakeup_at_tick)
      break;
    list_pop_front(&sleep_list_ordered);
    thread_unblock_without_yield(t);
  }
}

/* Updates the thread's location in the ready queue
  by deleting the thread from it's original position in queue
  and inserting it into it's new position using `list_insert_ordered` function.
  If the thread is RUNNING, check with top of ready_list, which is thread
  with max priority, and if less, yield/preempt this running thread */
void
thread_update_priority_and_yeild (struct thread *t)
{
  // If thread's in ready state, it's assumed ready_list has at least one thread
  if (t->status == THREAD_READY)
  {
    list_remove(&t->elem);
    list_insert_ordered(&ready_list, &t->elem, high_priority_condition, NULL);
  }
  else if (!list_empty(&ready_list) && t->status == THREAD_RUNNING)
  {
    if (t->priority < list_entry(list_begin(&ready_list),
                                struct thread, elem) ->priority)
      thread_yield();
  }
}

/* Calculates MLFQS priority using formula
  priority = PRI_MAX - (recent_cpu / 4) - (nice * 2)
  Adjusts the value to lie in between PRI_MAX and PRI_MIN */
void
thread_set_mlfqs_priority (struct thread* t, void *aux UNUSED)
{
  int priority;
  priority = SUB_FP_FP(CONVERT_INT_FP(PRI_MAX), DIV_FP_INT(t->recent_cpu, 4));
  priority =  SUB_FP_INT(priority, t->nice*2);
  priority = CONVERT_FP_INT_FLOOR(priority);
  if(priority > PRI_MAX)
    priority = PRI_MAX;
  if(priority < PRI_MIN)
    priority = PRI_MIN;
  t->priority = priority;
}

/* Sorts the ready List according to priority,
  only if ready list has some threads. */
void
thread_sort_ready_list (void){
  if(!list_empty(&ready_list))
    list_sort(&ready_list, high_priority_condition, NULL);
}

/* Calculates recent cpu using the equation of FP arithmetic
  Recommended to calc the coefficient of recent_cpu first, to avoid overflow,
  hen multiply load_avg with recent_cpu.
  recent_cpu = (2*load_avg)/(2*load_avg+1)*recent_cpu+nice */
void
thread_calculate_recent_cpu (struct thread *t, void *aux UNUSED)
{
  int coeff;
  coeff = DIV_FP_FP(MULT_FP_INT(load_avg, 2),
            ADD_FP_INT(MULT_FP_INT(load_avg, 2), 1));
  t->recent_cpu = ADD_FP_INT(MULT_FP_FP(coeff, t->recent_cpu), t->nice);
}

/* Increments the value of recent_cpu using FP arithmetic*/
void
thread_increment_recent_cpu (struct thread *t)
{
  t->recent_cpu = ADD_FP_INT(t->recent_cpu, 1);
}

/* Calculates system's load avg using the equation involving FP arithmetic
  load_avg=(59/60)*load_avg+(1/60)*ready_threads
  ready_thread is the count of threads which are running/ready/blocked
*/
void
thread_calculate_load_avg (void)
{
  int ready_threads = list_size(&ready_list);
  // If thread is running, ready to run, blocked
  if(thread_current() != idle_thread)
    ready_threads++;
  int avg1, avg2;
  avg1 = MULT_FP_FP(DIV_INT_INT(59, 60), load_avg);
  avg2 = DIV_INT_INT(ready_threads, 60);
  load_avg = ADD_FP_FP(avg1, avg2);
}

struct thread *get_thread_with_tid(tid_t tid)
{
  if (tid == -1 || list_begin(&all_list) == list_end(&all_list))
    return NULL;
  struct list_elem *iter;
  struct thread *t;
  for (iter = list_begin(&all_list); iter != list_end(&all_list); iter = list_next(iter))
    if (tid == list_entry(iter, struct thread, elem)->tid)
      return t;
  return NULL;
}
//Added Functions End.