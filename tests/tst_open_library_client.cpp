// Tests for OpenLibraryClient against a fake server on 127.0.0.1, so they
// need no internet connection and never send anything to the real Open
// Library. They check the rules the client exists to keep: one request per
// second at most, nothing about the user in the request, errors described
// and logged, and repeated requests answered from the cache.

#include "ErrorLog.h"
#include "FakeHttpServer.h"
#include "OpenLibrary.h"
#include "OpenLibraryClient.h"
#include "OpenLibrarySamples.h"

#include <QRegularExpression>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

using namespace mbt;

namespace {

// Collects the replies a test's requests produce.
struct Replies
{
	QList<OpenLibraryReply> received;
	OpenLibraryClient::Callback callback()
	{
		return [this](const OpenLibraryReply& reply) { received.append(reply); };
	}
};

} // namespace

class TestOpenLibraryClient : public QObject
{
	Q_OBJECT

private:
	QUrl url(const QString& path) const
	{
		QUrl result = m_server->baseUrl();
		result.setPath(path);
		return result;
	}

	std::unique_ptr<test::FakeHttpServer> m_server;
	std::unique_ptr<OpenLibraryClient> m_client;
	QTemporaryDir m_folder;
	QStringList m_console;

private slots:
	void initTestCase()
	{
		QVERIFY(m_folder.isValid());
	}

	void init()
	{
		m_server = std::make_unique<test::FakeHttpServer>();
		QVERIFY(m_server->listen());
		m_client = std::make_unique<OpenLibraryClient>();
		m_client->setApiBase(m_server->baseUrl());
		m_client->setCoversBase(m_server->baseUrl());
		// Logging goes to this test's own places, never the user's.
		m_console.clear();
		ErrorLog& log = ErrorLog::instance();
		log.setConsoleSink([this](const QString& text) { m_console.append(text); });
		log.setConsoleEnabled(true);
		log.setFileEnabled(false);
		log.setFilePath(m_folder.filePath(QStringLiteral("client.log")));
	}

	void cleanup()
	{
		m_client.reset();
		m_server.reset();
		ErrorLog::instance().setConsoleSink({});
	}

	void defaultsFollowOpenLibraryRules()
	{
		const OpenLibraryClient client;
		// Open Library's limit for clients that do not send contact details.
		QCOMPARE(client.minimumIntervalMs(), 1000);
		QCOMPARE(OpenLibraryClient::DefaultMinimumIntervalMs, 1000);
		QCOMPARE(client.apiBase(), QUrl(QStringLiteral("https://openlibrary.org")));
		QCOMPARE(client.coversBase(), QUrl(QStringLiteral("https://covers.openlibrary.org")));
	}

	void userAgentNamesOnlyTheApp()
	{
		QCOMPARE(OpenLibraryClient::userAgent(), QByteArray("MyBookTally/" MYBOOKTALLY_VERSION));
	}

	void getReturnsTheBody()
	{
		m_server->setJson(QStringLiteral("/search.json"), test::samples::searchHobbit());
		Replies replies;
		QObject context;
		m_client->get(url(QStringLiteral("/search.json")), QStringLiteral("test"), &context, replies.callback());
		// Never called before get() returns.
		QCOMPARE(replies.received.size(), 0);
		QTRY_COMPARE(replies.received.size(), 1);
		const OpenLibraryReply& reply = replies.received.first();
		QVERIFY(reply.ok);
		QCOMPARE(reply.httpStatus, 200);
		QCOMPARE(reply.body, test::samples::searchHobbit());
		QVERIFY(reply.error.isEmpty());
		QCOMPARE(m_client->pendingCount(), 0);
	}

