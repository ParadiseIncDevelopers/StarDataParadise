#pragma once

#include "migration_source.hpp"

namespace wb::migration::sourceDriver
{
ExternalSnapshot snapshot(const ExternalSource& source, QByteArray& password, const ExternalProgress& progress);
}
