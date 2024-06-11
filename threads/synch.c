/* This file is derived from source code for the Nachos
   instructional operating system.  The Nachos copyright notice
   is reproduced in full below. */

/* Copyright (c) 1992-1996 The Regents of the University of California.
   All rights reserved.

   Permission to use, copy, modify, and distribute this software
   and its documentation for any purpose, without fee, and
   without written agreement is hereby granted, provided that the
   above copyright notice and the following two paragraphs appear
   in all copies of this software.

   IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO
   ANY PARTY FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR
   CONSEQUENTIAL DAMAGES ARISING OUT OF THE USE OF THIS SOFTWARE
   AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY OF CALIFORNIA
   HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY
   WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
   PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS"
   BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATION TO
   PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR
   MODIFICATIONS.
   */

#include "threads/synch.h"
#include <stdio.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

static bool cmp_priority_donation(const struct list_elem *a_, const struct list_elem *b_, void *aux UNUSED);
static bool cmp_priority_by_sema(const struct list_elem *a_, const struct list_elem *b_, void *aux UNUSED);
static void priority_donation(struct thread *t);
static bool is_waiter(struct list_elem *e, struct lock *lock);

/* Initializes semaphore SEMA to VALUE.  A semaphore is a
   nonnegative integer along with two atomic operators for
   manipulating it:

   - down or "P": wait for the value to become positive, then
   decrement it.

   - up or "V": increment the value (and wake up one waiting
   thread, if any). */
void sema_init(struct semaphore *sema, unsigned value)
{
	ASSERT(sema != NULL);
	sema->value = value;
	list_init(&sema->waiters);
}

/* Down or "P" operation on a semaphore.  Waits for SEMA's value
   to become positive and then atomically decrements it.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but if it sleeps then the next scheduled
   thread will probably turn interrupts back on. This is
   sema_down function. */
void sema_down(struct semaphore *sema)
{
	enum intr_level old_level;

	ASSERT(sema != NULL);
	ASSERT(!intr_context());

	old_level = intr_disable();
	while (sema->value == 0)
	{
		/**
		 * NOTE: 우선순위 순으로 waiters에 삽입
		 * part: priority-sync
		 */
		list_insert_ordered(&sema->waiters, &thread_current()->elem, cmp_priority, NULL);
		thread_block();
	}
	sema->value--;
	intr_set_level(old_level);
}

/* Down or "P" operation on a semaphore, but only if the
   semaphore is not already 0.  Returns true if the semaphore is
   decremented, false otherwise.

   This function may be called from an interrupt handler. */
bool sema_try_down(struct semaphore *sema)
{
	enum intr_level old_level;
	bool success;

	ASSERT(sema != NULL);

	old_level = intr_disable();
	if (sema->value > 0)
	{
		sema->value--;
		success = true;
	}
	else
		success = false;
	intr_set_level(old_level);

	return success;
}

/* Up or "V" operation on a semaphore.  Increments SEMA's value
   and wakes up one thread of those waiting for SEMA, if any.

   This function may be called from an interrupt handler. */
void sema_up(struct semaphore *sema)
{
	enum intr_level old_level;

	ASSERT(sema != NULL);

	old_level = intr_disable();

	if (!list_empty(&sema->waiters))
	{
		/**
		 * NOTE: waiters 정렬
		 * part: priority-sync
		 */
		list_sort(&sema->waiters, cmp_priority, NULL);
		thread_unblock(list_entry(list_pop_front(&sema->waiters),
								  struct thread, elem));
	}

	sema->value++;
	thread_preempt();
	intr_set_level(old_level);
}

static void sema_test_helper(void *sema_);

/* Self-test for semaphores that makes control "ping-pong"
   between a pair of threads.  Insert calls to printf() to see
   what's going on. */
void sema_self_test(void)
{
	struct semaphore sema[2];
	int i;

	printf("Testing semaphores...");
	sema_init(&sema[0], 0);
	sema_init(&sema[1], 0);
	thread_create("sema-test", PRI_DEFAULT, sema_test_helper, &sema);
	for (i = 0; i < 10; i++)
	{
		sema_up(&sema[0]);
		sema_down(&sema[1]);
	}
	printf("done.\n");
}

/* Thread function used by sema_self_test(). */
static void
sema_test_helper(void *sema_)
{
	struct semaphore *sema = sema_;
	int i;

	for (i = 0; i < 10; i++)
	{
		sema_down(&sema[0]);
		sema_up(&sema[1]);
	}
}

