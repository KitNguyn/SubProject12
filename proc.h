#ifndef PROC_H
#define PROC_H

// ---------------- Per-CPU state ----------------
struct context; // forward

// Per-CPU state
struct cpu {
  uchar apicid;                 // Local APIC ID
  struct context *scheduler;    // swtch() here to enter scheduler
  struct taskstate ts;          // Used by x86 to find stack for interrupt
  struct segdesc gdt[NSEGS];    // x86 global descriptor table
  volatile uint started;        // Has the CPU started?
  int ncli;                     // Depth of pushcli nesting.
  int intena;                   // Were interrupts enabled before pushcli?
  struct proc *proc;            // The process running on this cpu or null
};

// Make cpus and ncpu available to C files
extern struct cpu cpus[NCPU];
extern int ncpu;

// ---------------- Saved registers for kernel context switches ----------------
struct context {
  uint edi;
  uint esi;
  uint ebx;
  uint ebp;
  uint eip;
};

// ---------------- Process states ----------------
enum procstate { UNUSED, EMBRYO, SLEEPING, RUNNABLE, RUNNING, ZOMBIE };

// ---------------- MLFQ config ----------------
#define MLFQ_LEVELS 3
#define QUEUE_SIZE 64
#define NQUEUE MLFQ_LEVELS

// ---------------- Per-process structure ----------------
struct proc {
  uint sz;                 // Size of process memory (bytes)
  pde_t* pgdir;            // Page table
  char *kstack;            // Bottom of kernel stack
  enum procstate state;    // Process state
  int pid;                 // Process ID
  struct proc *parent;     // Parent process
  struct trapframe *tf;    // Trap frame for current syscall
  struct context *context; // swtch() here to run process
  void *chan;              // If non-zero, sleeping on chan
  int killed;              // If non-zero, have been killed
  struct file *ofile[NOFILE]; // Open files
  struct inode *cwd;       // Current directory
  char name[16];           // Process name (debugging)

  // ---------- MLFQ fields ----------
  int priority;            // 0 = highest
  int ticks;               // ticks used in current queue
};

// ---------------- MLFQ struct ----------------
struct mlfq {
  struct proc *queue[NQUEUE][QUEUE_SIZE];
  int head[NQUEUE];
  int tail[NQUEUE];
};

extern struct mlfq mlfq;

// ---------------- MLFQ helper functions ----------------
void mlfq_init(void);
void mlfq_enqueue(int level, struct proc *p);
void mlfq_remove(struct proc *p);
int  in_mlfq(struct proc *p);
struct proc* mlfq_pick_next(void);
void mlfq_boost_all(void);


#endif // PROC_H
