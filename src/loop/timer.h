/* Private to src/loop/: the timers both loops share (timer.c). */
#ifndef LOOP_TIMER_H
#define LOOP_TIMER_H
/* timeout_ms (-1 forever) cut to the first timer due, or to first (ns,
   loop_now's clock; -1 none) if that is sooner. */
int timer_wait(long long first, int timeout_ms);
/* Runs the timers that are due; returns the time it took as now. */
long long timer_run(void);
#endif
