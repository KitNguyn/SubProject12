#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

static void wakeup1(void *chan);

// ===================== PTable =====================
struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

// ===================== MLFQ =====================
// Global MLFQ object
struct mlfq mlfq;

// Forward decl
void mlfq_remove(struct proc *p);

// ----- Internal MLFQ helpers (circular queue, RR) -----
static int
mlfq_contains_level(int lvl, struct proc *p)
{
  int h = mlfq.head[lvl], t = mlfq.tail[lvl];
  for(int i = h; i != t; i = (i + 1) % QUEUE_SIZE){
    if(mlfq.queue[lvl][i] == p) return 1;
  }
  return 0;
}

static void
mlfq_push_level(int lvl, struct proc *p)
{
  if(lvl < 0) lvl = 0;
  if(lvl >= NQUEUE) lvl = NQUEUE - 1;
  if(p == 0) return;

  // avoid duplicates
  if(mlfq_contains_level(lvl, p)) return;

  int nt = (mlfq.tail[lvl] + 1) % QUEUE_SIZE;
  if(nt == mlfq.head[lvl]){
    // queue full -> ignore or panic
    // panic("mlfq queue full");
    return;
  }
  mlfq.queue[lvl][mlfq.tail[lvl]] = p;
  mlfq.tail[lvl] = nt;
}

// Round-robin pick: pop head until find RUNNABLE.
// If RUNNABLE: push back to tail (RR) and return it.
// If not RUNNABLE: drop it (queue gets cleaned).
static struct proc*
mlfq_pick_rr_level(int lvl)
{
  while(mlfq.head[lvl] != mlfq.tail[lvl]){
    struct proc *p = mlfq.queue[lvl][mlfq.head[lvl]];
    mlfq.queue[lvl][mlfq.head[lvl]] = 0;
    mlfq.head[lvl] = (mlfq.head[lvl] + 1) % QUEUE_SIZE;

    if(p == 0) continue;

    if(p->state == RUNNABLE){
      mlfq_push_level(lvl, p); // RR rotate
      return p;
    }
    // else: drop (SLEEPING/ZOMBIE/UNUSED...)
  }
  return 0;
}

// Rebuild-remove (safe with circular queue)
void
mlfq_remove(struct proc *p)
{
  if(p == 0) return;

  for(int lvl = 0; lvl < NQUEUE; lvl++){
    struct proc *tmp[QUEUE_SIZE];
    int cnt = 0;

    int h = mlfq.head[lvl], t = mlfq.tail[lvl];
    int found = 0;

    for(int i = h; i != t; i = (i + 1) % QUEUE_SIZE){
      struct proc *x = mlfq.queue[lvl][i];
      if(x == 0) continue;
      if(x == p){ found = 1; continue; }
      if(cnt < QUEUE_SIZE) tmp[cnt++] = x;
    }

    if(found){
      // reset this level
      mlfq.head[lvl] = 0;
      mlfq.tail[lvl] = 0;
      for(int k = 0; k < QUEUE_SIZE; k++)
        mlfq.queue[lvl][k] = 0;

      // re-enqueue
      for(int k = 0; k < cnt; k++)
        mlfq_push_level(lvl, tmp[k]);

      return;
    }
  }
}

// ----- Wrappers to match proc.h prototypes -----
void
mlfq_init(void)
{
  for(int i = 0; i < NQUEUE; i++){
    mlfq.head[i] = 0;
    mlfq.tail[i] = 0;
    for(int j = 0; j < QUEUE_SIZE; j++)
      mlfq.queue[i][j] = 0;
  }
}

void
mlfq_enqueue(int level, struct proc *p)
{
  mlfq_push_level(level, p);
}

int
in_mlfq(struct proc *p)
{
  for(int lvl = 0; lvl < NQUEUE; lvl++){
    if(mlfq_contains_level(lvl, p)) return 1;
  }
  return 0;
}

struct proc*
mlfq_pick_next(void)
{
  for(int lvl = 0; lvl < NQUEUE; lvl++){
    struct proc *p = mlfq_pick_rr_level(lvl);
    if(p) return p;
  }
  return 0;
}

// ===================== Init =====================
void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
  mlfq_init();
}

// ===================== CPU/Proc Utilities =====================
int
cpuid(void)
{
  return mycpu() - cpus;
}

struct cpu*
mycpu(void)
{
  int apicid, i;

  if(readeflags()&FL_IF)
    panic("mycpu called with interrupts enabled");

  apicid = lapicid();
  for(i = 0; i < ncpu; ++i)
    if(cpus[i].apicid == apicid)
      return &cpus[i];

  panic("unknown apicid");
}

struct proc*
myproc(void)
{
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}

// ===================== allocproc =====================
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

  // MLFQ init per-proc
  p->priority = 0;
  p->ticks = 0;

  release(&ptable.lock);

  if((p->kstack = kalloc()) == 0){
    acquire(&ptable.lock);
    p->state = UNUSED;
    release(&ptable.lock);
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Trap frame
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  // Return to trapret
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  // Context
  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

  return p;
}

