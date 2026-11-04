/*
 * ps5-native-app-boilerplate - elfldr elevation client.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-FileCopyrightText: 2026 Press5elect
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Additional term (GPL-3.0 section 7(b)): keep the attribution "Crux2 by Press5elect"; see NOTICE.
 * OGBox: from Crux2 08c3507 (ps5/elevation), with this title's ID (PPSA99251) and the client's
 * steps reported for the boot log. Keep the attribution "OGBox by Press5elect" too.
 */
#include "elevation.hpp"

#include <array>
#include <cstdarg>
#include <cstdio>
#include <fcntl.h>
#include <unistd.h>

namespace
{
elevation::StepFn s_step = nullptr;
void *s_step_ctx = nullptr;

void step(const char *format, ...) noexcept __attribute__((format(printf, 1, 2)));
void step(const char *format, ...) noexcept
{
    if (s_step == nullptr)
        return;
    char line[160];
    va_list args;
    va_start(args, format);
    std::vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    s_step(s_step_ctx, line);
}

struct NetSockaddrIn
{
    std::uint8_t length;
    std::uint8_t family;
    std::uint16_t port;
    std::uint32_t address;
    std::uint16_t virtual_port;
    std::uint8_t zero[6];
};

extern "C"
{
    int sceKernelOpen(const char *path, int flags, mode_t mode);
    int sceKernelClose(int descriptor);
    std::int64_t sceKernelRead(int descriptor, void *buffer, std::size_t length);
    int sceNetConnect(int socket, const void *address, std::uint32_t address_length);
    int sceNetSend(int socket, const void *data, std::size_t length, int flags);
    int sceNetRecv(int socket, void *data, std::size_t length, int flags);
    int sceNetSetsockopt(int socket, int level, int option, const void *value, std::uint32_t size);
    int sceNetSocket(const char *name, int domain, int type, int protocol);
    int sceNetSocketClose(int socket);
}

bool send_all(int socket, const void *data, std::size_t size) noexcept
{
    return elevation::wire::transfer(static_cast<const std::uint8_t *>(data), size,
                                     [socket](const auto *bytes, std::size_t remaining)
                                     { return sceNetSend(socket, bytes, remaining, 0); });
}

bool receive(int socket, elevation::wire::Message &message) noexcept
{
    return elevation::wire::transfer(reinterpret_cast<std::uint8_t *>(&message), sizeof(message),
                                     [socket](auto *bytes, std::size_t remaining)
                                     { return sceNetRecv(socket, bytes, remaining, 0); });
}

elevation::Status exchange(int socket, const elevation::wire::Message &request) noexcept
{
    using namespace elevation;
    using wire::Kind;
    if (!send_all(socket, &request, sizeof(request)))
    {
        step("the request could not be sent to the helper");
        return Status::transport_error;
    }
    step("request sent (pid %u, capability %u)", request.pid, static_cast<unsigned>(request.capability));
    wire::Message reply{};
    if (!receive(socket, reply))
    {
        step("no answer from the helper (timeout %d ms)", wire::io_timeout_us / 1000);
        return Status::transport_error;
    }
    if (wire::matches(reply, request, Kind::response) && reply.status != Status::ok)
    {
        step("the helper refused: status %u", static_cast<unsigned>(reply.status));
        return reply.status;
    }
    if (!wire::matches(reply, request, Kind::prepare) || reply.status != Status::ok)
    {
        step("the helper's answer was not \"prepare\" (kind %u, status %u)", static_cast<unsigned>(reply.kind),
             static_cast<unsigned>(reply.status));
        return Status::protocol_error;
    }
    step("the helper asks to prepare: credentials cloned with seteuid");

    // The native same-UID syscall clones credentials before the helper edits them.
    // The helper independently verifies that p_ucred actually changed.
    wire::Message prepared = request;
    prepared.kind = Kind::prepared;
    if (seteuid(geteuid()) != 0)
    {
        prepared.status = Status::prepare_failed;
        step("seteuid failed: the helper is told so");
    }
    if (!send_all(socket, &prepared, sizeof(prepared)) || !receive(socket, reply))
    {
        step("the exchange after prepare broke off");
        return Status::transport_error;
    }
    if (!wire::matches(reply, request, Kind::response))
    {
        step("the helper's last answer was not a response (kind %u)", static_cast<unsigned>(reply.kind));
        return Status::protocol_error;
    }
    if (prepared.status != Status::ok)
        return Status::prepare_failed;
    step("the helper's response: status %u%s", static_cast<unsigned>(reply.status),
         reply.status == Status::ok             ? " (applied: the process left its sandbox)"
         : reply.status == Status::ok_unbalanced ? " (applied the old way: the root's references unbalanced)"
                                                 : "");
    return reply.status;
}

elevation::Status submit(int socket, int helper, const elevation::wire::Message &request) noexcept
{
    using elevation::Status;
    // SceNet uses integer microseconds and its own timeout options, not BSD timeval.
    constexpr int socket_level = 0xffff;
    constexpr int timeout_us = elevation::wire::io_timeout_us;
    for (const int option : {0x1105, 0x1106, 0x1109}) // send, receive, connect
    {
        if (sceNetSetsockopt(socket, socket_level, option, &timeout_us, sizeof(timeout_us)) < 0)
        {
            step("sceNetSetsockopt %#x failed", option);
            return Status::transport_error;
        }
    }
    constexpr std::uint16_t port = 9021;
    const NetSockaddrIn address{sizeof(NetSockaddrIn),
                                2,
                                static_cast<std::uint16_t>((port << 8) | (port >> 8)),
                                0x0100007f,
                                0,
                                {0}};
    if (sceNetConnect(socket, &address, sizeof(address)) < 0)
    {
        step("no payload loader at 127.0.0.1:%u (elfldr is not running)", port);
        return Status::transport_error;
    }
    step("connected to the payload loader at 127.0.0.1:%u", port);

    std::array<std::uint8_t, 4096> buffer{};
    std::size_t sent = 0;
    for (;;)
    {
        const auto count = sceKernelRead(helper, buffer.data(), buffer.size());
        if (count == 0)
            break;
        if (count < 0 || !send_all(socket, buffer.data(), static_cast<std::size_t>(count)))
        {
            step("sending the helper broke off after %zu bytes", sent);
            return Status::transport_error;
        }
        sent += static_cast<std::size_t>(count);
    }
    step("helper sent to the loader (%zu bytes)", sent);
    // elfldr consumes the ELF's section extent, then hands this same connection
    // to the helper as stdin/stdout. Keep it open for the protocol exchange.
    return exchange(socket, request);
}
} // namespace

elevation::Status elevation::request(Capability capability, const char *helper_path, StepFn step_fn,
                                     void *ctx) noexcept
{
    s_step = step_fn;
    s_step_ctx = ctx;
    wire::Message message{};
    message.pid = static_cast<std::uint32_t>(getpid());
    message.capability = capability;
    if (const auto error = wire::validate(message); error != Status::ok)
        return error;
    if (helper_path == nullptr)
        return Status::invalid_request;
    const int helper = sceKernelOpen(helper_path, O_RDONLY, 0);
    if (helper < 0)
    {
        step("%s cannot be opened (%#x)", helper_path, static_cast<unsigned>(helper));
        return Status::unavailable;
    }
    step("%s opened", helper_path);
    const int socket = sceNetSocket("sandbox_elevator", 2, 1, 6);
    if (socket < 0)
        step("sceNetSocket failed (%#x)", static_cast<unsigned>(socket));
    const auto result = socket < 0 ? Status::transport_error : submit(socket, helper, message);
    if (socket >= 0)
        (void)sceNetSocketClose(socket);
    (void)sceKernelClose(helper);
    return result;
}
