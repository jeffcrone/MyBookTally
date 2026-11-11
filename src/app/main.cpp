// MyBookTally entry point: parse the command line, open the library
// database, and show the main window.

#include "Database.h"
#include "DatabaseError.h"
#include "ErrorLog.h"
#include "MainWindow.h"
#include "SettingsDialog.h"
#include "UiUtil.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QMessageBox>
#include <QSettings>

#ifdef Q_OS_WIN
// Keeps windows.h from defining min/max macros and the many rarely used
// APIs it otherwise pulls in.
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdio>

namespace {

// MyBookTally is a Windows GUI program, which Windows starts without a
// console: anything written to standard error is simply lost, so the
// "print errors to the console" setting would do nothing. When the program
// was started from a Command Prompt or PowerShell, this connects standard
// error to that window. If standard error was already redirected (to a
// file, say) it is left alone, and when there is no parent console (started
// from Explorer or the Start menu) nothing happens.
void attachToParentConsole()
{
	const HANDLE handle = GetStdHandle(STD_ERROR_HANDLE);
	if (handle != nullptr && handle != INVALID_HANDLE_VALUE && GetFileType(handle) != FILE_TYPE_UNKNOWN)
		return;
	if (!AttachConsole(ATTACH_PARENT_PROCESS))
		return;
	FILE* stream = nullptr;
	freopen_s(&stream, "CONOUT$", "w", stderr);
}

} // namespace
#endif

int main(int argc, char* argv[])
{
#ifdef Q_OS_WIN
	attachToParentConsole();
#endif
	QApplication app(argc, argv);
	// The application name decides where the library and settings live
	// (see Database::defaultFilePath()). No organization name is set, so
	// the folder is just "MyBookTally" rather than "<org>/MyBookTally".
	QApplication::setApplicationName(QStringLiteral("MyBookTally"));
	QApplication::setApplicationDisplayName(QStringLiteral("MyBookTally"));
	QApplication::setApplicationVersion(QStringLiteral(MYBOOKTALLY_VERSION));
	QApplication::setWindowIcon(mbt::appIcon());
	// Lets Linux desktops match the window to mybooktally.desktop, so the
	// taskbar shows the right icon and name.
	QGuiApplication::setDesktopFileName(QStringLiteral("mybooktally"));

	QCommandLineParser parser;
	parser.setApplicationDescription(QStringLiteral("Keep track of the books in your personal collection."));
	parser.addHelpOption();
	parser.addVersionOption();
	// Mainly for trying things out without touching the real library.
	const QCommandLineOption databaseOption(QStringList{QStringLiteral("d"), QStringLiteral("database")},
		QStringLiteral("Open the library stored in <file> instead of the default one."),
		QStringLiteral("file"));
	parser.addOption(databaseOption);
	parser.process(app);

	const QString path = parser.isSet(databaseOption) ? parser.value(databaseOption)
													  : mbt::Database::defaultFilePath();

	// Read before the database is opened, so a failure to open it is logged
	// the way the user asked.
	QSettings settings(QSettings::UserScope, QStringLiteral("MyBookTally"), QStringLiteral("MyBookTally"));
	mbt::SettingsDialog::applySavedSettings(settings);

	mbt::Database database;
	try {
		database.open(path);
	} catch (const mbt::DatabaseError& error) {
		mbt::ErrorLog::instance().record(QStringLiteral("Opening the library %1").arg(path), error.message());
		QMessageBox::critical(nullptr, QStringLiteral("MyBookTally"),
			QStringLiteral("Your library could not be opened.\n\n%1").arg(error.message()));
		return 1;
	}

	mbt::MainWindow window(database, settings);
	window.show();
	return app.exec();
}
