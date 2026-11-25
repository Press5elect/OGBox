// SPDX-FileCopyrightText: 2026 Press5elect
// SPDX-License-Identifier: GPL-3.0-or-later
// Additional term (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
//
// OGBox 1.0.4 - the cache drives' write-back through the write broker (ps5_wbremote.h): the core's
// calls (native/kernel/wback.h NcWbRemote) on a ps5_broker.h Client. Only the core's write-back
// thread calls them once they are handed over; its requests are answered in the order sent.

#include "ps5_wbremote.h"

#include "ps5_broker.h"

#include <cerrno>
#include <cstdio>
#include <cstring>

extern "C"
{
#include "native/kernel/wback.h"
}

namespace ogbox::broker
{
#if defined(NC_WB_REMOTE_VERSION)
namespace
{
constexpr int kHelloMs = 8000;      // the loader starts the payload: its first answer
constexpr int kReplyMs = 120000;    // a send's wait (the core waits as long for an answer)
constexpr int kFinishMs = 120000;   // its end, once given up: what it was sent carried out first

struct Helper
{
	Client client;
	std::string strip;
	char name[96] = "";
	explicit Helper(int fd) : client(fd) {}
};

int OpenCall(void* ctx, const char* path, uint32_t* handle, uint64_t* ino)
{
	auto* h = static_cast<Helper*>(ctx);
	std::string p = path;
	if (!h->strip.empty() && p.compare(0, h->strip.size(), h->strip) == 0)
		p.erase(0, h->strip.size());
	if (!::broker::path_allowed(p.data(), p.size()))
		return EACCES;
	Reply r;
	std::vector<uint8_t> body;
	const int e = h->client.Call(Op::open, 0, 0, 0, ::broker::open_write, p.data(), p.size(), r, &body, kReplyMs);
	if (e != 0)
		return e;
	*handle = static_cast<uint32_t>(r.value);
	*ino = 0;
	if (body.size() >= sizeof(::broker::OpenInfo))
	{
		::broker::OpenInfo info;
		std::memcpy(&info, body.data(), sizeof(info));
		*ino = info.ino;
	}
	return 0;
}

bool WriteCall(void* ctx, uint32_t handle, uint64_t off, const struct iovec* iov, int n)
{
	return static_cast<Helper*>(ctx)->client.SendV(Op::write, handle, off, iov, n);
}

bool CloseCall(void* ctx, uint32_t handle)
{
	return static_cast<Helper*>(ctx)->client.Send(Op::close, handle, 0, 0, 0, nullptr, 0);
}

bool ReapCall(void* ctx, int* error, int timeout_ms)
{
	Reply r;
	if (!static_cast<Helper*>(ctx)->client.Receive(r, nullptr, timeout_ms))
		return false;
	*error = r.error;
	return true;
}

bool PingCall(void* ctx)
{
	Reply r;
	std::vector<uint8_t> body;
	return static_cast<Helper*>(ctx)->client.Call(Op::hello, 0, 0, ::broker::kVersion, 0, nullptr, 0, r, &body, kReplyMs) == 0;
}

void DoneCall(void* ctx)
{
	static_cast<Helper*>(ctx)->client.Finish(kFinishMs);
}
} // namespace

bool StartCacheHelper(const std::string& hddRoot, HelperConnector connect, std::string& why, const std::string& localRoot)
{
	const std::string seen = hddRoot.compare(0, localRoot.size(), localRoot) == 0 ? hddRoot.substr(localRoot.size()) : hddRoot;
	if (seen.compare(0, 6, "/data/") != 0)
	{
		why = "the Xbox hard disk (" + hddRoot + ") isn't on the console's storage (/data/): not needed there";
		return false;
	}
	if (!::broker::path_allowed(seen.data(), seen.size()))
	{
		why = "the Xbox hard disk's path (" + hddRoot + ") isn't one the broker takes";
		return false;
	}
	std::string launched;
	const int fd = connect(launched);
	if (fd < 0)
	{
		why = "the broker could not be started: " + launched;
		return false;
	}
	// one for the run: the core uses it until the title ends (or until it fails)
	auto* h = new Helper(fd);
	h->strip = localRoot;
	h->client.timeout_ms = kReplyMs;
	std::string text;
	uint32_t version = 0;
	const int e = h->client.Hello(text, version, kHelloMs);
	if (e != 0 || version != ::broker::kVersion)
	{
		why = e != 0 ? "the broker didn't answer (" + (e < 0 ? h->client.Error() : std::string(std::strerror(e))) + ")"
			: "the broker is of another version (" + std::to_string(version) + ")";
		delete h;
		return false;
	}
	std::snprintf(h->name, sizeof(h->name), "%s", text.c_str());
	static NcWbRemote ops;
	ops.version = NC_WB_REMOTE_VERSION;
	ops.ctx = h;
	ops.name = h->name;
	ops.open = OpenCall;
	ops.write = WriteCall;
	ops.close = CloseCall;
	ops.reap = ReapCall;
	ops.ping = PingCall;
	ops.done = DoneCall;
	nc_wb_set_remote(&ops);
	why = launched + "; " + text;
	return true;
}
#else
bool StartCacheHelper(const std::string&, HelperConnector, std::string& why, const std::string&)
{
	why = "this build's core has no way to use it (native/kernel/wback.h has no nc_wb_set_remote)";
	return false;
}
#endif
} // namespace ogbox::broker
