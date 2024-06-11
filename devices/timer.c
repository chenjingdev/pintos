#include "devices/timer.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include "threads/interrupt.h"
#include "threads/io.h"
#include "threads/synch.h"
#include "threads/thread.h"

/* See [8254] for hardware details of the 8254 timer chip. */

#if TIMER_FREQ < 19
#error 8254 timer requires TIMER_FREQ >= 19
#endif
#if TIMER_FREQ > 1000
#error TIMER_FREQ <= 1000 recommended
#endif

/* Number of timer ticks since OS booted. */
static int64_t ticks;

/* Number of loops per timer tick.
   Initialized by timer_calibrate(). */
static unsigned loops_per_tick;

static intr_handler_func timer_interrupt;
static bool too_many_loops(unsigned loops);
static void busy_wait(int64_t loops);
static void real_time_sleep(int64_t num, int32_t denom);

/**
 * @brief 8254 프로그래머블 인터벌 타이머(PIT)를 설정하여 초당 PIT_FREQ 번 인터럽트가 발생하도록 하고, 해당 인터럽트를 등록합니다.
 *
 * 이 함수는 8254 PIT를 설정하여 초당 PIT_FREQ 번 인터럽트가 발생하도록 합니다. 또한, 이에 해당하는 인터럽트를 시스템에 등록합니다.
 * 8254 PIT의 입력 주파수는 TIMER_FREQ로 나누어 가장 가까운 정수로 반올림됩니다.
 * 이 함수는 8254 타이머 인터럽트를 처리하는 데 필요한 초기 설정을 수행합니다.
 */
void timer_init(void)
{
	/* 8254 input frequency divided by TIMER_FREQ, rounded to
	   nearest. */
	uint16_t count = (1193180 + TIMER_FREQ / 2) / TIMER_FREQ;

	outb(0x43, 0x34); /* CW: counter 0, LSB then MSB, mode 2, binary. */
	outb(0x40, count & 0xff);
	outb(0x40, count >> 8);

	intr_register_ext(0x20, timer_interrupt, "8254 Timer"); /* 인터럽트 핸들러 등록 */
}

/* Calibrates loops_per_tick, used to implement brief delays. */
void timer_calibrate(void)
{
	unsigned high_bit, test_bit;

	ASSERT(intr_get_level() == INTR_ON);
	printf("Calibrating timer...  ");

	/* Approximate loops_per_tick as the largest power-of-two
	   still less than one timer tick. */
	loops_per_tick = 1u << 10;
	while (!too_many_loops(loops_per_tick << 1))
	{
		loops_per_tick <<= 1;
		ASSERT(loops_per_tick != 0);
	}

	/* Refine the next 8 bits of loops_per_tick. */
	high_bit = loops_per_tick;
	for (test_bit = high_bit >> 1; test_bit != high_bit >> 10; test_bit >>= 1)
		if (!too_many_loops(high_bit | test_bit))
			loops_per_tick |= test_bit;

	printf("%'" PRIu64 " loops/s.\n", (uint64_t)loops_per_tick * TIMER_FREQ);
}

/* Returns the number of timer ticks since the OS booted. */
int64_t
timer_ticks(void)
{
	enum intr_level old_level = intr_disable();
	int64_t t = ticks;
	intr_set_level(old_level);
	barrier();
	return t;
}

/**
 * @brief 타이머 틱으로 표현된 경과 시간을 계산합니다.
 *
 * 이 함수는 'then' 파라미터로 표현된 시간 이후에 경과한 타이머 틱의 수를 반환합니다.
 * 'then' 파라미터는 이전에 timer_ticks() 함수에 의해 반환된 값이어야 합니다.
 *
 * @param then 타이머 틱으로 표현된 이전 시간점.
 * @return int64_t 'then' 이후에 경과한 타이머 틱의 수.
 */
int64_t
timer_elapsed(int64_t then)
{
	return timer_ticks() - then;
}

