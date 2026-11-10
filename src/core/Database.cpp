#include "Database.h"

#include "DatabaseError.h"
#include "Schema.h"
#include "SqlUtil.h"

#include <QDir>
#include <QFileInfo>
#include <QSqlError>
#include <QStandardPaths>

namespace mbt {

Database::Database(const QString& connectionName)
	: m_connectionName(connectionName.isEmpty()
		? QStringLiteral("mybooktally-%1").arg(newId())
		: connectionName)
{
}

Database::~Database()
{
	close();
}

void Database::open(const QString& filePath)
{
	close();

	if (!QSqlDatabase::isDriverAvailable(QStringLiteral("QSQLITE")))
		throw DatabaseError(QStringLiteral(
			"The Qt SQLite driver (QSQLITE) is not available. "
			"Check that the Qt sqldrivers plugin folder was deployed with the app."));

	if (filePath != QLatin1String(":memory:")) {
		// SQLite creates the file but not missing parent folders, and on a
		// first run the per-user app data folder usually does not exist yet.
		const QString folder = QFileInfo(filePath).absolutePath();
		if (!QDir().mkpath(folder))
			throw DatabaseError(QStringLiteral("Could not create the folder %1").arg(folder));
	}

	{
		// Scoped so this handle is gone before any later removeDatabase().
		QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
		db.setDatabaseName(filePath);
		if (!db.open()) {
			const QString message = db.lastError().text();
			db = QSqlDatabase();
			QSqlDatabase::removeDatabase(m_connectionName);
			throw DatabaseError(QStringLiteral("Could not open %1: %2").arg(filePath, message));
		}
	}
	m_filePath = filePath;

	try {
		QSqlDatabase db = connection();
		// SQLite ignores FOREIGN KEY clauses unless this is switched on, and
		// the setting is per connection, so it must be done on every open.
		// Without it, deleting a book would leave orphaned rows in the
		// junction tables and ON DELETE CASCADE would do nothing.
		execSql(db, QStringLiteral("PRAGMA foreign_keys = ON"));
		Schema::migrate(db);
	} catch (...) {
		close();
		throw;
	}
}

void Database::close()
{
	if (!QSqlDatabase::contains(m_connectionName))
		return;
	{
		QSqlDatabase db = QSqlDatabase::database(m_connectionName, false);
		db.close();
	}
	QSqlDatabase::removeDatabase(m_connectionName);
	m_filePath.clear();
}

bool Database::isOpen() const
{
	return QSqlDatabase::contains(m_connectionName)
		&& QSqlDatabase::database(m_connectionName, false).isOpen();
}

QSqlDatabase Database::connection() const
{
	if (!QSqlDatabase::contains(m_connectionName))
		throw DatabaseError(QStringLiteral("The database is not open."));
	return QSqlDatabase::database(m_connectionName, false);
}

QString Database::defaultFilePath()
{
	return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
		.filePath(QStringLiteral("MyBookTally.sqlite"));
}

} // namespace mbt
