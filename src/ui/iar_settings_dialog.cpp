#include "iar_settings_dialog.hpp"

#include "../core/iar_version.h"
#include "../obs/engine.hpp"
#include "../obs/settings_store.hpp"
#include "../third_party/qrcodegen.hpp"

#include <obs.h>
#include <obs-module.h>
#include <util/platform.h>

#include <QApplication>
#include <QClipboard>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QVBoxLayout>

#include <cstdio>
#include <ctime>

namespace iar {

static const char *kDefaultRelayUrl = "wss://alerts.irlhosting.com/contribute";

// Render a QR code for `text` into a QPixmap of approximately targetPx pixels.
static QPixmap RenderQr(const std::string &text, int targetPx) {
	if (text.empty())
		return QPixmap();
	using qrcodegen::QrCode;
	QrCode qr = QrCode::encodeText(text.c_str(), QrCode::Ecc::MEDIUM);
	const int size = qr.getSize();
	const int border = 2;
	const int dim = size + border * 2;
	QImage img(dim, dim, QImage::Format_RGB32);
	img.fill(qRgb(255, 255, 255));
	for (int y = 0; y < size; ++y)
		for (int x = 0; x < size; ++x)
			if (qr.getModule(x, y))
				img.setPixel(x + border, y + border, qRgb(0, 0, 0));
	return QPixmap::fromImage(
	    img.scaled(targetPx, targetPx, Qt::KeepAspectRatio, Qt::FastTransformation));
}

Settings IarSettingsDialog::LoadOrDefaults() {
	Settings s;
	if (!LoadSettings(s)) {
		s.relay_url = kDefaultRelayUrl;
		s.track = 6;
		s.bitrate_bps = 32000;
		s.channels = 1;
		s.frame_ms = 20;
		s.sample_rate = 48000;
		s.reconnect = true;
	}
	if (s.relay_url.empty())
		s.relay_url = kDefaultRelayUrl;
	return s;
}

IarSettingsDialog::IarSettingsDialog(Engine *engine, QWidget *parent)
    : QDialog(parent), engine_(engine) {
	setWindowTitle(obs_module_text("Title"));
	setModal(true);
	setMinimumWidth(440);
	buildUi();
	load();
	updateListenerUrl();
}

void IarSettingsDialog::buildUi() {
	auto *root = new QVBoxLayout(this);
	root->setContentsMargins(18, 16, 18, 14);
	root->setSpacing(14);

	auto *header = new QLabel(
	    "<span style='color:#8b95a7;'>Powered by </span>"
	    "<span style='color:#eef2f8;font-weight:800;'>IRL</span>"
	    "<span style='color:#3b82f6;font-weight:800;'>Hosting</span>",
	    this);
	header->setAlignment(Qt::AlignCenter);
	header->setStyleSheet("font-size:15px;");
	root->addWidget(header);

	auto *connBox = new QGroupBox(obs_module_text("Group.Connection"), this);
	auto *connForm = new QFormLayout(connBox);
	connForm->setSpacing(10);
	connForm->setContentsMargins(14, 14, 14, 14);
	connForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	connForm->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

	pairing_ = new QLineEdit(connBox);
	pairing_->setPlaceholderText(obs_module_text("Field.Pairing.Placeholder"));
	connForm->addRow(obs_module_text("Field.Pairing"), pairing_);
	pairingStatus_ = new QLabel("", connBox);
	pairingStatus_->setStyleSheet("color:#38d39f;");
	connForm->addRow("", pairingStatus_);

	relayUrl_ = new QLineEdit(connBox);
	relayUrl_->setPlaceholderText("wss://relay.example.com/contribute");
	connForm->addRow(obs_module_text("Field.RelayURL"), relayUrl_);

	streamId_ = new QLineEdit(connBox);
	streamId_->setPlaceholderText("alerts-main");
	connForm->addRow(obs_module_text("Field.StreamID"), streamId_);

	token_ = new QLineEdit(connBox);
	token_->setEchoMode(QLineEdit::Password);
	token_->setPlaceholderText(obs_module_text("Field.Token.Placeholder"));
	connForm->addRow(obs_module_text("Field.Token"), token_);

	listenerToken_ = new QLineEdit(connBox);
	listenerToken_->setPlaceholderText(obs_module_text("Field.ListenerToken.Placeholder"));
	connForm->addRow(obs_module_text("Field.ListenerToken"), listenerToken_);
	root->addWidget(connBox);

	auto *audioBox = new QGroupBox(obs_module_text("Group.Audio"), this);
	auto *audioForm = new QFormLayout(audioBox);
	audioForm->setSpacing(10);
	audioForm->setContentsMargins(14, 14, 14, 14);
	audioForm->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
	track_ = new QComboBox(audioBox);
	for (int i = 1; i <= 6; ++i)
		track_->addItem(QString("Track %1").arg(i), i);
	audioForm->addRow(obs_module_text("Field.Track"), track_);
	auto *codec = new QLabel("Opus", audioBox);
	audioForm->addRow(obs_module_text("Field.Codec"), codec);
	bitrate_ = new QComboBox(audioBox);
	for (int kbps : {24, 32, 48, 64, 96})
		bitrate_->addItem(QString("%1 kbps").arg(kbps), kbps * 1000);
	audioForm->addRow(obs_module_text("Field.Bitrate"), bitrate_);
	channels_ = new QComboBox(audioBox);
	channels_->addItem(obs_module_text("Channels.Mono"), 1);
	channels_->addItem(obs_module_text("Channels.Stereo"), 2);
	audioForm->addRow(obs_module_text("Field.Channels"), channels_);
	frame_ = new QComboBox(audioBox);
	frame_->addItem("20 ms", 20);
	frame_->addItem("10 ms", 10);
	audioForm->addRow(obs_module_text("Field.FrameSize"), frame_);
	reconnect_ = new QCheckBox(obs_module_text("Field.Reconnect"), audioBox);
	audioForm->addRow("", reconnect_);
	root->addWidget(audioBox);

	auto *listenBox = new QGroupBox(obs_module_text("Group.Listener"), this);
	auto *lv = new QVBoxLayout(listenBox);
	lv->setSpacing(10);
	lv->setContentsMargins(14, 14, 14, 14);
	listenerUrl_ = new QLabel("-", listenBox);
	listenerUrl_->setTextInteractionFlags(Qt::TextSelectableByMouse);
	listenerUrl_->setWordWrap(true);
	listenerUrl_->setStyleSheet("font-family: monospace;");
	lv->addWidget(listenerUrl_);
	auto *row = new QHBoxLayout();
	auto *copyBtn = new QPushButton(obs_module_text("Btn.CopyListener"), listenBox);
	auto *diagBtn = new QPushButton(obs_module_text("Btn.Diagnostics"), listenBox);
	row->addWidget(copyBtn);
	row->addWidget(diagBtn);
	lv->addLayout(row);

	// Moblin QR: scan to point Moblin's Web browser at the listener page.
	qrLabel_ = new QLabel(listenBox);
	qrLabel_->setAlignment(Qt::AlignCenter);
	qrLabel_->setVisible(false);
	lv->addWidget(qrLabel_);
	auto *qrCaption = new QLabel(obs_module_text("Listener.MoblinQR"), listenBox);
	qrCaption->setAlignment(Qt::AlignCenter);
	qrCaption->setWordWrap(true);
	qrCaption->setStyleSheet("color:#8b95a7;font-size:11px;");
	lv->addWidget(qrCaption);
	root->addWidget(listenBox);

	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
	root->addWidget(buttons);

	connect(buttons, &QDialogButtonBox::accepted, this, &IarSettingsDialog::save);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	connect(copyBtn, &QPushButton::clicked, this, &IarSettingsDialog::onCopyListener);
	connect(diagBtn, &QPushButton::clicked, this, &IarSettingsDialog::onDiagnostics);
	connect(pairing_, &QLineEdit::textEdited, this, &IarSettingsDialog::onPairingChanged);
	connect(token_, &QLineEdit::textEdited, this, [this]() { tokenEdited_ = true; });
	connect(relayUrl_, &QLineEdit::textChanged, this, &IarSettingsDialog::updateListenerUrl);
	connect(listenerToken_, &QLineEdit::textChanged, this, &IarSettingsDialog::updateListenerUrl);
}

void IarSettingsDialog::load() {
	Settings s = LoadOrDefaults();
	savedAutoStart_ = s.auto_start;
	realToken_ = s.broadcaster_token;
	relayUrl_->setText(QString::fromStdString(s.relay_url));
	streamId_->setText(QString::fromStdString(s.stream_id));
	listenerToken_->setText(QString::fromStdString(s.listener_token));
	if (s.track >= 1 && s.track <= 6)
		track_->setCurrentIndex(s.track - 1);
	int bIdx = bitrate_->findData(s.bitrate_bps ? s.bitrate_bps : 32000);
	bitrate_->setCurrentIndex(bIdx < 0 ? 1 : bIdx);
	channels_->setCurrentIndex(s.channels == 2 ? 1 : 0);
	frame_->setCurrentIndex(s.frame_ms == 10 ? 1 : 0);
	reconnect_->setChecked(s.reconnect);
	if (!realToken_.empty()) {
		token_->clear();
		token_->setPlaceholderText(obs_module_text("Field.Token.Saved"));
	}
	tokenEdited_ = false;
}

void IarSettingsDialog::onPairingChanged() {
	const QString text = pairing_->text().trimmed();
	if (text.isEmpty()) {
		pairingStatus_->setText("");
		return;
	}
	Settings s;
	if (!ParsePairingCode(text.toStdString(), s)) {
		pairingStatus_->setStyleSheet("color:#ff5d6c;");
		pairingStatus_->setText(obs_module_text("Pairing.Invalid"));
		return;
	}
	relayUrl_->setText(QString::fromStdString(s.relay_url));
	streamId_->setText(QString::fromStdString(s.stream_id));
	listenerToken_->setText(QString::fromStdString(s.listener_token));
	// The decoded broadcaster token lives in realToken_; keep tokenEdited_ false
	// so collect() persists realToken_ rather than the (hidden, empty) field.
	realToken_ = s.broadcaster_token;
	tokenEdited_ = false;
	token_->clear();
	token_->setPlaceholderText(obs_module_text("Field.Token.Saved"));
	channels_->setCurrentIndex(s.channels == 2 ? 1 : 0);
	int bIdx = bitrate_->findData(s.bitrate_bps);
	if (bIdx >= 0)
		bitrate_->setCurrentIndex(bIdx);
	updateListenerUrl();
	pairingStatus_->setStyleSheet("color:#38d39f;");
	pairingStatus_->setText(obs_module_text("Pairing.Applied"));
}

void IarSettingsDialog::updateListenerUrl() {
	std::string url = BuildListenerHint(relayUrl_->text().toStdString(),
	                                    listenerToken_->text().toStdString());
	listenerUrl_->setText(url.empty() ? "-" : QString::fromStdString(url));

	std::string deeplink = BuildMoblinDeepLink(url);
	if (deeplink.empty()) {
		qrLabel_->setVisible(false);
	} else {
		QPixmap pm = RenderQr(deeplink, 180);
		if (!pm.isNull()) {
			qrLabel_->setPixmap(pm);
			qrLabel_->setVisible(true);
		}
	}
}

Settings IarSettingsDialog::collect() const {
	Settings s;
	s.relay_url = relayUrl_->text().trimmed().toStdString();
	s.stream_id = streamId_->text().trimmed().toStdString();
	s.broadcaster_token = tokenEdited_ ? token_->text().toStdString() : realToken_;
	s.listener_token = listenerToken_->text().trimmed().toStdString();
	s.track = track_->currentData().toInt();
	s.bitrate_bps = bitrate_->currentData().toInt();
	s.channels = channels_->currentData().toInt();
	s.frame_ms = frame_->currentData().toInt();
	s.sample_rate = 48000;
	s.reconnect = reconnect_->isChecked();
	s.auto_start = savedAutoStart_; // owned by the dock's toggle; preserve it
	return s;
}

void IarSettingsDialog::save() {
	Settings s = collect();
	auto v = Validate(s);
	if (!v.ok) {
		QString msg;
		for (const auto &e : v.errors)
			msg += "• " + QString::fromStdString(e) + "\n";
		if (!v.warnings.empty()) {
			msg += "\n";
			for (const auto &wn : v.warnings)
				msg += "⚠ " + QString::fromStdString(wn) + "\n";
		}
		QMessageBox::warning(this, obs_module_text("Title"), msg.trimmed());
		return; // keep the dialog open so the user can fix it
	}
	SaveSettings(s);
	accept();
}

void IarSettingsDialog::onCopyListener() {
	std::string url = BuildListenerHint(relayUrl_->text().toStdString(),
	                                    listenerToken_->text().toStdString());
	if (url.empty()) {
		QMessageBox::information(this, obs_module_text("Title"),
		                         obs_module_text("Msg.NoListenerToken"));
		return;
	}
	QApplication::clipboard()->setText(QString::fromStdString(url));
}

void IarSettingsDialog::onDiagnostics() {
	char *dir = obs_module_config_path("");
	if (dir) {
		os_mkdirs(dir);
		bfree(dir);
	}
	char *p = obs_module_config_path("diagnostics.txt");
	std::string path = p ? p : "";
	bfree(p);

	StatsSnapshot st = engine_->Snapshot();
	FILE *f = os_fopen(path.c_str(), "w");
	if (!f) {
		QMessageBox::warning(this, obs_module_text("Title"), obs_module_text("Msg.DiagFailed"));
		return;
	}
	std::time_t now = std::time(nullptr);
	std::fprintf(f, "IRL Audio Return diagnostic bundle\n");
	std::fprintf(f, "generated: %s", std::ctime(&now));
	std::fprintf(f, "plugin_version: %s\n", IAR_PLUGIN_VERSION);
	std::fprintf(f, "obs_version: %s\n", obs_get_version_string());
	std::fprintf(f, "\n[settings]\n");
	std::fprintf(f, "relay_url: %s\n", relayUrl_->text().toStdString().c_str());
	std::fprintf(f, "stream_id: %s\n", streamId_->text().toStdString().c_str());
	std::fprintf(f, "token: %s\n", MaskSecret(realToken_).c_str());
	std::fprintf(f, "listener_token: %s\n",
	             MaskSecret(listenerToken_->text().toStdString()).c_str());
	std::fprintf(f, "track: %d\n", track_->currentData().toInt());
	std::fprintf(f, "bitrate_bps: %d\n", bitrate_->currentData().toInt());
	std::fprintf(f, "channels: %d\n", channels_->currentData().toInt());
	std::fprintf(f, "frame_ms: %d\n", frame_->currentData().toInt());
	std::fprintf(f, "\n[status]\n");
	std::fprintf(f, "state: %s\n", ToString(st.state));
	std::fprintf(f, "listeners: %d\n", st.listeners);
	std::fprintf(f, "kbps_out: %.1f\n", st.kbps_out);
	std::fprintf(f, "frames_sent: %llu\n", (unsigned long long)st.frames_sent);
	std::fprintf(f, "dropped_frames: %llu\n", (unsigned long long)st.dropped_frames);
	std::fprintf(f, "ring_fill: %.2f\n", st.ring_fill);
	std::fprintf(f, "seconds_since_audio: %.1f\n", st.seconds_since_audio);
	std::fprintf(f, "last_error: %s\n", st.last_error.c_str());
	std::fclose(f);

	QMessageBox::information(
	    this, obs_module_text("Title"),
	    QString(obs_module_text("Msg.DiagSaved")).arg(QString::fromStdString(path)));
}

} // namespace iar
