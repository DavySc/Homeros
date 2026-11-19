// Every kind of type record and member the converter makes, in one file: built by make_pdbs.sh
// with clang, and by the Windows recorder with MSVC, whose file RAD then converts.
enum class Colour : unsigned char { red, green };
enum Plain { one = 1 };
union Either { int i; float f; };
struct Bits { unsigned a : 3; unsigned b : 5; long long c : 40; };
class Box {
public:
  int v;
  int get() const { return v; }
  int lref() & { return v; }
  int rref() && { return v; }
  static int make(int x) { return x; }
  int many(int n, ...) { return n + v; }
  volatile int *vp;
};
struct Later;
struct Unknown;
union UnknownUnion;
class UnknownClass;
enum class UnknownEnum : short;
typedef int (*Fn)(int, const char *, ...);
typedef int Box::*Member;
typedef int (Box::*Method)(void) const;
int use(Box &b, Box &&r, const Box *const cp, int *__restrict rp, volatile Either *e, Bits bits, Colour c, Plain p, Fn fn, Member m, Method me,
        Later *l, Unknown *u, UnknownUnion *uu, UnknownClass *uc, UnknownEnum *ue, signed char sc, unsigned short us, long lo, unsigned long ul, long long ll,
        unsigned long long ull, float fl, unsigned char uch, double grid[3][4], const volatile short cv, wchar_t w, char16_t c16, char32_t c32, bool flag, void *any, decltype(nullptr) np)
{
  return b.get() + b.lref() + static_cast<Box &&>(r).rref() + Box::make(cp->v) + *rp + e->i + bits.a + (int)c + p + fn(1, "") + b.*m + (b.*me)()
         + (l != 0) + (u != 0) + (uu != 0) + (uc != 0) + (ue != 0) + sc + us + lo + (int)ul + (int)ll + (int)ull + (int)fl + uch + (int)grid[1][2] + cv + w + c16 + (int)c32 + flag + (any != 0) + (np == nullptr);
}
struct Later { int x; Later *next; };
// two types with one name: a use of either must mean its own
int first() { struct Local { int a; }; Local l = {1}; Local *p = &l; return p->a; }
int second() { struct Local { int a, b; }; Local l = {1, 2}; Local *p = &l; return p->b; }
#ifdef __clang__   // built without the C runtime, which has these
extern "C" int _fltused = 0;               // the linker wants it once a float is used
extern "C" int _purecall() { return 0; }   // what a call of a virtual method without a body reaches
#endif
// every kind of member: bases, a virtual base seen directly and through a base, static data, nested types,
// one name with several methods, virtual methods new and overriding, with and without a body
struct Base { int base; virtual int intro() { return 1; } virtual int pure() = 0; };
struct Left : virtual Base { int left; int pure() override { return 2; } };
struct Right : virtual Base { int right; };
struct Bottom : Left, Right {
  static int shared;
  struct Inner { int in; };
  typedef int Alias;
  int over(int a) { return a; }
  int over(int a, int b) { return a + b; }
  static int over(char c) { return c; }
  int intro() override { return 3; }
  Inner inner;
};
int Bottom::shared = 1;
// values of every width a record can hold, and so many names that their list continues in a second one
enum Wide : long long { tiny = -1, small = -1000, medium = -100000, huge = -5000000000, top = 0x7fffffffffffffff, u16 = 0x8000, u32 = 0x80000000 };
enum UWide : unsigned long long { all = 0xffffffffffffffff };
enum Empty {};
enum Signed { minus_one = -1, minus_byte = -128, minus_short = -32768, minus_long = -2147483647 - 1 };
struct Abstract : Base { int intro() override = 0; };
struct Concrete : Abstract { int intro() override { return 4; } int pure() override { return 5; } };
#define E1(p) p##0, p##1, p##2, p##3, p##4, p##5, p##6, p##7,
#define E2(p) E1(p##0) E1(p##1) E1(p##2) E1(p##3) E1(p##4) E1(p##5) E1(p##6) E1(p##7)
#define E3(p) E2(p##0) E2(p##1) E2(p##2) E2(p##3) E2(p##4) E2(p##5) E2(p##6) E2(p##7)
#define E4(p) E3(p##0) E3(p##1) E3(p##2) E3(p##3) E3(p##4) E3(p##5) E3(p##6) E3(p##7)
enum Long { E4(a_long_enumerator_name_) };
int main()
{
  Later l = {1, 0}; Box b; b.v = 0; Bottom bottom; Bottom::Alias alias = Bottom::over('a'); Wide wide = tiny; UWide uwide = all; Signed sg = minus_one; Empty empty = Empty(); Concrete concrete; Long lng = a_long_enumerator_name_0000;
  return l.x + first() + second() + b.many(1, 2) + bottom.over(1) + bottom.over(1, 2) + bottom.intro() + bottom.pure() + alias + (int)wide + (int)uwide + sg + empty + concrete.intro() + lng;
}