// ===================== userinit =====================
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();
  initproc = p;

  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory");

  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  acquire(&ptable.lock);
  p->state = RUNNABLE;
  mlfq_enqueue(p->priority, p);
  release(&ptable.lock);
}

// ===================== growproc =====================
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

// ===================== fork =====================
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();

  if((np = allocproc()) == 0)
    return -1;

  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    acquire(&ptable.lock);
    np->state = UNUSED;
    release(&ptable.lock);
    return -1;
  }

  np->sz = curproc->sz;
  np->parent = curproc;
  *np->tf = *curproc->tf;
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  acquire(&ptable.lock);
  np->state = RUNNABLE;
  mlfq_enqueue(np->priority, np);
  release(&ptable.lock);

  return pid;
}

// ===================== exit =====================
void
exit(void)
{
  struct proc *p;
  struct proc *curproc = myproc();

  if(curproc == initproc)
    panic("init exiting");

  acquire(&ptable.lock);

  // reparent children to init
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // remove from MLFQ
  mlfq_remove(curproc);

  curproc->state = ZOMBIE;
  wakeup1(curproc->parent);
  sched();
  panic("zombie exit");
}

// ===================== wait =====================
int
wait(void)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();

  acquire(&ptable.lock);
  for(;;){
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;

      havekids = 1;
      if(p->state == ZOMBIE){
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);

        mlfq_remove(p);

        p->state = UNUSED;
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;

        release(&ptable.lock);
        return pid;
      }
    }

    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    sleep(curproc, &ptable.lock);
  }
}

// ===================== scheduler =====================
void
scheduler(void)
{
  struct cpu *c = mycpu();
  c->proc = 0;

  for(;;){
    sti();
    acquire(&ptable.lock);

    struct proc *p = mlfq_pick_next();

    if(p){
      c->proc = p;
      p->state = RUNNING;

      switchuvm(p);
      swtch(&c->scheduler, p->context);
      switchkvm();

      // If it became RUNNABLE again, ensure demote + enqueue.
      if(p->state == RUNNABLE){
        p->ticks++;

        // demote (đếm theo lần được schedule; chuẩn hơn là theo timer tick ở trap.c)
        if(p->ticks >= (1 << p->priority) && p->priority < NQUEUE - 1){
          p->ticks = 0;
          mlfq_remove(p);
          p->priority++;
        }
        mlfq_enqueue(p->priority, p);
      }

      c->proc = 0;
    }

    release(&ptable.lock);
  }
}

// ===================== sched / yield =====================
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
  if(readeflags() & FL_IF)
    panic("sched interruptible");

  intena = mycpu()->intena;
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

void
yield(void)
{
  acquire(&ptable.lock);
  struct proc *p = myproc();
  p->state = RUNNABLE;
  mlfq_enqueue(p->priority, p);
  sched();
  release(&ptable.lock);
}

// ===================== forkret =====================
void
forkret(void)
{
  static int first = 1;
  release(&ptable.lock);

  if(first){
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }
}

// ===================== sleep / wakeup =====================
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();

  if(p == 0 || lk == 0)
    panic("sleep");

  if(lk != &ptable.lock){
    acquire(&ptable.lock);
    release(lk);
  }

  // leaving runnable queue
  mlfq_remove(p);

  p->chan = chan;
  p->state = SLEEPING;
  sched();
  p->chan = 0;

  if(lk != &ptable.lock){
    release(&ptable.lock);
    acquire(lk);
  }
}

static void
wakeup1(void *chan)
{
  struct proc *p;
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == SLEEPING && p->chan == chan){
      p->state = RUNNABLE;
      mlfq_enqueue(p->priority, p);
    }
  }
}

void
wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// ===================== kill =====================
int
kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      if(p->state == SLEEPING){
        p->state = RUNNABLE;
        mlfq_enqueue(p->priority, p);
      }
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

// ===================== procdump =====================
void
procdump(void)
{
  static char *states[] = {
    [UNUSED]   "unused",
    [EMBRYO]   "embryo",
    [SLEEPING] "sleep ",
    [RUNNABLE] "runble",
    [RUNNING]  "run ",
    [ZOMBIE]   "zombie"
  };

  struct proc *p;
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    cprintf("%d %s %s pri:%d ticks:%d\n",
            p->pid, states[p->state], p->name, p->priority, p->ticks);
  }
}
// Boost tất cả process về level 0 để chống starvation
void
mlfq_boost_all(void)
{
  acquire(&ptable.lock);

  for(struct proc *p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED || p->state == ZOMBIE)
      continue;

    // reset về top
    p->priority = 0;
    p->ticks = 0;

    // nếu đang RUNNABLE thì đảm bảo nó nằm trong queue level 0
    if(p->state == RUNNABLE){
      mlfq_enqueue(0, p);
    }
  }

  release(&ptable.lock);
}

