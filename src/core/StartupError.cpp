#include "core/StartupError.h"

#include <windows.h>

namespace scrambler::core
{

StartupError MapWinDivertOpenError(uint32_t gle) noexcept
{
    switch (gle)
    {
        case ERROR_ACCESS_DENIED:
            return StartupError::kDriverAccessDenied;
        case ERROR_FILE_NOT_FOUND:
        case ERROR_SERVICE_DOES_NOT_EXIST:
            return StartupError::kDriverMissing;
        default:
            return StartupError::kDriverOpenFailedOther;
    }
}

}  // namespace scrambler::core
