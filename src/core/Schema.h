#pragma once

#include <QSqlDatabase>

namespace mbt {

// Creates the tables and upgrades older databases.
//
// The schema version lives in SQLite's built-in PRAGMA user_version (0 for a
// brand-new file). migrate() runs each upgrade step above the file's current
// version in order, each step in its own transaction, so a database written
// by any earlier release can be opened by a later one. Opening a database
// written by a *newer* release is refused rather than risking damage to
// tables this release does not understand.
//
// The full table layout is described in README.md; keep the two in step.
namespace Schema {

// The schema version this build of the app creates and understands.
constexpr int CurrentVersion = 1;

// Throws DatabaseError on failure.
void migrate(const QSqlDatabase& db);

// Reads PRAGMA user_version.
int version(const QSqlDatabase& db);

} // namespace Schema

} // namespace mbt
