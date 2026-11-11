#include "OpenLibraryClient.h"

#include "ErrorLog.h"
#include "OpenLibrary.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

#include <algorithm>

namespace mbt {

namespace {

// How much of an error reply's body goes into the log. Enough to see the
// server's explanation; an HTML error page can be much longer than that.
constexpr int maxLoggedBodyBytes = 2000;

constexpr qsizetype cacheBytes = 32 * 1024 * 1024;

// A sentence for the popup, from what went wrong.
QString userMessage(QNetworkReply::NetworkError error, int httpStatus, const QString& errorString)
{
	if (httpStatus == 429)
		return QObject::tr("Open Library is limiting requests from this computer (HTTP 429, "
			"too many requests). Please wait a minute and try again.");
	if (httpStatus == 404)
		return QObject::tr("Open Library does not have the requested record (HTTP 404, not found).");
	if (httpStatus >= 500)
		return QObject::tr("Open Library had a problem answering (HTTP %1). It may be busy or "
			"down for maintenance; please try again later.").arg(httpStatus);
	if (httpStatus >= 400)
		return QObject::tr("Open Library refused the request (HTTP %1).").arg(httpStatus);

	switch (error) {
	case QNetworkReply::HostNotFoundError:
		return QObject::tr("Open Library could not be found. Check that this computer is "
			"connected to the internet. (%1)").arg(errorString);
	case QNetworkReply::ConnectionRefusedError:
	case QNetworkReply::RemoteHostClosedError:
		return QObject::tr("Open Library could not be reached: the connection was refused or "
			"closed. (%1)").arg(errorString);
	case QNetworkReply::TimeoutError:
	case QNetworkReply::OperationCanceledError:
		// A transfer timeout aborts the reply, which Qt reports as
		// "canceled"; requests the app itself cancels never get here.
		return QObject::tr("Open Library took too long to answer. Please try again later.");
	case QNetworkReply::SslHandshakeFailedError:
		return QObject::tr("A secure connection to Open Library could not be made. (%1)")
			.arg(errorString);
	default:
		return QObject::tr("Open Library could not be reached. (%1)").arg(errorString);
	}
}

} // namespace

OpenLibraryClient::OpenLibraryClient(QObject* parent)
	: QObject(parent)
	, m_network(new QNetworkAccessManager(this))
	, m_apiBase(OpenLibrary::defaultApiBase())
	, m_coversBase(OpenLibrary::defaultCoversBase())
{
	m_wait.setSingleShot(true);
	connect(&m_wait, &QTimer::timeout, this, &OpenLibraryClient::pump);
	connect(m_network, &QNetworkAccessManager::finished, this, &OpenLibraryClient::finished);
	m_cache.setMaxCost(cacheBytes);
}

OpenLibraryClient::~OpenLibraryClient()
{
	// Replies still running are children of m_network and are deleted with
	// it; their finished signal must not reach a half-destroyed client.
	disconnect(m_network, nullptr, this, nullptr);
}

QByteArray OpenLibraryClient::userAgent()
{
	return QByteArrayLiteral("MyBookTally/" MYBOOKTALLY_VERSION);
}

void OpenLibraryClient::get(const QUrl& url, const QString& activity, QObject* context, Callback callback)
{
	if (const QByteArray* cached = m_cache.object(url.toString())) {
		OpenLibraryReply reply;
		reply.url = url;
		reply.ok = true;
		reply.httpStatus = 200;
		reply.body = *cached;
		// Queued, not called now, so a cache hit behaves like a response.
		// The context makes Qt drop the call if context is destroyed first.
		QMetaObject::invokeMethod(context ? context : this, [callback, reply] { callback(reply); },
			Qt::QueuedConnection);
		return;
	}

	// A request with no context belongs to the client itself.
	m_queue.append(Pending{url, activity, context ? context : this, std::move(callback)});
	pump();
}

void OpenLibraryClient::cancel(QObject* context)
{
	m_queue.erase(std::remove_if(m_queue.begin(), m_queue.end(),
		[context](const Pending& pending) { return pending.context == context; }), m_queue.end());
	// Collected first and aborted afterwards: abort() can emit finished()
	// straight away, and finished() removes the reply from m_inFlight, which
	// must not happen while looping over it.
	QList<QNetworkReply*> toAbort;
	for (auto it = m_inFlight.begin(); it != m_inFlight.end(); ++it) {
		if (it.value().context == context && !it.value().canceled) {
			it.value().canceled = true;
			toAbort.append(it.key());
		}
	}
	// finished() still runs for each of these, and sees the flag.
	for (QNetworkReply* reply : toAbort)
		reply->abort();
}

void OpenLibraryClient::pump()
{
	// Requests whose dialog has closed are not worth a turn.
	m_queue.erase(std::remove_if(m_queue.begin(), m_queue.end(),
		[](const Pending& pending) { return pending.context.isNull(); }), m_queue.end());
	if (m_queue.isEmpty() || m_wait.isActive())
		return;

	// The limit is measured from the start of one request to the start of
	// the next, which is what "once a second" means to the server.
	if (m_sinceLastStart.isValid()) {
		const qint64 elapsed = m_sinceLastStart.elapsed();
		if (elapsed < m_intervalMs) {
			m_wait.start(int(m_intervalMs - elapsed));
			return;
		}
	}

	m_sinceLastStart.start();
	start(m_queue.takeFirst());
	if (!m_queue.isEmpty())
		m_wait.start(m_intervalMs);
}

void OpenLibraryClient::start(Pending pending)
{
	QNetworkRequest request(pending.url);
	request.setHeader(QNetworkRequest::UserAgentHeader, userAgent());
	// Qt otherwise sends the computer's language list, which says something
	// about the user and does not change Open Library's answers.
	request.setRawHeader(QByteArrayLiteral("Accept-Language"), QByteArrayLiteral("en"));
	// Manual means Qt neither attaches stored cookies nor keeps new ones,
	// so nothing can tie one lookup to the next.
	request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
	request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
	// Covers are served from archive.org after a redirect; follow it, but
	// never from https down to http.
	request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
		QNetworkRequest::NoLessSafeRedirectPolicy);
	request.setTransferTimeout(m_timeoutMs);

