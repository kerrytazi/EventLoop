#include "Network.hpp"

#include <WS2tcpip.h>
#include <MSWSock.h>

#pragma comment(lib, "ws2_32.lib")


struct wsa_initer
{
	WSADATA wsd{ 0 };
	int err = 0;

	wsa_initer()
	{
		if (::WSAStartup(MAKEWORD(2, 2), &wsd) != 0)
			err = ::WSAGetLastError();
	}

	~wsa_initer()
	{
		if (err != 0)
			::WSACleanup();
	}

	static void init()
	{
		static wsa_initer instance;
	}
};

struct winsock_ex
{
	LPFN_ACCEPTEX accept_ex = nullptr;
	LPFN_GETACCEPTEXSOCKADDRS get_accept_ex_sockaddrs = nullptr;
	LPFN_CONNECTEX connect_ex = nullptr;
	LPFN_DISCONNECTEX disconnect_ex = nullptr;
	int err = 0;

	winsock_ex()
	{
		SOCKET sock = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

		DWORD bytes_returned = 0;
		GUID func_guid{ 0 };

		func_guid = WSAID_ACCEPTEX;
		if (::WSAIoctl(sock,
			SIO_GET_EXTENSION_FUNCTION_POINTER,
			&func_guid, sizeof(func_guid),
			&this->accept_ex, sizeof(this->accept_ex),
			&bytes_returned, NULL, NULL) != 0)
		{
			this->err = ::WSAGetLastError();
			return;
		}

		func_guid = WSAID_GETACCEPTEXSOCKADDRS;
		if (::WSAIoctl(sock,
			SIO_GET_EXTENSION_FUNCTION_POINTER,
			&func_guid, sizeof(func_guid),
			&this->get_accept_ex_sockaddrs, sizeof(this->get_accept_ex_sockaddrs),
			&bytes_returned, NULL, NULL) != 0)
		{
			this->err = ::WSAGetLastError();
			return;
		}

		func_guid = WSAID_CONNECTEX;
		if (::WSAIoctl(sock,
			SIO_GET_EXTENSION_FUNCTION_POINTER,
			&func_guid, sizeof(func_guid),
			&this->connect_ex, sizeof(this->connect_ex),
			&bytes_returned, NULL, NULL) != 0)
		{
			this->err = ::WSAGetLastError();
			return;
		}

		func_guid = WSAID_DISCONNECTEX;
		if (::WSAIoctl(sock,
			SIO_GET_EXTENSION_FUNCTION_POINTER,
			&func_guid, sizeof(func_guid),
			&this->disconnect_ex, sizeof(this->disconnect_ex),
			&bytes_returned, NULL, NULL) != 0)
		{
			this->err = ::WSAGetLastError();
			return;
		}

		::closesocket(sock);
	}

	static const winsock_ex &get_singleton()
	{
		static winsock_ex initer;
		return initer;
	}
};

union sockaddr_t {
	struct sockaddr         sa;
	struct sockaddr_in      s4;
	struct sockaddr_in6     s6;
	struct sockaddr_storage ss;
};

enum class overlapped_type
{
	o_accept,
	o_connect,
	o_recv,
	o_send,
};

struct overlapped_t
{
	OVERLAPPED base{ 0 };
	overlapped_type type;

	overlapped_t() = delete;
	overlapped_t(overlapped_type typ) : type(typ) {}
};

struct vinit_task_base : evl::__internal::vtask_base
{
	evl::__internal::context *_ctx = nullptr;
};

struct evl::__internal::__network::socket_t
{
	SOCKET sock = INVALID_SOCKET;
	sockaddr_t addr{ 0 };

	socket_t *parent = nullptr;

	overlapped_t ovlp_init;
	overlapped_t ovlp_recv{ overlapped_type::o_recv };
	overlapped_t ovlp_send{ overlapped_type::o_send };

	std::weak_ptr<vinit_task_base> _vt_init;
	std::weak_ptr<vrecv_task> _vt_recv;
	std::weak_ptr<vsend_task> _vt_send;

