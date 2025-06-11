#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
extern struct proc proc[NPROC];

uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  argaddr(0, &p);
  return wait(p);
}

uint64
sys_sbrk(void)
{
  uint64 addr;
  int n;

  argint(0, &n);
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;

  argint(0, &n);
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(killed(myproc())){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  argint(0, &pid);
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

uint64
sys_map_shared_pages(void)
{
  int source_pid_from_arg;
  uint64 src_va_from_arg;
  int size_from_arg;
  struct proc *p_iterator;
  struct proc *identified_source_process = 0;
  struct proc *destination_process = myproc(); // התהליך שקורא לקריאת המערכת הוא היעד

  // קבלת ארגומנטים ממרחב המשתמש
  argint(0, &source_pid_from_arg);
  argaddr(1, &src_va_from_arg);
  argint(2, &size_from_arg);

  // ולידציה בסיסית של ערכי הארגומנטים
  if(source_pid_from_arg <= 0 || src_va_from_arg == 0 || size_from_arg <= 0) {
    return 0; // ארגומנטים לא חוקיים
  }

  // חיפוש תהליך המקור לפי ה-PID שהתקבל
  for(p_iterator = proc; p_iterator < &proc[NPROC]; p_iterator++) {
    acquire(&p_iterator->lock);
    if(p_iterator->pid == source_pid_from_arg) {
      if(p_iterator->state != UNUSED && p_iterator->state != ZOMBIE) {
        identified_source_process = p_iterator;
        // לא משחררים את הנעילה כאן - נשמור אותה עד לסוף הפעולה!
        break;
      }
    }
    release(&p_iterator->lock);
  }

  if(identified_source_process == 0) {
    return 0; // תהליך המקור לא נמצא או לא במצב תקין
  }
  // נעילה בטוחה של שני התהליכים למניעת deadlock
  int need_dest_lock = (identified_source_process != destination_process);
  
  if(need_dest_lock) {
    // נעילה בסדר קבוע לפי כתובת זיכרון למניעת deadlock
    if(identified_source_process < destination_process) {
      // identified_source_process כבר נעול, רק ננעל את destination
      acquire(&destination_process->lock);
    } else {
      // צריך לשחרר ולנעול מחדש בסדר הנכון
      release(&identified_source_process->lock);
      acquire(&destination_process->lock);
      acquire(&identified_source_process->lock);
    }
  }

  // כעת שני התהליכים נעולים בבטחה - בצע את המיפוי
  uint64 result = map_shared_pages(identified_source_process, destination_process, src_va_from_arg, (uint64)size_from_arg);

  // שחרר נעילות בסדר הפוך
  if(need_dest_lock) {
    if(identified_source_process < destination_process) {
      release(&destination_process->lock);
      release(&identified_source_process->lock);
    } else {
      release(&identified_source_process->lock);
      release(&destination_process->lock);
    }
  } else {
    release(&identified_source_process->lock);
  }

  return result;
}

uint64
sys_unmap_shared_pages(void)
{
  uint64 addr;
  int size;
  
  // Fix: argint and argaddr are void functions
  argaddr(0, &addr);
  argint(1, &size);
  
  if(addr == 0 || size <= 0)
    return -1;
  
  struct proc *p = myproc();
  
  // נעילת התהליך לפני גישה לשדות שלו (דרישה חובה מההבהרה)
  acquire(&p->lock);
  
  // Add external declaration for unmap_shared_pages
  extern uint64 unmap_shared_pages(struct proc*, uint64, uint64);
  uint64 result = unmap_shared_pages(p, addr, size);
  
  release(&p->lock);
  
  return result;
}
