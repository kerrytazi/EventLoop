#pragma once

#include "pch.hpp"

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
	virtual void resume(const std::shared_ptr<vtask_base> &from_vt) = 0;
};

struct timer_info
{
	context *_ctx;
	std::shared_ptr<vtask_base> _parent_vt;
	std::chrono::system_clock::time_point _time;
};

struct context_impl
{
	virtual ~context_impl() = default;
	virtual void rename_native_thread(size_t worker_id) = 0;
	virtual void waiting_loop() = 0;
	virtual void mark_complete() = 0;
	virtual void continue_queue() = 0;
};

struct context
{
	struct ready_task_t
	{
		std::shared_ptr<vtask_base> vt;
		std::shared_ptr<vtask_base> from_vt;
	};

	std::unique_ptr<context_impl> _impl;
	std::vector<ready_task_t> _ready_tasks;
	std::vector<timer_info> _timers;

	std::mutex _mtx;
	std::condition_variable _cv;
	std::vector<std::thread> _ths;
	bool _complete = false;

	template <typename T>
	T *_get_impl() const { return static_cast<T *>(this->_impl.get()); }

	void _worker(size_t worker_id);
	void _mark_complete();

	void _add_timer(timer_info tim);
	void _add_ready_task(const std::shared_ptr<vtask_base> &vt, const std::shared_ptr<vtask_base> &from_vt);

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
			this->_ctx->_add_ready_task(this->_parent_vt.lock(), this->_vt.lock());
		else
			this->_ctx->_mark_complete();
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
	virtual void resume(const std::shared_ptr<vtask_base> &from_vt) override { this->_h.resume(); }
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

		ctx->_add_ready_task(this->_vt, {});
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
	std::weak_ptr<vtask_base> _vt;
	std::weak_ptr<vtask_base> _parent_vt;

	size_t _counter = 0;
	size_t _max_counter;

	vjoin_task(size_t max_counter) :
		_max_counter(max_counter)
	{
	}

	virtual void resume(const std::shared_ptr<vtask_base> &from_vt) override
	{
		if (++this->_counter == this->_max_counter)
		{
			if (!this->_parent_vt.expired())
				this->_ctx->_add_ready_task(this->_parent_vt.lock(), this->_vt.lock());
		}
	}
};

struct tmp_task_t
{
	context *_ctx;
	std::weak_ptr<vtask_base> _vt;
};

template <typename T>
struct join_wrapper
{
	T val;

	join_wrapper(T &&v) :
		val(std::move(v))
	{}
};

template <>
struct join_wrapper<void>
{
	join_wrapper() {}
};

template <typename T>
auto to_join_wrapper(const T &tt)
{
	if constexpr (std::is_void_v<typename T::return_type>)
	{
		tt.await_resume();
		return join_wrapper<void>{};
	}
	else
	{
		return join_wrapper<typename T::return_type>{ tt.await_resume() };
	}
}

template <typename... TTasks>
struct join_task
{
	using return_type = std::tuple<join_wrapper<typename TTasks::return_type>...>;

	constexpr bool await_ready() const noexcept { return false; }
	return_type await_resume() const { return std::apply([](const auto&... t) { return std::make_tuple(to_join_wrapper(t)...); }, this->_tasks); }

	template <typename K>
	void await_suspend(std::coroutine_handle<K> parent_cor) const
	{
		auto &parent_prom = parent_cor.promise();
		auto ctx = parent_prom._ctx;

		this->_vt->_ctx = ctx;
		this->_vt->_vt = this->_vt;
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

template <typename TFunc, typename TArg>
static constexpr bool is_invocable_v_voided = std::is_invocable_v<TFunc, TArg>;

template <typename TFunc>
static constexpr bool is_invocable_v_voided<TFunc, void> = std::is_invocable_v<TFunc>;

template <typename TTask, typename TCallback,
	std::enable_if_t<is_invocable_v_voided<TCallback, typename TTask::return_type>, bool> = true>
struct selector
{
	TTask _task;
	TCallback _callback;

	using Task_t = TTask;
	using TCallback_t = TCallback;

	selector(TTask &&task, TCallback &&callback) :
		_task(std::move(task)),
		_callback(std::move(callback))
	{
	}
};

template <typename>
struct unpack_pair;

template <template <typename T1, typename T2> typename unpacked, typename TTask, typename TCallback>
struct unpack_pair<unpacked<TTask, TCallback>>
{
	using type = selector<TTask, TCallback>;
};

template <typename P>
using unpack_pair_t = typename unpack_pair<P>::type;

struct vselect_task : vtask_base
{
	context *_ctx = nullptr;
	std::weak_ptr<vtask_base> _vt;
	std::weak_ptr<vtask_base> _parent_vt;

	std::shared_ptr<vtask_base> _from_vt;

	virtual void resume(const std::shared_ptr<vtask_base> &from_vt) override
	{
		if (!this->_from_vt)
		{
			this->_from_vt = from_vt;

			if (!this->_parent_vt.expired())
				this->_ctx->_add_ready_task(this->_parent_vt.lock(), this->_vt.lock());
		}
		else
		{
			// TODO
		}
	}
};

template <typename TSelector>
void to_select_wrapper(const TSelector &tt)
{
	if constexpr (std::is_void_v<typename TSelector::Task_t::return_type>)
	{
		tt._task.await_resume();
		tt._callback();
	}
	else
	{
		tt._callback(tt._task.await_resume());
	}
}

template <typename... TPairs>
struct select_task
{
	using return_type = void;

	constexpr bool await_ready() const noexcept { return false; }
	return_type await_resume() const {
		std::apply([&](const auto &... _t) {
			int _[] = {
				0,
				(
					[&](const auto &t) {
						if (t._task._vt == this->_vt->_from_vt)
							to_select_wrapper(t);
					}(_t),
					0
				)...
			};

			(void)_;
		}, this->_tasks);
	}

	template <typename K>
	void await_suspend(std::coroutine_handle<K> parent_cor) const
	{
		auto &parent_prom = parent_cor.promise();
		auto ctx = parent_prom._ctx;

		this->_vt->_ctx = ctx;
		this->_vt->_vt = this->_vt;
		this->_vt->_parent_vt = parent_prom._vt;

		tmp_task_t tmp_task;
		tmp_task._ctx = ctx;
		tmp_task._vt = this->_vt;
		const std::coroutine_handle<tmp_task_t> tmp_h = std::coroutine_handle<tmp_task_t>::from_promise(tmp_task);

		std::apply([&tmp_h](auto& ...p) { (..., p._task.await_suspend(tmp_h)); }, this->_tasks);
	}

	select_task(TPairs&&... tasks) :
		_vt(std::make_shared<vselect_task>()),
		_tasks(std::forward<TPairs>(tasks)...)
	{
	}

	std::shared_ptr<vselect_task> _vt;
	std::tuple<TPairs...> _tasks;
};

template <typename... TPairs>
select_task<TPairs...> select(TPairs&&... tasks)
{
	return select_task(std::forward<TPairs>(tasks)...);
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

		ctx->_add_ready_task(parent_prom._vt.lock(), {}); // TODO
	}
};

get_context_task get_context();

} // namespace __internal

using __internal::task;
using __internal::context;

using __internal::join_wrapper;
using __internal::join_task;
using __internal::join;

using __internal::selector;
using __internal::select_task;
using __internal::select;

using __internal::get_context_task;
using __internal::get_context;

} // namespace evl

