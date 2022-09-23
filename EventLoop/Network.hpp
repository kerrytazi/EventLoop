#pragma once

#include "EventLoop.hpp"


namespace evl
{

namespace __internal
{

namespace __network
{

struct socket_t;

struct vlisten_task;
struct vaccept_task;
struct vconnect_task;
struct vrecv_task;
struct vsend_task;

struct tcp_server;
struct tcp_client;


struct listen_task
{
	std::shared_ptr<vlisten_task> _vt;
	const char *_addr;
	uint16_t _port;

	using return_type = tcp_server;

	constexpr bool await_ready() const noexcept { return false; }
	return_type await_resume();

	template <typename K>
	void await_suspend(std::coroutine_handle<K> parent_cor) const
	{
		auto &parent_prom = parent_cor.promise();
		auto ctx = parent_prom._ctx;

		this->_start(ctx, parent_prom._vt);
	}

	void _start(context *ctx, std::weak_ptr<vtask_base> parent_vt) const;

	listen_task() = default;
	listen_task(listen_task &&);
	~listen_task();
};

struct accept_task
{
	std::shared_ptr<vaccept_task> _vt;
	socket_t *_server = nullptr;

	using return_type = tcp_client;

	constexpr bool await_ready() const noexcept { return false; }
	return_type await_resume();

	template <typename K>
	void await_suspend(std::coroutine_handle<K> parent_cor) const
	{
		auto &parent_prom = parent_cor.promise();
		auto ctx = parent_prom._ctx;

		this->_start(ctx, parent_prom._vt);
	}

	void _start(context *ctx, std::weak_ptr<vtask_base> parent_vt) const;

	accept_task() = default;
	accept_task(accept_task &&);
	~accept_task();
};

struct connect_task
{
	std::shared_ptr<vconnect_task> _vt;
	const char *_addr;
	uint16_t _port;

	using return_type = tcp_client;

	constexpr bool await_ready() const noexcept { return false; }
	return_type await_resume();

	template <typename K>
	void await_suspend(std::coroutine_handle<K> parent_cor) const
	{
		auto &parent_prom = parent_cor.promise();
		auto ctx = parent_prom._ctx;

		this->_start(ctx, parent_prom._vt);
	}

	void _start(context *ctx, std::weak_ptr<vtask_base> parent_vt) const;

	connect_task() = default;
	connect_task(connect_task &&);
	~connect_task();
};

struct recv_task
{
	std::shared_ptr<vrecv_task> _vt;
	socket_t *_client = nullptr;

	using return_type = size_t;

	constexpr bool await_ready() const noexcept { return false; }
	return_type await_resume();

	template <typename K>
	void await_suspend(std::coroutine_handle<K> parent_cor) const
	{
		auto &parent_prom = parent_cor.promise();
		auto ctx = parent_prom._ctx;

		this->_start(ctx, parent_prom._vt);
	}

	void _start(context *ctx, std::weak_ptr<vtask_base> parent_vt) const;
};

struct send_task
{
	std::shared_ptr<vsend_task> _vt;
	socket_t *_client = nullptr;

	using return_type = size_t;

	constexpr bool await_ready() const noexcept { return false; }
	return_type await_resume();

	template <typename K>
	void await_suspend(std::coroutine_handle<K> parent_cor) const
	{
		auto &parent_prom = parent_cor.promise();
		auto ctx = parent_prom._ctx;

		this->_start(ctx, parent_prom._vt);
	}

	void _start(context *ctx, std::weak_ptr<vtask_base> parent_vt) const;
};

struct tcp_server
{
	std::unique_ptr<socket_t> _sock;

	tcp_server() = default;
	tcp_server(tcp_server &&);
	~tcp_server();

	accept_task accept();
};

struct tcp_client
{
	std::unique_ptr<socket_t> _sock;

	recv_task recv(char *data, size_t size);
	send_task send(const char *data, size_t size);

	task<recv_task::return_type> recv_all(char *data, size_t size)
	{
		size_t total = 0;

		while (total != size)
		{
			size_t done = co_await this->recv(&data[total], size - total);
			total += done;
		}

		co_return total;
	}

	task<send_task::return_type> send_all(const char *data, size_t size)
	{
		size_t total = 0;

		while (total != size)
		{
			size_t done = co_await this->send(&data[total], size - total);
			total += done;
		}

		co_return total;
	}

	tcp_client() = default;
	tcp_client(tcp_client &&);
	~tcp_client();
};

listen_task listen(const char *addr, uint16_t port);
connect_task connect(const char *addr, uint16_t port);

} // namespace __network

} // namespace __internal

namespace network
{

using __internal::__network::listen_task;
using __internal::__network::accept_task;
using __internal::__network::connect_task;
using __internal::__network::recv_task;
using __internal::__network::send_task;

using __internal::__network::tcp_server;
using __internal::__network::tcp_client;

using __internal::__network::listen;
using __internal::__network::connect;

}

} // namespace evl

