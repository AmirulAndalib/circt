##===- CosimDpi.capnp - ESI cosim RPC schema ------------------*- CAPNP -*-===//
##
## Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
## See https://llvm.org/LICENSE.txt for license information.
## SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
##
##===----------------------------------------------------------------------===//
##
## The ESI cosimulation RPC Cap'nProto schema. Documentation is in
## docs/ESI/cosim.md. TL;DR: Run the simulation, then connect to its RPC server
## with a client generated by the Cap'nProto implementation for your language of
## choice! (https://capnproto.org/otherlang.html)
##
##===----------------------------------------------------------------------===//

@0xe642127a31681ef6;

# The primary interface exposed by an ESI cosim simulation.
interface CosimDpiServer @0x85e029b5352bcdb5 {
  # List all the registered endpoints.
  list @0 () -> (ifaces :List(EsiDpiInterfaceDesc));
  # Open one of them. Specify both the send and recv data types if want type
  # safety and your language supports it.
  open @1 [S, T] (iface :EsiDpiInterfaceDesc) -> (iface :EsiDpiEndpoint(S, T));

  # Get the zlib-compressed JSON system manifest.
  getCompressedManifest @2 () -> (compressedManifest :Data);

  # Create a low level interface into the simulation.
  openLowLevel @3 () -> (lowLevel :EsiLowLevel);

}

# Description of a registered endpoint.
struct EsiDpiInterfaceDesc @0xd2584d2506f01c8c {
  # Capn'Proto ID of the struct type being sent _to_ the simulator.
  sendTypeID @0 :UInt64;
  # Capn'Proto ID of the struct type being sent _from_ the simulator.
  recvTypeID @1 :UInt64;
  # Numerical identifier of the endpoint. Defined in the design.
  endpointID @2 :Text;
}

# Interactions with an open endpoint. Optionally typed.
interface EsiDpiEndpoint @0xfb0a36bf859be47b (SendMsgType, RecvMsgType) {
  # Send a message to the endpoint.
  send @0 (msg :SendMsgType);
  # Recieve a message from the endpoint. Non-blocking.
  recv @1 (block :Bool = true) -> (hasData :Bool, resp :RecvMsgType);
  # Close the connect to this endpoint.
  close @2 ();
}

# A struct for untyped access to an endpoint.
struct UntypedData @0xac6e64291027d47a {
  data @0 :Data;
}

# A low level interface simply provides MMIO and host memory access. In all
# cases, hardware errors become exceptions.
interface EsiLowLevel @0xae716100ef82f6d6 {
  # Write to an MMIO register.
  writeMMIO @0 (address :UInt32, data :UInt32) -> ();
  # Read from an MMIO register.
  readMMIO  @1 (address :UInt32) -> (data :UInt32);
}
