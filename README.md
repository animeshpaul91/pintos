# PintOS for CSE521 Operating Systems

<p>
Implemented synchronization primitives to enable kernel-level multithreading with priority scheduling. Developed 13 system calls in the PintOS kernel for an enhanced user-system interaction.
</p>

## There are 2 parts to this project: 

### First Part
Implementation of schedulers for threads in the PintOS kernel. The following schedulers were implemented: 
1. Priority Based Scheduling - Added support for preemption of an existing running process by another process that has a higher priority.
2. Multi-level Feedback Queue Scheduler - Added support for multiple queues in which threads can move as they age. The longer the process takes to execute, its priority reduces over time.


### Second Part
Implementation for running programs with user arguments and System calls. 
1. Added support for running programs with user arguments in the kernel. 
2. Implemented kernel code for 13 System Calls
  2.1 Process related System Calls - exit, exec, wait, halt
  2.2 File related System Calls - create, remove, open, filesize, read, write, seek, tell, close