	void requestsCarryNothingAboutTheUser()
	{
		// The server tries to set a cookie; the next request must not send
		// it back, or it could tie lookups together.
		test::FakeHttpServer::Response withCookie;
		withCookie.body = "{}";
		withCookie.extraHeaders.append(qMakePair(QByteArray("Set-Cookie"), QByteArray("session=abc123; Path=/")));
		m_server->setResponse(QStringLiteral("/a.json"), withCookie);
		m_server->setJson(QStringLiteral("/b.json"), "{}");
		m_client->setMinimumInterval(0);

		Replies replies;
		QObject context;
		m_client->get(url(QStringLiteral("/a.json")), QStringLiteral("test"), &context, replies.callback());
		QTRY_COMPARE(replies.received.size(), 1);
		m_client->get(url(QStringLiteral("/b.json")), QStringLiteral("test"), &context, replies.callback());
		QTRY_COMPARE(replies.received.size(), 2);

		QCOMPARE(m_server->requests().size(), 2);
		const QRegularExpression emailLike(QStringLiteral(R"([^\s@]+@[^\s@]+\.[^\s@]+)"));
		for (const auto& request : m_server->requests()) {
			QCOMPARE(request.method, QByteArray("GET"));
			QCOMPARE(request.headers.value("user-agent"), OpenLibraryClient::userAgent());
			QVERIFY2(!request.headers.contains("cookie"), request.rawHead.constData());
			QCOMPARE(request.headers.value("accept-language"), QByteArray("en"));
			QVERIFY(!request.headers.contains("from"));
			QVERIFY(!request.headers.contains("referer"));
			QVERIFY(!request.headers.contains("authorization"));
			QVERIFY2(!emailLike.match(QString::fromLatin1(request.rawHead)).hasMatch(),
				request.rawHead.constData());
		}
	}

	void requestsAreAtLeastAnIntervalApart()
	{
		// The real interval is a second; a shorter one keeps the test quick
		// while checking the same spacing logic.
		constexpr int interval = 250;
		m_client->setMinimumInterval(interval);
		for (const char* path : {"/1.json", "/2.json", "/3.json", "/4.json"})
			m_server->setJson(QString::fromLatin1(path), "{}");

		Replies replies;
		QObject context;
		for (const char* path : {"/1.json", "/2.json", "/3.json", "/4.json"})
			m_client->get(url(QString::fromLatin1(path)), QStringLiteral("test"), &context, replies.callback());
		QCOMPARE(m_client->pendingCount(), 4);
		QTRY_COMPARE_WITH_TIMEOUT(replies.received.size(), 4, 10000);

		const auto& requests = m_server->requests();
		QCOMPARE(requests.size(), 4);
		for (int i = 1; i < requests.size(); ++i) {
			const qint64 gap = requests.at(i).receivedAtMs - requests.at(i - 1).receivedAtMs;
			// A few milliseconds of slack for the timer and the loopback
			// connection; the client measures from when it sends.
			QVERIFY2(gap >= interval - 15, qPrintable(QStringLiteral("gap %1 ms").arg(gap)));
		}
		// Sent in the order asked for.
		QCOMPARE(requests.at(0).path(), QStringLiteral("/1.json"));
		QCOMPARE(requests.at(3).path(), QStringLiteral("/4.json"));
	}

	void oneSecondLimitHoldsForRealInterval()
	{
		// The same check at the real setting, with two requests.
		m_server->setJson(QStringLiteral("/1.json"), "{}");
		m_server->setJson(QStringLiteral("/2.json"), "{}");
		Replies replies;
		QObject context;
		m_client->get(url(QStringLiteral("/1.json")), QStringLiteral("test"), &context, replies.callback());
		m_client->get(url(QStringLiteral("/2.json")), QStringLiteral("test"), &context, replies.callback());
		QTRY_COMPARE_WITH_TIMEOUT(replies.received.size(), 2, 10000);
		const auto& requests = m_server->requests();
		QVERIFY(requests.at(1).receivedAtMs - requests.at(0).receivedAtMs >= 1000 - 15);
	}

	void cachedRepliesDoNotHitTheServer()
	{
		m_server->setJson(QStringLiteral("/search.json"), test::samples::searchHobbit());
		m_client->setMinimumInterval(0);
		Replies replies;
		QObject context;
		const QUrl searchUrl = url(QStringLiteral("/search.json"));
		m_client->get(searchUrl, QStringLiteral("test"), &context, replies.callback());
		QTRY_COMPARE(replies.received.size(), 1);
		m_client->get(searchUrl, QStringLiteral("test"), &context, replies.callback());
		QCOMPARE(replies.received.size(), 1);
		QTRY_COMPARE(replies.received.size(), 2);
		QCOMPARE(m_server->requests().size(), 1);
		QCOMPARE(m_client->requestsSent(), 1);
		QCOMPARE(replies.received.at(1).body, test::samples::searchHobbit());
	}

	void failuresAreNotCached()
	{
		m_server->setError(QStringLiteral("/flaky.json"), 503, "Service Unavailable");
		m_client->setMinimumInterval(0);
		Replies replies;
		QObject context;
		m_client->get(url(QStringLiteral("/flaky.json")), QStringLiteral("test"), &context, replies.callback());
		QTRY_COMPARE(replies.received.size(), 1);
		m_server->setJson(QStringLiteral("/flaky.json"), "{}");
		m_client->get(url(QStringLiteral("/flaky.json")), QStringLiteral("test"), &context, replies.callback());
		QTRY_COMPARE(replies.received.size(), 2);
		QVERIFY(!replies.received.at(0).ok);
		QVERIFY(replies.received.at(1).ok);
		QCOMPARE(m_server->requests().size(), 2);
	}

