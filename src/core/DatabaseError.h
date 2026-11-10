#pragma once

#include <QString>

#include <stdexcept>

namespace mbt {

// Thrown whenever a database operation fails: a query that SQLite rejects, a
// constraint that is violated, a file that cannot be opened.
//
// The repositories throw instead of returning a success flag because nearly
// every caller would otherwise have to thread a bool and an error string
// through several layers just to show one message box at the top. The user
// interface catches this in one place per action and reports message().
class DatabaseError : public std::runtime_error
{
public:
	explicit DatabaseError(const QString& message)
		: std::runtime_error(message.toStdString())
		, m_message(message)
	{
	}

	const QString& message() const { return m_message; }

private:
	// Kept as a QString as well so non-ASCII text (a book title in the
	// message, say) survives without a round trip through std::string.
	QString m_message;
};

} // namespace mbt
