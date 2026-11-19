#pragma once

#include "grtdb/db_helpers.h"

namespace bec {

  inline bool is_stardataparadise_server(const std::string &version) {
    static const std::string suffix = "-StarDataParadise";
    return version.size() > suffix.size() &&
           version.compare(version.size() - suffix.size(), suffix.size(), suffix) == 0;
  }

  inline std::string stardataparadise_endpoint(const db_mgmt_ConnectionRef &connection) {
    const auto params = connection->parameterValues();
    // Keep the target host and port even for drivers whose password identifier describes the SSH tunnel.
    return *connection->driver()->name() + "\n" + get_host_identifier_for_connection(connection) + "\n" +
           params.get_string("hostName") + "\n" + std::to_string(params.get_int("port", 3306)) + "\n" +
           params.get_string("socket") + "\n" + params.get_string("sshHost");
  }

  inline bool is_verified_stardataparadise_connection(const db_mgmt_ConnectionRef &connection) {
    return connection.is_valid() && connection->driver().is_valid() &&
           connection->modules().get_string("StarDataParadise:verifiedEndpoint") ==
             stardataparadise_endpoint(connection);
  }

  inline void set_stardataparadise_connection_verified(const db_mgmt_ConnectionRef &connection, bool verified) {
    auto modules = connection->modules();
    if (verified)
      modules.gset("StarDataParadise:verifiedEndpoint", stardataparadise_endpoint(connection));
    else if (modules.has_key("StarDataParadise:verifiedEndpoint"))
      modules.remove("StarDataParadise:verifiedEndpoint");
  }

}
