#include "Network.hpp"

#include <WS2tcpip.h>
#include <MSWSock.h>

#pragma comment(lib, "ws2_32.lib")


// static constexpr ULONG_PTR CK_TIMER = 1;


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

struct comp_port
{
	HANDLE val = nullptr;
	int err = ERROR_SUCCESS;

	comp_port()
	{
		this->val = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);

		if (this->val == nullptr)
			this->err = ::GetLastError();
	}

	~comp_port()
	{
		if (err != ERROR_SUCCESS)
			::CloseHandle(this->val);
	}

	static comp_port &get_singleton()
	{
		static comp_port instance;
		return instance;
	}
};

struct winsock_ex
{
	LPFN_ACCEPTEX accept_ex = nullptr;
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

	static winsock_ex &get_singleton()
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

struct evl::__internal::__network::socket_t
{
	SOCKET sock = INVALID_SOCKET;
	sockaddr_t addr{ 0 };

	HANDLE comp_port = nullptr;
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
		result->comp_port = comp_port;

		result->sock = ::WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
		if (result->sock == INVALID_SOCKET)
			throw 1;

		if (::CreateIoCompletionPort((HANDLE)result->sock, result->comp_port, 0, 0) == nullptr)
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
		client->comp_port = comp_port;

		client->sock = ::WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
		if (client->sock == INVALID_SOCKET)
			throw 1;

		if (::CreateIoCompletionPort((HANDLE)client->sock, client->comp_port, 0, 0) == nullptr)
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
	std::unique_ptr<socket_t> prepare_accept(char *buf)
	{
		std::unique_ptr<socket_t> client = std::make_unique<socket_t>(overlapped_type::o_accept);
		client->comp_port = this->comp_port;
		client->parent = this;

		client->sock = ::WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
		if (client->sock == INVALID_SOCKET)
			throw 1;

		DWORD breceived = 0;

		if (winsock_ex::get_singleton().accept_ex(this->sock, client->sock, buf, 0, sizeof(sockaddr_t::s4) + 16, sizeof(sockaddr_t::s4) + 16, &breceived, &client->ovlp_init.base) == FALSE)
		{
			if (::WSAGetLastError() != ERROR_IO_PENDING)
				throw 1;
		}

		if (::CreateIoCompletionPort((HANDLE)client->sock, this->comp_port, 0, 0) == nullptr)
			throw 1;

		return client;
	}
};

struct evl::__internal::__network::vaccept_task : evl::__internal::__network::vinit_task_base
{
	std::weak_ptr<vtask_base> _parent_vt;
	tcp_client _value;
	std::vector<char> _accept_buffer;

