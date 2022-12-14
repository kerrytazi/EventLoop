#include "EventLoop.hpp"


void evl::__internal::context::_loop(std::shared_ptr<vtask_base> main_vt, std::coroutine_handle<> main_handle)
{
	this->_add_ready_task(main_vt);

	while (!main_handle.done())
	{
		while (!this->_ready_tasks.empty())
		{
			auto vt = std::move(this->_ready_tasks.back());
			this->_ready_tasks.pop_back();
			vt->resume();
		}

		if (main_handle.done())
			break;

		this->_impl->waiting_loop();
	}
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