	~socket_t()
	{
		closesocket(this->sock);
	}

	static socket_t *from(overlapped_t *ovlp)
	{
		switch (ovlp->type)
		{
			case overlapped_type::o_accept: return reinterpret_cast<socket_t *>(reinterpret_cast<char *>(ovlp) - offsetof(socket_t, ovlp_init));
			case overlapped_type::o_connect: return reinterpret_cast<socket_t *>(reinterpret_cast<char *>(ovlp) - offsetof(socket_t, ovlp_init));
			case overlapped_type::o_recv: return reinterpret_cast<socket_t *>(reinterpret_cast<char *>(ovlp) - offsetof(socket_t, ovlp_recv));
			case overlapped_type::o_send: return reinterpret_cast<socket_t *>(reinterpret_cast<char *>(ovlp) - offsetof(socket_t, ovlp_send));
		}

		__assume(false);
	}

	socket_t(overlapped_type typ) :
		ovlp_init(typ)
	{
	}

	void recv_async(WSABUF *wsa_buf)
	{
		DWORD bytes_recv = 0;
		DWORD flags = MSG_PUSH_IMMEDIATE;
		if (::WSARecv(this->sock, wsa_buf, 1, &bytes_recv, &flags, &this->ovlp_recv.base, nullptr) == SOCKET_ERROR)
		{
			if (::WSAGetLastError() != WSA_IO_PENDING)
				throw 1;
		}
	}

	void send_async(WSABUF *wsa_buf)
	{
		DWORD bytes_send = 0;
		DWORD flags = 0;
		if (::WSASend(this->sock, wsa_buf, 1, &bytes_send, flags, &this->ovlp_send.base, nullptr) == SOCKET_ERROR)
		{
			if (::WSAGetLastError() != WSA_IO_PENDING)
				throw 1;
		}
	}

	[[nodiscard]]
	static std::unique_ptr<socket_t> create_listen(const char *addr, uint16_t port, HANDLE comp_port)
	{
		std::unique_ptr<socket_t> result = std::make_unique<socket_t>(overlapped_type::o_accept);

		result->sock = ::WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
		if (result->sock == INVALID_SOCKET)
			throw 1;

		if (::CreateIoCompletionPort((HANDLE)result->sock, comp_port, 0, 0) == nullptr)
			throw 1;

		result->addr.s4.sin_family = AF_INET;
		if (inet_pton(AF_INET, addr, &result->addr.s4.sin_addr.S_un.S_addr) != 1)
			throw 1;
		result->addr.s4.sin_port = ::htons(port);

		if (::bind(result->sock, &result->addr.sa, sizeof(result->addr)) != 0)
			throw 1;

		if (::listen(result->sock, SOMAXCONN) != 0)
			throw 1;

		return result;
	}

	[[nodiscard]]
	static std::unique_ptr<socket_t> create_connect(const char *addr, uint16_t port, HANDLE comp_port)
	{
		std::unique_ptr<socket_t> client = std::make_unique<socket_t>(overlapped_type::o_connect);

		client->sock = ::WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
		if (client->sock == INVALID_SOCKET)
			throw 1;

		if (::CreateIoCompletionPort((HANDLE)client->sock, comp_port, 0, 0) == nullptr)
			throw 1;

		{
			sockaddr_t addr{ 0 };
			addr.s4.sin_family = AF_INET;
			addr.s4.sin_addr.S_un.S_addr = INADDR_ANY;

			if (::bind(client->sock, &addr.sa, sizeof(addr)) != 0)
				throw 1;
		}

		client->addr.s4.sin_family = AF_INET;
		if (inet_pton(AF_INET, addr, &client->addr.s4.sin_addr.S_un.S_addr) != 1)
			throw 1;
		client->addr.s4.sin_port = ::htons(port);

		DWORD bsend = 0;

		if (winsock_ex::get_singleton().connect_ex(client->sock, &client->addr.sa, sizeof(client->addr), nullptr, 0, &bsend, &client->ovlp_init.base) == FALSE)
		{
			if (::WSAGetLastError() != ERROR_IO_PENDING)
				throw 1;
		}

		return client;
	}

