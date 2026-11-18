#include "queue.h"
#include <assert.h>
#include <string.h>

void queue_init(Queue *q, void (*wake)(void *), void *wake_arg)
{
  pthread_mutex_init(&q->lock, 0);
  pthread_cond_init(&q->changed, 0);
  q->wake = wake;
  q->wake_arg = wake_arg;
  q->head = q->used = 0;
}

/* copy n bytes between p and the ring at offset `at` from the head; to_ring gives the direction */
static void ring_copy(Queue *q, uint32_t at, void *p, uint32_t n, bool to_ring)
{
  uint32_t start = q->head + at;          /* head < QUEUE_BYTES and at <= QUEUE_BYTES */
  if (start >= QUEUE_BYTES) start -= QUEUE_BYTES;
  uint32_t first = n < QUEUE_BYTES - start ? n : QUEUE_BYTES - start;
  if (to_ring) {
    memcpy(q->bytes + start, p, first);
    memcpy(q->bytes, (uint8_t *)p + first, n - first);
  } else {
    memcpy(p, q->bytes + start, first);
    memcpy((uint8_t *)p + first, q->bytes, n - first);
  }
}

bool queue_push(Queue *q, const void *msg, uint32_t n, bool wait)
{
  assert(n >= 1 && n <= QUEUE_BYTES - sizeof n);
  pthread_mutex_lock(&q->lock);
  while (q->used + sizeof n + n > QUEUE_BYTES) {
    if (!wait) {
      pthread_mutex_unlock(&q->lock);
      return false;
    }
    pthread_cond_wait(&q->changed, &q->lock);
  }
  ring_copy(q, q->used, &n, sizeof n, true);
  ring_copy(q, q->used + sizeof n, (void *)(uintptr_t)msg, n, true);
  q->used += sizeof n + n;
  pthread_cond_signal(&q->changed);
  pthread_mutex_unlock(&q->lock);
  /* after the message is in: a wake sent first can be consumed before there is anything
     to take, and then nothing wakes the editor again (Queue_signalfirst.cfg) */
  if (q->wake) q->wake(q->wake_arg);
  return true;
}

uint32_t queue_pop(Queue *q, void *msg, uint32_t cap, bool wait)
{
  uint32_t n = 0;
  pthread_mutex_lock(&q->lock);
  /* test and wait under one lock: a push between an unlocked test and the wait would be
     missed (Queue_lostwake.cfg) */
  while (q->used == 0 && wait) pthread_cond_wait(&q->changed, &q->lock);
  if (q->used != 0) {
    ring_copy(q, 0, &n, sizeof n, false);
    assert(n <= cap);
    ring_copy(q, sizeof n, msg, n, false);
    q->head = (q->head + sizeof n + n) % QUEUE_BYTES;
    q->used -= sizeof n + n;
    pthread_cond_signal(&q->changed);
  }
  pthread_mutex_unlock(&q->lock);
  return n;
}