	void httpErrorsAreDescribedAndLogged_data()
	{
		QTest::addColumn<int>("status");
		QTest::addColumn<QByteArray>("reason");
		QTest::addColumn<QString>("expectedInMessage");
		QTest::newRow("404") << 404 << QByteArray("Not Found") << QStringLiteral("404");
		QTest::newRow("429") << 429 << QByteArray("Too Many Requests") << QStringLiteral("limiting");
		QTest::newRow("500") << 500 << QByteArray("Internal Server Error") << QStringLiteral("500");
		QTest::newRow("503") << 503 << QByteArray("Service Unavailable") << QStringLiteral("try again later");
		QTest::newRow("403") << 403 << QByteArray("Forbidden") << QStringLiteral("refused");
	}

	void httpErrorsAreDescribedAndLogged()
	{
		QFETCH(int, status);
		QFETCH(QByteArray, reason);
		QFETCH(QString, expectedInMessage);

		m_server->setError(QStringLiteral("/broken.json"), status, reason, "server says no");
		Replies replies;
		QObject context;
		m_client->get(url(QStringLiteral("/broken.json")), QStringLiteral("Doing the broken thing"),
			&context, replies.callback());
		QTRY_COMPARE(replies.received.size(), 1);

		const OpenLibraryReply& reply = replies.received.first();
		QVERIFY(!reply.ok);
		QCOMPARE(reply.httpStatus, status);
		QVERIFY2(reply.error.contains(expectedInMessage, Qt::CaseInsensitive), qPrintable(reply.error));
		// The full error has the URL, the status line, and the body.
		QVERIFY(reply.details.contains(QStringLiteral("/broken.json")));
		QVERIFY(reply.details.contains(QStringLiteral("HTTP status: %1 %2").arg(status).arg(QString::fromLatin1(reason))));
		QVERIFY(reply.details.contains(QStringLiteral("server says no")));

		// And it was logged, with what the app was doing.
		QCOMPARE(m_console.size(), 1);
		QVERIFY(m_console.first().contains(QStringLiteral("While: Doing the broken thing")));
		QVERIFY(m_console.first().contains(reply.details));
	}

	void longErrorBodiesAreShortenedInTheLog()
	{
		m_server->setError(QStringLiteral("/big.json"), 500, "Internal Server Error", QByteArray(5000, 'x'));
		Replies replies;
		QObject context;
		m_client->get(url(QStringLiteral("/big.json")), QStringLiteral("test"), &context, replies.callback());
		QTRY_COMPARE(replies.received.size(), 1);
		QVERIFY(replies.received.first().details.contains(QStringLiteral("first 2000 of 5000 bytes")));
		QVERIFY(!replies.received.first().details.contains(QString(2001, QLatin1Char('x'))));
	}

	void unreachableServerIsAnError()
	{
		const QUrl dead = url(QStringLiteral("/search.json"));
		m_server->close();
		Replies replies;
		QObject context;
		m_client->get(dead, QStringLiteral("Searching"), &context, replies.callback());
		QTRY_COMPARE_WITH_TIMEOUT(replies.received.size(), 1, 15000);
		const OpenLibraryReply& reply = replies.received.first();
		QVERIFY(!reply.ok);
		QCOMPARE(reply.httpStatus, 0);
		QVERIFY(reply.error.contains(QStringLiteral("could not be reached")));
		QVERIFY(reply.details.contains(QStringLiteral("no answer from the server")));
		QCOMPARE(m_console.size(), 1);
	}

	void slowServerTimesOut()
	{
		m_server->setSilent(QStringLiteral("/slow.json"));
		m_client->setTimeout(300);
		Replies replies;
		QObject context;
		m_client->get(url(QStringLiteral("/slow.json")), QStringLiteral("test"), &context, replies.callback());
		QTRY_COMPARE_WITH_TIMEOUT(replies.received.size(), 1, 10000);
		QVERIFY(!replies.received.first().ok);
		QVERIFY2(replies.received.first().error.contains(QStringLiteral("too long")),
			qPrintable(replies.received.first().error));
	}

