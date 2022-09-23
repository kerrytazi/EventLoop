#pragma once

#include <coroutine>
#include <vector>
#include <utility>
#include <tuple>
#include <memory>
#include <optional>
#include <chrono>
#include <thread>
#include <algorithm>


namespace evl
{

namespace __internal
{

struct vtask_base;

template <typename T>
struct vtask;

struct task_promise_type_base;

template <typename T>
struct task_promise_type;

template <typename T>
struct task_base;

template <typename T>
struct task;

struct timer_info;
struct vtask_base;
struct vjoin_task;

struct context;


struct vtask_base
{
	virtual ~vtask_base() = default;
	virtual void resume() = 0;
};

struct timer_info
{
	context *_ctx;
	std::shared_ptr<vtask_base> _task;
	std::chrono::system_clock::time_point _time;
};

struct context_impl
{
	virtual ~context_impl() = default;
	virtual void waiting_loop() = 0;
};

struct context
{
	std::unique_ptr<context_impl> _impl;
	std::vector<std::shared_ptr<vtask_base>> _ready_tasks;
	std::vector<timer_info> _timers;

	template <typename T>
	T *_get_impl() const { return static_cast<T *>(this->_impl.get()); }

	void _add_timer(timer_info tim)
	{
		const auto f = [](const timer_info &left, const timer_info &right) { return left._time > right._time; };
		this->_timers.insert(std::lower_bound(this->_timers.begin(), this->_timers.end(), tim, f), std::move(tim));
	}

	void _add_ready_task(const std::shared_ptr<vtask_base> &vt)
	{
		this->_ready_tasks.push_back(vt);
	}

	void _loop(std::shared_ptr<vtask_base> main_vt, std::coroutine_handle<> main_handle);

	template <typename T>
	T run(task<T> t)
	{
		t._vt->_h.promise()._ctx = this;
		this->_loop(t._vt, t._vt->_h);
		return t._vt->_h.promise()._get_result();
	}

	context();
};

struct task_promise_type_base
{
	context *_ctx = nullptr;
	std::weak_ptr<vtask_base> _vt;
	std::weak_ptr<vtask_base> _parent_vt;

	void _continue_parent() const
	{
		if (!this->_parent_vt.expired())
			this->_ctx->_add_ready_task(this->_parent_vt.lock());
	}

	constexpr void unhandled_exception() {}
	constexpr std::suspend_always initial_suspend() const noexcept { return {}; }
	std::suspend_always final_suspend() const noexcept
	{
		this->_continue_parent();
		return {};
	}
};

template <typename T>
struct task_promise_type : task_promise_type_base
{
	std::optional<T> _value;

	task<T> get_return_object();

	void return_value(T val) { this->_value = std::optional<T>{ val }; }

	T _get_result() { return std::move(this->_value).value(); }
};

template <>
struct task_promise_type<void> : task_promise_type_base
{
	task<void> get_return_object();

	void return_void() {}

	void _get_result() {}
};

template <typename T>
struct vtask : vtask_base
{
	std::coroutine_handle<task_promise_type<T>> _h;

	vtask(std::coroutine_handle<task_promise_type<T>> &&h) :
		_h(std::move(h))
	{}

	~vtask() { this->_h.destroy(); }
	virtual void resume() override { this->_h.resume(); }
};

template <typename T>
struct task_base
{
	using promise_type = task_promise_type<T>;
	using return_type = T;

	std::shared_ptr<vtask<T>> _vt;

	constexpr bool await_ready() const noexcept { return false; }

	template <typename K>
	void await_suspend(std::coroutine_handle<K> parent_cor) const
	{
		auto &parent_prom = parent_cor.promise();
		auto &this_prom = this->_vt->_h.promise();
		auto ctx = parent_prom._ctx;

		this_prom._ctx = ctx;
		this_prom._parent_vt = parent_prom._vt;

		ctx->_add_ready_task(this->_vt);
	}

	task_base(std::shared_ptr<vtask<T>> &&vt) :
		_vt(std::move(vt))
	{}
};

template <typename T>
struct task : task_base<T>
{
	task_base<T>::return_type await_resume() const { return std::move(this->_vt->_h.promise()._value).value(); }

