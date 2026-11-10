#include "ReviewRepository.h"

#include "Database.h"
#include "DatabaseError.h"
#include "SqlUtil.h"

namespace mbt {

namespace {

void validateReview(const Review& review)
{
	if (review.text.trimmed().isEmpty())
		throw DatabaseError(QStringLiteral("A review needs some text."));
}

// Checked here as well as by the CHECK constraint in the ratings table, so
// the user sees a readable message instead of SQLite's "CHECK constraint
// failed".
void validateRating(const Rating& rating)
{
	if (!(rating.scale > 0))
		throw DatabaseError(QStringLiteral("A rating must be out of a number greater than zero."));
	if (rating.value < 0 || rating.value > rating.scale)
		throw DatabaseError(QStringLiteral("A rating must be between 0 and %1.")
			.arg(rating.scale));
}

Review readReview(const QSqlQuery& query)
{
	return {query.value(0).toString(), query.value(1).toString(), query.value(2).toString(),
		query.value(3).toString(), query.value(4).toString()};
}

Rating readRating(const QSqlQuery& query)
{
	Rating rating;
	rating.id = query.value(0).toString();
	rating.bookId = query.value(1).toString();
	rating.value = query.value(2).toDouble();
	rating.scale = query.value(3).toDouble();
	rating.reviewer = query.value(4).toString();
	rating.createdAt = query.value(5).toString();
	return rating;
}

} // namespace

ReviewRepository::ReviewRepository(const Database& database)
	: m_database(database)
{
}

QList<Review> ReviewRepository::reviews(const QString& bookId) const
{
	QSqlQuery query = prepareQuery(m_database.connection(), QStringLiteral(
		"SELECT id, book_id, review_text, reviewer, created_at FROM reviews "
		"WHERE book_id = :book_id ORDER BY created_at DESC, id"));
	query.bindValue(QStringLiteral(":book_id"), bookId);
	execQuery(query);
	QList<Review> result;
	while (query.next())
		result.append(readReview(query));
	return result;
}

std::optional<Review> ReviewRepository::findReview(const QString& id) const
{
	QSqlQuery query = prepareQuery(m_database.connection(), QStringLiteral(
		"SELECT id, book_id, review_text, reviewer, created_at FROM reviews WHERE id = :id"));
	query.bindValue(QStringLiteral(":id"), id);
	execQuery(query);
	if (!query.next())
		return std::nullopt;
	return readReview(query);
}

QString ReviewRepository::addReview(Review review) const
{
	validateReview(review);
	if (review.id.isEmpty())
		review.id = newId();
	if (review.createdAt.isEmpty())
		review.createdAt = nowUtc();
	QSqlQuery query = prepareQuery(m_database.connection(), QStringLiteral(
		"INSERT INTO reviews (id, book_id, review_text, reviewer, created_at) "
		"VALUES (:id, :book_id, :review_text, :reviewer, :created_at)"));
	query.bindValue(QStringLiteral(":id"), review.id);
	query.bindValue(QStringLiteral(":book_id"), review.bookId);
	query.bindValue(QStringLiteral(":review_text"), review.text.trimmed());
	query.bindValue(QStringLiteral(":reviewer"), nullIfEmpty(review.reviewer.trimmed()));
	query.bindValue(QStringLiteral(":created_at"), review.createdAt);
	execQuery(query);
	return review.id;
}

void ReviewRepository::updateReview(const Review& review) const
{
	validateReview(review);
	QSqlQuery query = prepareQuery(m_database.connection(), QStringLiteral(
		"UPDATE reviews SET review_text = :review_text, reviewer = :reviewer WHERE id = :id"));
	query.bindValue(QStringLiteral(":id"), review.id);
	query.bindValue(QStringLiteral(":review_text"), review.text.trimmed());
	query.bindValue(QStringLiteral(":reviewer"), nullIfEmpty(review.reviewer.trimmed()));
	execQuery(query);
	if (query.numRowsAffected() == 0)
		throw DatabaseError(QStringLiteral("That review no longer exists."));
}

bool ReviewRepository::removeReview(const QString& id) const
{
	QSqlQuery query = prepareQuery(m_database.connection(),
		QStringLiteral("DELETE FROM reviews WHERE id = :id"));
	query.bindValue(QStringLiteral(":id"), id);
	execQuery(query);
	return query.numRowsAffected() > 0;
}

QList<Rating> ReviewRepository::ratings(const QString& bookId) const
{
	QSqlQuery query = prepareQuery(m_database.connection(), QStringLiteral(
		"SELECT id, book_id, rating_value, rating_scale, reviewer, created_at FROM ratings "
		"WHERE book_id = :book_id ORDER BY created_at DESC, id"));
	query.bindValue(QStringLiteral(":book_id"), bookId);
	execQuery(query);
	QList<Rating> result;
	while (query.next())
		result.append(readRating(query));
	return result;
}

std::optional<Rating> ReviewRepository::findRating(const QString& id) const
{
	QSqlQuery query = prepareQuery(m_database.connection(), QStringLiteral(
		"SELECT id, book_id, rating_value, rating_scale, reviewer, created_at FROM ratings "
		"WHERE id = :id"));
	query.bindValue(QStringLiteral(":id"), id);
	execQuery(query);
	if (!query.next())
		return std::nullopt;
	return readRating(query);
}

QString ReviewRepository::addRating(Rating rating) const
{
	validateRating(rating);
	if (rating.id.isEmpty())
		rating.id = newId();
	if (rating.createdAt.isEmpty())
		rating.createdAt = nowUtc();
	QSqlQuery query = prepareQuery(m_database.connection(), QStringLiteral(
		"INSERT INTO ratings (id, book_id, rating_value, rating_scale, reviewer, created_at) "
		"VALUES (:id, :book_id, :rating_value, :rating_scale, :reviewer, :created_at)"));
	query.bindValue(QStringLiteral(":id"), rating.id);
	query.bindValue(QStringLiteral(":book_id"), rating.bookId);
	query.bindValue(QStringLiteral(":rating_value"), rating.value);
	query.bindValue(QStringLiteral(":rating_scale"), rating.scale);
	query.bindValue(QStringLiteral(":reviewer"), nullIfEmpty(rating.reviewer.trimmed()));
	query.bindValue(QStringLiteral(":created_at"), rating.createdAt);
	execQuery(query);
	return rating.id;
}

void ReviewRepository::updateRating(const Rating& rating) const
{
	validateRating(rating);
	QSqlQuery query = prepareQuery(m_database.connection(), QStringLiteral(
		"UPDATE ratings SET rating_value = :rating_value, rating_scale = :rating_scale, "
		"reviewer = :reviewer WHERE id = :id"));
	query.bindValue(QStringLiteral(":id"), rating.id);
	query.bindValue(QStringLiteral(":rating_value"), rating.value);
	query.bindValue(QStringLiteral(":rating_scale"), rating.scale);
	query.bindValue(QStringLiteral(":reviewer"), nullIfEmpty(rating.reviewer.trimmed()));
	execQuery(query);
	if (query.numRowsAffected() == 0)
		throw DatabaseError(QStringLiteral("That rating no longer exists."));
}

bool ReviewRepository::removeRating(const QString& id) const
{
	QSqlQuery query = prepareQuery(m_database.connection(),
		QStringLiteral("DELETE FROM ratings WHERE id = :id"));
	query.bindValue(QStringLiteral(":id"), id);
	execQuery(query);
	return query.numRowsAffected() > 0;
}

QString formatRating(double value, double scale)
{
	// 'g' drops a trailing ".0", so 4.0 shows as "4" but 3.5 stays "3.5".
	return QStringLiteral("%1/%2").arg(QString::number(value, 'g', 4),
		QString::number(scale, 'g', 4));
}

} // namespace mbt
