#pragma once

#include <string>

namespace corpcron {

enum class StorageErrorKind {
    None,
    Connection,
    Timeout,
    Authentication,
    DuplicateKey,
    NotFound,
    Query,
    Protocol,
    Unknown,
};

struct StorageError {
    StorageErrorKind kind = StorageErrorKind::None;
    int code = 0;
    std::string message;

    bool ok() const {
        return kind == StorageErrorKind::None;
    }
};

const char* storageErrorKindName(StorageErrorKind kind);

} // namespace corpcron