	[[nodiscard]]
	std::unique_ptr<socket_t> create_accept(char *buf, HANDLE comp_port)
	{
		std::unique_ptr<socket_t> client = std::make_unique<socket_t>(overlapped_type::o_accept);
		client->parent = this;

		client->sock = ::WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
		if (client->sock == INVALID_SOCKET)
			throw 1;

		DWORD breceived = 0;

		if (winsock_ex::get_singleton().accept_ex(this->sock, client->sock, buf, 0, sizeof(sockaddr_t) + 16, sizeof(sockaddr_t) + 16, &breceived, &client->ovlp_init.base) == FALSE)
		{
			if (::WSAGetLastError() != ERROR_IO_PENDING)
				throw 1;
		}

		if (::CreateIoCompletionPort((HANDLE)client->sock, comp_port, 0, 0) == nullptr)
			throw 1;

		return client;
	}
};

struct evl::__internal::__network::vlisten_task : vtask_base
{
	tcp_server _value;

	virtual void resume() override {}
};

struct evl::__internal::__network::vaccept_task : vinit_task_base
{
	std::weak_ptr<vtask_base> _parent_vt;
	tcp_client _value;
	std::vector<char> _accept_buffer;

	virtual void resume() override
	{
		this->_ctx->_add_ready_task(_parent_vt.lock());
	}
};

struct evl::__internal::__network::vconnect_task : vinit_task_base
{
	std::weak_ptr<vtask_base> _parent_vt;
	tcp_client _value;
	std::vector<char> _connect_buffer;

	virtual void resume() override
	{
		this->_ctx->_add_ready_task(_parent_vt.lock());
	}
};

struct evl::__internal::__network::vrecv_task : evl::__internal::vtask_base
{
	context *_ctx = nullptr;
	size_t _done = 0;
	WSABUF _wsa_buf{ 0 };
	std::weak_ptr<vtask_base> _parent_vt;

	virtual void resume() override
	{
		this->_ctx->_add_ready_task(_parent_vt.lock());
	}
};

struct evl::__internal::__network::vsend_task : evl::__internal::vtask_base
{
	context *_ctx = nullptr;
	size_t _done = 0;
	WSABUF _wsa_buf{ 0 };
	std::weak_ptr<vtask_base> _parent_vt;

	virtual void resume() override
	{
		this->_ctx->_add_ready_task(_parent_vt.lock());
	}
};

static NTSTATUS(__stdcall *ZwSetTimerResolution)(IN ULONG RequestedResolution, IN BOOLEAN Set, OUT PULONG ActualResolution) = (NTSTATUS(__stdcall *)(ULONG, BOOLEAN, PULONG)) GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "ZwSetTimerResolution");

struct win32_new_context_impl : evl::__internal::context_impl
{
	HANDLE _comp_port = nullptr;
	evl::__internal::context *_ctx = nullptr;

	win32_new_context_impl(evl::__internal::context *ctx) :
		_ctx(ctx)
	{
		this->_comp_port = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);

		if (this->_comp_port == nullptr)
			throw 1;

		wsa_initer::init();
		(void)winsock_ex::get_singleton();

