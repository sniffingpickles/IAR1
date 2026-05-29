#pragma once

#include <QWidget>

class QCheckBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QTimer;

namespace iar {

class Engine;

// IarDock is the slim, glanceable live panel shown while streaming: connection
// state, listeners, outbound bitrate, dropped frames, audio activity, a big
// Start/Stop button, and an auto-start toggle. All configuration lives in the
// separate settings dialog (opened via the "Settings..." button / Tools menu).
class IarDock : public QWidget {
	Q_OBJECT
public:
	explicit IarDock(Engine *engine, QWidget *parent = nullptr);
	~IarDock() override;

	// Start the engine on OBS launch if the user enabled auto-start.
	void AutoStartIfEnabled();
	// Open the configuration dialog (also invoked from the Tools menu).
	void OpenSettings();
	// Called when OBS streaming starts/stops, to drive the "with stream" toggle.
	void OnStreamingStarted();
	void OnStreamingStopped();

private slots:
	void onStartStop();
	void onAutoStartToggled(bool checked);
	void onWithStreamToggled(bool checked);
	void onTestTone();
	void onCopyListener();
	void refreshStatus();

private:
	void buildUi();
	void startEngine(bool fromAutoStart);
	void setRunningUi(bool running);

	Engine *engine_;

	QPushButton *startBtn_ = nullptr;
	QCheckBox *autoStart_ = nullptr;
	QCheckBox *withStream_ = nullptr;
	QPushButton *settingsBtn_ = nullptr;
	QPushButton *toneBtn_ = nullptr;
	QPushButton *copyBtn_ = nullptr;

	QLabel *statePill_ = nullptr;
	QLabel *listeners_ = nullptr;
	QLabel *bitrate_ = nullptr;
	QLabel *dropped_ = nullptr;
	QLabel *audio_ = nullptr;
	QProgressBar *ringBar_ = nullptr;
	QLabel *warn_ = nullptr;
	QLabel *error_ = nullptr;

	QTimer *timer_ = nullptr;
};

} // namespace iar