/* Initializes LOCK.  A lock can be held by at most a single
   thread at any given time.  Our locks are not "recursive", that
   is, it is an error for the thread currently holding a lock to
   try to acquire that lock.

   A lock is a specialization of a semaphore with an initial
   value of 1.  The difference between a lock and such a
   semaphore is twofold.  First, a semaphore can have a value
   greater than 1, but a lock can only be owned by a single
   thread at a time.  Second, a semaphore does not have an owner,
   meaning that one thread can "down" the semaphore and then
   another one "up" it, but with a lock the same thread must both
   acquire and release it.  When these restrictions prove
   onerous, it's a good sign that a semaphore should be used,
   instead of a lock. */
void lock_init(struct lock *lock)
{
	ASSERT(lock != NULL);

	lock->holder = NULL;
	sema_init(&lock->semaphore, 1);
}

/* Acquires LOCK, sleeping until it becomes available if
   necessary.  The lock must not already be held by the current
   thread.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void lock_acquire(struct lock *lock)
{
	ASSERT(lock != NULL);
	ASSERT(!intr_context());
	ASSERT(!lock_held_by_current_thread(lock));

	/* NOTE: [Part2-3] lock을 사용할 수 없는 경우 우선순위 상속 */
	if (!lock_try_acquire(lock))
	{
		thread_current()->wait_on_lock = lock;

		/* NOTE: [Part3] MLFQ 사용 시 priority donation 사용 금지 */
		if (!thread_mlfqs)
		{
			list_insert_ordered(&lock->holder->donations, &thread_current()->d_elem, cmp_priority_donation, NULL);
			priority_donation(lock->holder);
		}

		sema_down(&lock->semaphore);
		lock->holder = thread_current();
	}

	/* 할당 완료 시 wait_on_lock NULL로 초기화 */
	lock->holder->wait_on_lock = NULL;
}

/* Tries to acquires LOCK and returns true if successful or false
   on failure.  The lock must not already be held by the current
   thread.

   This function will not sleep, so it may be called within an
   interrupt handler. */
bool lock_try_acquire(struct lock *lock)
{
	bool success;

	ASSERT(lock != NULL);
	ASSERT(!lock_held_by_current_thread(lock));

	success = sema_try_down(&lock->semaphore);
	if (success)
		lock->holder = thread_current();
	return success;
}

/* Releases LOCK, which must be owned by the current thread.
   This is lock_release function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to release a lock within an interrupt
   handler. */
void lock_release(struct lock *lock)
{
	ASSERT(lock != NULL);
	ASSERT(lock_held_by_current_thread(lock));

	/* NOTE: [Part3] MLFQ 사용 시 priority donation 사용 금지 */
	if (!thread_mlfqs)
	{
		/* NOTE: [Part2-3] lock 해제 시 우선순위 재설정 */
		struct list *donations = &lock->holder->donations;
		if (!list_empty(donations))
		{
			struct list_elem *e = list_front(donations);
			while (e != list_end(donations))
			{
				if (is_waiter(e, lock))
					e = list_remove(e);
				else
					e = list_next(e);
			}
		}
		list_sort(donations, cmp_priority_donation, NULL);
		priority_donation(lock->holder);
	}

	lock->holder = NULL;
	sema_up(&lock->semaphore);
}

/* Returns true if the current thread holds LOCK, false
   otherwise.  (Note that testing whether some other thread holds
   a lock would be racy.) */
bool lock_held_by_current_thread(const struct lock *lock)
{
	ASSERT(lock != NULL);

	return lock->holder == thread_current();
}

/* One semaphore in a list. */
struct semaphore_elem
{
	struct list_elem elem;		/* List element. */
	struct semaphore semaphore; /* This semaphore. */
};

/* Initializes condition variable COND.  A condition variable
   allows one piece of  code to signal a condition and cooperating
   code to receive the signal and act upon it. */
void cond_init(struct condition *cond)
{
	ASSERT(cond != NULL);

	list_init(&cond->waiters);
}

/* Atomically releases LOCK and waits for COND to be signaled by
   some other piece of code.  After COND is signaled, LOCK is
   reacquired before returning.  LOCK must be held before calling
   this function.

   The monitor implemented by this function is "Mesa" style, not
   "Hoare" style, that is, sending and receiving a signal are not
   an atomic operation.  Thus, typically the caller must recheck
   the condition after the wait completes and, if necessary, wait
   again.

   A given condition variable is associated with only a single
   lock, but one lock may be associated with any number of
   condition variables.  That is, there is a one-to-many mapping
   from locks to condition variables.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void cond_wait(struct condition *cond, struct lock *lock)
{
	struct semaphore_elem waiter;

	ASSERT(cond != NULL);
	ASSERT(lock != NULL);
	ASSERT(!intr_context());
	ASSERT(lock_held_by_current_thread(lock));

	sema_init(&waiter.semaphore, 0);
	/**
	 * NOTE: 우선순위 순으로 waiters에 삽입
	 * part: priority-sync
	 */
	list_insert_ordered(&cond->waiters, &waiter.elem, cmp_priority_by_sema, NULL);
	lock_release(lock);
	sema_down(&waiter.semaphore);
	lock_acquire(lock);
}

