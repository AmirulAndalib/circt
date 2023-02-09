//===- capnp.h - ESI CPP Capnproto runtime layer ----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This is a specialization of the ESI CPP interface to target Capnproto
// cosimulation.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "circt/Dialect/ESI/Runtime/esi.h"

#include <capnp/ez-rpc.h>

// Assert that an ESI_COSIM_CAPNP_H variable is defined. This is the capnp
// header file generated from the ESI schema, containing definitions for e.g.
// CosimDpiServer, ...
#ifndef ESI_COSIM_CAPNP_H
#error "ESI_COSIM_CAPNP_H must be defined to include this file"
#endif
#include ESI_COSIM_CAPNP_H

namespace circt {
namespace esi {
namespace runtime {
namespace cosim {

namespace detail {
// Custom type to hold the interface descriptions because i can't for the life
// of me figure out how to cleanly keep capnproto messages around...
struct EsiDpiInterfaceDesc {
  std::string endpointID;
  uint64_t sendTypeID;
  uint64_t recvTypeID;
};
} // namespace detail

using EsiDpiInterfaceDesc = detail::EsiDpiInterfaceDesc;

template <typename WriteType, typename ReadType>
class CosimReadWritePort;

template <typename WriteType>
class CosimWritePort;

class CosimBackend {
public:
  // Using directives to point the base class implementations to the cosim
  // port implementations.

  template <typename WriteType, typename ReadType>
  using ReadWritePort = CosimReadWritePort<WriteType, ReadType>;

  template <typename WriteType>
  using WritePort = CosimWritePort<WriteType>;

  CosimBackend(const std::string &host, uint64_t hostPort) {
    ezClient = std::make_unique<capnp::EzRpcClient>(host, hostPort);
    dpiClient = std::make_unique<CosimDpiServer::Client>(
        ezClient->getMain<CosimDpiServer>());

    list();
  }

  // Returns a list of all available endpoints.
  const std::vector<detail::EsiDpiInterfaceDesc> &list() {
    if (endpoints.has_value())
      return *endpoints;

    // Query the DPI server for a list of available endpoints.
    auto listReq = dpiClient->listRequest();
    auto ifaces = listReq.send().wait(ezClient->getWaitScope()).getIfaces();
    endpoints = std::vector<detail::EsiDpiInterfaceDesc>();
    for (auto iface : ifaces) {
      detail::EsiDpiInterfaceDesc desc;
      desc.endpointID = iface.getEndpointID().cStr();
      desc.sendTypeID = iface.getSendTypeID();
      desc.recvTypeID = iface.getRecvTypeID();
      endpoints->push_back(desc);
    }

    // print out the endpoints
    for (auto ep : *endpoints) {
      std::cout << "Endpoint: " << ep.endpointID << std::endl;
      std::cout << "  Send Type: " << ep.sendTypeID << std::endl;
      std::cout << "  Recv Type: " << ep.recvTypeID << std::endl;
    }

    return *endpoints;
  }

  template <typename CnPWriteType, typename CnPReadType>
  auto getPort(const std::vector<std::string> &clientPath) {
    // Join client path into a single string with '.' as a separator.
    std::string clientPathStr;
    for (auto &path : clientPath) {
      if (!clientPathStr.empty())
        clientPathStr += '.';
      clientPathStr += path;
    }

    // Everything is nested under "TOP.top"
    clientPathStr = "TOP.top." + clientPathStr;

    auto openReq = dpiClient->openRequest<CnPWriteType, CnPReadType>();

    // Scan through the available endpoints to find the requested one.
    bool found = false;
    for (auto &ep : list()) {
      auto epid = ep.endpointID;
      if (epid == clientPathStr) {
        auto iface = openReq.getIface();
        iface.setEndpointID(epid);
        iface.setSendTypeID(ep.sendTypeID);
        iface.setRecvTypeID(ep.recvTypeID);
        found = true;
        break;
      }
    }

    if (!found) {
      throw std::runtime_error("Could not find endpoint: " + clientPathStr);
    }

    // Open the endpoint.
    auto openResp = openReq.send().wait(ezClient->getWaitScope());
    return openResp.getIface();
  }

  bool supportsImpl(const std::string &implType) {
    // The cosim backend only supports cosim connectivity implementations
    return implType == "cosim";
  }

  kj::WaitScope &getWaitScope() { return ezClient->getWaitScope(); }

protected:
  std::unique_ptr<capnp::EzRpcClient> ezClient;
  std::unique_ptr<CosimDpiServer::Client> dpiClient;
  std::optional<std::vector<detail::EsiDpiInterfaceDesc>> endpoints;
};

template <typename WriteType, typename ReadType>
class CosimReadWritePort : public Port<CosimBackend> {
  using BasePort = Port<CosimBackend>;

public:
  CosimReadWritePort(const std::vector<std::string> &clientPath,
                     CosimBackend &backend, const std::string &implType)
      : BasePort(clientPath, backend, implType) {}

  ReadType operator()(WriteType arg) {
    auto req = port->sendRequest();
    arg.fillCapnp(req.getMsg());
    req.send().wait(this->backend->getWaitScope());
    std::optional<capnp::Response<typename EsiDpiEndpoint<
        typename WriteType::CPType, typename ReadType::CPType>::RecvResults>>
        resp;
    do {
      auto recvReq = port->recvRequest();
      recvReq.setBlock(false);

      resp = recvReq.send().wait(this->backend->getWaitScope());
    } while (!resp->getHasData());
    auto data = resp->getResp();
    return ReadType::fromCapnp(data);
  }

  void initBackend() override {
    port =
        backend->getPort<typename WriteType::CPType, typename ReadType::CPType>(
            clientPath);
  }

private:
  // Handle to the underlying endpoint.
  std::optional<typename ::EsiDpiEndpoint<typename WriteType::CPType,
                                          typename ReadType::CPType>::Client>
      port;
};

template <typename WriteType>
class CosimWritePort : public Port<CosimBackend> {
  using BasePort = Port<CosimBackend>;

public:
  CosimWritePort(const std::vector<std::string> &clientPath,
                 CosimBackend &backend, const std::string &implType)
      : BasePort(clientPath, backend, implType) {}

  void initBackend() override {
    port = backend->getPort<typename WriteType::CPType, ::I1>(clientPath);
  }

  void operator()(WriteType arg) {
    auto req = port->sendRequest();
    arg.fillCapnp(req.getMsg());
    req.send().wait(this->backend->getWaitScope());
  }

private:
  // Handle to the underlying endpoint.
  std::optional<
      typename ::EsiDpiEndpoint<typename WriteType::CPType, ::I1>::Client>
      port;
};

} // namespace cosim
} // namespace runtime
} // namespace esi
} // namespace circt