/**
 * @brief 주어진 타이머 틱 수만큼 실행을 중지합니다.
 *
 * @param ticks 실행을 중지할 타이머 틱의 수.
 */
void timer_sleep(int64_t ticks)
{
	ASSERT(intr_get_level() == INTR_ON); /* 인터럽트가 켜져 있지 않으면 assert */
	thread_sleep(timer_ticks() + ticks); /* 현재 쓰레드를 일정 tick만큼 sleep 시키는 함수 호출 */
}

/* Suspends execution for approximately MS milliseconds. */
void timer_msleep(int64_t ms)
{
	real_time_sleep(ms, 1000);
}

/* Suspends execution for approximately US microseconds. */
void timer_usleep(int64_t us)
{
	real_time_sleep(us, 1000 * 1000);
}

/* Suspends execution for approximately NS nanoseconds. */
void timer_nsleep(int64_t ns)
{
	real_time_sleep(ns, 1000 * 1000 * 1000);
}

/* Prints timer statistics. */
void timer_print_stats(void)
{
	printf("Timer: %" PRId64 " ticks\n", timer_ticks());
}

/* Timer interrupt handler. */

/**
 * @brief 타이머 인터럽트 핸들러
 *
 * @param UNUSED 인터럽트 프레임 (현재 사용되지 않음)
 */
static void
timer_interrupt(struct intr_frame *args UNUSED)
{
	ticks++;
	thread_tick();

	/**
	 * NOTE: [Part3]
	 * - 4 tick마다 모든 쓰레드의 우선순위 재계산
	 * - 1 sec마다 load_avg, recent_cpu 재계산
	 */
	if (thread_mlfqs)
	{
		thread_incr_recent_cpu();

		if (timer_ticks() % 4 == 0)
			thread_all_calc_priority();

		if (timer_ticks() % TIMER_FREQ == 0)
		{
			calc_load_avg();
			thread_all_calc_recent_cpu();
		}
	}

	thread_wakeup(ticks); /* 지정된 틱 시간에 깨어날 스레드를 깨우는 함수 호출 */
}

/* Returns true if LOOPS iterations waits for more than one timer
   tick, otherwise false. */
static bool
too_many_loops(unsigned loops)
{
	/* Wait for a timer tick. */
	int64_t start = ticks;
	while (ticks == start)
		barrier();

	/* Run LOOPS loops. */
	start = ticks;
	busy_wait(loops);

	/* If the tick count changed, we iterated too long. */
	barrier();
	return start != ticks;
}

/* Iterates through a simple loop LOOPS times, for implementing
   brief delays.

   Marked NO_INLINE because code alignment can significantly
   affect timings, so that if this function was inlined
   differently in different places the results would be difficult
   to predict. */
static void NO_INLINE
busy_wait(int64_t loops)
{
	while (loops-- > 0)
		barrier();
}

/* Sleep for approximately NUM/DENOM seconds. */
static void
real_time_sleep(int64_t num, int32_t denom)
{
	/* Convert NUM/DENOM seconds into timer ticks, rounding down.

	   (NUM / DENOM) s
	   ---------------------- = NUM * TIMER_FREQ / DENOM ticks.
	   1 s / TIMER_FREQ ticks
	   */
	int64_t ticks = num * TIMER_FREQ / denom;

	ASSERT(intr_get_level() == INTR_ON);
	if (ticks > 0)
	{
		/* We're waiting for at least one full timer tick.  Use
		   timer_sleep() because it will yield the CPU to other
		   processes. */
		timer_sleep(ticks);
	}
	else
	{
		/* Otherwise, use a busy-wait loop for more accurate
		   sub-tick timing.  We scale the numerator and denominator
		   down by 1000 to avoid the possibility of overflow. */
		ASSERT(denom % 1000 == 0);
		busy_wait(loops_per_tick * num / 1000 * TIMER_FREQ / (denom / 1000));
	}
}
