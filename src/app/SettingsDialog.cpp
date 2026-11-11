#include "SettingsDialog.h"

#include "ErrorLog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QGroupBox>
#include <QLabel>
#include <QSettings>
#include <QVBoxLayout>

namespace mbt {

const char* const SettingsDialog::LogToConsoleKey = "logging/console";
const char* const SettingsDialog::LogToFileKey = "logging/file";

SettingsDialog::SettingsDialog(QSettings& settings, QWidget* parent)
	: QDialog(parent)
	, m_settings(settings)
{
	setWindowTitle(tr("Settings"));

	const QString logPath = QDir::toNativeSeparators(ErrorLog::instance().filePath());
	const QFileInfo logFile(logPath);

	auto* intro = new QLabel(tr(
		"When something goes wrong (for example, Open Library cannot be reached), "
		"a message explains the problem. The full error, with what MyBookTally was "
		"doing at the time, can also be written down here, which helps when "
		"working out why it happened."), this);
	intro->setWordWrap(true);

	m_console = new QCheckBox(tr("Print the full error to the &console (standard error)"), this);
	auto* consoleHelp = new QLabel(tr(
		"Seen only when MyBookTally is started from a terminal or command prompt."), this);
	consoleHelp->setWordWrap(true);
	consoleHelp->setIndent(24);

	// The file's name and folder are spelled out in the setting itself, so
	// the user knows exactly what will be written and where before turning
	// it on.
	m_file = new QCheckBox(tr("Save the full error to a &log file named %1").arg(logFile.fileName()), this);
	m_fileLocation = new QLabel(tr(
		"File name: %1\n"
		"Folder: %2\n"
		"Full path: %3\n"
		"New errors are added to the end of the file; nothing is ever removed from it.")
			.arg(logFile.fileName(), QDir::toNativeSeparators(logFile.absolutePath()), logPath), this);
	m_fileLocation->setWordWrap(true);
	m_fileLocation->setIndent(24);
	m_fileLocation->setTextFormat(Qt::PlainText);
	// So the path can be copied into a file manager.
	m_fileLocation->setTextInteractionFlags(Qt::TextSelectableByMouse);

	auto* group = new QGroupBox(tr("Error logging"), this);
	auto* groupLayout = new QVBoxLayout(group);
	groupLayout->addWidget(intro);
	groupLayout->addSpacing(6);
	groupLayout->addWidget(m_console);
	groupLayout->addWidget(consoleHelp);
	groupLayout->addSpacing(6);
	groupLayout->addWidget(m_file);
	groupLayout->addWidget(m_fileLocation);

	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

	auto* layout = new QVBoxLayout(this);
	layout->addWidget(group);
	layout->addStretch();
	layout->addWidget(buttons);

	m_console->setChecked(m_settings.value(QLatin1String(LogToConsoleKey),
		ErrorLog::DefaultConsoleEnabled).toBool());
	m_file->setChecked(m_settings.value(QLatin1String(LogToFileKey),
		ErrorLog::DefaultFileEnabled).toBool());

	resize(560, sizeHint().height());
}

void SettingsDialog::applySavedSettings(QSettings& settings)
{
	ErrorLog& log = ErrorLog::instance();
	log.setConsoleEnabled(settings.value(QLatin1String(LogToConsoleKey),
		ErrorLog::DefaultConsoleEnabled).toBool());
	log.setFileEnabled(settings.value(QLatin1String(LogToFileKey),
		ErrorLog::DefaultFileEnabled).toBool());
}

void SettingsDialog::accept()
{
	m_settings.setValue(QLatin1String(LogToConsoleKey), m_console->isChecked());
	m_settings.setValue(QLatin1String(LogToFileKey), m_file->isChecked());
	applySavedSettings(m_settings);
	QDialog::accept();
}

} // namespace mbt
