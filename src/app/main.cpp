// MyBookTally entry point: parse the command line, open the library
// database, and show the main window.

#include "Database.h"
#include "DatabaseError.h"
#include "MainWindow.h"
#include "UiUtil.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QMessageBox>
#include <QSettings>

int main(int argc, char* argv[])
{
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

	mbt::Database database;
	try {
		database.open(path);
	} catch (const mbt::DatabaseError& error) {
		QMessageBox::critical(nullptr, QStringLiteral("MyBookTally"),
			QStringLiteral("Your library could not be opened.\n\n%1").arg(error.message()));
		return 1;
	}

	QSettings settings(QSettings::UserScope, QStringLiteral("MyBookTally"), QStringLiteral("MyBookTally"));
	mbt::MainWindow window(database, settings);
	window.show();
	return app.exec();
}
