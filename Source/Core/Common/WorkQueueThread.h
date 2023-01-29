// Copyright 2017 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <functional>
#include <queue>
#include <thread>

#include "Common/Event.h"
#include "Common/Flag.h"
#include "Common/Thread.h"

// A thread that executes the given function for every item placed into its queue.

namespace Common
{
template <typename T>
class WorkQueueThread
{
public:
  WorkQueueThread() = default;
  WorkQueueThread(std::function<void(T)> function) { Reset(std::move(function)); }
  ~WorkQueueThread() { Shutdown(); }
  void Reset(std::function<void(T)> function)
  {
    Shutdown();
    m_shutdown.Clear();
    m_cancelled.Clear();
    m_function = std::move(function);
    m_thread = std::thread(&WorkQueueThread::ThreadLoop, this);
  }

  template <typename... Args>
  void EmplaceItem(Args&&... args)
  {
    if (!m_cancelled.IsSet())
    {
      std::lock_guard lg(m_lock);
      m_items.emplace(std::forward<Args>(args)...);
    }
    m_wakeup.Set();
  }

  void Push(T&& item)
  {
    if (!m_cancelled.IsSet())
    {
      std::lock_guard lg(m_lock);
      m_items.push(item);
    }
    m_wakeup.Set();
  }

  void Push(const T& item)
  {
    if (!m_cancelled.IsSet())
    {
      std::lock_guard lg(m_lock);
      m_items.push(item);
    }
    m_wakeup.Set();
  }

  void Clear()
  {
    {
      std::lock_guard lg(m_lock);
      m_items = std::queue<T>();
    }
    m_wakeup.Set();
  }

  void Cancel()
  {
    m_cancelled.Set();
    Clear();
    Shutdown();
  }

  bool IsCancelled() const { return m_cancelled.IsSet(); }

  void Shutdown()
  {
    if (m_thread.joinable())
    {
      m_shutdown.Set();
      m_wakeup.Set();
      m_thread.join();
    }
  }

  // Doesn't return until the most recent function invocation has finished.
  void FlushOne()
  {
    if (m_thread.joinable())
    {
      m_flush.Set();
      Clear();
      m_flushed.Wait();
    }
  }

  // Doesn't return until the queue is empty.
  void Flush()
  {
    if (m_thread.joinable())
    {
      m_flush.Set();
      m_wakeup.Set();
      m_flushed.Wait();
    }
  }

  bool IsFlushing() const { return m_flush.IsSet() || m_shutdown.IsSet(); }

private:
  void ThreadLoop()
  {
    Common::SetCurrentThreadName("WorkQueueThread");

    while (true)
    {
      m_wakeup.Wait();

      while (true)
      {
        std::unique_lock lg(m_lock);
        if (m_items.empty())
        {
          if (m_flush.IsSet())
          {
            m_flush.Clear();
            m_flushed.Set();
          }
          break;
        }
        T item{std::move(m_items.front())};
        m_items.pop();
        lg.unlock();

        m_function(std::move(item));
      }

      if (m_shutdown.IsSet())
        break;
    }
  }

  std::function<void(T)> m_function;
  std::thread m_thread;
  Common::Event m_wakeup;
  Common::Flag m_shutdown;
  Common::Flag m_cancelled;
  Common::Flag m_flush;
  Common::Event m_flushed;
  std::mutex m_lock;
  std::queue<T> m_items;
};

}  // namespace Common
