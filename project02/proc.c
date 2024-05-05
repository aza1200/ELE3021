#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"
// #include "mlfq.h"
// #include "queue.h"


struct {
  struct spinlock lock;
  struct proc proc[NPROC];
  struct proc* heads[5];
  struct proc* tail[5];
  int numproc[5];
  int is_monopoly;
  // 0,1,2 -> normal큐
  // 3 -> prioirty큐
  // 4 -> mo큐
} ptable;

static struct proc *initproc;
// mlfq my_mlfq;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);
void print_proc_info();
void print_queue_info();
const char* to_string(enum procstate state);
static void wakeup1(void *chan);
int is_interrupt_enabled();

int time_quantum[4] = {2,4,6,8};

// lock 중에서 실행되어야함 
void queue_push(struct proc* p){
  int tmp_level = p->level;
  //cprintf("pid : %d panem : %s state : %s\n", p->pid, p->name, to_string(p->state));
  // 0,1,2, 4(monopoly) 일때 ,FCFS 방식임  
  if(tmp_level != 3){
    if(ptable.numproc[tmp_level] == 0) {
      ptable.heads[tmp_level] = ptable.tail[tmp_level] = p;
    }
    else{
      ptable.tail[tmp_level]->next = p;
      ptable.tail[tmp_level] = p;
    }
    p->next = NULL;
  }
  else if(tmp_level==3){
    if (ptable.numproc[tmp_level] == 0) {
      ptable.heads[tmp_level] = ptable.tail[tmp_level] = p;
      p->next = NULL; //이거 안해서 에러난듯 ㅇㅇ
    }
    else{
      struct proc* curr = ptable.heads[tmp_level];
      struct proc* prev = NULL;

      while (curr != NULL && curr->priority >= p->priority){
        prev = curr;
        curr = curr->next;
      }

      if(prev == NULL){
        // 리스트 맨앞에 삽입
        p->next = ptable.heads[tmp_level];
        ptable.heads[tmp_level] = p;
      }
      else{
        // 중간 또는 끝에 삽입
        // cprintf("여기임 리스트 중간또는 끝에 삽입 %d\n", p->pid);
        p->next = curr;
        prev->next = p;

        // 끝에 삽입할때, 
        if(curr == NULL){
          ptable.tail[tmp_level] = p;
        }
      }

    }
  }
  ptable.numproc[tmp_level]++;
}

// table lock 걸고 무조건 해야함!
// 길이가 1 이상일떄에만 해당 함수 실행하는거임 ㅇㅇ 
struct proc* queue_pop(int level){
  struct proc* tmp_proc = ptable.heads[level];
  ptable.heads[level] = ptable.heads[level]->next;
  if(ptable.heads[level] == NULL) ptable.tail[level] = NULL;
  ptable.numproc[level]--;
  tmp_proc->next=NULL;
  return tmp_proc;
}


void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
}

