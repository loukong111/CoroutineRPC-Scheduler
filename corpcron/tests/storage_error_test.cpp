#include "corpcron/common/storage_error.hpp"
#include <cassert>
#include <string>

int main() {
    assert(std::string(corpcron::storageErrorKindName(corpcron::StorageErrorKind::None)) == "none");
    assert(std::string(corpcron::storageErrorKindName(corpcron::StorageErrorKind::Connection)) == "connection");
    assert(std::string(corpcron::storageErrorKindName(corpcron::StorageErrorKind::Timeout)) == "timeout");
    assert(std::string(corpcron::storageErrorKindName(corpcron::StorageErrorKind::Authentication)) == "authentication");
    assert(std::string(corpcron::storageErrorKindName(corpcron::StorageErrorKind::DuplicateKey)) == "duplicate_key");
    return 0;
}