/* If any threads are waiting on COND (protected by LOCK), then
   this function signals one of them to wake up from its wait.
   LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void cond_signal(struct condition *cond, struct lock *lock UNUSED)
{
	ASSERT(cond != NULL);
	ASSERT(lock != NULL);
	ASSERT(!intr_context());
	ASSERT(lock_held_by_current_thread(lock));

	/**
	 * NOTE: waiters 정렬
	 * part: priority-sync
	 */
	list_sort(&cond->waiters, cmp_priority_by_sema, NULL);
	if (!list_empty(&cond->waiters))
		sema_up(&list_entry(list_pop_front(&cond->waiters),
							struct semaphore_elem, elem)
					 ->semaphore);
}

/* Wakes up all threads, if any, waiting on COND (protected by
   LOCK).  LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void cond_broadcast(struct condition *cond, struct lock *lock)
{
	ASSERT(cond != NULL);
	ASSERT(lock != NULL);

	while (!list_empty(&cond->waiters))
		cond_signal(cond, lock);
}

/**
 * @brief 두 쓰레드의 우선순위를 비교하는 함수
 *
 * @param a_ 첫 번째 리스트 요소
 * @param b_ 두 번째 리스트 요소
 * @param UNUSED 사용하지 않는 매개변수
 * @return true 첫 번째 쓰레드의 우선순위가 두 번째 쓰레드보다 높은 경우 true 반환
 * @return false 그렇지 않은 경우 false 반환
 */
static bool cmp_priority_donation(const struct list_elem *a_, const struct list_elem *b_, void *aux UNUSED)
{
	struct thread *a = list_entry(a_, struct thread, d_elem);
	struct thread *b = list_entry(b_, struct thread, d_elem);

	return a->priority > b->priority;
}

/**
 * @brief 두 세마포어의 우선순위를 비교하는 함수
 *
 * @param a_ 첫 번째 리스트 요소
 * @param b_ 두 번째 리스트 요소
 * @param UNUSED 사용하지 않는 매개변수
 * @return true 첫 번째 세마포어의 우선순위가 두 번째 세마포어보다 높은 경우 true 반환
 * @return false 그렇지 않은 경우 false 반환
 */
static bool cmp_priority_by_sema(const struct list_elem *a_, const struct list_elem *b_, void *aux UNUSED)
{
	struct semaphore_elem *a_sema = list_entry(a_, struct semaphore_elem, elem);
	struct semaphore_elem *b_sema = list_entry(b_, struct semaphore_elem, elem);

	/* semaphore.waiters 리스트가 빈 리스트인 경우 예외처리 */
	if (!list_empty(&a_sema->semaphore.waiters) && !list_empty(&b_sema->semaphore.waiters))
	{
		cmp_priority(list_front(&a_sema->semaphore.waiters), list_front(&b_sema->semaphore.waiters), NULL);
	}
	else
		return false;
}

/**
 * @brief 우선순위를 기부받고, 필요한 경우 우선순위 기부를 재귀적으로 수행하는 함수
 *
 * @param t 우선순위를 기부받을 쓰레드
 */
static void priority_donation(struct thread *t)
{
	ASSERT(t != NULL);

	/* 리스트가 비어있는 경우 - 원래의 우선순위로 복구 */
	if (list_empty(&t->donations))
		t->priority = t->origin_priority;
	/* 그렇지 않은 경우 - donations 중 우선순위가 가장 높은 쓰레드에게 우선순위 기부 받기 */
	else
	{
		struct thread *highest = list_entry(list_front(&t->donations), struct thread, d_elem);
		/* NOTE: [Improve] 본인의 기존 우선순위와 비교 필요 */
		if (t->origin_priority < highest->priority)
			t->priority = highest->priority;
		else
			t->priority = t->origin_priority;
	}

	/* 현재 쓰레드가 락을 기다리고 있는 경우, 락의 소유자에게 재귀적으로 우선순위 기부 */
	if (t->wait_on_lock)
		priority_donation(t->wait_on_lock->holder);
}

/**
 * @brief 주어진 리스트 요소가 특정 lock을 대기하고 있는 중인지 확인하는 함수
 *
 * @param e 검사할 리스트 요소. 이 요소는 쓰레드로 변환된다.
 * @param lock 쓰레드가 대기하고 있는지 확인할 락.
 * @return true 해당 쓰레드가 주어진 락을 대기하고 있는 경우 true 반환
 * @return false 그렇지 않은 경우 false 반환
 */
static bool is_waiter(struct list_elem *e, struct lock *lock)
{
	struct thread *t = list_entry(e, struct thread, d_elem);
	return t->wait_on_lock == lock;
}
