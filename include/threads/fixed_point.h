/**
 * NOTE: [Part3] 고정소수점 연산에 필요한 로직
 *
 * 17.14 고정 소수점 숫자 표현을 사용합니다.
 */

#define F (1 << 14) /* 1 in 17.14 format */

typedef int32_t fixed_point; /* 고정 소수점을 나타내는 타입 */

fixed_point int_to_fp(int n);
int fp_to_int_round_zero(fixed_point x);
int fp_to_int_round_near(fixed_point x);
fixed_point add_fp(fixed_point x, fixed_point y);
fixed_point sub_fp(fixed_point x, fixed_point y);
fixed_point mul_fp(fixed_point x, fixed_point y);
fixed_point div_fp(fixed_point x, fixed_point y);
