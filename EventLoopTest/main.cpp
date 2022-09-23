#include "../EventLoop/EventLoop.hpp"
#include "../EventLoop/Network.hpp"

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

evl::task<int> async_tasks()
{
	auto w = co_await long_work();
	std::cout << "w: " << w << "\n";
	co_return w;
}

evl::task<void> async_server()
{
	auto listener = evl::network::listen("0.0.0.0", 5150);

	while (42)
	{
		std::vector<char> data(6);

		auto client = co_await listener.accept();
		auto done_recv = co_await client.recv_all(data.data(), data.size());
		std::cout << "Server: recv: '" << data.data() << "'\n";
		auto done_send = co_await client.send_all(data.data(), data.size());
	}
}

evl::task<void> async_connect()
{
	while (42)
	{
		std::vector<char> data{ 'h', 'e', 'l', 'l', 'o', '\0' };

		auto client = co_await evl::network::connect("127.0.0.1", 5150);
		auto done_send = co_await client.send_all(data.data(), data.size());
		auto done_recv = co_await client.recv_all(data.data(), data.size());

		std::cout << "Client: recv: '" << data.data() << "'\n";
	}
}

evl::task<void> async_server_client()
{
	co_await evl::join(async_server(), async_connect());
}

evl::task<void> async_context()
{
	auto ctx = co_await evl::get_context();
	int a = 0;
}

int main()
{
	int b = 0;

	if (false)
	{
		evl::context ctx;
		ctx.run(async_context());
	}

	if (false)
	{
		evl::context ctx;
		ctx.run(async_server_client());
	}

	// for (int i = 0; i < 10; ++i)
	{
		evl::context ctx;
		auto result = ctx.run(async_tasks());
		std::cout << "run: " << result << "\n";
	}

	int a = 0;
}

