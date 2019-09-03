#include <queue>
#include <mutex>
#include <atomic_wait>
#include <thread>
#include <latch>
#include <iostream>
#include <semaphore>
#include <functional>
#include <optional>

struct thread_group {
private:
  std::vector<std::thread> members;

public:
  thread_group(thread_group const&) = delete;
  thread_group& operator=(thread_group const&) = delete;

  ~thread_group() {
    for (auto& t : members) t.join();
  }

  template <typename Invocable>  
  thread_group(std::size_t count, Invocable f) {
    members.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
      members.emplace_back(std::thread(f));
    }  
  }
};

template <typename T, std::size_t N>
struct concurrent_bounded_queue {
private:
  std::queue<T> items;
  std::mutex items_mtx;
  std::counting_semaphore<N> items_produced  { 0 };
  std::counting_semaphore<N> remaining_space { N };

public:
  constexpr concurrent_bounded_queue() = default;

  // Enqueue one entry.
  template <typename U>
  void enqueue(U&& t)
  {
    remaining_space.acquire();
    {
      std::scoped_lock l(items_mtx);
      items.emplace(std::forward<decltype(t)>(t));            
    }
    items_produced.release();
  }

  // Attempt to dequeue one entry with a timeout.
  template <typename Rep, typename Period>
  std::optional<T> try_dequeue_for(
    std::chrono::duration<Rep, Period> const& rel_time
  ) {
    std::optional<T> tmp;
    if (!items_produced.try_acquire_for(rel_time))
      return tmp; 
    // This sleep is a stand-in for a log statement in the actual code. The bug
    // reproduces without the delay after acquiring and before locking, but it
    // reproduces much less frequently.
    std::this_thread::sleep_for(std::chrono::nanoseconds(500));
    {
      std::scoped_lock l(items_mtx);
      assert(!items.empty()); // <--- FIXME: This assert fails rarely when you
                              //             don't define __NO_SEM_FRONT.
      tmp = std::move(items.front());
      items.pop();
    }
    remaining_space.release();
    return tmp;
  }
};

int main() 
{
  std::atomic<int> count(0);

  concurrent_bounded_queue<std::function<void()>, 32> tasks;

  constexpr std::size_t num_runs = 20000;
  constexpr std::size_t num_enqueues_per_run = 256;

  for (std::size_t runs = 0; runs < num_runs; ++runs)
  {
    std::atomic<bool> stop_requested(false);

    constexpr std::size_t num_threads = 8;

    thread_group tg(num_threads,
      [&] 
      {
        while (!stop_requested.load(std::memory_order_relaxed)) {
          auto f = tasks.try_dequeue_for(std::chrono::milliseconds(1));
          if (f) {
            assert(*f);
            (*f)();
          }
        }
      }
    );
    
    for (std::size_t i = 0; i < num_enqueues_per_run; ++i)
      tasks.enqueue([&] { ++count; });

    std::latch l(num_threads + 1);
    for (std::size_t i = 0; i < num_threads; ++i)
      tasks.enqueue([&] { l.arrive_and_wait(); });
    l.arrive_and_wait();

    stop_requested.store(true, std::memory_order_relaxed);
  }

  assert(num_runs * num_enqueues_per_run == count);
}

