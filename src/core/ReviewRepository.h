#pragma once

#include "Entities.h"

#include <QList>
#include <QString>

#include <optional>

namespace mbt {

class Database;

// Reviews and ratings of a book. They are separate tables because a rating
// is often given without a review, and a review sometimes without a score.
class ReviewRepository
{
public:
	explicit ReviewRepository(const Database& database);

	// Newest first.
	QList<Review> reviews(const QString& bookId) const;
	std::optional<Review> findReview(const QString& id) const;
	// Gives the review a new id and createdAt if it has none; returns the id.
	// Throws DatabaseError if the text is empty or the book does not exist.
	QString addReview(Review review) const;
	// Changes the text and reviewer. The book and creation time stay.
	void updateReview(const Review& review) const;
	bool removeReview(const QString& id) const;

	// Newest first.
	QList<Rating> ratings(const QString& bookId) const;
	std::optional<Rating> findRating(const QString& id) const;
	// Throws DatabaseError if the value is outside 0..scale, the scale is not
	// positive, or the book does not exist.
	QString addRating(Rating rating) const;
	void updateRating(const Rating& rating) const;
	bool removeRating(const QString& id) const;

private:
	const Database& m_database;
};

// "4/5", "3.5/5", "8/10": a rating as people write it.
QString formatRating(double value, double scale);

} // namespace mbt
