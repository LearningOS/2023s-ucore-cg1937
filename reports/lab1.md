1. 
SBI 版本：RustSBI version 0.3.0-alpha.2, adapting to RISC-V SBI v1.0.0
ch2_bad_address：进入内部trap处理，因为没有实现对于处理函数，会执行unknown_trap
[ERROR 1]unknown trap: 0x0000000000000007, stval = 0x0000000000000000
ch2b_bad_instruction : 触发 IllegalInstruction in application, core dumped.
ch2b_bad_register: 和上述一样

2. sfence.vma zero, zero 是用来清空TLB映射表的，目前ch3下还没有引入虚拟内存的概念，所有操作都是在物理地址上进行的，所以把这行注释掉也没有影响（实际测试注释后运行内核确实没有影响）
3. 
这里a0寄存器的值已经被特意存在了(112)a0中
(112)a0 存储了a0寄存器里的值，a0的值会存储在一个临时变量寄存器sscratch值里，待回到U态其他操作完成之后把a0值恢复

4. sret会发生状态切换，主要是PC寄存器的切换实现了两个状态的跳转，还有其他一些重要信息的入栈和出栈

5. a0会获取到sscratch寄存器里面的值，也就是中断帧的地址(trapframe)

6. 从第六项开始保存，不是，少了112(a0)，这个特定保存a0本身的值，要放在一遍进行单独处理，至于为什么从前5项不用保存，是因为前面这几个变量时内核态限定变量，用户态没有权限访问到，即使进行U态和S态的切换时也影响不到这几个。

7.  ld sp, 8(a0)
    ld tp, 32(a0)
    ld t1, 0(a0)
    # csrw satp, t1
    # sfence.vma zero, zero
    ld t0, 16(a0)
    jr t0
    这部分代码是进入到S态。

8. kernel_trap usertrap()

lab1 实现思路：
需要结合测例来进行实现，get_time()需要结合get_cycle()和get_mtime()，然后就是仔细阅读需求，gdb单步调试也比较有帮助
出现的问题，解决方案：程序时间计算不对，通过gdb调试发现在调度器找可用进程进行运行时会重复更新start_time, 为此设置一个if判断记录第一次start的时间即可，