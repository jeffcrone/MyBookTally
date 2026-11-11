#pragma once

#include <QDialog>

class QCheckBox;
class QLabel;
class QSettings;

namespace mbt {

// File > Settings: the app-wide preferences. For now, where the full text
// of errors is written (see ErrorLog): to the console, which is on by
// default, and to a log file, which is off by default.
//
// The choices are kept in QSettings and applied to ErrorLog when the user
// presses OK, and again at every start-up by applySavedSettings().
class SettingsDialog : public QDialog
{
	Q_OBJECT

public:
	// Settings keys, public so the tests can check what was saved.
	static const char* const LogToConsoleKey;
	static const char* const LogToFileKey;

	explicit SettingsDialog(QSettings& settings, QWidget* parent = nullptr);

	// Reads the saved choices (or the defaults, the first time) and applies
	// them to ErrorLog. Called once at start-up.
	static void applySavedSettings(QSettings& settings);

	void accept() override;

	// Exposed for the automated tests.
	QCheckBox* consoleCheck() const { return m_console; }
	QCheckBox* fileCheck() const { return m_file; }
	QLabel* fileLocationLabel() const { return m_fileLocation; }

private:
	QSettings& m_settings;
	QCheckBox* m_console = nullptr;
	QCheckBox* m_file = nullptr;
	QLabel* m_fileLocation = nullptr;
};

} // namespace mbt
