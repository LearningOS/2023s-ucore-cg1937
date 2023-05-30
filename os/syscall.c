#include "syscall.h"
#include "console.h"
#include "defs.h"
#include "loader.h"
#include "syscall_ids.h"
#include "timer.h"
#include "trap.h"

#define MAX_BYTE (1ULL << 30)
#define max(a, b) ((a) > (b) ? (a) : (b))

uint64 console_write(uint64 va, uint64 len)
{
	struct proc *p = curr_proc();
	char str[MAX_STR_LEN];
	int size = copyinstr(p->pagetable, str, va, MIN(len, MAX_STR_LEN));
	tracef("write size = %d", size);
	for (int i = 0; i < size; ++i) {
		console_putchar(str[i]);
	}
	return len;
}

uint64 console_read(uint64 va, uint64 len)
{
	struct proc *p = curr_proc();
	char str[MAX_STR_LEN];
	tracef("read size = %d", len);
	for (int i = 0; i < len; ++i) {
		int c = consgetc();
		str[i] = c;
	}
	copyout(p->pagetable, va, str, len);
	return len;
}

uint64 sys_write(int fd, uint64 va, uint64 len)
{
	if (fd < 0 || fd > FD_BUFFER_SIZE)
		return -1;
	struct proc *p = curr_proc();
	struct file *f = p->files[fd];
	if (f == NULL) {
		errorf("invalid fd %d\n", fd);
		return -1;
	}
	switch (f->type) {
	case FD_STDIO:
		return console_write(va, len);
	case FD_INODE:
		return inodewrite(f, va, len);
	default:
		panic("unknown file type %d\n", f->type);
	}
}

