/* The only memory the editor and the engine thread share: a bounded queue of byte messages,
   one for each direction (specs/Queue.tla). A message is copied in and copied out, so neither
   side ever holds a pointer into the other's memory.
   The editor passes wait = false and never blocks. The engine passes wait = true: it sleeps for
   a command while the target is stopped, and for room when the editor is behind. */
#ifndef DBG_QUEUE_H
#define DBG_QUEUE_H
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

#ifndef QUEUE_BYTES
#define QUEUE_BYTES 65536
#endif

typedef struct {
  pthread_mutex_t lock;
  pthread_cond_t changed;     /* at most one thread waits on a queue: the engine */
  void (*wake)(void *);       /* called after each push, outside the lock: wakes the editor's loop */
  void *wake_arg;
  uint32_t head, used;
  uint8_t bytes[QUEUE_BYTES];
} Queue;

void queue_init(Queue *q, void (*wake)(void *), void *wake_arg);
/* n is 1..QUEUE_BYTES-4. False when there is no room and wait is false. */
bool queue_push(Queue *q, const void *msg, uint32_t n, bool wait);
/* The length of the message copied to msg (cap must hold it), or 0 when empty and wait is false. */
uint32_t queue_pop(Queue *q, void *msg, uint32_t cap, bool wait);
#endif
