
/*
template <typename T>
class task
{
public:

	struct promise_type
	{
		std::optional<T> value_ = std::nullopt;

		task get_return_object()
		{
			return {
				.h_ = std::coroutine_handle<promise_type>::from_promise(*this)
			};
		}

		std::suspend_never initial_suspend() { return {}; }
		std::suspend_always final_suspend() noexcept { return {}; }
		void unhandled_exception() {}

		void return_value(T &&) { value_ = std::optional{ static_cast<std::optional<T> &&>(value_) }; }
	};

	const T &ret_val() const & { return h_.value_; }
	T ret_val() && { return static_cast<T&&>(h_.value_); }

	bool done() const noexcept { return h_.done(); }
	void resume()
	{
		std::cout << "resume\n";
		h_.resume();
	}

	~task() { h_.destroy(); }


	bool await_ready() const noexcept
	{
		std::cout << "await_ready\n";
		return done();
	}

	void await_suspend(std::coroutine_handle<> continuation) const
	{
		std::cout << "await_suspend\n";
		h_.promise().continuation = continuation;
		h_.resume();
	}

	const T &await_resume() const noexcept
	{
		std::cout << "await_resume\n";
		return h_.value_.value();
	}

private:

	std::coroutine_handle<promise_type> h_;
};
*/

/*
template <typename T>
class mtask
{
public:

	struct promise_type
	{
		T value_;

		mtask get_return_object()
		{
			return {
				.h_ = std::coroutine_handle<promise_type>::from_promise(*this)
			};
		}

		std::suspend_never initial_suspend() { return {}; }
		std::suspend_always final_suspend() noexcept { return {}; }
		void unhandled_exception() {}
		std::suspend_always yield_value(T value)
		{
			value_ = value;
			return {};
		}
		void return_void() {}
	};

	std::optional<T> next()
	{
		if (done())
			return std::nullopt;

		T val = h_.promise().value_;
		h_();
		return std::optional{ val };
	}

	bool done() const { return h_.done(); }

	~mtask() { h_.destroy(); }

private:

	std::coroutine_handle<promise_type> h_;
};

mtask<int> counter()
{
	for (int i = 0; i < 3; ++i)
		co_yield i;
	// falling off end of function or co_return; => promise.return_void();
	// (co_return value; => promise.return_value(value);)
}

*/

/*
auto c = counter();

while (!c.done())
{
	std::cout << "counter: " << c.next().value() << "\n";
}
*/

/*
int main()
{
	auto evctx = event_loop::create();

	auto task = evctx->pool_fn([]() { return 123; });
	auto task = task.then([](int arg) { printf("%d", arg); });

	evctx->await();
}
*/


/*
template <>
class task<void>
{
public:
	struct promise_type {
		task get_return_object() { return task(std::coroutine_handle<promise_type>::from_promise(*this)); }
		constexpr std::suspend_always initial_suspend() const noexcept { return {}; }
		constexpr std::suspend_always final_suspend() const noexcept { return {}; }
		void return_void() {}
		constexpr void unhandled_exception() {}
	};

	constexpr bool await_ready() const noexcept { return false; }
	constexpr void await_resume() {}

	std::coroutine_handle<> await_suspend(std::coroutine_handle<> continuation) const noexcept { return this->h_; }

private:

	task(std::coroutine_handle<promise_type> &&h):
		h_(static_cast<std::coroutine_handle<promise_type> &&>(h))
	{}

	std::coroutine_handle<promise_type> h_;
};

task<void> not_long_work2()
{
	std::cout << "not_long_work 2\n";
	co_return;
}*/
