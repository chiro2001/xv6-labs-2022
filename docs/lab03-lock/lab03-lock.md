## 实验 lock

### 回答问题

#### 内存分配器

1. 什么是内存分配器？它的作用是？

   > ![image-20221012175648080](lab03-lock.assets/image-20221012175648080.png)内存分配器是操作系统用于管理上图所示的空闲地址空间的分配的程序。在物理内存中，内核在 `Kernel data` 到 `PHYSTOP` 的位置专门为用户和部分内核的应用程序划分了一段可以被分配的内存空间，内存分配器可以管理这一部分空间的使用，管理某一块内存块是否被占用等。

2. 内存分配器的数据结构是什么？它有哪些操作（函数），分别完成了什么功能？

   > 内存分配器的数据结构是链表。可以调用 `kalloc()` 分配一块内存，`kfree()` 释放一块内存，`kinit()` 初始化内存分配器。

3. 为什么指导书提及的优化方法可以提升性能？

   > 当多个核心同时访问内存时，如果使用暂未优化的方法，由于物理内存是在多进程之间共享的，所以不管是分配还是释放页面，每次操作内存分配器的链表都需要申请 `kmem.lock` 这个全局自旋锁，造成每次只能有一个 CPU 核心访问内存，这造成了性能的损失。
   >
   > 使用了指导书提及的优化方法之后，每个 CPU 对应一个锁，将原来单个链表分割为 N 个链表，使得 CPU 访问内存不再需要申请全局自旋锁而只需要申请对应链表的自旋锁，提高了程序的并行性，从而提升了性能。

#### 磁盘缓存

1. 什么是磁盘缓存？它的作用是？
2. buf结构体为什么有prev和next两个成员，而不是只保留其中一个？请从这样做的优点分析（提示：结合通过这两种指针遍历链表的具体场景进行思考）。
3. 为什么哈希表可以提升磁盘缓存的性能？可以使用内存分配器的优化方法优化磁盘缓存吗？请说明原因。







测试结果：

```sh
$ make grade

# 编译输出等...

$ make qemu-gdb
(145.9s) 
== Test   kalloctest: test1 == 
  kalloctest: test1: OK 
== Test   kalloctest: test2 == 
  kalloctest: test2: OK 
== Test kalloctest: sbrkmuch == 
$ make qemu-gdb
kalloctest: sbrkmuch: OK (15.8s) 
== Test running bcachetest == 
$ make qemu-gdb
(8.6s) 
== Test   bcachetest: test0 == 
  bcachetest: test0: OK 
== Test   bcachetest: test1 == 
  bcachetest: test1: OK 
== Test usertests == 
$ make qemu-gdb
usertests: OK (207.5s) 
== Test time == 
time: OK 
Score: 70/70
```

