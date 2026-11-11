#pragma once

// Replies in the shape Open Library really sends (trimmed from live
// responses for "The Hobbit"), shared by the parsing, client, and dialog
// tests.

#include <QByteArray>

namespace mbt::test::samples {

// search.json?title=the hobbit&author=tolkien&fields=...: two works, each
// with its best-matching edition nested under "editions".
inline QByteArray searchHobbit()
{
	return QByteArrayLiteral(R"json({
	"numFound": 2, "start": 0, "numFoundExact": true,
	"docs": [
		{
			"key": "/works/OL27482W",
			"title": "The Hobbit",
			"author_name": ["J.R.R. Tolkien"],
			"cover_i": 14627509,
			"editions": {
				"numFound": 435, "start": 0, "numFoundExact": true,
				"docs": [{
					"key": "/books/OL10236414M",
					"title": "The Hobbit",
					"subtitle": "or There and Back Again",
					"cover_i": 10522108,
					"publish_date": ["2011"],
					"isbn": ["0261103342", "9780261103344"]
				}]
			}
		},
		{
			"key": "/works/OL219602W",
			"title": "The Hobbit",
			"author_name": ["Charles Dixon", "Sean  Deming", "J.R.R. Tolkien"],
			"cover_i": 8406766,
			"editions": {
				"numFound": 10, "start": 0, "numFoundExact": true,
				"docs": [{
					"key": "/books/OL22039557M",
					"title": "The  Hobbit",
					"publish_date": ["2001"],
					"isbn": ["0345445600", "9780345445605"]
				}]
			}
		}
	]
})json");
}

inline QByteArray searchNothing()
{
	return QByteArrayLiteral(R"json({"numFound": 0, "start": 0, "docs": []})json");
}

// /books/OL10236414M.json
inline QByteArray editionHobbit()
{
	return QByteArrayLiteral(R"json({
	"key": "/books/OL10236414M",
	"title": "The Hobbit",
	"subtitle": "or There and Back Again",
	"publishers": ["HarperCollins Publishers"],
	"description": {"type": "/type/text", "value": "Bilbo Baggins is a hobbit.\r\n\r\n--back cover"},
	"isbn_10": ["0261103342"],
	"isbn_13": ["9780261103344"],
	"covers": [10522108],
	"physical_format": "Paperback",
	"number_of_pages": 310,
	"publish_date": "2011",
	"subjects": ["Fantasy"],
	"works": [{"key": "/works/OL27482W"}]
})json");
}

// /works/OL27482W.json
inline QByteArray workHobbit()
{
	return QByteArrayLiteral(R"json({
	"key": "/works/OL27482W",
	"title": "The Hobbit",
	"description": {"type": "/type/text", "value": "The Hobbit is a tale of high adventure."},
	"covers": [14627509],
	"first_publish_date": "January 1938",
	"subjects": ["Fantasy", "Dragons", "Fiction", "Middle Earth (Imaginary place)"]
})json");
}

} // namespace mbt::test::samples
