lab4 实现思路：
根据文档，主要利用dirlink和dirunlink这两个工具函数来控制对一个目录下（根目录）的文件名的分配和取消。
dirunlink其实就是dirlink的逆过程。
注意ivalid，iupdate和iput的使用来保证inode不会出现没有处理干净的情况
整体来讲难度和文档描述相同，不高，但是概念很多，并且错综复杂。

ch7 问答：
1. pipe：grep ls 组合过滤展示特定的文件，或者 cat 和 tee / sed 组合进行字符的匹配和修改
2. 共享内存，在内核态开辟一个所有进程都可以访问的内存空间。
