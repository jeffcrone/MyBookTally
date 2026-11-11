#include "ErrorLog.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutexLocker>
#include <QStandardPaths>

#include <cstdio>

namespace mbt {

ErrorLog& ErrorLog::instance()
{
	static ErrorLog log;
	return log;
}

ErrorLog::ErrorLog()
	: m_path(defaultFilePath())
{
}

QString ErrorLog::defaultFilePath()
{
	return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath(fileName());
}

void ErrorLog::setConsoleEnabled(bool enabled)
{
	const QMutexLocker lock(&m_mutex);
	m_console = enabled;
}

bool ErrorLog::consoleEnabled() const
{
	const QMutexLocker lock(&m_mutex);
	return m_console;
}

void ErrorLog::setFileEnabled(bool enabled)
{
	const QMutexLocker lock(&m_mutex);
	m_file = enabled;
}

bool ErrorLog::fileEnabled() const
{
	const QMutexLocker lock(&m_mutex);
	return m_file;
}

void ErrorLog::setFilePath(const QString& path)
{
	const QMutexLocker lock(&m_mutex);
	m_path = path;
}

QString ErrorLog::filePath() const
{
	const QMutexLocker lock(&m_mutex);
	return m_path;
}

void ErrorLog::setConsoleSink(std::function<void(const QString&)> sink)
{
	const QMutexLocker lock(&m_mutex);
	m_consoleSink = std::move(sink);
}

QString ErrorLog::formatEntry(const QDateTime& when, const QString& activity, const QString& details)
{
	// UTC with an explicit "Z", so entries written before and after a
	// daylight-saving change still sort and compare correctly.
	QString entry = QStringLiteral("[%1] MyBookTally %2 error\n")
		.arg(when.toUTC().toString(Qt::ISODateWithMs), QStringLiteral(MYBOOKTALLY_VERSION));
	entry += QStringLiteral("While: %1\n").arg(activity);
	entry += details.trimmed();
	entry += QStringLiteral("\n\n");
	return entry;
}

bool ErrorLog::record(const QString& activity, const QString& details)
{
	const QMutexLocker lock(&m_mutex);
	if (!m_console && !m_file)
		return true;

	const QString entry = formatEntry(QDateTime::currentDateTimeUtc(), activity, details);

	if (m_console) {
		if (m_consoleSink) {
			m_consoleSink(entry);
		} else {
			// Straight to stderr rather than qWarning(): Qt's message
			// handler may add its own prefix or be redirected, and this text
			// is meant to be read as-is.
			std::fputs(entry.toUtf8().constData(), stderr);
			std::fflush(stderr);
		}
	}

	if (!m_file)
		return true;
	// The folder normally exists already (the database lives there), but a
	// --database elsewhere means it may not.
	QDir().mkpath(QFileInfo(m_path).absolutePath());
	QFile file(m_path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
		return false;
	const QByteArray bytes = entry.toUtf8();
	return file.write(bytes) == bytes.size();
}

} // namespace mbt