		{
			ULONG actualResolution;
			ZwSetTimerResolution(1, 1, &actualResolution);
		}
	}

	~win32_new_context_impl()
	{
		if (::CloseHandle(this->_comp_port) == FALSE)
			__debugbreak();
	}

	virtual void waiting_loop() override
	{
		DWORD timeout = INFINITE;
		bool wait_timer = !this->_ctx->_timers.empty();

		if (wait_timer)
		{
			auto delta = this->_ctx->_timers.back()._time - std::chrono::system_clock::now();
			if (delta <= decltype(delta)(0))
			{
				auto t = std::move(this->_ctx->_timers.back());
				this->_ctx->_timers.pop_back();
				this->_ctx->_add_ready_task(t._task);
				return;
			}

			timeout = (DWORD)std::chrono::duration_cast<std::chrono::milliseconds>(delta).count();

			if (timeout == 0)
				timeout = 1;
		}

		while (42)
		{
			DWORD bytes_transfered = 0;
			ULONG_PTR completion_key = 0;
			overlapped_t *ovlp = nullptr;
			BOOL success = ::GetQueuedCompletionStatus(this->_comp_port, &bytes_transfered, &completion_key, reinterpret_cast<OVERLAPPED **>(&ovlp), timeout);
			timeout = 0;

			if (success == FALSE)
			{
				if (wait_timer)
				{
					auto t = std::move(this->_ctx->_timers.back());
					this->_ctx->_timers.pop_back();
					this->_ctx->_add_ready_task(t._task);
				}

				return;
			}

			if (ovlp->type == overlapped_type::o_accept)
			{
				auto s = evl::__internal::__network::socket_t::from(ovlp);

				auto vt = std::static_pointer_cast<evl::__internal::__network::vaccept_task>(s->_vt_init.lock());
				s->_vt_init = {};

				{
					sockaddr *p_local_addr = nullptr;
					sockaddr *p_remote_addr = nullptr;
					INT local_addr_len = 0;
					INT remote_addr_len = 0;

					winsock_ex::get_singleton().get_accept_ex_sockaddrs(vt->_accept_buffer.data(), 0, sizeof(sockaddr_t) + 16, sizeof(sockaddr_t) + 16, &p_local_addr, &local_addr_len, &p_remote_addr, &remote_addr_len);
					::memcpy(&s->addr, p_remote_addr, remote_addr_len);
				}

				vt->_ctx->_add_ready_task(vt);

				continue;
			}

			if (ovlp->type == overlapped_type::o_connect)
			{
				auto s = evl::__internal::__network::socket_t::from(ovlp);

				auto vt = std::static_pointer_cast<evl::__internal::__network::vconnect_task>(s->_vt_init.lock());
				s->_vt_init = {};
				vt->_ctx->_add_ready_task(vt);

				continue;
			}

			if (bytes_transfered == 0)
				throw 1;

			if (ovlp->type == overlapped_type::o_recv)
			{
				auto s = evl::__internal::__network::socket_t::from(ovlp);

				auto vt = s->_vt_recv.lock();
				s->_vt_recv = {};
				vt->_done = bytes_transfered;
				vt->_ctx->_add_ready_task(vt);

				continue;
			}

			if (ovlp->type == overlapped_type::o_send)
			{
				auto s = evl::__internal::__network::socket_t::from(ovlp);

				auto vt = s->_vt_send.lock();
				s->_vt_recv = {};
				vt->_done = bytes_transfered;
				vt->_ctx->_add_ready_task(vt);

				continue;
			}
		}
	}
};

evl::__internal::context::context() :
	_impl(std::make_unique<win32_new_context_impl>(this))
{
}

// listen
evl::__internal::__network::listen_task::return_type evl::__internal::__network::listen_task::await_resume() { return std::move(this->_vt->_value); }

void evl::__internal::__network::listen_task::_start(context *ctx, std::weak_ptr<vtask_base> parent_vt) const
{
	this->_vt->_value._sock = socket_t::create_listen(this->_addr, this->_port, ctx->_get_impl<win32_new_context_impl>()->_comp_port);

	// no waiting
	ctx->_add_ready_task(parent_vt.lock());
}

evl::__internal::__network::listen_task::listen_task(listen_task &&) = default;
evl::__internal::__network::listen_task::~listen_task() = default;

// accept
evl::__internal::__network::accept_task::return_type evl::__internal::__network::accept_task::await_resume() { return std::move(this->_vt->_value); }

