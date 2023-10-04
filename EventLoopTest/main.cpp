#include "../EventLoop/EventLoop.hpp"
#include "../EventLoop/Network.hpp"
#include "../EventLoop/Time.hpp"

#include <iostream>
#include <string>


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

	auto j = co_await evl::join(evl::time::async_sleep(std::chrono::seconds(1)), empty_work(), empty_work_int());
	std::cout << "asd " << std::get<2>(j).val << "\n";

	for (int i = 0; i < 30; ++i)
	{
		const auto now = std::chrono::system_clock::now();
		std::cout << std::format("{:%F %T}", now) << '\n';
		co_await evl::time::async_sleep(std::chrono::milliseconds(1));
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
	auto listener = co_await evl::network::listen("0.0.0.0", 5150);

	//while (42)
	{
		std::vector<char> data(6);

		auto client = co_await listener.accept();
		auto done_recv = co_await client.recv_all(data);
		std::cout << "Server: recv: '" << data.data() << "'\n";
		auto done_send = co_await client.send_all(data);
	}
}

evl::task<void> async_connect()
{
	//while (42)
	{
		std::vector<char> data;

		auto client = co_await evl::network::connect("127.0.0.1", 5150);
		auto done_send = co_await client.send_all(data);
		auto done_recv = co_await client.recv_all(data);

		std::cout << "Client: recv: '" << data.data() << "'\n";
	}
}

evl::task<void> async_server_client()
{
	co_await evl::join(async_server(), async_connect());
}


evl::task<void> buffered_server()
{
	auto listener = co_await evl::network::listen("0.0.0.0", 5150);

	//while (42)
	{
		std::vector<char> data;

		auto client = (co_await listener.accept()).make_buffered();

		auto done_recv1 = co_await client.recv_line(data);
		std::cout << "Server: recv1: '" << std::string(data.begin(), data.end()) << "'\n";
		data.push_back('\n');

		auto done_recv2 = co_await client.recv_line(data);
		std::cout << "Server: recv2: '" << std::string(data.begin(), data.end()) << "'\n";
		data.push_back('\n');

		auto done_send = co_await client.send_all(data);
	}
}

evl::task<void> buffered_connect()
{
	//while (42)
	{
		const char *s = "hello\nworld\n";
		std::vector<char> data(s, s + 12);

		auto client = (co_await evl::network::connect("127.0.0.1", 5150)).make_buffered();
		auto done_send = co_await client.send_all(data);
		auto done_recv1 = co_await client.recv_line(data);
		auto done_recv2 = co_await client.recv_line(data);

		std::cout << "Client: recv: '" << std::string(data.begin(), data.end()) << "'\n";
	}
}

evl::task<void> buffered_server_client()
{
	co_await evl::join(buffered_server(), buffered_connect());
}

evl::task<void> async_context()
{
	auto ctx = co_await evl::get_context();
	int a = 0;
}

evl::task<void> test_sleep(int ms)
{
	co_await evl::time::async_sleep(std::chrono::milliseconds(ms));
}

evl::task<void> test_select()
{
	auto t1 = evl::selector(test_sleep(1000), []() { std::cout << "sleep 1\n"; });
	auto t2 = evl::selector(test_sleep(1000), []() { std::cout << "sleep 2\n"; });

	co_await evl::select(std::move(t1), std::move(t2));

	int a = 0;
}

#include <Windows.h>

int main()
{
	int b = 0;

	{
		std::cout << "lala\n";
		delete new int{};
	}

	{
		DWORD count = 0;
		if (::GetProcessHandleCount(::GetCurrentProcess(), &count) == FALSE)
			throw 1;

		std::cout << "app start. handles: " << count << "\n";
	}

	if (true)
	{
		evl::context ctx;
		ctx.run(test_select());
	}

	if (false)
	{
		evl::context ctx;
		ctx.run(async_context());
	}

	for (int i = 0; i < 20; ++i)
	if (false)
	{
		evl::context ctx;
		ctx.run(buffered_server_client());
	}

	if (false)
	{
		evl::context ctx;
		ctx.run(async_server_client());
	}

	if (false)
	{
		evl::context ctx;
		auto result = ctx.run(async_tasks());
		std::cout << "run: " << result << "\n";
	}

	{
		DWORD count = 0;
		if (::GetProcessHandleCount(::GetCurrentProcess(), &count) == FALSE)
			throw 1;

		std::cout << "app end. handles: " << count << "\n";
	}

	int a = 0;
}

