#include "Time.hpp"


evl::__internal::__time::timer_task evl::__internal::__time::async_sleep_until(std::chrono::system_clock::time_point time)
{
	return timer_task(time);
}

evl::__internal::__time::timer_task evl::__internal::__time::async_sleep(std::chrono::system_clock::duration dur)
{
	return async_sleep_until(std::chrono::system_clock::now() + dur);
}
