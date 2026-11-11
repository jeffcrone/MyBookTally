#pragma once

#include <QDateTime>
#include <QMutex>
#include <QString>

#include <functional>

namespace mbt {

// Where the full text of an error goes, beyond the short message the user
// sees in a popup.
//
// The popup says what went wrong in a sentence; the full version (the URL,
// the HTTP status, the server's reply, the SQL) would only confuse there,
// but it is exactly what is needed to work out why something failed. The
// user chooses in Settings whether it is written to the console (standard
// error), to a log file, both, or neither.
//
// One shared instance, because errors are reported from code that has no
// other connection to the settings (the network client, the database error
// helper). It is safe to use from any thread.
class ErrorLog
{
public:
	static ErrorLog& instance();

	// <app data folder>/MyBookTally.log - next to the library database, so
	// there is one folder to look in. On Linux that is
	// ~/.local/share/MyBookTally, on Windows %APPDATA%\MyBookTally.
	static QString defaultFilePath();
	static QString fileName() { return QStringLiteral("MyBookTally.log"); }

	// The defaults match what a new user gets: console on, file off.
	static constexpr bool DefaultConsoleEnabled = true;
	static constexpr bool DefaultFileEnabled = false;

	void setConsoleEnabled(bool enabled);
	bool consoleEnabled() const;
	void setFileEnabled(bool enabled);
	bool fileEnabled() const;
	// Changing the path does not move an existing log; it only decides
	// where later entries go. Used by the tests to keep out of the real one.
	void setFilePath(const QString& path);
	QString filePath() const;

	// Replaces writing to standard error, so tests can see what would have
	// been printed. Pass an empty function to go back to standard error.
	void setConsoleSink(std::function<void(const QString&)> sink);

	// Records one error, if logging is on. activity says what the app was
	// doing ("Searching Open Library for ..."); details is the full error.
	// Returns false only if the file was meant to be written and could not
	// be. A logging failure is never itself reported, because the error it
	// was trying to record has already been shown to the user.
	bool record(const QString& activity, const QString& details);

	// The text of one entry, ending in a blank line so entries in the file
	// are easy to tell apart.
	static QString formatEntry(const QDateTime& when, const QString& activity, const QString& details);

private:
	ErrorLog();

	mutable QMutex m_mutex;
	bool m_console = DefaultConsoleEnabled;
	bool m_file = DefaultFileEnabled;
	QString m_path;
	std::function<void(const QString&)> m_consoleSink;
};

} // namespace mbt
