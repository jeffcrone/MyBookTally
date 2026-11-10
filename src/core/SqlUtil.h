#pragma once

// Small helpers shared by every repository, so each one does not repeat the
// prepare / bind / check-for-errors boilerplate that Qt's SQL API needs.

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QString>
#include <QVariant>

#include <optional>

namespace mbt {

// Prepares sql on db. Throws DatabaseError if SQLite rejects the statement
// (a typo in a query shows up here, with the SQL in the message).
QSqlQuery prepareQuery(const QSqlDatabase& db, const QString& sql);

// Executes an already prepared and bound query. Throws DatabaseError on
// failure, including constraint violations.
void execQuery(QSqlQuery& query);

// Prepares and executes a statement with no parameters, such as a CREATE
// TABLE or a PRAGMA.
void execSql(const QSqlDatabase& db, const QString& sql);

// A fresh GUID as lowercase text without braces.
QString newId();

// The current time as ISO 8601 UTC text with milliseconds, which sorts
// correctly as a plain string.
QString nowUtc();

// Conversions between the C++ "not set" conventions (empty string, empty
// optional) and SQL NULL.
QVariant nullIfEmpty(const QString& text);
QVariant nullIfEmpty(const QByteArray& bytes);
QVariant nullIfUnset(const std::optional<int>& value);
QVariant nullIfUnset(const std::optional<double>& value);
std::optional<int> optionalInt(const QVariant& value);
std::optional<double> optionalDouble(const QVariant& value);

// RAII guard for an all-or-nothing group of statements.
//
// Uses SQLite SAVEPOINTs rather than BEGIN/COMMIT because savepoints nest:
// importing a file runs many BookRepository::insert() calls inside one outer
// guard, and each insert opens its own guard too. BEGIN inside BEGIN is an
// error in SQLite; SAVEPOINT inside SAVEPOINT is not. The outermost
// savepoint behaves exactly like a transaction.
//
// If the guard is destroyed without commit() - because an exception is
// propagating - everything since the guard was created is rolled back.
class Transaction
{
public:
	explicit Transaction(const QSqlDatabase& db);
	~Transaction();

	Transaction(const Transaction&) = delete;
	Transaction& operator=(const Transaction&) = delete;

	void commit();

private:
	QSqlDatabase m_db;
	QString m_name;
	bool m_finished = false;
};

} // namespace mbt
