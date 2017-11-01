What is this?
=============

Load Averages for Windows!
Linux/Unix operating systems have usually a 3-digit metric for measuring how busy the system is, known as load averages.
Windows does not have these metrics and instead assumes you are fine with real-time CPU info or watching perfmon.exe all day.

Windows has no method or metric to watch the system over a period of time (as load times do). I wrote this program to observe
the processor queue to determine not only how busy the processor is but determine how long the processor will be busy.


Why?
=====

Because I got tired of not being able to see how busy the system was over time (which can help see if there is a short-term
issue or a long term issue). Load Averages may not be a super useful metric but they give a quick overview of what the system
is doing.


Why do the numbers change much faster than on linux?
====================================================

Two reasons:
  1. Linux (and Linux exclusively) includes threads that are blocked for I/O operations by the kernel. This means that if a lot of threads are writing to the disk and the disk is really busy then the load averages will reflect that (this is so the user isn't going "WTF WHY IS MY SYSTEM SOOOOO SLOW!" with a 0.04 5-min load time). This program includes the Disk Queue in it's calculation as well as the Processor Queue.
  2. The program updates every 2 seconds over the various time periods (1 minute, 5 minute, and 15 minute) whereas everything else updates every 5 seconds.

