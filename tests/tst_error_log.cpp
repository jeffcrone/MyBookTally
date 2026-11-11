// Tests for ErrorLog: what is written where, depending on the two settings
// (console on by default, file off by default).

#include "ErrorLog.h"

#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

using namespace mbt;

class TestErrorLog : public QObject
{
	Q_OBJECT

private:
	QString readFile(const QString& path)
	{
		QFile file(path);
		if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
			return QString();
		return QString::fromUtf8(file.readAll());
	}

	QTemporaryDir m_folder;
	QStringList m_console;
	int m_fileCount = 0;

private slots:
	void initTestCase()
	{
		QVERIFY(m_folder.isValid());
		// The default path is worked out from the app's name, as in the
		// real app.
		QCoreApplication::setApplicationName(QStringLiteral("MyBookTally"));
	}

	void init()
	{
		m_console.clear();
		ErrorLog& log = ErrorLog::instance();
		log.setConsoleSink([this](const QString& text) { m_console.append(text); });
		log.setConsoleEnabled(true);
		log.setFileEnabled(false);
		log.setFilePath(m_folder.filePath(QStringLiteral("log-%1.log").arg(++m_fileCount)));
	}

	void cleanup()
	{
		ErrorLog::instance().setConsoleSink({});
	}

	void defaultsAreConsoleOnFileOff()
	{
		QCOMPARE(ErrorLog::DefaultConsoleEnabled, true);
		QCOMPARE(ErrorLog::DefaultFileEnabled, false);
	}

	void defaultFileIsNextToTheLibrary()
	{
		const QString path = ErrorLog::defaultFilePath();
		QCOMPARE(QFileInfo(path).fileName(), QStringLiteral("MyBookTally.log"));
		QCOMPARE(ErrorLog::fileName(), QStringLiteral("MyBookTally.log"));
		QCOMPARE(QFileInfo(path).absolutePath(),
			QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).absolutePath());
	}

	void entryHasTimeActivityAndDetails()
	{
		const QDateTime when(QDate(2026, 3, 4), QTime(5, 6, 7, 89), Qt::UTC);
		const QString entry = ErrorLog::formatEntry(when, QStringLiteral("Searching"),
			QStringLiteral("  line one\nline two\n"));
		QCOMPARE(entry, QStringLiteral("[2026-03-04T05:06:07.089Z] MyBookTally " MYBOOKTALLY_VERSION " error\n"
			"While: Searching\n"
			"line one\nline two\n\n"));
	}

	void consoleOnlyByDefault()
	{
		const QString path = ErrorLog::instance().filePath();
		QVERIFY(ErrorLog::instance().record(QStringLiteral("Doing a thing"), QStringLiteral("It broke")));
		QCOMPARE(m_console.size(), 1);
		QVERIFY(m_console.first().contains(QStringLiteral("While: Doing a thing")));
		QVERIFY(m_console.first().contains(QStringLiteral("It broke")));
		QVERIFY(!QFile::exists(path));
	}

	void fileAppendsEntries()
	{
		ErrorLog& log = ErrorLog::instance();
		log.setConsoleEnabled(false);
		log.setFileEnabled(true);
		QVERIFY(log.record(QStringLiteral("First"), QStringLiteral("one")));
		QVERIFY(log.record(QStringLiteral("Second"), QStringLiteral("two")));
		QVERIFY(m_console.isEmpty());

		const QString text = readFile(log.filePath());
		QCOMPARE(text.count(QStringLiteral("MyBookTally " MYBOOKTALLY_VERSION " error")), 2);
		QVERIFY(text.indexOf(QStringLiteral("While: First")) < text.indexOf(QStringLiteral("While: Second")));
		QVERIFY(text.contains(QStringLiteral("one")));
		QVERIFY(text.contains(QStringLiteral("two")));
	}

	void bothAtOnce()
	{
		ErrorLog& log = ErrorLog::instance();
		log.setFileEnabled(true);
		QVERIFY(log.record(QStringLiteral("Both"), QStringLiteral("details")));
		QCOMPARE(m_console.size(), 1);
		QVERIFY(readFile(log.filePath()).contains(QStringLiteral("While: Both")));
	}

	void neitherWritesNothing()
	{
		ErrorLog& log = ErrorLog::instance();
		log.setConsoleEnabled(false);
		QVERIFY(log.record(QStringLiteral("Quiet"), QStringLiteral("details")));
		QVERIFY(m_console.isEmpty());
		QVERIFY(!QFile::exists(log.filePath()));
	}

	void missingFolderIsCreated()
	{
		ErrorLog& log = ErrorLog::instance();
		const QString path = m_folder.filePath(QStringLiteral("new/deeper/errors.log"));
		log.setFilePath(path);
		log.setFileEnabled(true);
		QVERIFY(log.record(QStringLiteral("Creating"), QStringLiteral("details")));
		QVERIFY(QFile::exists(path));
	}

	void unwritableFileFailsQuietly()
	{
		ErrorLog& log = ErrorLog::instance();
		// A folder where the file should be: it cannot be opened for writing.
		const QString path = m_folder.filePath(QStringLiteral("a-folder.log"));
		QVERIFY(QDir().mkpath(path));
		log.setFilePath(path);
		log.setFileEnabled(true);
		QVERIFY(!log.record(QStringLiteral("Failing"), QStringLiteral("details")));
		// The console still got it.
		QCOMPARE(m_console.size(), 1);
	}

	void settersAreReadBack()
	{
		ErrorLog& log = ErrorLog::instance();
		log.setConsoleEnabled(false);
		log.setFileEnabled(true);
		log.setFilePath(QStringLiteral("/some/where.log"));
		QVERIFY(!log.consoleEnabled());
		QVERIFY(log.fileEnabled());
		QCOMPARE(log.filePath(), QStringLiteral("/some/where.log"));
	}
};

QTEST_GUILESS_MAIN(TestErrorLog)
#include "tst_error_log.moc"