	task(std::shared_ptr<vtask<T>> &&vt) :
		task_base<T>(std::move(vt))
	{}
};

template <>
struct task<void> : task_base<void>
{
	constexpr task_base<void>::return_type await_resume() const {}

	task(std::shared_ptr<vtask<void>> vt) :
		task_base<void>(std::move(vt))
	{}
};

template <typename T>
task<T> task_promise_type<T>::get_return_object()
{
	auto vt = std::make_shared<vtask<T>>(std::coroutine_handle<task_promise_type<T>>::from_promise(*this));
	this->_vt = std::weak_ptr(vt);
	return task<T>(std::move(vt));
}

struct vjoin_task : vtask_base
{
	context *_ctx = nullptr;
	std::weak_ptr<vtask_base> _parent_vt;

	size_t _counter = 0;
	size_t _max_counter;

	vjoin_task(size_t max_counter) :
		_max_counter(max_counter)
	{
	}

	virtual void resume() override
	{
		if (++this->_counter == this->_max_counter)
		{
			if (!this->_parent_vt.expired())
				this->_ctx->_add_ready_task(this->_parent_vt.lock());
		}
	}
};

struct tmp_task_t
{
	context *_ctx;
	std::weak_ptr<vtask_base> _vt;
};

template <typename T>
struct wrapper
{
	T val;

	wrapper(T &&v) :
		val(std::move(v))
	{}
};

template <>
struct wrapper<void>
{
	wrapper() {}
};

template <typename T>
auto to_wrapper(const T &tt)
{
	if constexpr (std::is_void_v<typename T::return_type>)
	{
		tt.await_resume();
		return wrapper<void>{};
	}
	else
	{
		return wrapper<typename T::return_type>{ tt.await_resume() };
	}
}

template <typename... TTasks>
struct join_task
{
	using return_type = std::tuple<wrapper<typename TTasks::return_type>...>;

	constexpr bool await_ready() const noexcept { return false; }
	return_type await_resume() const { return std::apply([](const auto&... t) { return std::make_tuple(to_wrapper(t)...); }, this->_tasks); }

	template <typename K>
	void await_suspend(std::coroutine_handle<K> parent_cor) const
	{
		auto &parent_prom = parent_cor.promise();
		auto ctx = parent_prom._ctx;

		this->_vt->_ctx = ctx;
		this->_vt->_parent_vt = parent_prom._vt;

		tmp_task_t tmp_task;
		tmp_task._ctx = ctx;
		tmp_task._vt = this->_vt;
		const std::coroutine_handle<tmp_task_t> tmp_h = std::coroutine_handle<tmp_task_t>::from_promise(tmp_task);

		std::apply([&tmp_h](auto& ...t) { (..., t.await_suspend(tmp_h)); }, this->_tasks);
	}

	join_task(TTasks&&... tasks) :
		_vt(std::make_shared<vjoin_task>(sizeof...(TTasks))),
		_tasks(std::forward<TTasks>(tasks)...)
	{}

	std::shared_ptr<vjoin_task> _vt;
	std::tuple<TTasks...> _tasks;
};

template <typename... TTasks>
join_task<TTasks...> join(TTasks&&... tasks)
{
	return join_task(std::forward<TTasks>(tasks)...);
}

struct get_context_task
{
	context *_value;
	using return_type = context *;

	constexpr bool await_ready() const noexcept { return false; }
	return_type await_resume() const { return this->_value; }

	template <typename K>
	void await_suspend(std::coroutine_handle<K> parent_cor)
	{
		auto &parent_prom = parent_cor.promise();
		auto ctx = parent_prom._ctx;

		this->_value = ctx;

		ctx->_add_ready_task(parent_prom._vt.lock());
	}
};

get_context_task get_context();

} // namespace __internal

using __internal::task;
using __internal::context;

using __internal::join_task;
using __internal::wrapper;
using __internal::join;

using __internal::get_context_task;
using __internal::get_context;

} // namespace evl

