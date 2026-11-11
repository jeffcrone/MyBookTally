#pragma once

#include <QByteArray>
#include <QPixmap>
#include <QSize>
#include <QString>

namespace mbt::CoverImage {

// Covers are kept inside the database, so a photo straight off a phone
// (several megabytes, 4000 pixels tall) would bloat the file and slow every
// export. Images larger than this are scaled down to fit before saving;
// this is still sharper than any size the app displays them at.
constexpr int MaxWidth = 600;
constexpr int MaxHeight = 900;

// Reads an image file and returns the bytes to store: scaled down to fit
// MaxWidth x MaxHeight if larger, rotated upright if the file says it was
// taken sideways, and re-encoded (JPEG, or PNG if it has transparency).
// Returns an empty array and sets errorMessage if the file is not an image.
QByteArray loadForStorage(const QString& filePath, QString* errorMessage);

// The same, for an image already in memory, such as a cover downloaded
// from Open Library.
QByteArray prepareForStorage(const QByteArray& imageData, QString* errorMessage);

// The stored bytes as a pixmap scaled to fit within box, or a null pixmap
// if there is no (readable) image.
QPixmap toPixmap(const QByteArray& bytes, const QSize& box);

} // namespace mbt::CoverImage
