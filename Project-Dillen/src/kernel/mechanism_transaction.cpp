#include "mechanism_transaction.hpp"

namespace dillen::kernel {

MechanismTransactionResult::operator bool() const noexcept
{
    return status == MechanismTransactionStatus::Committed;
}

}
