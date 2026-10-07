#pragma once

#include <gi/gi.hpp>

#include <coroutine>
#include <future>

#ifdef CO_DEBUG
#include <iostream>
static auto &dout = std::cerr;
#else
#include <sstream>
static std::ostringstream dout;
#endif

template<typename T, typename SELF>
struct holder
{
  void return_value(T &&v)
  {
    dout << "return value " << std::endl;
    auto self = (SELF *)this;
    self->set_value(std::move(v));
  }
};

template<typename SELF>
struct holder<void, SELF>
{
  void return_void()
  {
    auto self = (SELF *)this;
    self->set_value();
  }
};

template<typename RESULT>
class co_promise : public std::promise<RESULT>
{
  using self_type = co_promise;
  // some protection for a coroutine_handle<>
  // so we do not try to access one that may be invalid/dangling

  // `this` is alive as long as the co_await expression is alive (suspended)
  // so this can be passed in raw form for the lifetime of `waiter` below
  // conversely, as long as a `waiter` is alive, it tracks a refcount in count_
  // so, if the count is positive, the handle of the suspended coroutine is ok
  // if not, the objects in the expression of the suspended coroutine could
  // no longer be around either

  int count_ = 0;
  std::future<RESULT> result_;
  std::coroutine_handle<> waiter_;

public:
  struct waiter
  {
    self_type *target_;

    waiter(self_type *t) : target_(t) { ++target_->count_; }
    ~waiter() { --target_->count_; }

    self_type &w() const { return *target_; }

    bool await_ready() const
    {
      return w().result_.wait_for(std::chrono::seconds(0)) ==
             std::future_status::ready;
    }
    void await_suspend(std::coroutine_handle<> handle)
    {
      assert(!w().waiter_);
      w().waiter_ = handle;
    }
    RESULT await_resume()
    {
      // should have value now, or may hang otherwise
      assert(await_ready());
      return w().result_.get();
    }
  };

  co_promise() { result_ = this->get_future(); }

  void reset() { *this = self_type(); }

  waiter operator co_await() { return waiter{this}; }

  bool resume()
  {
    auto w = coro();
    // waiter takes care of itself again
    waiter_ = nullptr;
    w.resume();
    return bool(w);
  }

  std::coroutine_handle<> coro(bool check = true)
  {
    auto handle = waiter_;
    return (count_ || !check) && handle ? handle : std::noop_coroutine();
  }
};

// task represents an owning handle of a coroutine
// in particular, the task can be cancelled by mere destruction
// however, this is generally a bad idea (sort of equivalent to pthread_cancel),
// as such, some checks serve to check/discourage this (by default)
template<typename RESULT, bool CANCELLABLE = false>
class task
{
public:
  struct promise_type;
  std::coroutine_handle<promise_type> coro_;

public:
  struct promise_type : public holder<RESULT, promise_type>
  {
    co_promise<RESULT> result_;
    bool finish_{false};

    auto get_return_object()
    {
      return task(std::coroutine_handle<promise_type>::from_promise(*this));
    }
    std::suspend_never initial_suspend() { return {}; }
    class final_awaiter
    {
      bool ready_;

    public:
      final_awaiter(bool f) : ready_(f) {}
      bool await_ready() const noexcept { return ready_; }
      void await_resume() noexcept {}
      std::coroutine_handle<> await_suspend(
          std::coroutine_handle<promise_type> h) noexcept
      {
        dout << "final await_suspend " << std::endl;
        // final_awaiter::await_suspend is called when the execution of the
        // current coroutine (referred to by 'h') is about to finish.
        // If the current coroutine was resumed by another coroutine via
        // co_await, a handle to that coroutine has been stored in result_.
        // Use that to refer to previous coroutine (as next to resume).
        return h.promise().result_.coro();
      }
    };

    final_awaiter final_suspend() noexcept { return {finish_}; }
    void unhandled_exception()
    {
      result_.set_exception(std::current_exception());
    }
    // coroutine may not define both return_value and return_void
    // so we bounce through a helper (super)class and method
    template<typename... T>
    void set_value(T &&...t)
    {
      static_assert(sizeof...(T) <= 1, "");
      result_.set_value(std::forward<T>(t)...);
    }
  };

  task(std::coroutine_handle<promise_type> h) : coro_(h) {}
  task(task &&t) = delete;
  ~task()
  {
    if (coro_) {
      // notify in a hard way that a coroutine task is dropped mid-air
      if (!CANCELLABLE && !coro_.done()) {
        gi::detail::try_throw(std::runtime_error("coroutine cancelled"));
      }
      coro_.destroy();
    }
  }

  // similar to thread's detach()
  // the task is subsequently on its own (to cleanup)
  void detach()
  {
    // only makes sense if no value is produced/expected
    static_assert(std::is_same<void, RESULT>::value, "");
    // release ownership
    if (coro_) {
      // may be done (in final suspend) already
      // only needs to destroy upon resume, so do that here directly
      if (coro_.done()) {
        coro_.destroy();
      } else {
        // disable final suspend of final waiter above
        coro_.promise().finish_ = true;
      }
    }
    coro_ = nullptr;
  }

  auto operator co_await()
  {
    if (!coro_)
      gi::detail::try_throw(std::invalid_argument("no coroutine"));
    return coro_.promise().result_.operator co_await();
  }
};