	++m_requestsSent;
	QNetworkReply* reply = m_network->get(request);
	m_inFlight.insert(reply, std::move(pending));
}

void OpenLibraryClient::finished(QNetworkReply* reply)
{
	reply->deleteLater();
	const auto found = m_inFlight.find(reply);
	if (found == m_inFlight.end())
		return;
	const Pending pending = found.value();
	m_inFlight.erase(found);

	if (pending.canceled || pending.context.isNull())
		return;

	OpenLibraryReply result;
	result.url = pending.url;
	result.httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
	// A reply that timed out has been closed already, and reading it would
	// only print a Qt warning.
	if (reply->isOpen())
		result.body = reply->readAll();
	result.ok = reply->error() == QNetworkReply::NoError
		&& result.httpStatus >= 200 && result.httpStatus < 300;

	if (result.ok) {
		m_cache.insert(pending.url.toString(), new QByteArray(result.body),
			std::max<qsizetype>(1, result.body.size()));
	} else {
		result.error = userMessage(reply->error(), result.httpStatus, reply->errorString());

		QStringList lines;
		lines << QStringLiteral("Request: GET %1").arg(pending.url.toString());
		const QUrl finalUrl = reply->url();
		if (finalUrl != pending.url)
			lines << QStringLiteral("Redirected to: %1").arg(finalUrl.toString());
		if (result.httpStatus != 0) {
			lines << QStringLiteral("HTTP status: %1 %2").arg(result.httpStatus)
				.arg(reply->attribute(QNetworkRequest::HttpReasonPhraseAttribute).toString());
		} else {
			lines << QStringLiteral("HTTP status: none (no answer from the server)");
		}
		lines << QStringLiteral("Network error: %1 (Qt code %2)")
			.arg(reply->errorString()).arg(int(reply->error()));
		lines << QStringLiteral("User-Agent sent: %1").arg(QString::fromLatin1(userAgent()));
		if (!result.body.isEmpty()) {
			QByteArray shown = result.body.left(maxLoggedBodyBytes);
			lines << QStringLiteral("Response body%1:")
				.arg(result.body.size() > maxLoggedBodyBytes
					? QStringLiteral(" (first %1 of %2 bytes)").arg(maxLoggedBodyBytes).arg(result.body.size())
					: QString());
			lines << QString::fromUtf8(shown);
		}
		result.details = lines.join(QLatin1Char('\n'));
		ErrorLog::instance().record(pending.activity, result.error + QLatin1Char('\n') + result.details);
	}

	pending.callback(result);
}

} // namespace mbt