// Must be called with interrupts disabled
int
cpuid() {
  return mycpu()-cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu*
mycpu(void)
{
  int apicid, i;
  
  if(readeflags()&FL_IF)
    panic("mycpu called with interrupts enabled\n");
  
  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i) {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc*
myproc(void) {
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto found;

  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;

  p->next = NULL;
  p->priority = 0;
  p->passed_time = 0;
  p->level = 0;

  release(&ptable.lock);

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;
  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();
  
  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;  // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);
  ptable.is_monopoly = 0;
  p->state = RUNNABLE;
  // cprintf("mlfq 정의전\n");
  // userinit 할때 scheduler 에다가 집어넣을거임 
  // mlfq_init(&my_mlfq);
  queue_push(p); 
  // cprintf("현재 my_mlfq의 q0  길이 : %d, head 의 pid :  %d name ->%s\n",my_mlfq.queues[0].length, my_mlfq.queues[0].head->process->pid, my_mlfq.queues[0].head->process->name);
  
  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  sz = curproc->sz;
  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sz = sz;
  switchuvm(curproc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy process state from proc.
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->sz = curproc->sz;
  np->parent = curproc;
  *np->tf = *curproc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  acquire(&ptable.lock);

  np->state = RUNNABLE;

  // process 생성 이후에는 집어넣을거임 
  // cprintf("fork 생성이전 %d pid -> %d\n",my_mlfq.queues[0].length,pid);
  //queue_push(&my_mlfq.queues[0], np); 
  queue_push(np);
  // cprintf("fork 생성이후 %d pid\n",my_mlfq.queues[0].length);
  release(&ptable.lock);

  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  if(curproc == initproc)
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;
  // 큐에서 뺴라 ㅇㅇ 
  // 웬만 
  // cprintf("exit sched 호출\n");
  // cprintf("pid:  %d, name : %s, state: %s\n",curproc->pid, curproc->name, to_string(curproc->state));
  // print_proc_info();
  // print_queue_info();
  sched();
  // cprintf("pid: %d, name : %s, state: %s\n",curproc->pid, curproc->name, to_string(curproc->state));
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();
  
  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}

//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
const char* to_string(enum procstate state) {
    switch (state) {
        case UNUSED: return "UNUSED";
        case EMBRYO: return "EMBRYO";
        case SLEEPING: return "SLEEPING";
        case RUNNABLE: return "RUNNABLE";
        case RUNNING: return "RUNNING";
        case ZOMBIE: return "ZOMBIE";
        default: return "UNKNOWN";
    }
}


void change_context(struct proc* p1,struct cpu* c1){
  // cprintf("(%d) [scheduler-> process] pid :%d, pname:%s, level : %d\n",ticks ,p1->pid, p1->name, p1->level);
  c1->proc = p1;
  switchuvm(p1);
  p1->state = RUNNING;
  swtch(&(c1->scheduler), p1->context);
  switchkvm();
  c1->proc = 0;
}


void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;
  // int cnt = 0;
  for(;;){
    // Enable interrupts on this processor.
    sti();
    acquire(&ptable.lock);
queue_search_start:
    if(!ptable.is_monopoly){
      for(int i =0 ; i<=3 ; i++){
        if(ptable.numproc[i]){
          p = queue_pop(i);
          change_context(p, c);
          if(p->state == RUNNABLE) queue_push(p);
          goto queue_search_start;
        }
      }
    }
    else{
      if(ptable.numproc[4]){
        p = queue_pop(4);
        if(p->state == RUNNABLE) change_context(p, c);
        if(p->state == RUNNABLE) queue_push(p);

        if(ptable.numproc[4] == 0) ptable.is_monopoly =0;
        goto queue_search_start;
      }
    }
    release(&ptable.lock);
  }
}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.

// 인터럽트가 활성화되어 있는지 확인하는 함수
int is_interrupt_enabled() {
    uint eflags = readeflags();
    return eflags & FL_IF;
}
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(mycpu()->ncli != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
  swtch(&p->context, mycpu()->scheduler);
  // cprintf("(%d) [process->scheduler] pid : %d pname: %s\n",ticks, p->pid,p->name);
  // print_proc_info();
  // print_queue_info();
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
  myproc()->state = RUNNABLE;
  sched();
  release(&ptable.lock);
}

int
getlev(void)
{
  struct proc *p = myproc();
  if(p->level == 4) return 99;
  else return p->level;
}

void pop_specific_pid(int lev,int pid){
  struct proc* current = ptable.heads[lev];
  struct proc* previous = NULL;

  while (current != NULL && current->pid != pid) {
      previous = current;
      current = current->next;
  }

  // 만일 길이가 1일시.. 
  if (previous == NULL) {
      ptable.heads[lev] = current->next;
      if (ptable.heads[lev] == NULL) ptable.tail[lev] = NULL;
  } else {
      previous->next = current->next;
      if (current->next == NULL) ptable.tail[lev] = previous;
  }

  // 프로세스 수 감소
  current->next = NULL;
  ptable.numproc[lev]--;
}

int
setpriority(int pid, int priority)
{ 
  if(priority<0 || priority>10) return -2;
  int find = -1;
  
  acquire(&ptable.lock);
  for(struct proc* p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->priority = priority;
      find = 0;
      break;
    }
  }

  struct proc* prev = NULL;
  for(struct proc* p=ptable.heads[3] ; p!= NULL ; p=p->next){
    if(p->pid == pid){
      if (prev == NULL) queue_pop(3);
      else{
        // 만일 꼬리부분일시 
        if(p==ptable.tail[3]){
          ptable.tail[3] = prev;
          prev->next = NULL;
        } // 꼬리부분 아니면 그냥 p를 중간에 잘라버리면 되긴함 
        else prev->next = p->next;
      }
      p->next = NULL; 
      queue_push(p);    
    }
    prev = p;
  }
  
  release(&ptable.lock);
  return find;
}


int setmonopoly(int pid, int password){

  struct proc* now_proc = myproc();
  int find = -1;
  if(password !=2018008068) return -2;
  acquire(&ptable.lock);
  int before_level;
  if(now_proc->pid == pid){
    now_proc->level = 4;
    find = -4;
  }
  else{
    for(struct proc* p = ptable.proc; p < &ptable.proc[NPROC]; p++){

      if(p->pid == pid){
        before_level = p->level;
        if(p->level ==4) find = -3;
        else{
          p->level = 4;
          queue_push(p);
          find = ptable.numproc[4];
          if(p->state == RUNNABLE) pop_specific_pid(before_level, pid); 
        }
        break;
      }
    }
  }
  release(&ptable.lock);
  return find;
}

void monopolize(void){
  acquire(&ptable.lock);
  ptable.is_monopoly = 1;
  release(&ptable.lock);
}

void unmonopolize(void){
  acquire(&ptable.lock);
  acquire(&tickslock);
  
  ptable.is_monopoly = 0;
  ticks = 0;

  release(&tickslock);
  release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first) {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  if(p == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    release(lk);
  }
  // Go to sleep.

  if(p->level !=4){
    p->chan = chan;
    p->state = SLEEPING;
    // cprintf("sleep sched 호출 process 이름은 %d\n", p->pid);
    sched();
  }
  // print_proc_info();
  // print_queue_info();
  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;
  // cprintf(" wakeup 호출 \n");
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == SLEEPING && p->chan == chan){
      p->state = RUNNABLE;
      queue_push(p);
    }
      // sleeping 되었다가 다시 돌아올 코드 ㅇㅇㅇ 
}

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING)
        p->state = RUNNABLE;
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}

