#include <coroutine>

struct promise;

struct coroutine : std::coroutine_handle<promise>
{
  using promise_type = ::promise;
};

struct promise
{
  coroutine get_return_object() { return {coroutine::from_promise(*this)}; }
  std::suspend_always initial_suspend() noexcept { return {}; }
  std::suspend_always final_suspend() noexcept { return {}; }
  void return_void() {}
  void unhandled_exception() {}
};

static coroutine
f()
{
  co_return;
}

int
main(int argc, char **argv)
{
  return 0;
}
