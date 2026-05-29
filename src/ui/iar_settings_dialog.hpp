#pragma once

#include "../core/settings.hpp"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;

namespace iar {

class Engine;

// IarSettingsDialog houses all configuration for the plugin. The dock is a slim
// live-status panel; this dialog is where the streamer pastes a pairing code or
// edits individual fields, then saves. Opened from the dock's "Settings..." button
// and the Tools menu.
class IarSettingsDialog : public QDialog {
	Q_OBJECT
public:
	explicit IarSettingsDialog(Engine *engine, QWidget *parent = nullptr);

	// Loads persisted settings into a Settings struct (used by the dock to start
	// the engine without duplicating the field mapping).
	static Settings LoadOrDefaults();

private slots:
	void onPairingChanged();
	void onCopyListener();
	void onDiagnostics();
	void save();

private:
	void buildUi();
	void load();
	Settings collect() const;
	void updateListenerUrl();

	Engine *engine_;

	QLineEdit *pairing_ = nullptr;
	QLabel *pairingStatus_ = nullptr;
	QLineEdit *relayUrl_ = nullptr;
	QLineEdit *streamId_ = nullptr;
	QLineEdit *token_ = nullptr;
	QLineEdit *listenerToken_ = nullptr;
	QComboBox *track_ = nullptr;
	QComboBox *bitrate_ = nullptr;
	QComboBox *channels_ = nullptr;
	QComboBox *frame_ = nullptr;
	QCheckBox *reconnect_ = nullptr;
	QLabel *listenerUrl_ = nullptr;
	QLabel *qrLabel_ = nullptr;

	std::string realToken_;
	bool tokenEdited_ = false;
	bool savedAutoStart_ = false; // preserved across save (toggled from the dock)
};

} // namespace iar
