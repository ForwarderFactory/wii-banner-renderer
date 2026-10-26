/* This file is a gross hack, because I'm too lazy to redesign the APIs properly.
 * We just declare a static object here which the implementation can use.
 */
#pragma once

#include <libwb/Logger.h>
#include <memory>

namespace wb {
    inline Logger logger;
}