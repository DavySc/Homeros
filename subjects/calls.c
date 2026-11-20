/* Calls from one line into code with and without line information: stepping into a line goes into a call that
   is not the line's last only where the called code has lines. Lines made of several statements. And, built a
   third time with /JMC, the call the compiler puts at each function's start under made-up line numbers.
   The code without lines is bytes in a section of their own, which the linker gives the functions' names. */
#pragma section(".bare", execute, read)
__declspec(allocate(".bare")) const unsigned char bare_one_code[] = {0x8d, 0x41, 0x01, 0xc3};                     /* lea eax, [rcx + 1]; ret */
__declspec(allocate(".bare")) const unsigned char bare_two_code[] = {0x8d, 0x04, 0x09, 0xc3};                     /* lea eax, [rcx + rcx]; ret */
__declspec(allocate(".bare")) const unsigned char bare_call_code[] = {0x48, 0x89, 0xc8, 0x89, 0xd1, 0xff, 0xe0};  /* mov rax, rcx; mov ecx, edx; jmp rax */
#pragma comment(linker, "/alternatename:bare_one=bare_one_code")
#pragma comment(linker, "/alternatename:bare_two=bare_two_code")
#pragma comment(linker, "/alternatename:bare_call=bare_call_code")
int bare_one(int x);
int bare_two(int x);
int bare_call(int (*f)(int), int x);

static int lined(int x)
{
  return x + 1;
}

#define BOTH(a, b) a += 1; b += lined(a);

int main(void)
{
  volatile int n = 0, m = 0;
  n += lined(bare_one(n));           /* the last call has lines, the first has none */
  n += bare_one(lined(n));           /* the last call has none */
  n += bare_one(n) + bare_two(n);    /* neither has */
  n += bare_two(bare_one(n));
  n += lined(n) + lined(n + 1);      /* both have */
  n += bare_call(lined, n);          /* code without lines that jumps into code with lines */
  n += lined(bare_call(lined, n));
  n += 1; m += 2; n += lined(m); m += bare_one(n);
  BOTH(n, m)
  for (int i = 0; i < 3; i += 1) n += lined(i);
#line 200
  n += 1;
#line 200
  m += lined(n);
#line 300
  return (n + m) & 0;
}