uint64 sys_read(int fd, uint64 va, uint64 len)
{
	if (fd < 0 || fd > FD_BUFFER_SIZE)
		return -1;
	struct proc *p = curr_proc();
	struct file *f = p->files[fd];
	if (f == NULL) {
		errorf("invalid fd %d\n", fd);
		return -1;
	}
	switch (f->type) {
	case FD_STDIO:
		return console_read(va, len);
	case FD_INODE:
		return inoderead(f, va, len);
	default:
		panic("unknown file type %d\n", f->type);
	}
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

int mmap(void *start, unsigned long long len, int port, int flag, int fd)
{
	// 合并到ch5的思路，max_page需要更新（之前输出调试信息发现max_page不够大，如果不更新会导致mmap成功，但是后续的freewalk会找到叶子页表导致panic）
	if ((uint64)start % PGSIZE)
		return -1;
	if (len == 0)
		return 0;
	if (len > MAX_BYTE)
		return -1;
	if ((port & ~0x7) != 0 || (port & 0x7) == 0)
		return -1;
	int perm = ((port << 1) | PTE_U);
	struct proc *p = curr_proc();
	uint64 va = (uint64)start;
	uint64 pages = PGROUNDUP(len) / PGSIZE;
	for (uint64 i = 0; i < pages; ++i) {
		void *pa = kalloc();
		if (pa == 0) {
			uvmunmap(p->pagetable, va, i, 1);
			return -1;
		}
		if (mappages(p->pagetable, va + i * PGSIZE, PGSIZE, (uint64)pa,
			     perm) != 0) {
			uvmunmap(p->pagetable, va, i, 1);
			kfree(pa);
			return -1;
		}
		uint64 curr_pages = va / PGSIZE + i + 1;
		p->max_page = max(curr_pages, p->max_page);
	}
	return 0;
}

int munmap(void *start, unsigned long long len)
{
	if ((uint64)start % PGSIZE != 0)
		return -1;
	if (len == 0)
		return 0;
	if (len > MAX_BYTE)
		return -1;
	struct proc *p = curr_proc();
	uint64 num_pages = (len + PGSIZE - 1) / PAGE_SIZE;
	if (num_pages > p->max_page)
		return -1;

	for (uint64 i = (uint64)start; i < (uint64)start + len;
	     i += PAGE_SIZE) {
		if (walkaddr(p->pagetable, (uint64)i) == 0)
			return -1;
	}
	uvmunmap(p->pagetable, (uint64)start, num_pages, 1);
	return 0;
}

int sys_task_info(TaskInfo *ti)
{
	struct proc *p = curr_proc();
	TaskInfo kernel_tmp_ti; // 定义一个临时的 TaskInfo 变量，用于在内核空间中操作

	// 将用户空间的 TaskInfo 结构体复制到内核空间中
	// if (copyin(p->pagetable, (char *)&kernel_tmp_ti, (uint64)ti,
	// 	   sizeof(TaskInfo)) != 0)
	// 	return -1; // copyin 失败，返回错误

	// 在内核空间中对 kti 结构体进行操作
	kernel_tmp_ti.status = Running;
	kernel_tmp_ti.time = get_time() - p->start_time;
	memmove(kernel_tmp_ti.syscall_times, p->syscall_times,
		sizeof(p->syscall_times));

	// 将修改后的 kti 结构体复制回用户空间中
	if (copyout(p->pagetable, (uint64)ti, (char *)&kernel_tmp_ti,
		    sizeof(TaskInfo)) != 0) {
		return -1; // copyout 失败，返回错误
	}

	return 0; // 成功执行 syscall，返回 0
}

uint64 sys_gettimeofday(uint64 val, int _tz)
{
	struct proc *p = curr_proc();
	uint64 cycle = get_cycle();
	TimeVal t;
	t.sec = cycle / CPU_FREQ;
	t.usec = (cycle % CPU_FREQ) * 1000000 / CPU_FREQ;
	copyout(p->pagetable, val, (char *)&t, sizeof(TimeVal));
	return 0;
}

uint64 sys_getpid()
{
	return curr_proc()->pid;
}

uint64 sys_getppid()
{
	struct proc *p = curr_proc();
	return p->parent == NULL ? IDLE_PID : p->parent->pid;
}

uint64 sys_clone()
{
	debugf("fork!");
	return fork();
}

static inline uint64 fetchaddr(pagetable_t pagetable, uint64 va)
{
	uint64 *addr = (uint64 *)useraddr(pagetable, va);
	return *addr;
}

uint64 sys_exec(uint64 path, uint64 uargv)
{
	struct proc *p = curr_proc();
	char name[MAX_STR_LEN];
	copyinstr(p->pagetable, name, path, MAX_STR_LEN);
	uint64 arg;
	static char strpool[MAX_ARG_NUM][MAX_STR_LEN];
	char *argv[MAX_ARG_NUM];
	int i;
	for (i = 0; uargv && (arg = fetchaddr(p->pagetable, uargv));
	     uargv += sizeof(char *), i++) {
		copyinstr(p->pagetable, (char *)strpool[i], arg, MAX_STR_LEN);
		argv[i] = (char *)strpool[i];
	}
	argv[i] = NULL;
	return exec(name, (char **)argv);
}

uint64 sys_wait(int pid, uint64 va)
{
	struct proc *p = curr_proc();
	int *code = (int *)useraddr(p->pagetable, va);
	return wait(pid, code);
}

int sys_spawn(char *filename)
//思路：filename在用户空间，需要copyin到内核空间，然后调用loader加载程序，然后调用add_task添加到任务队列
{
	// TODO: your job is to complete the sys call
	struct proc *p = curr_proc();
	struct proc *np;
	if ((np = allocproc()) == 0)
		return -1;
	np->parent = p;
	char k_filename[200];
	copyinstr(p->pagetable, k_filename, (uint64)filename, 200);
	struct inode *ip;
	if ((ip = namei(k_filename)) == 0)
		return -1;
	bin_loader(ip, np);
	iput(ip);
	add_task(np);
	return np->pid;
}

uint64 sys_set_priority(long long prio)
{
	// TODO: your job is to complete the sys call
	if (prio >= 2) {
		struct proc *p = curr_proc();
		p->priority = prio;
		return prio;
	}
	return -1;
}

uint64 sys_openat(uint64 va, uint64 omode, uint64 _flags)
{
	struct proc *p = curr_proc();
	char path[200];
	copyinstr(p->pagetable, path, va, 200);
	return fileopen(path, omode);
}

uint64 sys_close(int fd)
{
	if (fd < 0 || fd > FD_BUFFER_SIZE)
		return -1;
	struct proc *p = curr_proc();
	struct file *f = p->files[fd];
	if (f == NULL) {
		errorf("invalid fd %d", fd);
		return -1;
	}
	fileclose(f);
	p->files[fd] = 0;
	return 0;
}

struct Stat {
	uint64 dev;
	uint64 ino;
	uint32 mode;
	uint32 nlink;
	uint64 pad[7];
};
#define DIR 0x040000
#define FILE 0x100000

int sys_fstat(int fd, struct Stat *stat)
{
	//TODO: your job is to complete the syscall
	if (fd < 0 || fd > FD_BUFFER_SIZE)
		return -1;
	struct proc *p = curr_proc();
	struct file *f = p->files[fd];
	if (f == NULL)
		return -1;
	struct Stat k_stat;
	k_stat.dev = 0;
	k_stat.ino = f->ip->inum;
	k_stat.mode = f->ip->type == T_DIR ? DIR : FILE;
	k_stat.nlink = f->ip->link;
	copyout(p->pagetable, (uint64)stat, (char *)&k_stat,
		sizeof(struct Stat));
	return 0;
}

int sys_linkat(int olddirfd, char *oldpath, int newdirfd, char *newpath,
	       uint64 flags)
{
	//TODO: your job is to complete the syscall
	printf("linkat\n");
	struct inode *old_ip, *new_ip, *dp;
	char k_oldpath[200];
	char k_newpath[200];
	copyinstr(curr_proc()->pagetable, k_oldpath, (uint64)oldpath, 200);
	copyinstr(curr_proc()->pagetable, k_newpath, (uint64)newpath, 200);
	if ((old_ip = namei(k_oldpath)) == 0)
		return -1;
	if ((new_ip = namei(k_newpath)) != 0) {
		iput(old_ip);
		iput(new_ip);
		return -1;
	}

	dp = root_dir();
	ivalid(dp);
	if (dirlink(dp, k_newpath, old_ip->inum) < 0) {
		iput(old_ip);
		return -1;
	}

	old_ip->link++;
	iupdate(old_ip);
	iput(old_ip);
	iput(dp);

	return 0;
}

int sys_unlinkat(int dirfd, char *name, uint64 flags)
{
	//TODO: your job is to complete the syscall
	// printf("unlinkat\n");
	struct inode *dp;
	char k_name[200];
	copyinstr(curr_proc()->pagetable, k_name, (uint64)name, 200);
	dp = root_dir();
	ivalid(dp);
	if (dirunlink(dp, k_name) < 0) {
		return -1;
	}
	iput(dp);
	return 0;
}

uint64 sys_sbrk(int n)
{
	uint64 addr;
	struct proc *p = curr_proc();
	addr = p->program_brk;
	if (growproc(n) < 0)
		return -1;
	return addr;
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
	curr_proc()->syscall_times[id]++;
	switch (id) {
	case SYS_write:
		ret = sys_write(args[0], args[1], args[2]);
		break;
	case SYS_read:
		ret = sys_read(args[0], args[1], args[2]);
		break;
	case SYS_openat:
		ret = sys_openat(args[0], args[1], args[2]);
		break;
	case SYS_close:
		ret = sys_close(args[0]);
		break;
	case SYS_exit:
		sys_exit(args[0]);
		// __builtin_unreachable();
	case SYS_sched_yield:
		ret = sys_sched_yield();
		break;
	case SYS_gettimeofday:
		ret = sys_gettimeofday(args[0], args[1]);
		break;
	case SYS_getpid:
		ret = sys_getpid();
		break;
	case SYS_getppid:
		ret = sys_getppid();
		break;
	case SYS_clone: // SYS_fork
		ret = sys_clone();
		break;
	case SYS_execve:
		ret = sys_exec(args[0], args[1]);
		break;
	case SYS_wait4:
		ret = sys_wait(args[0], args[1]);
		break;
	case SYS_fstat:
		ret = sys_fstat(args[0], (struct Stat *)args[1]);
		break;
	case SYS_linkat:
		ret = sys_linkat(args[0], (char *)args[1], args[2],
				 (char *)args[3], args[4]);
		break;
	case SYS_unlinkat:
		ret = sys_unlinkat(args[0], (char *)args[1], args[2]);
		break;
	case SYS_spawn:
		ret = sys_spawn((char *)args[0]);
		break;
	case SYS_sbrk:
		ret = sys_sbrk(args[0]);
		break;
	case SYS_setpriority:
		ret = sys_set_priority(args[0]);
		break;
	case SYS_mmap:
		ret = mmap((void *)args[0], args[1], args[2], args[3], args[4]);
		break;
	case SYS_munmap:
		ret = munmap((void *)args[0], args[1]);
		break;
	case SYS_task_info:
		ret = sys_task_info((TaskInfo *)args[0]);
		break;
	default:
		ret = -1;
		errorf("unknown syscall %d", id);
	}
	trapframe->a0 = ret;
	tracef("syscall ret %d", ret);
}
