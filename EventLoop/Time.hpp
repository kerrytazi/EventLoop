#pragma once

#include "EventLoop.hpp"


namespace evl
{

namespace __internal
{

namespace __time
{

struct timer_task
{
	using return_type = void;

	std::chrono::system_clock::time_point _time;

	timer_task(std::chrono::system_clock::time_point time) :
		_time(time)
	{}

	constexpr bool await_ready() const noexcept { return false; }
	constexpr return_type await_resume() const noexcept {}

	template <typename K>
	void await_suspend(std::coroutine_handle<K> parent_cor) const
	{
		auto &parent_prom = parent_cor.promise();
		auto ctx = parent_prom._ctx;

		timer_info info;
		info._ctx = ctx;
		info._parent_vt = parent_prom._vt.lock();
		info._time = this->_time;

		ctx->_add_timer(std::move(info));
	}
};

timer_task async_sleep_until(std::chrono::system_clock::time_point time);
timer_task async_sleep(std::chrono::system_clock::duration dur);

} // namespace __time

} // namespace __internal

namespace time
{

using __internal::__time::timer_task;
using __internal::__time::async_sleep;
using __internal::__time::async_sleep_until;

} // namespace time

} // namespace evl

