/** @file

  A brief file description

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
 */

/****************************************************************************

  Protected Queue


 ****************************************************************************/
#pragma once

#include "I_EventSystem.h"

TS_INLINE
ProtectedQueue::ProtectedQueue()
{
  Event e;
  ink_mutex_init(&lock);
#if TS_USE_DLB
    dlb_q = nullptr;
#else
  ink_atomiclist_init(&al, "ProtectedQueue", (char *)&e.link.next - (char *)&e);
#endif
  ink_cond_init(&might_have_data);
}

#if TS_USE_DLB
TS_INLINE void
ProtectedQueue::init_queue()
{
  printf("Get dlb queue\t");
  dlb_q = IDLB::get_dlb_queue();
}

TS_INLINE void
ProtectedQueue::port_init()
{
  for(auto i = 0; i < IDLB::DLB_device::dlb_dev_ctr; i++)
    dlb_port.push_back(IDLB::get_tx_port(i));
}
#endif

TS_INLINE
ProtectedQueue::~ProtectedQueue()
{
#if TS_USE_DLB
	IDLB::push_back_dlb_queue(&dlb_q);
#endif
}

TS_INLINE void
ProtectedQueue::signal()
{
  // Need to get the lock before you can signal the thread
  ink_mutex_acquire(&lock);
  ink_cond_signal(&might_have_data);
  ink_mutex_release(&lock);
}

TS_INLINE int
ProtectedQueue::try_signal()
{
  // Need to get the lock before you can signal the thread
  if (ink_mutex_try_acquire(&lock)) {
    ink_cond_signal(&might_have_data);
    ink_mutex_release(&lock);
    return 1;
  } else {
    return 0;
  }
}

// Called from the same thread (don't need to signal)
TS_INLINE void
ProtectedQueue::enqueue_local(Event *e)
{
  ink_assert(!e->in_the_prot_queue && !e->in_the_priority_queue);
  e->in_the_prot_queue = 1;
  localQueue.enqueue(e);
}

TS_INLINE void
ProtectedQueue::remove(Event *e)
{
  #ifndef TS_USE_DLB
  ink_assert(e->in_the_prot_queue);
  if (!ink_atomiclist_remove(&al, e)) {
    localQueue.remove(e);
  }
  e->in_the_prot_queue = 0;
  #endif
}

TS_INLINE Event *
ProtectedQueue::dequeue_local()
{
  Event *e = localQueue.dequeue();
  if (e) {
    ink_assert(e->in_the_prot_queue);
    e->in_the_prot_queue = 0;
  }
  return e;
}
