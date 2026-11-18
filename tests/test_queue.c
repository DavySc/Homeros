/* Stress for the two queues with a stub engine thread, in the shape specs/Queue.tla models:
   the editor side never waits (wait = false, a poll loop woken through an eventfd, clear then
   take); the engine side waits for commands and for room. Every command is answered by 0 to 3
   events whose bytes are a function of the command, so a lost, repeated, reordered or damaged
   message is seen. Built with a small QUEUE_BYTES so both queues are full most of the time. */
#include "../dbg/queue.h"
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <unistd.h>

enum { MAX = 100, QUIT = 0xffffffff };
typedef struct { uint32_t seq, part, len; uint8_t body[MAX]; } Msg;
static Queue cmds, events;

static uint32_t next(uint32_t *s) { *s = *s * 1103515245 + 12345; return *s >> 16; }
static uint32_t fill(Msg *m, uint32_t seq, uint32_t part)   /* the message for (seq, part); its size */
{
  uint32_t s = seq * 4 + part + 1;
  m->seq = seq; m->part = part; m->len = next(&s) % (MAX + 1);
  for (uint32_t i = 0; i < m->len; i += 1) m->body[i] = (uint8_t)next(&s);
  return (uint32_t)(sizeof *m - MAX + m->len);
}
static uint32_t parts(uint32_t seq) { uint32_t s = seq + 7; return seq == QUIT ? 1 : next(&s) % 4; }

static void *engine(void *arg)
{
  (void)arg;
  for (uint32_t expect = 0;; expect += 1) {
    Msg m, want, out;
    uint32_t n = queue_pop(&cmds, &m, sizeof m, true);
    if (m.seq != QUIT && (m.seq != expect || n != fill(&want, expect, 0) || memcmp(&m, &want, n) != 0)) {
      fprintf(stderr, "engine: command %u wrong\n", expect);
      exit(1);
    }
    for (uint32_t p = 0; p < parts(m.seq); p += 1) queue_push(&events, &out, fill(&out, m.seq, p + 1), true);
    if (m.seq == QUIT) return 0;
  }
}

static void wake(void *fd) { uint64_t one = 1; if (write(*(int *)fd, &one, sizeof one) < 0) abort(); }

int main(int argc, char **argv)
{
  (void)argv;
  uint32_t total = argc > 2 ? (uint32_t)atoi(argv[2]) : 200000, sent = 0, seq = 0, part = 0, got = 0;
  int fd = eventfd(0, EFD_NONBLOCK);
  queue_init(&cmds, 0, 0);
  queue_init(&events, wake, &fd);
  pthread_t t;
  pthread_create(&t, 0, engine, 0);
  for (bool exited = false; !exited;) {
    /* the editor's loop: do what input asks (send commands), then sleep until woken */
    uint32_t before = sent;
    for (Msg m; sent <= total && queue_push(&cmds, &m, fill(&m, sent < total ? sent : QUIT, 0), false); sent += 1) {}
    struct pollfd p = {fd, POLLIN, 0};
    /* nothing could be sent, so only the engine can move things on: it must wake us */
    if (poll(&p, 1, sent == before ? 5000 : 0) == 0 && sent == before) { fprintf(stderr, "editor: no wake for 5 s after %u events\n", got); return 1; }
    uint64_t count;
    if (read(fd, &count, sizeof count) < 0) continue;      /* clear first, then take everything */
    for (Msg m, want; !exited;) {
      uint32_t n = queue_pop(&events, &m, sizeof m, false);
      if (n == 0) break;
      while (seq != QUIT && part == parts(seq)) { seq = seq + 1 < total ? seq + 1 : QUIT; part = 0; }
      part += 1;
      if (n != fill(&want, seq, part) || memcmp(&m, &want, n) != 0) { fprintf(stderr, "editor: event %u wrong\n", got); return 1; }
      got += 1;
      exited = seq == QUIT;
    }
  }
  pthread_join(t, 0);
  if (queue_pop(&events, &(Msg){0}, sizeof(Msg), false) != 0) { fprintf(stderr, "an event after the last one\n"); return 1; }
  printf("queue: %u commands, %u events\n", total, got);
  return 0;
}
