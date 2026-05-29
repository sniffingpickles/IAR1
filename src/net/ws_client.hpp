#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ix {
class WebSocket;
}

namespace iar {

// WsClient wraps an IXWebSocket connection used to contribute Opus to the relay.
// TLS (wss://) is provided by IXWebSocket's platform backend (SecureTransport on
// macOS, mbedTLS elsewhere). All callbacks are invoked on IXWebSocket's internal
// thread; the engine marshals state with atomics.
class WsClient {
public:
	using OpenFn = std::function<void()>;
	using TextFn = std::function<void(const std::string &)>;
	using CloseFn = std::function<void(uint16_t code, const std::string &reason)>;
	using ErrorFn = std::function<void(const std::string &reason)>;

	WsClient();
	~WsClient();

	WsClient(const WsClient &) = delete;
	WsClient &operator=(const WsClient &) = delete;

	void set_on_open(OpenFn fn) { on_open_ = std::move(fn); }
	void set_on_text(TextFn fn) { on_text_ = std::move(fn); }
	void set_on_close(CloseFn fn) { on_close_ = std::move(fn); }
	void set_on_error(ErrorFn fn) { on_error_ = std::move(fn); }

	// Connect starts the connection to url. auto_reconnect enables IXWebSocket's
	// backoff reconnection (hello is re-sent on each Open).
	void Connect(const std::string &url, bool auto_reconnect);

	// Stop closes and tears down the connection (blocking, safe for shutdown).
	void Stop();

	void SendText(const std::string &text);
	// SendBinary copies `len` bytes. Returns false if the socket is not open.
	bool SendBinary(const std::uint8_t *data, std::size_t len);

	bool IsOpen() const;
	// BufferedAmount is bytes queued in the socket's send buffer; used for
	// congestion-based drop decisions.
	std::size_t BufferedAmount() const;

private:
	std::unique_ptr<ix::WebSocket> ws_;
	OpenFn on_open_;
	TextFn on_text_;
	CloseFn on_close_;
	ErrorFn on_error_;
};

} // namespace iar
