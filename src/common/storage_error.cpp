#include "corpcron/common/storage_error.hpp"

namespace corpcron {

const char* storageErrorKindName(StorageErrorKind kind) {
    switch (kind) {
        case StorageErrorKind::None:
            return "none";
        case StorageErrorKind::Connection:
            return "connection";
        case StorageErrorKind::Timeout:
            return "timeout";
        case StorageErrorKind::Authentication:
            return "authentication";
        case StorageErrorKind::DuplicateKey:
            return "duplicate_key";
        case StorageErrorKind::NotFound:
            return "not_found";
        case StorageErrorKind::Query:
            return "query";
        case StorageErrorKind::Protocol:
            return "protocol";
        case StorageErrorKind::Unknown:
            return "unknown";
    }
    return "unknown";
}

} // namespace corpcron
