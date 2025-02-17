/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <folly/IPAddress.h>
#include <chrono>
#include <string>
#include <string_view>

namespace facebook::fboss {
class RestClient {
 public:
  RestClient(std::string hostname, int port);
  RestClient(folly::IPAddress ipAddress, int port);
  RestClient(folly::IPAddress ipAddress, int port, std::string interface);
  virtual ~RestClient() = default;
  /*
   * Calls the particular Rest api
   */
  bool request(std::string path);
  virtual std::string requestWithOutput(
      std::string path,
      std::string postData = "");
  void setTimeout(std::chrono::milliseconds timeout);

  void setClientCertAndKey(std::string_view cert, std::string_view key);
  void setVerifyHostname(bool verify);

 private:
  // Forbidden copy contructor and assignment operator
  RestClient(RestClient const&) = delete;
  RestClient& operator=(RestClient const&) = delete;

  static size_t writer(
      char* buffer,
      size_t size,
      size_t entries,
      std::stringbuf* writer_buffer);
  void createEndpoint();
  std::string hostname_;
  folly::IPAddress ipAddress_;
  std::string interface_;
  int port_;
  std::chrono::milliseconds timeout_{1000};
  std::string endpoint_;
  std::string cert_, key_;
  bool verifyHostname_ = true;
};

} // namespace facebook::fboss
