/** @file

  FIFO queue

  @section license License

  Licensed to the Apache Software Foundation (ASF) under one
  or more contributor license agreements.  See the NOTICE file
  distributed with this work for additional information
  regarding copyright ownership.  The ASF licenses this file
  to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance
  with the License.  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

  @section details Details

  ProtectedQueue implements a FIFO queue with the following functionality:
    -# Multiple threads could be simultaneously trying to enqueue and
      dequeue. Hence the queue needs to be protected with mutex.
    -# In case the queue is empty, dequeue() sleeps for a specified amount
      of time, or until a new element is inserted, whichever is earlier.

*/

#include "P_EventSystem.h"
#include <thread>
// The protected queue is designed to delay signaling of threads
// until some amount of work has been completed on the current thread
// in order to prevent excess context switches.
//
// Defining EAGER_SIGNALLING disables this behavior and causes
// threads to be made runnable immediately.
//
// #define EAGER_SIGNALLING

extern ClassAllocator<Event> eventAllocator;

void
#ifdef TS_USE_DLB
ProtectedQueue::enqueue(Event *e, dlb_port_hdl_t port)
#else
ProtectedQueue::enqueue(Event *e)
#endif
{
  //printf("Enqueue: %p\n", e);
  ink_assert(!e->in_the_prot_queue && !e->in_the_priority_queue);
  EThread *e_ethread   = e->ethread;
  e->in_the_prot_queue = 1;
#ifdef TS_USE_DLB
  bool was_empty = dlb_q->enqueue(e, port);
#else
  bool was_empty       = (ink_atomiclist_push(&al, e) == nullptr);
#endif

  if (was_empty) {
    EThread *inserting_thread = this_ethread();
    // queue e->ethread in the list of threads to be signalled
    // inserting_thread == 0 means it is not a regular EThread
    if (inserting_thread != e_ethread) {
      e_ethread->tail_cb->signalActivity();
    }
  }
}

void
ProtectedQueue::dequeue_external()
{
#ifdef TS_USE_DLB
  Event *e;
  dlb_q->prepare_dequeue();
  int elem_recv = dlb_q->get_rx_elem();

  for(int i = 0; i < elem_recv; i++)
  {
    e = dlb_q->dequeue_external(i);
    //JSJS printf("t: %d q: %d d: %p\n", std::this_thread::get_id(), dlb_q->get_queue_id(), e);
    if(!e->cancelled)
      localQueue.enqueue(e);
    else
    {
      e->mutex = nullptr;
      eventAllocator.free(e);
    }
  }
#else
  Event *e = static_cast<Event *>(ink_atomiclist_popall(&al));
  // invert the list, to preserve order
  SLL<Event, Event::Link_link> l, t;
  t.head = e;
  while ((e = t.pop())) {
    l.push(e);
  }
  // insert into localQueue
  while ((e = l.pop())) {
    if (!e->cancelled) {
      localQueue.enqueue(e);
    } else {
      e->mutex = nullptr;
      eventAllocator.free(e);
    }
  }
#endif
}

void
ProtectedQueue::wait(ink_hrtime timeout)
{
  /* If there are no external events available, will do a cond_timedwait.
   *
   *   - The `EThread::lock` will be released,
   *   - And then the Event Thread goes to sleep and waits for the wakeup signal of `EThread::might_have_data`,
   *   - The `EThread::lock` will be locked again when the Event Thread wakes up.
   */
#ifdef TS_USE_DLB
  if(dlb_q->is_empty() && localQueue.empty()) {
#else
  if (INK_ATOMICLIST_EMPTY(al) && localQueue.empty()) {
#endif
    timespec ts = ink_hrtime_to_timespec(timeout);
    ink_cond_timedwait(&might_have_data, &lock, &ts);
  }
}