void print_queue_info() {
  struct proc *current_proc;
  for (int i = 0; i < 5; i++) {  // 각 레벨별로 순회
      if (ptable.numproc[i]) cprintf("Level %d processes:\n", i);
      current_proc = ptable.heads[i];
      while (current_proc != NULL) {
          // UNUSED 상태가 아닌 프로세스 정보 출력
          if (current_proc->state != UNUSED) {
              cprintf(" PID: %d,Plevel: %d, Name: %s, State: %s\n", current_proc->pid,current_proc->level, current_proc->name, to_string(current_proc->state));
          }
          current_proc = current_proc->next;  // 다음 노드로 이동
      }
  }
  return;
}
void print_proc_info() {
    struct proc *tmp_p;
    cprintf("Current processes in the system:\n");
    for(tmp_p = ptable.proc; tmp_p < &ptable.proc[NPROC]; tmp_p++) {
        if (tmp_p->state != UNUSED) {  // UNUSED 상태가 아닌 프로세스만 출력
            cprintf("PID: %d, Name: %s, State: %s Level : %d priority : %d\n", tmp_p->pid, tmp_p->name, to_string(tmp_p->state), tmp_p->level,tmp_p->priority);
        }
    }

}
int degrade(){
  // 현재 프로세스에서 passed time 1증가시킴 
  int ret = 0;
  acquire(&ptable.lock);
  if(ptable.is_monopoly){
    release(&ptable.lock);
    return 0;
  }
  
  struct proc *p = myproc();
  // int before_level = p->level;
  p->passed_time++;
  if(p->passed_time == time_quantum[p->level]){
    ret = 1;
    // 다음 큐로 보내기 근데 만약 L3 면..?
    if(p->level == 0){
      if(p->pid % 2) p->level = 1;
      else p->level =2;
    }
    else if(p->level == 1 || p->level ==2) p->level = 3;
    else if(p->level == 3){
      if(p->priority > 0) p->priority--;
    }
    p->passed_time = 0;
  }

  release(&ptable.lock);
  return ret;
}

// 스읍 
void boost(){
  // 모든 프로세스 돌면서 ,L1,L2, L3 
  struct proc* tmp_proc;
  // 모든 큐를 L0 로 재조정 
  // 이떄 timequantum 0으로 모두 초기화 
  acquire(&ptable.lock);
  if(ptable.is_monopoly==1){
    release(&ptable.lock);
    return;
  }

  // ptable 돌면서 level 0 으로 해주는게 좋을듯 ...  sleep, running, runnable 모두 어쨋든 수정해주는 게 맞음
  for(struct proc* p= ptable.proc ; p < &ptable.proc[NPROC]; p++){
    if(p->level != 4 && p != NULL){
      p->level = 0;
      p->passed_time = 0;
    }
  }

  for(int i=1 ; i<=3 ; i++){
    while(ptable.numproc[i]){
      tmp_proc = queue_pop(i);
      queue_push(tmp_proc);
    }
  }

  release(&ptable.lock);
}