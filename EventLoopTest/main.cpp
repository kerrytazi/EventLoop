#include "../EventLoop/EventLoop.hpp"

#include <iostream>


evl::task<int> empty_work_int()
{
	std::cout << "zxc\n";
	co_return 123;
}

evl::task<void> empty_work()
{
	std::cout << "dsa\n";
	co_return;
}

evl::task<void> not_long_work2()
{
	std::cout << "not_long_work 2\n";

	auto j = co_await evl::join(evl::async_sleep(std::chrono::seconds(1)), empty_work(), empty_work_int());
	std::cout << "asd " << std::get<2>(j).val << "\n";

	for (int i = 0; i < 3; ++i)
	{
		const auto now = std::chrono::system_clock::now();
		std::cout << std::format("{:%F %T}", now) << '\n';
		co_await evl::async_sleep(std::chrono::milliseconds(1000));
	}

	co_return;
}

evl::task<float> not_long_work()
{
	co_await not_long_work2();
	std::cout << "not_long_work 1\n";
	co_return 3.3f;
}

evl::task<int> long_work()
{
	std::cout << "long_work 1\n";
	auto v = co_await not_long_work();

	std::cout << "long_work 2\n";
	co_return static_cast<int>(v);
}

evl::task<int> async_main()
{
	auto w = co_await long_work();
	std::cout << "w: " << w << "\n";
	co_return w;
}


int main()
{
	int b = 0;

	// for (int i = 0; i < 10; ++i)
	{
		evl::context ctx;
		auto result = ctx.run(async_main());
		std::cout << "run: " << result << "\n";
	}

	int a = 0;
}

