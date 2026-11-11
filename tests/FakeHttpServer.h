#pragma once

// A tiny HTTP server on 127.0.0.1 standing in for Open Library, so the
// network client and the lookup dialog can be tested without the internet,
// without sending anything to the real service, and with answers the test
// controls - including the failures that are hard to provoke for real.

#include <QByteArray>
#include <QElapsedTimer>
#include <QHash>
#include <QList>
#include <QMap>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>

#include <functional>

namespace mbt::test {

class FakeHttpServer : public QObject
{
public:
	struct Request
	{
		QByteArray method;
		// Path and query, as sent: "/search.json?title=Dune&...".
		QByteArray target;
		// Header names lowercased.
		QMap<QByteArray, QByteArray> headers;
		QByteArray rawHead;
		// Milliseconds since the server started, when the request arrived.
		qint64 receivedAtMs = 0;

		QUrl url() const { return QUrl(QString::fromLatin1(target)); }
		QString path() const { return url().path(); }
	};

	struct Response
	{
		int status = 200;
		QByteArray reason = "OK";
		QByteArray contentType = "application/json";
		QByteArray body;
		QList<QPair<QByteArray, QByteArray>> extraHeaders;
	};

	FakeHttpServer()
	{
		m_clock.start();
		connect(&m_server, &QTcpServer::newConnection, this, [this] {
			while (QTcpSocket* socket = m_server.nextPendingConnection()) {
				socket->setParent(this);
				connect(socket, &QTcpSocket::readyRead, this, [this, socket] { readFrom(socket); });
				connect(socket, &QTcpSocket::disconnected, this, [this, socket] { m_buffers.remove(socket); });
				connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
			}
		});
	}

	bool listen() { return m_server.listen(QHostAddress::LocalHost, 0); }
	QUrl baseUrl() const { return QUrl(QStringLiteral("http://127.0.0.1:%1").arg(m_server.serverPort())); }
	// Stops accepting connections, so the next request is refused.
	void close() { m_server.close(); }

	// The answer for a path (without the query). Anything not set is 404.
	void setResponse(const QString& path, const Response& response) { m_responses.insert(path, response); }
	void setJson(const QString& path, const QByteArray& json) { setResponse(path, Response{200, "OK", "application/json", json, {}}); }
	void setError(const QString& path, int status, const QByteArray& reason, const QByteArray& body = {})
	{
		setResponse(path, Response{status, reason, "text/plain", body, {}});
	}
	// Requests for this path are read but never answered, to test timeouts.
	void setSilent(const QString& path) { m_silent.append(path); }

	const QList<Request>& requests() const { return m_requests; }
	QList<Request> requestsFor(const QString& path) const
	{
		QList<Request> result;
		for (const Request& request : m_requests) {
			if (request.path() == path)
				result.append(request);
		}
		return result;
	}

private:
	void readFrom(QTcpSocket* socket)
	{
		QByteArray& buffer = m_buffers[socket];
		buffer += socket->readAll();
		// GET requests have no body, so the head is the whole request.
		for (;;) {
			const qsizetype end = buffer.indexOf("\r\n\r\n");
			if (end < 0)
				return;
			const QByteArray head = buffer.left(end);
			buffer.remove(0, end + 4);
			handle(socket, head);
		}
	}

	void handle(QTcpSocket* socket, const QByteArray& head)
	{
		Request request;
		request.rawHead = head;
		request.receivedAtMs = m_clock.elapsed();
		const QList<QByteArray> lines = head.split('\n');
		const QList<QByteArray> requestLine = lines.value(0).trimmed().split(' ');
		request.method = requestLine.value(0);
		request.target = requestLine.value(1);
		for (qsizetype i = 1; i < lines.size(); ++i) {
			const QByteArray line = lines.at(i).trimmed();
			const qsizetype colon = line.indexOf(':');
			if (colon > 0)
				request.headers.insert(line.left(colon).trimmed().toLower(), line.mid(colon + 1).trimmed());
		}
		m_requests.append(request);

		if (m_silent.contains(request.path()))
			return;

		const Response response = m_responses.value(request.path(),
			Response{404, "Not Found", "text/plain", "not found", {}});
		QByteArray out = "HTTP/1.1 " + QByteArray::number(response.status) + ' ' + response.reason + "\r\n";
		out += "Content-Type: " + response.contentType + "\r\n";
		out += "Content-Length: " + QByteArray::number(response.body.size()) + "\r\n";
		for (const auto& header : response.extraHeaders)
			out += header.first + ": " + header.second + "\r\n";
		out += "\r\n";
		out += response.body;
		socket->write(out);
	}

	QTcpServer m_server;
	QElapsedTimer m_clock;
	QHash<QString, Response> m_responses;
	QList<QString> m_silent;
	QList<Request> m_requests;
	QHash<QTcpSocket*, QByteArray> m_buffers;
};

} // namespace mbt::test
