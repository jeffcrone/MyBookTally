#include "SqlUtil.h"

#include "DatabaseError.h"

#include <QDateTime>
#include <QSqlError>
#include <QUuid>

#include <atomic>

namespace mbt {

namespace {

QString describe(const QSqlError& error, const QString& sql)
{
	return QStringLiteral("%1\n\nSQL: %2").arg(error.text(), sql.simplified());
}

} // namespace

QSqlQuery prepareQuery(const QSqlDatabase& db, const QString& sql)
{
	QSqlQuery query(db);
	if (!query.prepare(sql))
		throw DatabaseError(describe(query.lastError(), sql));
	return query;
}

void execQuery(QSqlQuery& query)
{
	if (!query.exec())
		throw DatabaseError(describe(query.lastError(), query.lastQuery()));
}

void execSql(const QSqlDatabase& db, const QString& sql)
{
	QSqlQuery query = prepareQuery(db, sql);
	execQuery(query);
}

QString newId()
{
	return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QString nowUtc()
{
	return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}

QVariant nullIfEmpty(const QString& text)
{
	// A default-constructed QVariant binds as SQL NULL.
	return text.isEmpty() ? QVariant() : QVariant(text);
}

QVariant nullIfEmpty(const QByteArray& bytes)
{
	return bytes.isEmpty() ? QVariant() : QVariant(bytes);
}

QVariant nullIfUnset(const std::optional<int>& value)
{
	return value ? QVariant(*value) : QVariant();
}

QVariant nullIfUnset(const std::optional<double>& value)
{
	return value ? QVariant(*value) : QVariant();
}

std::optional<int> optionalInt(const QVariant& value)
{
	if (value.isNull())
		return std::nullopt;
	return value.toInt();
}

std::optional<double> optionalDouble(const QVariant& value)
{
	if (value.isNull())
		return std::nullopt;
	return value.toDouble();
}

Transaction::Transaction(const QSqlDatabase& db)
	: m_db(db)
{
	// Each guard needs its own savepoint name so nested guards release and
	// roll back the right one. A process-wide counter is enough.
	static std::atomic<int> counter{0};
	m_name = QStringLiteral("mbt_sp_%1").arg(++counter);
	execSql(m_db, QStringLiteral("SAVEPOINT %1").arg(m_name));
}

Transaction::~Transaction()
{
	if (m_finished)
		return;
	// Not committed, so an exception is unwinding. Undo the work, then
	// release the savepoint so it does not linger on the savepoint stack.
	// Errors are ignored: throwing from a destructor during unwinding would
	// terminate the program, and there is nothing better to do here anyway.
	QSqlQuery rollback(m_db);
	rollback.exec(QStringLiteral("ROLLBACK TO SAVEPOINT %1").arg(m_name));
	QSqlQuery release(m_db);
	release.exec(QStringLiteral("RELEASE SAVEPOINT %1").arg(m_name));
}

void Transaction::commit()
{
	execSql(m_db, QStringLiteral("RELEASE SAVEPOINT %1").arg(m_name));
	m_finished = true;
}

} // namespace mbt
