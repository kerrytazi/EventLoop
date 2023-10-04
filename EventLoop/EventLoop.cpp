#include "EventLoop.hpp"


void evl::__internal::context::_worker(size_t worker_id)
{
	this->_impl->rename_native_thread(worker_id);

	while (42)
	{
		ready_task_t rt;

		{
			std::unique_lock lck(this->_mtx);
			this->_cv.wait(lck, [this]() { return this->_complete || !this->_ready_tasks.empty(); });

			if (this->_complete)
				return;

			rt = std::move(this->_ready_tasks.back());
			this->_ready_tasks.pop_back();
		}

		rt.vt->resume(rt.from_vt);
	}
}

void evl::__internal::context::_mark_complete()
{
	{
		std::lock_guard _lck(this->_mtx);
		this->_complete = true;
	}

	this->_cv.notify_all();
	this->_impl->mark_complete();
}

void evl::__internal::context::_add_timer(timer_info tim)
{
	{
		std::lock_guard _lck(this->_mtx);
		const auto f = [](const timer_info &left, const timer_info &right) { return left._time > right._time; };
		this->_timers.insert(std::lower_bound(this->_timers.begin(), this->_timers.end(), tim, f), std::move(tim));
	}

	this->_impl->continue_queue();
}

void evl::__internal::context::_add_ready_task(const std::shared_ptr<vtask_base> &vt, const std::shared_ptr<vtask_base> &from_vt)
{
	{
		std::lock_guard _lck(this->_mtx);
		this->_ready_tasks.push_back({ vt, from_vt });
	}

	this->_cv.notify_one();
}

void evl::__internal::context::_loop(std::shared_ptr<vtask_base> main_vt, std::coroutine_handle<> main_handle)
{
	size_t num_workers = std::max<size_t>(1, std::thread::hardware_concurrency());

	this->_ths.reserve(num_workers);
	for (size_t i = 0; i < num_workers; ++i)
		this->_ths.push_back(std::thread([this, i]() { this->_worker(i); }));

	this->_add_ready_task(main_vt, {});

	this->_impl->waiting_loop();

	for (auto &th : this->_ths)
		th.join();
}

evl::__internal::task<void> evl::__internal::task_promise_type<void>::get_return_object()
{
	auto vt = std::make_shared<vtask<void>>(std::coroutine_handle<task_promise_type<void>>::from_promise(*this));
	this->_vt = std::weak_ptr(vt);
	return task<void>(std::move(vt));
}

evl::__internal::get_context_task evl::__internal::get_context()
{
	return get_context_task{};
}