	void errorsGoToTheLogFileWhenTurnedOn()
	{
		ErrorLog& log = ErrorLog::instance();
		log.setConsoleEnabled(false);
		log.setFileEnabled(true);
		const QString path = m_folder.filePath(QStringLiteral("file-on.log"));
		log.setFilePath(path);

		m_server->setError(QStringLiteral("/gone.json"), 404, "Not Found");
		Replies replies;
		QObject context;
		m_client->get(url(QStringLiteral("/gone.json")), QStringLiteral("Getting a record"), &context,
			replies.callback());
		QTRY_COMPARE(replies.received.size(), 1);

		QVERIFY(m_console.isEmpty());
		QFile file(path);
		QVERIFY(file.open(QIODevice::ReadOnly));
		const QString text = QString::fromUtf8(file.readAll());
		QVERIFY(text.contains(QStringLiteral("While: Getting a record")));
		QVERIFY(text.contains(QStringLiteral("HTTP status: 404 Not Found")));
	}

	void destroyedContextGetsNoCallback()
	{
		m_server->setJson(QStringLiteral("/1.json"), "{}");
		m_server->setJson(QStringLiteral("/2.json"), "{}");
		m_client->setMinimumInterval(200);
		bool called = false;
		auto context = std::make_unique<QObject>();
		m_client->get(url(QStringLiteral("/1.json")), QStringLiteral("test"), context.get(),
			[&](const OpenLibraryReply&) { called = true; });
		m_client->get(url(QStringLiteral("/2.json")), QStringLiteral("test"), context.get(),
			[&](const OpenLibraryReply&) { called = true; });
		context.reset();
		QTest::qWait(600);
		QVERIFY(!called);
		// The queued second request was dropped rather than sent.
		QVERIFY(m_server->requestsFor(QStringLiteral("/2.json")).isEmpty());
		QTRY_COMPARE(m_client->pendingCount(), 0);
	}

	void cancelDropsQueuedAndRunningRequests()
	{
		m_server->setSilent(QStringLiteral("/hang.json"));
		m_server->setJson(QStringLiteral("/later.json"), "{}");
		m_server->setJson(QStringLiteral("/other.json"), "{}");
		m_client->setMinimumInterval(100);

		QObject mine;
		QObject other;
		Replies mineReplies;
		Replies otherReplies;
		m_client->get(url(QStringLiteral("/hang.json")), QStringLiteral("test"), &mine, mineReplies.callback());
		m_client->get(url(QStringLiteral("/later.json")), QStringLiteral("test"), &mine, mineReplies.callback());
		m_client->get(url(QStringLiteral("/other.json")), QStringLiteral("test"), &other, otherReplies.callback());
		QTRY_COMPARE(m_server->requestsFor(QStringLiteral("/hang.json")).size(), 1);

		m_client->cancel(&mine);
		QTRY_COMPARE(otherReplies.received.size(), 1);
		QTest::qWait(200);
		QVERIFY(mineReplies.received.isEmpty());
		QVERIFY(m_server->requestsFor(QStringLiteral("/later.json")).isEmpty());
		// Canceling is not an error, so nothing is logged.
		QVERIFY(m_console.isEmpty());
		QCOMPARE(m_client->pendingCount(), 0);
	}

	void canceledRequestsDoNotDelayOthers()
	{
		// A request canceled while waiting must not use up a turn.
		m_server->setJson(QStringLiteral("/a.json"), "{}");
		m_server->setJson(QStringLiteral("/b.json"), "{}");
		m_server->setJson(QStringLiteral("/c.json"), "{}");
		m_client->setMinimumInterval(300);
		QObject first;
		QObject dropped;
		Replies replies;
		Replies droppedReplies;
		m_client->get(url(QStringLiteral("/a.json")), QStringLiteral("test"), &first, replies.callback());
		m_client->get(url(QStringLiteral("/b.json")), QStringLiteral("test"), &dropped, droppedReplies.callback());
		m_client->get(url(QStringLiteral("/c.json")), QStringLiteral("test"), &first, replies.callback());
		m_client->cancel(&dropped);
		QTRY_COMPARE_WITH_TIMEOUT(replies.received.size(), 2, 5000);
		const auto& requests = m_server->requests();
		QCOMPARE(requests.size(), 2);
		const qint64 gap = requests.at(1).receivedAtMs - requests.at(0).receivedAtMs;
		QVERIFY2(gap >= 300 - 15 && gap < 550, qPrintable(QStringLiteral("gap %1 ms").arg(gap)));
	}
};

QTEST_GUILESS_MAIN(TestOpenLibraryClient)
#include "tst_open_library_client.moc"
