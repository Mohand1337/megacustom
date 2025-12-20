#include "core/Error.h"
#include <megaapi.h>

namespace MegaCustom {

Error Error::fromMegaError(int megaErrorCode, const std::string& megaMessage) {
    ErrorCode code;
    std::string message;

    switch (megaErrorCode) {
        case mega::MegaError::API_OK:
            return Error::ok();

        case mega::MegaError::API_EINTERNAL:
            code = ErrorCode::UNKNOWN_ERROR;
            message = "Internal MEGA error";
            break;

        case mega::MegaError::API_EARGS:
            code = ErrorCode::VALIDATION_INVALID_FORMAT;
            message = "Invalid arguments";
            break;

        case mega::MegaError::API_EAGAIN:
            code = ErrorCode::NETWORK_TIMEOUT;
            message = "Temporary error, please retry";
            break;

        case mega::MegaError::API_ERATELIMIT:
            code = ErrorCode::CLOUD_BANDWIDTH_EXCEEDED;
            message = "Rate limit exceeded";
            break;

        case mega::MegaError::API_EFAILED:
            code = ErrorCode::TRANSFER_FAILED;
            message = "Operation failed";
            break;

        case mega::MegaError::API_ETOOMANY:
            code = ErrorCode::VALIDATION_INVALID_FORMAT;
            message = "Too many requests";
            break;

        case mega::MegaError::API_ERANGE:
            code = ErrorCode::VALIDATION_INVALID_FORMAT;
            message = "Value out of range";
            break;

        case mega::MegaError::API_EEXPIRED:
            code = ErrorCode::AUTH_SESSION_EXPIRED;
            message = "Session or link expired";
            break;

        case mega::MegaError::API_ENOENT:
            code = ErrorCode::CLOUD_NODE_NOT_FOUND;
            message = "Resource not found";
            break;

        case mega::MegaError::API_ECIRCULAR:
            code = ErrorCode::VALIDATION_INVALID_FORMAT;
            message = "Circular reference detected";
            break;

        case mega::MegaError::API_EACCESS:
            code = ErrorCode::CLOUD_ACCESS_DENIED;
            message = "Access denied";
            break;

        case mega::MegaError::API_EEXIST:
            code = ErrorCode::FS_FILE_EXISTS;
            message = "Resource already exists";
            break;

        case mega::MegaError::API_EINCOMPLETE:
            code = ErrorCode::TRANSFER_INCOMPLETE;
            message = "Operation incomplete";
            break;

        case mega::MegaError::API_EKEY:
            code = ErrorCode::AUTH_INVALID_CREDENTIALS;
            message = "Invalid or missing key";
            break;

        case mega::MegaError::API_ESID:
            code = ErrorCode::AUTH_SESSION_EXPIRED;
            message = "Invalid session ID";
            break;

        case mega::MegaError::API_EBLOCKED:
            code = ErrorCode::AUTH_ACCOUNT_BLOCKED;
            message = "Account blocked";
            break;

        case mega::MegaError::API_EOVERQUOTA:
            code = ErrorCode::CLOUD_OVER_QUOTA;
            message = "Storage quota exceeded";
            break;

        case mega::MegaError::API_ETEMPUNAVAIL:
            code = ErrorCode::NETWORK_TIMEOUT;
            message = "Temporarily unavailable";
            break;

        case mega::MegaError::API_ETOOMANYCONNECTIONS:
            code = ErrorCode::NETWORK_TIMEOUT;
            message = "Too many connections";
            break;

        case mega::MegaError::API_EWRITE:
            code = ErrorCode::FS_WRITE_ERROR;
            message = "Write error";
            break;

        case mega::MegaError::API_EREAD:
            code = ErrorCode::FS_READ_ERROR;
            message = "Read error";
            break;

        case mega::MegaError::API_EAPPKEY:
            code = ErrorCode::AUTH_INVALID_CREDENTIALS;
            message = "Invalid application key";
            break;

        case mega::MegaError::API_ESSL:
            code = ErrorCode::NETWORK_SSL_ERROR;
            message = "SSL verification failed";
            break;

        case mega::MegaError::API_EGOINGOVERQUOTA:
            code = ErrorCode::CLOUD_OVER_QUOTA;
            message = "Approaching storage quota";
            break;

        case mega::MegaError::API_EMFAREQUIRED:
            code = ErrorCode::AUTH_2FA_REQUIRED;
            message = "Two-factor authentication required";
            break;

        case mega::MegaError::API_EMASTERONLY:
            code = ErrorCode::CLOUD_ACCESS_DENIED;
            message = "Operation requires master key";
            break;

        case mega::MegaError::API_EBUSINESSPASTDUE:
            code = ErrorCode::AUTH_ACCOUNT_BLOCKED;
            message = "Business account past due";
            break;

        case mega::MegaError::API_EPAYWALL:
            code = ErrorCode::CLOUD_OVER_QUOTA;
            message = "Transfer quota exceeded (paywall)";
            break;

        default:
            code = ErrorCode::UNKNOWN_ERROR;
            message = "Unknown MEGA error";
            break;
    }

    Error error(code, message, megaMessage);
    error.withMegaError(megaErrorCode);
    return error;
}

} // namespace MegaCustom
