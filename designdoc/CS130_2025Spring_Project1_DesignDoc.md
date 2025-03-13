# CS 130: Project 1 - Threads Design Document

## Group Information

Please provide the details for your group below, including your group's name and the names and email addresses of all members.

- **Group Name**: *[printf("team")]*
- **Member 1**: Zirui Wang`<wangzr2024@shanghaitech.edu.cn>`
- **Member 2**: Xing Wu `<wuxing2024@shanghaitech.edu.cn>`

---

## Preliminaries

> If you have any preliminary comments on your submission, notes for the TAs, or extra credit, please give them here.
>

None.

---

## Alarm Clock

### Data Structures

> **A1:** Copy here the declaration of each new or changed `struct` or struct member, global or static variable, `typedef`, or enumeration. Identify the purpose of each in 25 words or less.

*Your answer here.*

### Algorithms

> **A2:** Briefly describe what happens in a call to `timer_sleep()`, including the effects of the timer interrupt handler.

*Your answer here.*

> **A3:** What steps are taken to minimize the amount of time spent in the timer interrupt handler?

*Your answer here.*

### Synchronization

> **A4:** How are race conditions avoided when multiple threads call `timer_sleep()` simultaneously?

*Your answer here.*

> **A5:** How are race conditions avoided when a timer interrupt occurs during a call to `timer_sleep()`?

*Your answer here.*

### Rationale

> **A6:** Why did you choose this design? In what ways is it superior to another design you considered?

*Your answer here.*

---

## Priority Scheduling

### Data Structures

> **B1:** Copy here the declaration of each new or changed `struct` or struct member, global or static variable, `typedef`, or enumeration. Identify the purpose of each in 25 words or less.

*Your answer here.*

> **B2:** Explain the data structure used to track priority donation. Describe how priority donation works in a nested scenario using a detailed textual explanation.

*Your answer here.*

### Algorithms

> **B3:** How do you ensure that the highest priority thread waiting for a lock, semaphore, or condition variable wakes up first?

*Your answer here.*

> **B4:** Describe the sequence of events when a call to `lock_acquire()` causes a priority donation. How is nested donation handled?

*Your answer here.*

> **B5:** Describe the sequence of events when `lock_release()` is called on a lock that a higher-priority thread is waiting for.

*Your answer here.*

### Synchronization

> **B6:** Describe a potential race in `thread_set_priority()` and explain how your implementation avoids it. Can you use a lock to avoid this race?

*Your answer here.*

### Rationale

> **B7:** Why did you choose this design? In what ways is it superior to another design you considered?

*Your answer here.*

---

## Advanced Scheduler

### Data Structures

> **C1:** Copy here the declaration of each new or changed `struct` or struct member, global or static variable, `typedef`, or enumeration. Identify the purpose of each in 25 words or less.

*Your answer here.*

### Algorithms

> **C2:** Suppose threads A, B, and C have nice values 0, 1, and 2. Each has a `recent_cpu` value of 0. Fill in the table below showing the scheduling decision and the priority and `recent_cpu` values for each thread after each given number of timer ticks:

*Fill in the table.*

| Timer Ticks | recent_cpu A | recent_cpu B | recent_cpu C | Priority A | Priority B | Priority C | Thread to Run |
|-------------|--------------|--------------|--------------|------------|------------|------------|---------------|
| 0           |              |              |              |            |            |            |               |
| 4           |              |              |              |            |            |            |               |
| 8           |              |              |              |            |            |            |               |
| 12          |              |              |              |            |            |            |               |
| 16          |              |              |              |            |            |            |               |
| 20          |              |              |              |            |            |            |               |
| 24          |              |              |              |            |            |            |               |
| 28          |              |              |              |            |            |            |               |
| 32          |              |              |              |            |            |            |               |
| 36          |              |              |              |            |            |            |               |

> **C3:** Did any ambiguities in the scheduler specification make values in the table uncertain? If so, what rule did you use to resolve them? Does this match the behavior of your scheduler?

*Your answer here.*

> **C4:** How is the way you divided the cost of scheduling between code inside and outside interrupt context likely to affect performance?

*Your answer here.*

### Rationale

> **C5:** Briefly critique your design, pointing out advantages and disadvantages in your design choices. If you were to have extra time to work on this part of the project, how might you choose to refine or improve your design?

*Your answer here.*

---

> **C6:** The assignment explains arithmetic for fixed-point math in detail, but it leaves it open to you to implement it. Why did you decide to implement it the way you did? If you created an abstraction layer for fixed-point math (i.e., an abstract data type and/or a set of functions or macros to manipulate fixed-point numbers), why did you do so? If not, why not?

*Your answer here.*

---

## Survey Questions

> Do you have any suggestions for the TAs to more effectively assist students, either for future quarters or the remaining projects? Any other comments?

*Your answer here.*
