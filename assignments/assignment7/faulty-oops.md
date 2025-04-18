# `Faulty` kernel module Oops message breakdown
## Description
This is a breakdown of an Oops kerrnel message from kernel module "Faulty".
It's triggered when trying to write to `/dev/faulty` device. In this specific
example, command to trigger Oops message was `echo “hello_world” > /dev/faulty`
Here's the resulting log:
```
Unable to handle kernel NULL pointer dereference at virtual address 0000000000000000
Mem abort info:
  ESR = 0x0000000096000045
  EC = 0x25: DABT (current EL), IL = 32 bits
  SET = 0, FnV = 0
  EA = 0, S1PTW = 0
  FSC = 0x05: level 1 translation fault
Data abort info:
  ISV = 0, ISS = 0x00000045
  CM = 0, WnR = 1
user pgtable: 4k pages, 39-bit VAs, pgdp=0000000041b62000
[0000000000000000] pgd=0000000000000000, p4d=0000000000000000, pud=0000000000000000
Internal error: Oops: 0000000096000045 [#1] SMP
Modules linked in: faulty(O) hello(O) scull(O)
CPU: 0 PID: 159 Comm: sh Tainted: G           O       6.1.44 #1
Hardware name: linux,dummy-virt (DT)
pstate: 80000005 (Nzcv daif -PAN -UAO -TCO -DIT -SSBS BTYPE=--)
pc : faulty_write+0x10/0x20 [faulty]
lr : vfs_write+0xc8/0x390
sp : ffffffc008da3d20
x29: ffffffc008da3d80 x28: ffffff8001b9b500 x27: 0000000000000000
x26: 0000000000000000 x25: 0000000000000000 x24: 0000000000000000
x23: 0000000000000012 x22: 0000000000000012 x21: ffffffc008da3dc0
x20: 000000555c157990 x19: ffffff8001c21600 x18: 0000000000000000
x17: 0000000000000000 x16: 0000000000000000 x15: 0000000000000000
x14: 0000000000000000 x13: 0000000000000000 x12: 0000000000000000
x11: 0000000000000000 x10: 0000000000000000 x9 : 0000000000000000
x8 : 0000000000000000 x7 : 0000000000000000 x6 : 0000000000000000
x5 : 0000000000000001 x4 : ffffffc00078c000 x3 : ffffffc008da3dc0
x2 : 0000000000000012 x1 : 0000000000000000 x0 : 0000000000000000
Call trace:
 faulty_write+0x10/0x20 [faulty]
 ksys_write+0x74/0x110
 __arm64_sys_write+0x1c/0x30
 invoke_syscall+0x54/0x130
 el0_svc_common.constprop.0+0x44/0xf0
 do_el0_svc+0x2c/0xc0
 el0_svc+0x2c/0x90
 el0t_64_sync_handler+0xf4/0x120
 el0t_64_sync+0x18c/0x190
Code: d2800001 d2800000 d503233f d50323bf (b900003f)
---[ end trace 0000000000000000 ]---
```
## Breakdown
Oops message structure and explanation of each point.
* Description
   * The very first line summarizes the error:
     `Unable to handle kernel NULL pointer dereference at virtual address 0000000000000000`.
     Kernel module tried to access data at address zero.
* Memory Abort Info
   * The memory access that caused the issue was a data abort (`DABT`), and it occurred during the translation of the   virtual  address (`FSC = 0x05: level 1 translation fault`), meaning there was an error in translating the virtual    memory address to physical memory.
* Call Trace
   * The error happened in the `faulty_write` function
* Other Information
   * The stack trace shows that the failure occurred during a write operation (`ksys_write`), which then propagated  through system calls (`__arm64_sys_write`).

## Fix
When checking `faulty_write` in `misc_modules/faulty.c` it's clear that the line #53 `*(int *)0 = 0;` tries to dereference zero address. This line needs to be rewritten so incoming data is written to a correct address.
