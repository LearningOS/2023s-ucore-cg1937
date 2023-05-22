#include "syscall.h"
#include "defs.h"
#include "loader.h"
#include "syscall_ids.h"
#include "timer.h"
#include "trap.h"
#include "proc.h"

#define MAX_BYTE (1ULL << 30)
#define MAX_PAGES (MAX_BYTE / PGSIZE)

uint64 sys_write(int fd, uint64 va, uint len)
{
	debugf("sys_write fd = %d va = %x, len = %d", fd, va, len);
	if (fd != STDOUT)
		return -1;
	struct proc *p = curr_proc();
	char str[MAX_STR_LEN];
	int size = copyinstr(p->pagetable, str, va, MIN(len, MAX_STR_LEN));
	debugf("size = %d", size);
	for (int i = 0; i < size; ++i) {
		console_putchar(str[i]);
	}
	return size;
}

__attribute__((noreturn)) void sys_exit(int code)
{
	exit(code);
	__builtin_unreachable();
}

uint64 sys_sched_yield()
{
	yield();
	return 0;
}

uint64 sys_gettimeofday(TimeVal *val, int _tz) // TODO: implement sys_gettimeofday in pagetable. (VA to PA)
{
	// YOUR CODE
	// val->sec = 0;
	// val->usec = 0;
	TimeVal tmp_val;
	if (copyin(curr_proc()->pagetable, (char *)&tmp_val, (uint64)val, sizeof(TimeVal)) < 0)
		return -1;
	
	uint64 cycle = get_cycle();
	tmp_val.sec = cycle / CPU_FREQ;
	tmp_val.usec = (cycle % CPU_FREQ) * 1000000 / CPU_FREQ;

	if (copyout(curr_proc()->pagetable, (uint64)val, (char*)&tmp_val, sizeof(TimeVal)) < 0)
		return -1;
	
	/* The code in `ch3` will leads to memory bugs*/

	// uint64 cycle = get_cycle();
	// val->sec = cycle / CPU_FREQ;
	// val->usec = (cycle % CPU_FREQ) * 1000000 / CPU_FREQ;
	return 0;
}

uint64 sys_sbrk(int n)
{
	uint64 addr;
        struct proc *p = curr_proc();
        addr = p->program_brk;
        if(growproc(n) < 0)
                return -1;
        return addr;	
}



// TODO: add support for mmap and munmap syscall.
// hint: read through docstrings in vm.c. Watching CH4 video may also help.
// Note the return value and PTE flags (especially U,X,W,R)
int mmap(void* start, unsigned long long len, int port, int flag, int fd)
{
	if ((uint64) start % PGSIZE != 0) return -1;

	if (len == 0) return 0;
	if (len > MAX_BYTE) return -1;
	uint64 num_pages = (len + PGSIZE - 1) / PAGE_SIZE;
	if (num_pages > MAX_PAGES) return -1;

	if ((port & ~0x7) != 0 || (port & 0x7) == 0) return -1;
	

	struct proc *p = curr_proc();
	int perm = ((port << 1) | PTE_V | PTE_U);

	for (uint64 i = (uint64)start; i < (uint64)start + len; i += PAGE_SIZE)
	{
		if (walkaddr(p->pagetable, (uint64)i) != 0)
			return -1;
		void* pa_mem = kalloc();
		if (pa_mem == 0)
			return -1;
		memset(pa_mem, 0, PGSIZE);
		if(mappages(p->pagetable, i, PGSIZE, (uint64)pa_mem, perm) != 0)
			return -1;
	}
	return 0;
}
int munmap(void* start, unsigned long long len)
{
	if ((uint64) start % PGSIZE != 0) return -1;

	if (len == 0) return 0;
	if (len > MAX_BYTE) return -1;
	uint64 num_pages = (len + PGSIZE - 1) / PAGE_SIZE;
	if (num_pages > MAX_PAGES) return -1;

	struct proc *p = curr_proc();

	for (uint64 i = (uint64)start; i < (uint64)start + len; i += PAGE_SIZE)
	{
		if (walkaddr(p->pagetable, (uint64)i) == 0)
			return -1;
	}
	uvmunmap(p->pagetable, (uint64)start, num_pages, 1);
	return 0;
}
/*
* LAB1: you may need to define sys_task_info here
*/
int sys_task_info(TaskInfo *ti)
{

	struct proc *p = curr_proc();
	TaskInfo kernel_tmp_ti;  // 定义一个临时的 TaskInfo 变量，用于在内核空间中操作

    // 将用户空间的 TaskInfo 结构体复制到内核空间中
    if (copyin(p->pagetable, (char *)&kernel_tmp_ti, (uint64)ti, sizeof(TaskInfo)) != 0)
        return -1;  // copyin 失败，返回错误

    // 在内核空间中对 kti 结构体进行操作
    kernel_tmp_ti.status = Running;
    kernel_tmp_ti.time = get_time() - p->start_time;
	memmove(kernel_tmp_ti.syscall_times, p->syscall_times, sizeof(p->syscall_times));

    // 将修改后的 kti 结构体复制回用户空间中
    if (copyout(p->pagetable, (uint64)ti, (char *)&kernel_tmp_ti, sizeof(TaskInfo)) != 0) {
        return -1;  // copyout 失败，返回错误
    }

    return 0;  // 成功执行 syscall，返回 0
}


extern char trap_page[];

void syscall()
{
	struct trapframe *trapframe = curr_proc()->trapframe;
	int id = trapframe->a7, ret;
	uint64 args[6] = { trapframe->a0, trapframe->a1, trapframe->a2,
			   trapframe->a3, trapframe->a4, trapframe->a5 };
	tracef("syscall %d args = [%x, %x, %x, %x, %x, %x]", id, args[0],
	       args[1], args[2], args[3], args[4], args[5]);
	/*
	* LAB1: you may need to update syscall counter for task info here
	*/
	curr_proc()->syscall_times[id]++;
	switch (id) {
	case SYS_write:
		ret = sys_write(args[0], args[1], args[2]);
		break;
	case SYS_exit:
		sys_exit(args[0]);
		// __builtin_unreachable();
	case SYS_sched_yield:
		ret = sys_sched_yield();
		break;
	case SYS_gettimeofday:
		ret = sys_gettimeofday((TimeVal *)args[0], args[1]);
		break;
	case SYS_sbrk:
		ret = sys_sbrk(args[0]);
		break;
	/*
	* LAB1: you may need to add SYS_taskinfo case here
	*/
	case SYS_mmap:
		ret = mmap((void*)args[0], args[1], args[2], args[3], args[4]);
		break;
	case SYS_munmap:
		ret = munmap((void*)args[0], args[1]);
		break;
	case SYS_task_info:
		ret = sys_task_info((TaskInfo*)args[0]);
		break;
	default:
		ret = -1;
		errorf("unknown syscall %d", id);
	}
	trapframe->a0 = ret;
	tracef("syscall ret %d", ret);
}
