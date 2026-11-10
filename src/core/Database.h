#pragma once

#include <QSqlDatabase>
#include <QString>

namespace mbt {

// Owns one SQLite connection.
//
// Qt keeps connections in a process-wide registry keyed by name, and warns if
// a connection is removed while QSqlDatabase handles to it are still alive.
// So this class owns the name, and the repositories ask it for the
// connection on each call instead of keeping their own QSqlDatabase copy.
// That also lets the tests run several independent databases side by side.
class Database
{
public:
	// connectionName defaults to a fresh unique name.
	explicit Database(const QString& connectionName = QString());
	~Database();

	Database(const Database&) = delete;
	Database& operator=(const Database&) = delete;

	// Opens (creating if needed) the SQLite file at filePath and brings its
	// schema up to date. Pass ":memory:" for a throwaway in-memory database.
	// Throws DatabaseError on failure.
	void open(const QString& filePath);
	void close();

	bool isOpen() const;
	QSqlDatabase connection() const;
	QString filePath() const { return m_filePath; }

	// The default location for the user's library:
	// <per-user app data folder>/MyBookTally.sqlite
	// (on Linux ~/.local/share/MyBookTally/, on Windows
	// %APPDATA%\MyBookTally\).
	static QString defaultFilePath();

private:
	QString m_connectionName;
	QString m_filePath;
};

} // namespace mbt
