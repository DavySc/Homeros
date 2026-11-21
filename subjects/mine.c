/* The user's functions reached through code that is not the user's, and left into it: the C runtime's qsort
   calls back, code without lines jumps into a function. Built a third time with /JMC ("just my code"), where
   Visual Studio's steps pass the code between and stop in the user's function, or back in main. */
#include <stdlib.h>
#pragma section(".bare", execute, read)
__declspec(allocate(".bare")) const unsigned char bare_jump_code[] = {0x48, 0x89, 0xc8, 0x89, 0xd1, 0xff, 0xe0};  /* mov rax, rcx; mov ecx, edx; jmp rax */
#pragma comment(linker, "/alternatename:bare_jump=bare_jump_code")
int bare_jump(int (*f)(int), int x);

static int calls;

static int order(const void *a, const void *b)
{
  calls += 1;
  return *(const int *)a - *(const int *)b;
}

static int twice(int x)
{
  return 2 * x;
}

static int sorted(int *v)
{
  qsort(v, 3, sizeof *v, order);     /* called back from a function that is itself called */
  return v[0];
}

int main(void)
{
  int v[3] = {2, 3, 1}, w[3] = {1, 3, 2};
  volatile int n = 0;
  qsort(v, 3, sizeof *v, order);     /* the runtime calls back, more than once; from the last it returns here */
  n += atoi("12");                   /* the runtime's code alone */
  n += bare_jump(twice, v[0]);       /* code without lines jumps into the user's */
  n += sorted(w) + twice(n);
  n += twice(atoi("3"));
  return (n + calls) & 0;
}