	virtual void resume() override
	{
		this->_ctx->_add_ready_task(_parent_vt.lock());
	}
};

struct evl::__internal::__network::vconnect_task : evl::__internal::__network::vinit_task_base
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

void evl::__internal::context::_impl_init()
{
	wsa_initer::init();
	(void)comp_port::get_singleton();
	(void)winsock_ex::get_singleton();

	{
		ULONG actualResolution;
		ZwSetTimerResolution(1, 1, &actualResolution);
	}
}

/*
static void timer_callback(void *arg, DWORD timer_low, DWORD timer_high)
{
	HANDLE comp_port = (HANDLE)arg;
	::PostQueuedCompletionStatus(comp_port, 0, CK_TIMER, nullptr);

	// NOTE: post timer to read_tasks instead of CP?
}
*/


void evl::__internal::context::_impl_waiting_loop()
{
	HANDLE comp_port = comp_port::get_singleton().val;
	DWORD timeout = INFINITE;
	bool wait_timer = !this->_timers.empty();

	if (wait_timer)
	{
		auto delta = this->_timers.back()._time - std::chrono::system_clock::now();
		if (delta <= decltype(delta)(0))
		{
			auto t = std::move(this->_timers.back());
			this->_timers.pop_back();
			this->_add_ready_task(t._task);
			return;
		}

		timeout = (DWORD)std::chrono::duration_cast<std::chrono::milliseconds>(delta).count();

		if (timeout == 0)
			timeout = 1;

		/*
		{
			auto t = std::move(this->_timers.back());
			this->_timers.pop_back();
			std::this_thread::sleep_until(t._time);
			this->_add_ready_task(t._task);
			return;
		}
		*/

		/*
		HANDLE timer = ::CreateWaitableTimerW(NULL, TRUE, NULL);
		if (timer == nullptr)
			throw 1;

		LARGE_INTEGER li_timeout{ 0 };
		li_timeout.QuadPart = this->_timers.back()._time.time_since_epoch().count() + 116444736000000000LL;

		if (::SetWaitableTimer(timer, &li_timeout, 0, &timer_callback, comp_port, FALSE) == 0)
			throw 1;
		*/
	}

	while (42)
	{
		/*
		ULONG removed = 0;
		OVERLAPPED_ENTRY entry{ 0 };
		BOOL success = ::GetQueuedCompletionStatusEx(comp_port, &entry, 1, &removed, timeout, TRUE);

		if (success == FALSE)
		{
			DWORD err = ::GetLastError();

			if (err == WAIT_IO_COMPLETION)
				continue; // alerted by ACP. don't do anything. wait for overlapped event

			if (err == ERROR_TIMEOUT)
				break; // we already handled at least one event and can continue

			throw 1; // wtf
		}

		ULONG_PTR completion_key = entry.lpCompletionKey;
		overlapped_t *ovlp = reinterpret_cast<overlapped_t *>(entry.lpOverlapped);
		DWORD bytes_transfered = entry.dwNumberOfBytesTransferred;
		*/

		DWORD bytes_transfered = 0;
		ULONG_PTR completion_key = 0;
		overlapped_t *ovlp = nullptr;
		BOOL success = ::GetQueuedCompletionStatus(comp_port, &bytes_transfered, &completion_key, reinterpret_cast<OVERLAPPED **>(&ovlp), timeout);
		timeout = 0;

		if (success == FALSE)
		{
			if (wait_timer)
			{
				auto t = std::move(this->_timers.back());
				this->_timers.pop_back();
				this->_add_ready_task(t._task);
				return;
			}

			throw 1;
		}

		/*
		if (completion_key == CK_TIMER)
		{
			auto t = std::move(this->_timers.back());
			this->_timers.pop_back();
			this->_add_ready_task(t._task);

			return;
		}
		*/

		if (ovlp->type == overlapped_type::o_accept)
		{
			auto s = evl::__internal::__network::socket_t::from(ovlp);

			auto vt = std::static_pointer_cast<evl::__internal::__network::vaccept_task>(s->_vt_init.lock());
			s->_vt_init = {};
			s->addr.s4 = *(sockaddr_in *)(vt->_accept_buffer.data() + (sizeof(sockaddr_t::s4) + 16));
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
		{
			if (ovlp)
			{
				auto s = evl::__internal::__network::socket_t::from(ovlp);
				::closesocket(s->sock);
				// s->parent->remove_client(s);
			}

			continue;
		}

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

evl::__internal::__network::recv_task::return_type evl::__internal::__network::recv_task::await_resume() { return this->_vt->_done; }

void evl::__internal::__network::recv_task::_start(context *ctx, std::weak_ptr<vtask_base> parent_vt) const
{
	this->_vt->_ctx = ctx;
	this->_vt->_parent_vt = std::move(parent_vt);

	this->_client->recv_async(&this->_vt->_wsa_buf);
	this->_client->_vt_recv = this->_vt;
}

evl::__internal::__network::recv_task evl::__internal::__network::tcp_client::recv(char *data, size_t size)
{
	recv_task t;

	t._vt = std::make_shared<vrecv_task>();
	t._client = this->_sock.get();
	t._vt->_wsa_buf.buf = data;
	t._vt->_wsa_buf.len = (ULONG)size;

	return t;
}

evl::__internal::__network::send_task::return_type evl::__internal::__network::send_task::await_resume() { return this->_vt->_done; }

void evl::__internal::__network::send_task::_start(context *ctx, std::weak_ptr<vtask_base> parent_vt) const
{
	this->_vt->_ctx = ctx;
	this->_vt->_parent_vt = std::move(parent_vt);

	this->_client->send_async(&this->_vt->_wsa_buf);
	this->_client->_vt_send = this->_vt;
}

evl::__internal::__network::send_task evl::__internal::__network::tcp_client::send(const char *data, size_t size)
{
	send_task t;

	t._vt = std::make_shared<vsend_task>();
	t._client = this->_sock.get();
	t._vt->_wsa_buf.buf = const_cast<char *>(data);
	t._vt->_wsa_buf.len = (ULONG)size;

	return t;
}

evl::__internal::__network::connect_task::return_type evl::__internal::__network::connect_task::await_resume() { return std::move(this->_vt->_value); }

void evl::__internal::__network::connect_task::_start(context *ctx, std::weak_ptr<vtask_base> parent_vt) const
{
	this->_vt->_ctx = ctx;
	this->_vt->_parent_vt = std::move(parent_vt);

	this->_vt->_value._sock = socket_t::create_connect(this->_addr, this->_port, comp_port::get_singleton().val);
	this->_vt->_value._sock->_vt_init = this->_vt;
}

evl::__internal::__network::connect_task::connect_task(connect_task &&) = default;
evl::__internal::__network::connect_task::~connect_task() = default;

evl::__internal::__network::tcp_client::tcp_client(tcp_client &&) = default;
evl::__internal::__network::tcp_client::~tcp_client() = default;

evl::__internal::__network::accept_task::return_type evl::__internal::__network::accept_task::await_resume() { return std::move(this->_vt->_value); }

void evl::__internal::__network::accept_task::_start(context *ctx, std::weak_ptr<vtask_base> parent_vt) const
{
	this->_vt->_ctx = ctx;
	this->_vt->_parent_vt = std::move(parent_vt);

	this->_vt->_accept_buffer.resize((sizeof(sockaddr_t::s4) + 16) * 2);

	this->_vt->_value._sock = this->_server->prepare_accept(this->_vt->_accept_buffer.data());
	this->_vt->_value._sock->_vt_init = this->_vt;
}

evl::__internal::__network::accept_task::accept_task(accept_task &&) = default;
evl::__internal::__network::accept_task::~accept_task() = default;

evl::__internal::__network::tcp_server::tcp_server(tcp_server &&) = default;
evl::__internal::__network::tcp_server::~tcp_server() = default;

evl::__internal::__network::accept_task evl::__internal::__network::tcp_server::accept()
{
	accept_task t;

	t._vt = std::make_shared<vaccept_task>();
	t._server = this->_sock.get();

	return t;
}

evl::__internal::__network::tcp_server evl::__internal::__network::listen(const char *addr, uint16_t port)
{
	tcp_server s;

	s._sock = socket_t::create_listen(addr, port, comp_port::get_singleton().val);

	return s;
}

evl::__internal::__network::connect_task evl::__internal::__network::connect(const char *addr, uint16_t port)
{
	connect_task t;

	t._vt = std::make_shared<vconnect_task>();
	t._addr = addr;
	t._port = port;

	return t;
}

