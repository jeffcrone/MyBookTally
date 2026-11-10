#include "CoverImage.h"

#include <QBuffer>
#include <QImage>
#include <QImageReader>

namespace mbt::CoverImage {

QByteArray loadForStorage(const QString& filePath, QString* errorMessage)
{
	QImageReader reader(filePath);
	// Phone cameras store sideways pixels plus an "orientation" tag; without
	// this the cover would show up rotated.
	reader.setAutoTransform(true);
	QImage image = reader.read();
	if (image.isNull()) {
		if (errorMessage)
			*errorMessage = reader.errorString();
		return QByteArray();
	}

	if (image.width() > MaxWidth || image.height() > MaxHeight)
		image = image.scaled(MaxWidth, MaxHeight, Qt::KeepAspectRatio, Qt::SmoothTransformation);

	QByteArray bytes;
	QBuffer buffer(&bytes);
	buffer.open(QIODevice::WriteOnly);
	// JPEG is far smaller for photos and scans; PNG only when the image has
	// transparency, which JPEG would flatten to black.
	if (image.hasAlphaChannel())
		image.save(&buffer, "PNG");
	else
		image.save(&buffer, "JPEG", 90);
	return bytes;
}

QPixmap toPixmap(const QByteArray& bytes, const QSize& box)
{
	if (bytes.isEmpty())
		return QPixmap();
	QPixmap pixmap;
	if (!pixmap.loadFromData(bytes))
		return QPixmap();
	return pixmap.scaled(box, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

} // namespace mbt::CoverImage
