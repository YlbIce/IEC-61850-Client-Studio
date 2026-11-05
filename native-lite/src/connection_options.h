#pragma once
#include "session.h"
#include <QVariantMap>

namespace ConnectionOptions {
// Validate before touching the active connection; all setters run in its worker thread.
void validate(const QVariantMap& args);
void apply(IedConnection connection, const QVariantMap& args);
QString summary(const QVariantMap& args);
} // namespace ConnectionOptions
