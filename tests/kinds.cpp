// Every kind of type record the converter makes, in one file, for make_pdbs.sh (clang only: the
// Windows recorder takes subjects/*.c). Never run.
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
extern "C" int _fltused = 0;   // the linker wants it once a float is used
int main() { Later l = {1, 0}; Box b; b.v = 0; return l.x + first() + second() + b.many(1, 2); }
