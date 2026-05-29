#include "iar_dock.hpp"

#include "../core/settings.hpp"
#include "../obs/engine.hpp"
#include "../obs/settings_store.hpp"
#include "iar_settings_dialog.hpp"

#include <obs-module.h>

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTimer>
#include <QVBoxLayout>

namespace iar {

IarDock::IarDock(Engine *engine, QWidget *parent) : QWidget(parent), engine_(engine) {
	setObjectName("IarAudioReturnDock");
	buildUi();

	Settings s = IarSettingsDialog::LoadOrDefaults();
	autoStart_->setChecked(s.auto_start);
	withStream_->setChecked(s.start_with_stream);

	setRunningUi(engine_->running());
	timer_ = new QTimer(this);
	connect(timer_, &QTimer::timeout, this, &IarDock::refreshStatus);
	timer_->start(500);
}

IarDock::~IarDock() = default;

void IarDock::buildUi() {
	auto *root = new QVBoxLayout(this);
	root->setContentsMargins(10, 10, 10, 10);
	root->setSpacing(8);

	statePill_ = new QLabel("Disconnected", this);
	statePill_->setAlignment(Qt::AlignCenter);
	statePill_->setStyleSheet(
	    "padding:7px;border-radius:9px;font-weight:600;"
	    "background:rgba(255,180,84,.12);color:#ffb454;");
	root->addWidget(statePill_);

	startBtn_ = new QPushButton(obs_module_text("Btn.Start"), this);
	startBtn_->setMinimumHeight(38);
	root->addWidget(startBtn_);

	autoStart_ = new QCheckBox(obs_module_text("Field.AutoStart"), this);
	root->addWidget(autoStart_);

	withStream_ = new QCheckBox(obs_module_text("Field.WithStream"), this);
	root->addWidget(withStream_);

	// Compact stats grid.
	auto *grid = new QFormLayout();
	grid->setContentsMargins(2, 4, 2, 4);
	grid->setVerticalSpacing(3);
	grid->setLabelAlignment(Qt::AlignLeft);
	listeners_ = new QLabel("0", this);
	bitrate_ = new QLabel("0.0 kbps", this);
	dropped_ = new QLabel("0", this);
	audio_ = new QLabel("-", this);
	grid->addRow(obs_module_text("Status.Listeners"), listeners_);
	grid->addRow(obs_module_text("Status.Bitrate"), bitrate_);
	grid->addRow(obs_module_text("Status.Dropped"), dropped_);
	grid->addRow(obs_module_text("Status.Audio"), audio_);
	root->addLayout(grid);

	ringBar_ = new QProgressBar(this);
	ringBar_->setRange(0, 100);
	ringBar_->setTextVisible(false);
	ringBar_->setFixedHeight(6);
	root->addWidget(ringBar_);

	warn_ = new QLabel(this);
	warn_->setWordWrap(true);
	warn_->setStyleSheet("color:#ffb454;font-size:11px;");
	warn_->setVisible(false);
	root->addWidget(warn_);

	error_ = new QLabel(this);
	error_->setWordWrap(true);
	error_->setStyleSheet("color:#ff5d6c;font-size:11px;");
	error_->setVisible(false);
	root->addWidget(error_);

	auto *btnRow = new QHBoxLayout();
	settingsBtn_ = new QPushButton(obs_module_text("Btn.Settings"), this);
	toneBtn_ = new QPushButton(obs_module_text("Btn.TestTone"), this);
	copyBtn_ = new QPushButton(obs_module_text("Btn.CopyListener"), this);
	btnRow->addWidget(settingsBtn_);
	btnRow->addWidget(toneBtn_);
	btnRow->addWidget(copyBtn_);
	root->addLayout(btnRow);

	root->addStretch(1);

	auto *brand = new QLabel(
	    "<span style='color:#8b95a7;'>Powered by </span>"
	    "<span style='color:#eef2f8;font-weight:700;'>IRL</span>"
	    "<span style='color:#3b82f6;font-weight:700;'>Hosting</span>",
	    this);
	brand->setAlignment(Qt::AlignCenter);
	brand->setStyleSheet("font-size:11px;padding-top:4px;");
	root->addWidget(brand);

	connect(startBtn_, &QPushButton::clicked, this, &IarDock::onStartStop);
	connect(autoStart_, &QCheckBox::toggled, this, &IarDock::onAutoStartToggled);
	connect(withStream_, &QCheckBox::toggled, this, &IarDock::onWithStreamToggled);
	connect(settingsBtn_, &QPushButton::clicked, this, &IarDock::OpenSettings);
	connect(toneBtn_, &QPushButton::clicked, this, &IarDock::onTestTone);
	connect(copyBtn_, &QPushButton::clicked, this, &IarDock::onCopyListener);
}

void IarDock::setRunningUi(bool running) {
	startBtn_->setText(running ? obs_module_text("Btn.Stop") : obs_module_text("Btn.Start"));
}

void IarDock::OpenSettings() {
	IarSettingsDialog dlg(engine_, this);
	dlg.exec();
	// Reflect any auto-start / config changes the dialog persisted.
	Settings s = IarSettingsDialog::LoadOrDefaults();
	QSignalBlocker block(autoStart_);
	autoStart_->setChecked(s.auto_start);
}

void IarDock::onStartStop() {
	if (engine_->running()) {
		engine_->Stop();
		setRunningUi(false);
		return;
	}
	startEngine(/*fromAutoStart=*/false);
}

void IarDock::startEngine(bool fromAutoStart) {
	Settings s = IarSettingsDialog::LoadOrDefaults();
	s.enabled = true;

	auto v = Validate(s);
	if (!v.warnings.empty()) {
		QString w;
		for (const auto &wn : v.warnings)
			w += "• " + QString::fromStdString(wn) + "\n";
		warn_->setText(w.trimmed());
		warn_->setVisible(true);
	} else {
		warn_->setVisible(false);
	}
	if (!v.ok) {
		QString msg = v.errors.empty() ? QString("Not configured")
		                               : QString::fromStdString(v.errors.front());
		if (fromAutoStart) {
			error_->setText(msg);
			error_->setVisible(true);
		} else {
			QMessageBox::warning(
			    this, obs_module_text("Title"),
			    QString(obs_module_text("Msg.OpenSettingsToConfigure")) + "\n\n" + msg);
		}
		return;
	}

	std::string err;
	if (!engine_->Start(s, err)) {
		if (fromAutoStart) {
			error_->setText(QString::fromStdString(err));
			error_->setVisible(true);
		} else {
			QMessageBox::critical(this, obs_module_text("Title"), QString::fromStdString(err));
		}
		return;
	}
	error_->setVisible(false);
	setRunningUi(true);
}

void IarDock::AutoStartIfEnabled() {
	Settings s = IarSettingsDialog::LoadOrDefaults();
	if (!s.auto_start || engine_->running())
		return;
	startEngine(/*fromAutoStart=*/true);
}

void IarDock::onAutoStartToggled(bool checked) {
	Settings s = IarSettingsDialog::LoadOrDefaults();
	s.auto_start = checked;
	SaveSettings(s);
}

void IarDock::onWithStreamToggled(bool checked) {
	Settings s = IarSettingsDialog::LoadOrDefaults();
	s.start_with_stream = checked;
	SaveSettings(s);
}

void IarDock::OnStreamingStarted() {
	if (!withStream_->isChecked() || engine_->running())
		return;
	startEngine(/*fromAutoStart=*/true); // non-modal: OBS just went live
}

void IarDock::OnStreamingStopped() {
	if (!withStream_->isChecked() || !engine_->running())
		return;
	engine_->Stop();
	setRunningUi(false);
}

void IarDock::onTestTone() {
	if (!engine_->running()) {
		QMessageBox::information(this, obs_module_text("Title"),
		                         obs_module_text("Msg.StartFirst"));
		return;
	}
	engine_->InjectTestTone();
}

void IarDock::onCopyListener() {
	Settings s = IarSettingsDialog::LoadOrDefaults();
	std::string url = BuildListenerHint(s.relay_url, s.listener_token);
	if (url.empty()) {
		QMessageBox::information(this, obs_module_text("Title"),
		                         obs_module_text("Msg.NoListenerToken"));
		return;
	}
	QApplication::clipboard()->setText(QString::fromStdString(url));
}

void IarDock::refreshStatus() {
	bool running = engine_->running();
	if (!running && startBtn_->text() == obs_module_text("Btn.Stop"))
		setRunningUi(false);

	StatsSnapshot st = engine_->Snapshot();
	statePill_->setText(ToString(st.state));
	const char *bg = "rgba(255,180,84,.12)", *fg = "#ffb454";
	switch (st.state) {
	case ConnState::Connected:
		bg = "rgba(56,211,159,.14)";
		fg = "#38d39f";
		break;
	case ConnState::AuthFailed:
	case ConnState::RelayUnreachable:
		bg = "rgba(255,93,108,.14)";
		fg = "#ff5d6c";
		break;
	default:
		break;
	}
	statePill_->setStyleSheet(QString("padding:7px;border-radius:9px;font-weight:600;"
	                                   "background:%1;color:%2;")
	                              .arg(bg)
	                              .arg(fg));

	listeners_->setText(QString::number(st.listeners));
	bitrate_->setText(QString::number(st.kbps_out, 'f', 1) + " kbps");
	dropped_->setText(QString::number(st.dropped_frames));
	ringBar_->setValue(static_cast<int>(st.ring_fill * 100.0));

	if (!running) {
		audio_->setText("-");
		audio_->setStyleSheet("");
	} else if (st.seconds_since_audio > 10.0) {
		audio_->setText(obs_module_text("Audio.Silent"));
		audio_->setStyleSheet("color:#ffb454;");
	} else {
		audio_->setText(obs_module_text("Audio.Present"));
		audio_->setStyleSheet("color:#38d39f;");
	}

	if (running && !st.last_error.empty()) {
		error_->setText(QString::fromStdString(st.last_error));
		error_->setVisible(true);
	} else if (running) {
		error_->setVisible(false);
	}
}

} // namespace iar
