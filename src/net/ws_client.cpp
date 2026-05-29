#include "ws_client.hpp"

#include <ixwebsocket/IXNetSystem.h>
#include <ixwebsocket/IXWebSocket.h>

#include <atomic>

namespace iar {
namespace {
// IXWebSocket requires WSA startup on Windows; do it once per process.
struct NetSystemInit {
	NetSystemInit() { ix::initNetSystem(); }
	~NetSystemInit() { ix::uninitNetSystem(); }
};
NetSystemInit g_net_init;
} // namespace

WsClient::WsClient() : ws_(std::make_unique<ix::WebSocket>()) {}

WsClient::~WsClient() {
	Stop();
}

void WsClient::Connect(const std::string &url, bool auto_reconnect) {
	ws_->setUrl(url);

	// Advertise the IAR1 subprotocol.
	ws_->setExtraHeaders(ix::WebSocketHttpHeaders{{"Sec-WebSocket-Protocol", "iar1"}});

	// Application-level + transport-level keepalive.
	ws_->setPingInterval(15);

	if (auto_reconnect) {
		ws_->enableAutomaticReconnection();
		ws_->setMinWaitBetweenReconnectionRetries(1000);   // 1s
		ws_->setMaxWaitBetweenReconnectionRetries(15000);  // 15s
	} else {
		ws_->disableAutomaticReconnection();
	}

	ws_->setOnMessageCallback([this](const ix::WebSocketMessagePtr &msg) {
		switch (msg->type) {
		case ix::WebSocketMessageType::Open:
			if (on_open_)
				on_open_();
			break;
		case ix::WebSocketMessageType::Message:
			if (!msg->binary && on_text_)
				on_text_(msg->str);
			// Inbound binary from the relay is not part of IAR1 v1; ignore.
			break;
		case ix::WebSocketMessageType::Close:
			if (on_close_)
				on_close_(static_cast<uint16_t>(msg->closeInfo.code), msg->closeInfo.reason);
			break;
		case ix::WebSocketMessageType::Error:
			if (on_error_)
				on_error_(msg->errorInfo.reason);
			break;
		default:
			break;
		}
	});

	ws_->start();
}

void WsClient::Stop() {
	if (ws_) {
		ws_->stop();
	}
}

void WsClient::SendText(const std::string &text) {
	ws_->sendText(text);
}

bool WsClient::SendBinary(const std::uint8_t *data, std::size_t len) {
	if (ws_->getReadyState() != ix::ReadyState::Open)
		return false;
	// IXWebSocket copies the buffer internally.
	std::string buf(reinterpret_cast<const char *>(data), len);
	auto res = ws_->sendBinary(buf);
	return res.success;
}

bool WsClient::IsOpen() const {
	return ws_->getReadyState() == ix::ReadyState::Open;
}

std::size_t WsClient::BufferedAmount() const {
	return ws_->bufferedAmount();
}

} // namespace iar
