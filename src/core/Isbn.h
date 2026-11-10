#pragma once

#include <QString>

namespace mbt::Isbn {

// ISBNs are printed with hyphens and spaces in varying places
// ("978-0-306-40615-7", "0 306 40615 2"). They are stored without them so
// that the same book typed two ways matches, and so searching and duplicate
// detection on import compare like with like.
//
// Returns the input with spaces and hyphens removed and a lowercase "x"
// made uppercase. Does not otherwise validate.
QString normalize(const QString& isbn);

// True when the normalized text has the shape of an ISBN: 13 digits, or 9
// digits followed by a digit or X. An empty string is not well formed (the
// caller decides whether an ISBN is required; it never is in this app).
bool isWellFormed(const QString& isbn);

// True when the ISBN is well formed and its check digit is correct.
//
// This is kept separate from isWellFormed() because some genuine older books
// were printed with a wrong check digit. The editor refuses a malformed ISBN
// outright, but only warns about a bad check digit and lets the user keep it.
bool hasValidChecksum(const QString& isbn);

} // namespace mbt::Isbn
