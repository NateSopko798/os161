/*
 * Copyright (c) 2001, 2002, 2009
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Driver code is in kern/tests/synchprobs.c We will replace that file. This
 * file is yours to modify as you see fit.
 *
 * You should implement your solution to the stoplight problem below. The
 * quadrant and direction mappings for reference: (although the problem is, of
 * course, stable under rotation)
 *
 *   |0 |
 * -     --
 *    01  1
 * 3  32
 * --    --
 *   | 2|
 *
 * As way to think about it, assuming cars drive on the right: a car entering
 * the intersection from direction X will enter intersection quadrant X first.
 * The semantics of the problem are that once a car enters any quadrant it has
 * to be somewhere in the intersection until it call leaveIntersection(),
 * which it should call while in the final quadrant.
 *
 * As an example, let's say a car approaches the intersection and needs to
 * pass through quadrants 0, 3 and 2. Once you call inQuadrant(0), the car is
 * considered in quadrant 0 until you call inQuadrant(3). After you call
 * inQuadrant(2), the car is considered in quadrant 2 until you call
 * leaveIntersection().
 *
 * You will probably want to write some helper functions to assist with the
 * mappings. Modular arithmetic can help, e.g. a car passing straight through
 * the intersection entering from direction X will leave to direction (X + 2)
 * % 4 and pass through quadrants X and (X + 3) % 4.  Boo-yah.
 *
 * Your solutions below should call the inQuadrant() and leaveIntersection()
 * functions in synchprobs.c to record their progress.
 */

#include <types.h>
#include <lib.h>
#include <thread.h>
#include <test.h>
#include <synch.h>

struct lock *lk_first;
struct lock *lk_second;
struct lock *lk_third;
struct lock *lk_fourth;
struct semaphore *sem;
int count;

static struct lock* quadrant_to_lock(int quadrant){
	switch(quadrant){
		case 1:
			return lk_first;
		case 2:
                        return lk_second;
		case 3:
                        return lk_third;
		default:
			return lk_fourth;
	}
}


static int next_quadrant_straight(int x){
	return (x + 3) % 4;
}

/*
 * Called by the driver during initialization.
 */

void
stoplight_init() {
	lk_first = lock_create("1");
	lk_second = lock_create("2");
	lk_third = lock_create("3");
	lk_fourth = lock_create("4");
	sem = sem_create("Intersection", 3);
	count = 0;
	return;
}

/*
 * Called by the driver during teardown.
 */

void stoplight_cleanup() {
	lock_destroy(lk_first);
	lock_destroy(lk_second);
	lock_destroy(lk_third);
	lock_destroy(lk_fourth);
	sem_destroy(sem);
	return;
}

void
turnright(uint32_t direction, uint32_t index)
{
	P(sem);
	lock_acquire(quadrant_to_lock(direction));
	inQuadrant(direction, index);
	leaveIntersection(index);
	lock_release(quadrant_to_lock(direction));
	V(sem);
	return;
}
void
gostraight(uint32_t direction, uint32_t index)
{
	P(sem);
        lock_acquire(quadrant_to_lock(direction));
        inQuadrant(direction, index);
	lock_acquire(quadrant_to_lock(next_quadrant_straight(direction)));
	inQuadrant(next_quadrant_straight(direction), index);
        lock_release(quadrant_to_lock(direction));
        leaveIntersection(index);
        lock_release(quadrant_to_lock(next_quadrant_straight(direction)));
        V(sem);
	return;
}
void
turnleft(uint32_t direction, uint32_t index)
{
	P(sem);
	int pass = (direction + 3) % 4;
	int exit = (direction + 2) % 4;
        lock_acquire(quadrant_to_lock(direction));
        inQuadrant(direction, index);
	lock_acquire(quadrant_to_lock(pass));
	inQuadrant(pass, index);
        lock_release(quadrant_to_lock(direction));
	lock_acquire(quadrant_to_lock(exit));
	inQuadrant(exit, index);
        lock_release(quadrant_to_lock(pass));
	leaveIntersection(index);
        lock_release(quadrant_to_lock(exit));
        V(sem);
	return;
}
