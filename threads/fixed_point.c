/**
 * NOTE: [Part3] 고정소수점 연산에 필요한 로직
 *
 * 17.14 고정 소수점 숫자 표현을 사용합니다.
 */

#include <stdint.h>
#include "threads/fixed_point.h"

/**
 * @brief 정수를 고정 소수점 값으로 변환하는 함수
 *
 * @param n 고정 소수점 값으로 변환할 정수
 * @return fixed_point 변환된 고정 소수점 값
 */
fixed_point int_to_fp(int n)
{
    return n * F;
}

/**
 * @brief rounding toward zero 방식으로 고정 소수점 값을 정수로 변환하는 함수
 * rounding toward zero: 버림
 *
 * @param x 정수로 변환할 고정 소수점 값
 * @return int 변환된 정수
 */
int fp_to_int_round_zero(fixed_point x)
{
    return x / F;
}

/**
 * @brief rounding to nearest 방식으로 고정 소수점 값을 정수로 변환하는 함수
 * rounding to nearest: 반올림
 *
 * @param x 정수로 변환할 고정 소수점 값
 * @return int 변환된 정수
 */
int fp_to_int_round_near(fixed_point x)
{
    if (x >= 0)
        return (x + F / 2) / F;
    else
        return (x - F / 2) / F;
}

/**
 * @brief 고정 소수점 값을 더하는 함수
 *
 * @param x 첫 번째 고정 소수점 값
 * @param y 두 번째 고정 소수점 값
 * @return fixed_point 두 고정 소수점 값의 합
 */
fixed_point add_fp(fixed_point x, fixed_point y)
{
    return x + y;
}

/**
 * @brief 고정 소수점 값을 빼는 함수
 *
 * @param x 첫 번째 고정 소수점 값
 * @param y 두 번재 고정 소수점 값
 * @return fixed_point 두 고정 소수점 값의 차
 */
fixed_point sub_fp(fixed_point x, fixed_point y)
{
    return x - y;
}

/**
 * @brief 고정 소수점 값을 곱하는 함수
 *
 * @param x 첫 번째 고정 소수점 값
 * @param y 두 번째 고정 소수점 값
 * @return fixed_point 두 고정 소수점 값의 곱
 */
fixed_point mul_fp(fixed_point x, fixed_point y)
{
    return ((int64_t)x * y) / F;
}

/**
 * @brief 고정 소수점 값을 나누는 함수
 *
 * @param x 나눗셈에서 분자로 사용될 고정 소수점 값
 * @param y 나눗셈에서 분모로 사용될 고정 소수점 값
 * @return fixed_point 나눗셈의 결과로 얻어진 고정 소수점 값
 */
fixed_point div_fp(fixed_point x, fixed_point y)
{
    return ((int64_t)x * F) / y;
}
