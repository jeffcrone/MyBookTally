#pragma once

#include <QByteArray>
#include <QCache>
#include <QElapsedTimer>
#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <QUrl>

#include <functional>

class QNetworkAccessManager;
class QNetworkReply;

namespace mbt {

// The outcome of one request.
struct OpenLibraryReply
{
	QUrl url;
	bool ok = false;
	QByteArray body;
	// 0 when no HTTP answer arrived at all (no connection, timed out).
	int httpStatus = 0;
	// One sentence for the user's popup.
	QString error;
	// Everything known about the failure, for the console and log file.
	QString details;
};

// Sends GET requests to Open Library on behalf of the whole app, following
// its rules for API clients (https://openlibrary.org/developers/api):
//
// - At most one request per second. Open Library allows that much from a
//   client that does not identify itself with contact details; it allows
//   three per second only for clients whose User-Agent carries an email
//   address or phone number, and the user does not want either sent. The
//   app has one client (owned by the main window), so every dialog shares
//   this limit; requests beyond it wait in a queue rather than being sent.
// - Responses are cached for the rest of the session, as Open Library asks,
//   so looking at the same results again costs it nothing.
//
// Nothing about the user is sent: the User-Agent names only the app and its
// version, cookies are neither stored nor sent, and the Accept-Language
// header (which Qt would otherwise fill in from the computer's locale) is
// fixed. What does go out is what the request needs - the search terms the
// user typed - plus the computer's IP address, which no HTTP request can
// avoid.
class OpenLibraryClient : public QObject
{
	Q_OBJECT

public:
	using Callback = std::function<void(const OpenLibraryReply&)>;

	static constexpr int DefaultMinimumIntervalMs = 1000;
	static constexpr int DefaultTimeoutMs = 30000;

	explicit OpenLibraryClient(QObject* parent = nullptr);
	~OpenLibraryClient() override;

	// Exactly what is sent as the User-Agent: "MyBookTally/<version>".
	static QByteArray userAgent();

	// Where requests go; the tests point these at a local fake server.
	QUrl apiBase() const { return m_apiBase; }
	void setApiBase(const QUrl& url) { m_apiBase = url; }
	QUrl coversBase() const { return m_coversBase; }
	void setCoversBase(const QUrl& url) { m_coversBase = url; }

	// The shortest time between the start of one request and the next. The
	// tests shorten it to keep the suite quick; the app never changes it.
	int minimumIntervalMs() const { return m_intervalMs; }
	void setMinimumInterval(int milliseconds) { m_intervalMs = milliseconds; }
	void setTimeout(int milliseconds) { m_timeoutMs = milliseconds; }

	// Queues a GET of url. callback runs later, on context's thread, with
	// the result - never before get() returns, even for a cached reply, so
	// callers see the same order of events every time. If context is
	// destroyed or cancel(context) is called first, callback never runs.
	//
	// activity says what the app is doing ("Searching Open Library for
	// title "Dune""); it goes into the log entry if the request fails.
	// Failures are logged here, so callers only need to tell the user.
	void get(const QUrl& url, const QString& activity, QObject* context, Callback callback);

	// Drops context's queued requests and aborts its ones in progress.
	void cancel(QObject* context);

	// Requests waiting for their turn plus those in progress.
	int pendingCount() const { return int(m_queue.size() + m_inFlight.size()); }
	// Requests actually sent over the network (cache hits excluded).
	int requestsSent() const { return m_requestsSent; }

private:
	struct Pending
	{
		QUrl url;
		QString activity;
		QPointer<QObject> context;
		Callback callback;
		bool canceled = false;
	};

	void pump();
	void start(Pending pending);
	void finished(QNetworkReply* reply);

	QNetworkAccessManager* m_network;
	QUrl m_apiBase;
	QUrl m_coversBase;
	int m_intervalMs = DefaultMinimumIntervalMs;
	int m_timeoutMs = DefaultTimeoutMs;

	QList<Pending> m_queue;
	QHash<QNetworkReply*, Pending> m_inFlight;
	QTimer m_wait;
	QElapsedTimer m_sinceLastStart;
	int m_requestsSent = 0;

	// Keyed by URL; cost is the size in bytes. Covers are the big items
	// (a large one is around 100 KB), so this holds a few hundred.
	QCache<QString, QByteArray> m_cache;
};

} // namespace mbt
