#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"
#include "traps.h"
#include "spinlock.h"

// ===== MLFQ tuning =====
#define BOOST_TICKS 100   // mỗi 100 ticks boost 1 lần (đổi 50/200 tuỳ bạn)

static inline int
quantum_for_level(int lvl)
{
  // level 0: 1 tick, level 1: 2 ticks, level 2: 4 ticks, ...
  return 1 << lvl;
}

// Interrupt descriptor table (shared by all CPUs).
struct gatedesc idt[256];
extern uint vectors[]; // in vectors.S: array of 256 entry pointers
struct spinlock tickslock;
uint ticks;

void
tvinit(void)
{
  int i;

  for(i = 0; i < 256; i++)
    SETGATE(idt[i], 0, SEG_KCODE<<3, vectors[i], 0);
  SETGATE(idt[T_SYSCALL], 1, SEG_KCODE<<3, vectors[T_SYSCALL], DPL_USER);

  initlock(&tickslock, "time");
}

void
idtinit(void)
{
  lidt(idt, sizeof(idt));
}

//PAGEBREAK: 41
void
trap(struct trapframe *tf)
{
  if(tf->trapno == T_SYSCALL){
    if(myproc() && myproc()->killed)
      exit();
    myproc()->tf = tf;
    syscall();
    if(myproc() && myproc()->killed)
      exit();
    return;
  }

  switch(tf->trapno){
  case T_IRQ0 + IRQ_TIMER: {
    // update global ticks on CPU0
    if(cpuid() == 0){
      acquire(&tickslock);
      ticks++;
      wakeup(&ticks);
      release(&tickslock);

      // periodic priority boost (chống starvation)
      if((ticks % BOOST_TICKS) == 0){
        mlfq_boost_all();  // <-- hàm này bạn thêm trong proc.c
      }
    }

    // ===== MLFQ accounting theo timer tick =====
    struct proc *p = myproc();
    if(p && p->state == RUNNING){
      p->ticks++;

      // hết quantum -> demote + yield
      if(p->ticks >= quantum_for_level(p->priority)){
        p->ticks = 0;
        if(p->priority < NQUEUE - 1)
          p->priority++;
        yield();
      }
    }

    lapiceoi();
    break;
  }

  case T_IRQ0 + IRQ_IDE:
    ideintr();
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_IDE+1:
    // Bochs generates spurious IDE1 interrupts.
    break;
  case T_IRQ0 + IRQ_KBD:
    kbdintr();
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_COM1:
    uartintr();
    lapiceoi();
    break;
  case T_IRQ0 + 7:
  case T_IRQ0 + IRQ_SPURIOUS:
    cprintf("cpu%d: spurious interrupt at %x:%x\n",
            cpuid(), tf->cs, tf->eip);
    lapiceoi();
    break;

  //PAGEBREAK: 13
  default:
    if(myproc() == 0 || (tf->cs&3) == 0){
      // In kernel, it must be our mistake.
      cprintf("unexpected trap %d from cpu %d eip %x (cr2=0x%x)\n",
              tf->trapno, cpuid(), tf->eip, rcr2());
      panic("trap");
    }
    // In user space, assume process misbehaved.
    cprintf("pid %d %s: trap %d err %d on cpu %d "
            "eip 0x%x addr 0x%x--kill proc\n",
            myproc()->pid, myproc()->name, tf->trapno,
            tf->err, cpuid(), tf->eip, rcr2());
    myproc()->killed = 1;
  }

  // Force process exit if it has been killed and is in user space.
  if(myproc() && myproc()->killed && (tf->cs&3) == DPL_USER)
    exit();

  // QUAN TRỌNG: bỏ yield() mặc định mỗi tick (RR)
  // Vì MLFQ yield theo quantum đã làm ở case TIMER rồi.

  // Check if the process has been killed since we yielded
  if(myproc() && myproc()->killed && (tf->cs&3) == DPL_USER)
    exit();
}