void evl::__internal::__network::accept_task::_start(context *ctx, std::weak_ptr<vtask_base> parent_vt) const
{
	this->_vt->_ctx = ctx;
	this->_vt->_parent_vt = std::move(parent_vt);

	this->_vt->_accept_buffer.resize((sizeof(sockaddr_t) + 16) * 2);

	this->_vt->_value._sock = this->_server->create_accept(this->_vt->_accept_buffer.data(), ctx->_get_impl<win32_new_context_impl>()->_comp_port);
	this->_vt->_value._sock->_vt_init = this->_vt;
}

evl::__internal::__network::accept_task::accept_task(accept_task &&) = default;
evl::__internal::__network::accept_task::~accept_task() = default;

// connect
evl::__internal::__network::connect_task::return_type evl::__internal::__network::connect_task::await_resume() { return std::move(this->_vt->_value); }

void evl::__internal::__network::connect_task::_start(context *ctx, std::weak_ptr<vtask_base> parent_vt) const
{
	this->_vt->_ctx = ctx;
	this->_vt->_parent_vt = std::move(parent_vt);

	this->_vt->_value._sock = socket_t::create_connect(this->_addr, this->_port, ctx->_get_impl<win32_new_context_impl>()->_comp_port);
	this->_vt->_value._sock->_vt_init = this->_vt;
}

evl::__internal::__network::connect_task::connect_task(connect_task &&) = default;
evl::__internal::__network::connect_task::~connect_task() = default;

// recv
evl::__internal::__network::recv_task::return_type evl::__internal::__network::recv_task::await_resume() { return this->_vt->_done; }

void evl::__internal::__network::recv_task::_start(context *ctx, std::weak_ptr<vtask_base> parent_vt) const
{
	this->_vt->_ctx = ctx;
	this->_vt->_parent_vt = std::move(parent_vt);

	this->_client->recv_async(&this->_vt->_wsa_buf);
	this->_client->_vt_recv = this->_vt;
}

// send
evl::__internal::__network::send_task::return_type evl::__internal::__network::send_task::await_resume() { return this->_vt->_done; }

void evl::__internal::__network::send_task::_start(context *ctx, std::weak_ptr<vtask_base> parent_vt) const
{
	this->_vt->_ctx = ctx;
	this->_vt->_parent_vt = std::move(parent_vt);

	this->_client->send_async(&this->_vt->_wsa_buf);
	this->_client->_vt_send = this->_vt;
}

// tcp_server
evl::__internal::__network::accept_task evl::__internal::__network::tcp_server::accept()
{
	accept_task t;

	t._vt = std::make_shared<vaccept_task>();
	t._server = this->_sock.get();

	return t;
}

evl::__internal::__network::tcp_server::tcp_server(tcp_server &&) = default;
evl::__internal::__network::tcp_server::~tcp_server() = default;

// tcp_client
evl::__internal::__network::recv_task evl::__internal::__network::tcp_client::recv(std::span<char> data)
{
	recv_task t;

	t._vt = std::make_shared<vrecv_task>();
	t._client = this->_sock.get();
	t._vt->_wsa_buf.buf = data.data();
	t._vt->_wsa_buf.len = (ULONG)data.size();

	return t;
}

evl::__internal::__network::send_task evl::__internal::__network::tcp_client::send(std::span<std::add_const_t<char>> data)
{
	send_task t;

	t._vt = std::make_shared<vsend_task>();
	t._client = this->_sock.get();
	t._vt->_wsa_buf.buf = const_cast<char *>(data.data());
	t._vt->_wsa_buf.len = (ULONG)data.size();

	return t;
}

evl::__internal::__network::tcp_client::tcp_client(tcp_client &&) = default;
evl::__internal::__network::tcp_client::~tcp_client() = default;

// listen
evl::__internal::__network::listen_task evl::__internal::__network::listen(const char *addr, uint16_t port)
{
	listen_task t;

	t._vt = std::make_shared<vlisten_task>();
	t._addr = addr;
	t._port = port;

	return t;
}

// connect
evl::__internal::__network::connect_task evl::__internal::__network::connect(const char *addr, uint16_t port)
{
	connect_task t;

	t._vt = std::make_shared<vconnect_task>();
	t._addr = addr;
	t._port = port;

	return t;
}

