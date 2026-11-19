// An enum with so many names that their list continues in a second and a third one. Apart from
// kinds.cpp so that file stays small enough to be a fuzzing seed.
#define E1(p) p##0, p##1, p##2, p##3, p##4, p##5, p##6, p##7,
#define E2(p) E1(p##0) E1(p##1) E1(p##2) E1(p##3) E1(p##4) E1(p##5) E1(p##6) E1(p##7)
#define E3(p) E2(p##0) E2(p##1) E2(p##2) E2(p##3) E2(p##4) E2(p##5) E2(p##6) E2(p##7)
#define E4(p) E3(p##0) E3(p##1) E3(p##2) E3(p##3) E3(p##4) E3(p##5) E3(p##6) E3(p##7)
enum Long { E4(a_long_enumerator_name_) };
int main() { Long l = a_long_enumerator_name_0000; return l; }
