#include "ot/user/user.hpp"

CommWriter::CommWriter()
    : comm_page(ou_get_comm_page()),
      _writer(comm_page.as<char>(), OT_PAGE_SIZE) {
}
